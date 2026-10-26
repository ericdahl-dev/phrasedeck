// Phrasedeck panel (#68, #98): the panel's own Buttons, saved on the panel. A tap types the
// Button's Phrase on the paired Computer; a Phrase changed on the web page asks "Send?" first.

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include <string>
#include <vector>

#include "admin.h"
#include "admin_password.h"
#include "board_pins.h"
#include "display_driver.h"
#include "net.h"
#include "panel_store.h"
#include "pairing.h"
#include "panel_mcu.h"
#include "phrases.h"
#include "touch.h"

namespace {

// The first Buttons on a new panel, until it's edited on its web page (#98).
const char* const kStarterPhrases[] = {
    "Thanks!", "On my way", "LGTM", "Can you take a look?",
    "Running late", "Back in 5", "Ship it", "Let's sync tomorrow",
};

// The panel's own Buttons, saved in LittleFS on the "spiffs" partition.
constexpr const char* kPhrasesFile = "/phrases.json";
phrases::List g_phrases;
lv_obj_t* g_confirm = nullptr;  // the "Send?" overlay
uint32_t g_confirm_id = 0, g_confirm_edit = 0;  // the edit it shows, not an index that can shift

lv_obj_t* g_status = nullptr;
lv_obj_t* g_net = nullptr;
unsigned g_net_version = 0;
unsigned g_message_seq = 0;
lv_obj_t* g_grid = nullptr;
lv_obj_t* g_sending = nullptr;  // the Button whose Send is in flight, dimmed until its result
lv_obj_t* g_maybe = nullptr;    // a Maybe typed Button stays orange until the next tap
lv_timer_t* g_flash = nullptr;  // ends the 700 ms result color on g_flashed
lv_obj_t* g_flashed = nullptr;
bool g_busy = false;  // a pair or Send is queued or running: further taps are refused, not queued

// The Pair screen: which Nearby Computer, the Pairing code and the short Pin from `phrased pair`.
lv_obj_t* g_pair = nullptr;
lv_obj_t* g_pair_list = nullptr;
lv_obj_t* g_code = nullptr;
lv_obj_t* g_pin = nullptr;
lv_obj_t* g_keyboard = nullptr;
lv_obj_t* g_pin_count = nullptr;  // "Pin 12/16"
lv_obj_t* g_pair_note = nullptr;  // progress and errors, shown in place
lv_obj_t* g_pair_go = nullptr;
bool g_pairing_now = false;       // a pair from this screen is in flight: it stays open for the result
size_t g_pair_index = 0;
std::vector<net::Helper> g_pair_helpers;  // copied when the Pair screen opens, so it can't shift

void closePair(lv_event_t*);
void pairResult(const net::Snapshot& s);
void sendResult(const net::Snapshot& s);

// Shows Wi-Fi and the paired Computer; runs on the LVGL loop, reading the net task's snapshot.
// Nearby Computers are listed on the Pair screen, not here.
void showNet() {
    if (net::version() == g_net_version) return;
    const net::Snapshot s = net::snapshot();
    g_net_version = s.version;
    std::string text = LV_SYMBOL_WIFI " ";
    if (!s.wifiConfigured) {
        text += "Not set up";
    } else if (!s.wifiUp) {
        text += "Connecting";
    } else {
        text += s.paired.empty() ? "Not paired" : s.paired;
    }
    lv_label_set_text(g_net, text.c_str());
    if (s.messageSeq != g_message_seq) {
        g_message_seq = s.messageSeq;
        g_busy = false;
        lv_obj_remove_local_style_prop(g_status, LV_STYLE_TEXT_COLOR, 0);  // only Send results are colored
        if (g_pairing_now) {  // also after Cancel and reopen, so a reopened screen still gets its result
            g_pairing_now = false;
            if (g_pair != nullptr) pairResult(s);
        }
        if (g_sending != nullptr) sendResult(s);
        lv_label_set_text(g_status, s.message.c_str());
    }
}

void closePair(lv_event_t*) {
    if (g_pair == nullptr) return;
    lv_obj_delete_async(g_pair);  // not during its own event
    g_pair = nullptr;
}

// Queues a request unless one is already in flight; says so either way.
bool startRequest(bool queued, const std::string& sending) {
    if (!queued) {
        lv_label_set_text(g_status, "Busy: try again in a moment");
        return false;
    }
    g_busy = true;
    lv_label_set_text(g_status, sending.c_str());
    return true;
}

void pairInputChanged(lv_event_t*);

void focusOn(lv_obj_t* ta) {
    lv_obj_t* other = ta == g_code ? g_pin : g_code;
    lv_obj_remove_state(other, LV_STATE_FOCUSED);
    lv_obj_add_state(ta, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(g_keyboard, ta);
    lv_keyboard_set_mode(g_keyboard, ta == g_code ? LV_KEYBOARD_MODE_USER_2 : LV_KEYBOARD_MODE_USER_1);
}

void submitPair(lv_event_t*) {
    const std::string code = lv_textarea_get_text(g_code);
    const std::string pin = lv_textarea_get_text(g_pin);
    if (!pairing::readyToPair(code, pin) || g_pair_index >= g_pair_helpers.size() || g_pairing_now) return;
    const net::Helper& h = g_pair_helpers[g_pair_index];
    if (g_busy || !net::pair(h, code, pin)) {
        lv_label_set_text(g_pair_note, "Busy: try again in a moment");
        return;
    }
    g_busy = true;
    g_pairing_now = true;
    lv_obj_add_state(g_pair_go, LV_STATE_DISABLED);
    lv_label_set_text(g_pair_note, ("Pairing with " + h.name + "...").c_str());
}

// Paired: close. Anything else stays on this screen, so the code and Pin can be fixed in place.
void pairResult(const net::Snapshot& s) {
    if (s.ok) {
        closePair(nullptr);
        return;
    }
    lv_label_set_text(g_pair_note, s.message.c_str());
    pairInputChanged(nullptr);
}

// Keeps "Pin n/16" and Pair's enabled state current, and moves to the Pin after the 6th digit.
void pairInputChanged(lv_event_t* e) {
    const std::string code = lv_textarea_get_text(g_code);
    const std::string pin = lv_textarea_get_text(g_pin);
    lv_label_set_text_fmt(g_pin_count, "Pin %u/16", static_cast<unsigned>(pairing::pinDigits(pin)));
    if (pairing::readyToPair(code, pin) && !g_pairing_now) lv_obj_remove_state(g_pair_go, LV_STATE_DISABLED);
    else lv_obj_add_state(g_pair_go, LV_STATE_DISABLED);
    if (e != nullptr && lv_event_get_target(e) == g_code && code.size() == 6) focusOn(g_pin);
}

// The keyboard's OK key: from the code, go to the Pin; from the Pin, Pair.
void keyboardReady(lv_event_t*) {
    if (lv_keyboard_get_textarea(g_keyboard) == g_code) focusOn(g_pin);
    else submitPair(nullptr);
}

void pickComputer(lv_event_t* e) {
    g_pair_index = reinterpret_cast<size_t>(lv_event_get_user_data(e));
    for (uint32_t i = 0; i < lv_obj_get_child_count(g_pair_list); ++i) {
        lv_obj_t* b = lv_obj_get_child(g_pair_list, i);
        if (i == g_pair_index) lv_obj_add_state(b, LV_STATE_CHECKED);
        else lv_obj_remove_state(b, LV_STATE_CHECKED);
    }
}

void focusField(lv_event_t* e) { focusOn(static_cast<lv_obj_t*>(lv_event_get_target(e))); }

lv_obj_t* field(lv_obj_t* parent, const char* placeholder, const char* accepted, uint32_t max) {
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_accepted_chars(ta, accepted);
    lv_textarea_set_max_length(ta, max);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_28, 0);
    lv_obj_set_width(ta, 300);
    lv_obj_add_event_cb(ta, focusField, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(ta, pairInputChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    return ta;
}

lv_obj_t* textButton(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* data) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);
    lv_obj_set_style_min_width(b, 140, 0);  // big enough for a finger (UI review)
    lv_obj_set_style_min_height(b, 64, 0);
    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

// The short Pin is hex, so its keyboard is a 4 x 4 hex pad with big keys.
// Not const: LVGL 9.1 takes a non-const map.
const char* kHexMap[] = {"1", "2", "3", "A", "\n", "4", "5", "6", "B", "\n", "7", "8", "9", "C", "\n",
                               "0", "D", "E", "F", "\n", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""};
// OK fires once, on release, as in LVGL's own maps: held down, it must not pair twice.
constexpr lv_buttonmatrix_ctrl_t kOk = LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG;
const lv_buttonmatrix_ctrl_t kHexCtrl[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, kOk | 2};
// The code is digits only: no +/-, dot or arrows.
const char* kDigitMap[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
                           LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, ""};
const lv_buttonmatrix_ctrl_t kDigitCtrl[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, kOk | 1};

void openPair(lv_event_t*) {
    if (g_pair != nullptr) return;
    g_pair = lv_obj_create(lv_screen_active());
    lv_obj_set_size(g_pair, board::LCD_WIDTH, board::LCD_HEIGHT);
    lv_obj_set_style_radius(g_pair, 0, 0);
    lv_obj_remove_flag(g_pair, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(g_pair);
    g_pair_helpers = net::snapshot().nearby;
    lv_label_set_text(title, g_pair_helpers.empty()
                                 ? "No Computer nearby. Start phrased with -lan, then open this again."
                                 : "Run phrased pair on the Computer, then enter the 6-digit code and the Pin.");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    g_pair_list = lv_obj_create(g_pair);
    lv_obj_set_size(g_pair_list, 340, 150);
    lv_obj_align(g_pair_list, LV_ALIGN_TOP_LEFT, 0, 34);
    lv_obj_set_flex_flow(g_pair_list, LV_FLEX_FLOW_COLUMN);
    g_pair_index = 0;
    for (size_t i = 0; i < g_pair_helpers.size(); ++i) {
        lv_obj_t* b = textButton(g_pair_list, g_pair_helpers[i].name.c_str(), pickComputer, reinterpret_cast<void*>(i));
        lv_obj_add_flag(b, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_width(b, lv_pct(100));
        lv_obj_set_style_bg_color(b, lv_color_hex(0x3A3F47), 0);  // the theme's checked color is red
        lv_obj_set_style_bg_color(b, lv_palette_main(LV_PALETTE_BLUE), LV_STATE_CHECKED);
        if (i == 0) lv_obj_add_state(b, LV_STATE_CHECKED);
    }

    g_code = field(g_pair, "6-digit code", "0123456789", 6);
    lv_obj_align(g_code, LV_ALIGN_TOP_RIGHT, 0, 34);
    g_pin = field(g_pair, "Pin (16 characters)", "0123456789abcdefABCDEF ", 19);
    lv_obj_align(g_pin, LV_ALIGN_TOP_RIGHT, 0, 96);

    g_pin_count = lv_label_create(g_pair);
    lv_obj_align(g_pin_count, LV_ALIGN_TOP_RIGHT, -(300 + 12), 112);  // left of the Pin field

    lv_obj_t* cancel = textButton(g_pair, "Cancel", closePair, nullptr);
    lv_obj_set_size(cancel, 140, 56);
    lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, -156, 156);
    g_pair_go = textButton(g_pair, "Pair", submitPair, nullptr);
    lv_obj_set_size(g_pair_go, 140, 56);
    lv_obj_align(g_pair_go, LV_ALIGN_TOP_RIGHT, 0, 156);

    g_pair_note = lv_label_create(g_pair);
    lv_obj_set_width(g_pair_note, 340);
    lv_label_set_long_mode(g_pair_note, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_pair_note, g_pairing_now ? "Pairing..." : "");
    lv_obj_set_style_text_color(g_pair_note, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_align(g_pair_note, LV_ALIGN_TOP_LEFT, 0, 190);

    g_keyboard = lv_keyboard_create(g_pair);
    lv_obj_set_size(g_keyboard, board::LCD_WIDTH - 32, 200);
    lv_obj_set_style_text_font(g_keyboard, &lv_font_montserrat_28, 0);
    lv_obj_align(g_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_map(g_keyboard, LV_KEYBOARD_MODE_USER_1, kHexMap, kHexCtrl);
    lv_keyboard_set_map(g_keyboard, LV_KEYBOARD_MODE_USER_2, kDigitMap, kDigitCtrl);
    lv_obj_add_event_cb(g_keyboard, keyboardReady, LV_EVENT_READY, nullptr);
    focusOn(g_code);
    pairInputChanged(nullptr);
}

// Written to a temporary file, then renamed over the old one, so a power cut never leaves half a list.
bool savePhrases() {
    File f = LittleFS.open("/phrases.tmp", "w");
    if (!f) return false;
    const std::string json = g_phrases.toJson();
    const bool written = f.write(reinterpret_cast<const uint8_t*>(json.data()), json.size()) == json.size();
    f.close();
    return written && LittleFS.rename("/phrases.tmp", kPhrasesFile);
}

// The saved list; the starter Buttons, already confirmed, when there's none or it won't load.
void loadPhrases() {
    if (!LittleFS.begin(true)) Serial.println("phrasedeck panel: LittleFS unavailable, Buttons won't be saved");
    File f = LittleFS.open(kPhrasesFile, "r");
    if (f) {
        std::string json(f.size(), '\0');
        const bool read = f.read(reinterpret_cast<uint8_t*>(&json[0]), json.size()) == json.size();
        f.close();
        if (read && g_phrases.fromJson(json)) return;
        // Kept, not overwritten: a load can fail for a reason that passes (out of memory).
        LittleFS.remove("/phrases.bad");
        LittleFS.rename(kPhrasesFile, "/phrases.bad");
        Serial.println("phrasedeck panel: saved Buttons didn't load, kept as /phrases.bad; using the starter Buttons");
    }
    for (const char* p : kStarterPhrases) g_phrases.add(p, p);
    for (size_t i = 0; i < g_phrases.size(); ++i) g_phrases.confirmed(i);
    savePhrases();
}

// Which panel Phrase this is, for the Helper's edit notice: unique across panels by the chip's MAC.
std::string phraseKey(uint32_t id) {
    char key[32];
    snprintf(key, sizeof key, "%012llx:%lu", ESP.getEfuseMac(), static_cast<unsigned long>(id));
    return key;
}

void clearColor(lv_obj_t* button) {
    lv_obj_remove_local_style_prop(button, LV_STYLE_BG_COLOR, 0);
    lv_obj_remove_local_style_prop(g_status, LV_STYLE_TEXT_COLOR, 0);
}

void endFlash(lv_timer_t*) {
    clearColor(g_flashed);
    g_flash = nullptr;  // a one-shot timer deletes itself
    g_flashed = nullptr;
}

// Ends any result color now, so an older timer can't clear a newer one.
void clearResult() {
    if (g_flash != nullptr) {
        lv_timer_delete(g_flash);
        endFlash(nullptr);
    }
    if (g_maybe != nullptr) {
        clearColor(g_maybe);
        g_maybe = nullptr;
    }
}

// Like the iPhone: the Button flashes green (Typed), orange (Maybe typed) or red (Not typed) for
// 700 ms, the status line in the same color. Maybe typed stays until the next tap.
void sendResult(const net::Snapshot& s) {
    lv_obj_t* b = g_sending;
    g_sending = nullptr;
    clearResult();
    lv_obj_remove_state(b, LV_STATE_DISABLED);
    const lv_color_t c = lv_palette_main(s.ok ? LV_PALETTE_GREEN : s.maybe ? LV_PALETTE_ORANGE : LV_PALETTE_RED);
    lv_obj_set_style_bg_color(b, c, 0);
    lv_obj_set_style_text_color(g_status, c, 0);
    if (s.maybe) {
        g_maybe = b;
        return;
    }
    g_flashed = b;
    g_flash = lv_timer_create(endFlash, 700, nullptr);
    lv_timer_set_repeat_count(g_flash, 1);
}

void sendPhrase(size_t i) {
    const phrases::Button& b = g_phrases.at(i);
    if (!startRequest(net::type(b.text, phraseKey(b.id), b.edit), "Sending: " + b.label)) return;
    g_sending = lv_obj_get_child(g_grid, static_cast<int32_t>(i));
    lv_obj_add_state(g_sending, LV_STATE_DISABLED);  // dimmed while it sends
    Serial.printf("tap: %u at %d,%d\n", static_cast<unsigned>(i), touch_x(), touch_y());  // never the text
}

void closeConfirm(lv_event_t*) {
    lv_obj_delete_async(g_confirm);  // not during its own event
    g_confirm = nullptr;
}

void confirmSend(lv_event_t* e) {
    closeConfirm(e);
    if (g_busy) {
        lv_label_set_text(g_status, "Still sending the last one: wait for it");
        return;
    }
    const size_t i = g_phrases.find(g_confirm_id, g_confirm_edit);
    if (i >= g_phrases.size()) {
        lv_label_set_text(g_status, "That Button changed: tap it again");
        return;
    }
    g_phrases.confirmed(i);
    if (!savePhrases()) Serial.println("phrasedeck panel: couldn't save the confirm");
    sendPhrase(i);
}

// Eric (#98): a Phrase added or edited on the web page shows its text and asks first. Enter and
// Tab are drawn as marks, so what will be typed is what's shown.
void openConfirm(size_t i) {
    const phrases::Button& b = g_phrases.at(i);
    g_confirm_id = b.id;
    g_confirm_edit = b.edit;
    g_confirm = lv_obj_create(lv_screen_active());
    lv_obj_set_size(g_confirm, board::LCD_WIDTH, board::LCD_HEIGHT);
    lv_obj_set_style_radius(g_confirm, 0, 0);
    lv_obj_remove_flag(g_confirm, LV_OBJ_FLAG_SCROLLABLE);

    const std::string computer = net::snapshot().paired;
    lv_obj_t* title = lv_label_create(g_confirm);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_label_set_text(title, computer.empty() ? "Send? No Computer is paired" : ("Send to " + computer + "?").c_str());
    lv_obj_set_width(title, board::LCD_WIDTH - 48);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* about = lv_label_create(g_confirm);
    lv_obj_set_style_text_color(about, lv_color_hex(0x8A929C), 0);
    const unsigned chars = phrases::characters(b.text), lines = phrases::lines(b.text);
    lv_label_set_text_fmt(about, "Changed on the web page  |  %u character%s, %u line%s", chars, chars == 1 ? "" : "s",
                          lines, lines == 1 ? "" : "s");
    lv_obj_align(about, LV_ALIGN_TOP_LEFT, 0, 40);

    lv_obj_t* box = lv_obj_create(g_confirm);
    lv_obj_set_size(box, board::LCD_WIDTH - 48, board::LCD_HEIGHT - 48 - 72 - 84);
    lv_obj_align(box, LV_ALIGN_TOP_LEFT, 0, 72);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_ON);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    lv_obj_t* text = lv_label_create(box);
    lv_obj_set_style_text_font(text, &lv_font_montserrat_28, 0);
    lv_obj_set_width(text, lv_pct(100));
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_label_set_text(text, phrases::visible(b.text, LV_SYMBOL_NEW_LINE, LV_SYMBOL_RIGHT " ").c_str());

    // Send is the wide one, far from Cancel, so a slip doesn't pick the wrong one.
    lv_obj_t* cancel = textButton(g_confirm, "Cancel", closeConfirm, nullptr);
    lv_obj_set_size(cancel, 160, 64);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x3A3F47), 0);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t* send = textButton(g_confirm, "Send", confirmSend, nullptr);
    lv_obj_set_size(send, 320, 64);
    lv_obj_align(send, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

void tap(size_t i) {
    clearResult();
    if (g_busy) {
        lv_label_set_text(g_status, "Still sending the last one: wait for it");
        return;
    }
    if (g_confirm != nullptr || i >= g_phrases.size()) return;
    if (g_phrases.needsConfirm(i)) {
        openConfirm(i);
        return;
    }
    sendPhrase(i);
}

void onTap(lv_event_t* e) { tap(reinterpret_cast<size_t>(lv_event_get_user_data(e))); }

// Settings (#98): one screen of tabs behind the admin password. The first time, the gear asks for a
// new password instead, typed twice.
lv_obj_t* g_lock = nullptr;  // the password screen
lv_obj_t* g_pw = nullptr;
lv_obj_t* g_pw_again = nullptr;  // only when setting one
lv_obj_t* g_lock_note = nullptr;
lv_obj_t* g_lock_keyboard = nullptr;
lv_obj_t* g_settings = nullptr;

void closeLock(lv_event_t*) {
    if (g_lock == nullptr) return;
    // Overwrite the typed password before LVGL frees its buffers.
    lv_textarea_set_text(g_pw, "");
    if (g_pw_again != nullptr) lv_textarea_set_text(g_pw_again, "");
    lv_obj_delete_async(g_lock);
    g_lock = nullptr;
}

void closeSettings(lv_event_t*) {
    if (g_settings == nullptr) return;
    lv_obj_delete_async(g_settings);
    g_settings = nullptr;
}

void openLock(bool setting);

void changePassword(lv_event_t*) { openLock(true); }

void choosePair(lv_event_t*) { openPair(nullptr); }

lv_obj_t* tabText(lv_obj_t* tab, const char* text) {
    lv_obj_t* l = lv_label_create(tab);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, text);
    return l;
}

void openSettings() {
    if (g_settings != nullptr) return;
    const net::Snapshot s = net::snapshot();
    g_settings = lv_obj_create(lv_screen_active());
    lv_obj_set_size(g_settings, board::LCD_WIDTH, board::LCD_HEIGHT);
    lv_obj_set_style_radius(g_settings, 0, 0);
    lv_obj_set_style_pad_all(g_settings, 0, 0);
    lv_obj_remove_flag(g_settings, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* tabs = lv_tabview_create(g_settings);
    lv_tabview_set_tab_bar_size(tabs, 64);
    lv_obj_set_size(tabs, board::LCD_WIDTH, board::LCD_HEIGHT);

    lv_obj_t* computer = lv_tabview_add_tab(tabs, "Computer");
    lv_obj_set_flex_flow(computer, LV_FLEX_FLOW_COLUMN);
    tabText(computer, s.paired.empty() ? "Not paired with a Computer." : ("Paired with " + s.paired + ".").c_str());
    textButton(computer, "Pair a Computer", choosePair, nullptr);

    lv_obj_t* web = lv_tabview_add_tab(tabs, "Web editor");
    lv_obj_set_flex_flow(web, LV_FLEX_FLOW_COLUMN);
    if (s.ip.empty()) {
        tabText(web, "Connect the panel to Wi-Fi to edit its Buttons from a browser.");
    } else {
        const std::string url = "https://" + s.ip + "/";
        tabText(web, ("Edit this panel's Buttons from a browser on your home network:\n" + url +
                      "\n\nThe web editor comes in a later firmware update.").c_str());
        lv_obj_t* qr = lv_qrcode_create(web);
        lv_qrcode_set_size(qr, 180);
        lv_qrcode_update(qr, url.data(), url.size());
    }

    lv_obj_t* password = lv_tabview_add_tab(tabs, "Password");
    lv_obj_set_flex_flow(password, LV_FLEX_FLOW_COLUMN);
    tabText(password, "The admin password guards Settings here and, later, the web editor.");
    textButton(password, "Change password", changePassword, nullptr);

    lv_obj_t* mode = lv_tabview_add_tab(tabs, "Mode");
    lv_obj_set_flex_flow(mode, LV_FLEX_FLOW_COLUMN);
    tabText(mode, "How the panel types:\n" LV_SYMBOL_OK "  Over Wi-Fi, through phrased on the Computer\n"
                  "     As a Bluetooth keyboard: coming in a later update (#94)");

    lv_obj_t* close = textButton(g_settings, LV_SYMBOL_CLOSE, closeSettings, nullptr);
    lv_obj_set_size(close, 64, 64);
    lv_obj_set_style_min_width(close, 64, 0);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);
    // The tab bar fills the width; leave room for the close button.
    lv_obj_set_width(lv_tabview_get_tab_bar(tabs), board::LCD_WIDTH - 64);
}

void showHide(lv_event_t*) {
    const bool hidden = lv_textarea_get_password_mode(g_pw);
    lv_textarea_set_password_mode(g_pw, !hidden);
    if (g_pw_again != nullptr) lv_textarea_set_password_mode(g_pw_again, !hidden);
}

void submitLock(lv_event_t*) {
    const std::string pw = lv_textarea_get_text(g_pw);
    if (g_pw_again != nullptr) {
        if (pw.size() < admin_password::kMinLength) {
            lv_label_set_text(g_lock_note, "At least 8 characters");
        } else if (pw != lv_textarea_get_text(g_pw_again)) {
            lv_label_set_text(g_lock_note, "The two don't match");
        } else if (!admin::set(pw)) {
            lv_label_set_text(g_lock_note, "Couldn't save it: try again");
        } else {
            closeLock(nullptr);
            openSettings();
        }
        return;
    }
    uint32_t wait = 0;
    switch (admin::check(pw, wait)) {
        case admin::Check::Ok:
            closeLock(nullptr);
            openSettings();
            break;
        case admin::Check::Wrong:
            lv_textarea_set_text(g_pw, "");
            lv_label_set_text(g_lock_note, "Wrong password");
            break;
        case admin::Check::Wait:
            lv_label_set_text_fmt(g_lock_note, "Too many wrong tries: wait %u s", static_cast<unsigned>((wait + 999) / 1000));
            break;
    }
}

void focusLockField(lv_event_t* e) { lv_keyboard_set_textarea(g_lock_keyboard, static_cast<lv_obj_t*>(lv_event_get_target(e))); }

lv_obj_t* passwordField(const char* placeholder) {
    lv_obj_t* ta = lv_textarea_create(g_lock);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_max_length(ta, 64);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_28, 0);
    lv_obj_set_width(ta, 460);
    lv_obj_add_event_cb(ta, focusLockField, LV_EVENT_CLICKED, nullptr);
    return ta;
}

void openLock(bool setting) {
    closeLock(nullptr);
    g_lock = lv_obj_create(lv_screen_active());
    lv_obj_set_size(g_lock, board::LCD_WIDTH, board::LCD_HEIGHT);
    lv_obj_set_style_radius(g_lock, 0, 0);
    lv_obj_remove_flag(g_lock, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(g_lock);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_label_set_text(title, setting ? "Set an admin password" : "Admin password");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    g_pw = passwordField(setting ? "At least 8 characters" : "Password");
    lv_obj_align(g_pw, LV_ALIGN_TOP_LEFT, 0, 44);
    g_pw_again = nullptr;
    if (setting) {
        g_pw_again = passwordField("Type it again");
        lv_obj_align(g_pw_again, LV_ALIGN_TOP_LEFT, 0, 108);
    }
    lv_obj_t* eye = textButton(g_lock, LV_SYMBOL_EYE_OPEN, showHide, nullptr);
    lv_obj_set_style_min_width(eye, 64, 0);
    lv_obj_align(eye, LV_ALIGN_TOP_LEFT, 476, 44);

    lv_obj_t* cancel = textButton(g_lock, "Cancel", closeLock, nullptr);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x3A3F47), 0);
    lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t* ok = textButton(g_lock, setting ? "Save" : "Open", submitLock, nullptr);
    lv_obj_align(ok, LV_ALIGN_TOP_RIGHT, 0, 76);

    g_lock_note = lv_label_create(g_lock);
    lv_label_set_text(g_lock_note, "");
    lv_obj_set_style_text_color(g_lock_note, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_align(g_lock_note, LV_ALIGN_TOP_LEFT, 0, setting ? 172 : 108);

    g_lock_keyboard = lv_keyboard_create(g_lock);
    lv_obj_set_size(g_lock_keyboard, board::LCD_WIDTH - 32, 220);
    lv_obj_align(g_lock_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(g_lock_keyboard, g_pw);
    lv_obj_add_event_cb(g_lock_keyboard, submitLock, LV_EVENT_READY, nullptr);
}

void openGear(lv_event_t*) {
    if (g_settings != nullptr || g_lock != nullptr) return;
    openLock(!admin::isSet());
}

void buildPage() {
    // LVGL's default theme is light; dark, so every screen and overlay matches without per-object colors.
    lv_display_set_theme(display_lvgl(), lv_theme_default_init(display_lvgl(), lv_palette_main(LV_PALETTE_BLUE),
                                                                lv_palette_main(LV_PALETTE_RED), true,
                                                                &lv_font_montserrat_20));
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x111418), 0);

    // Header: status on the left, then Wi-Fi and the paired Computer, then Pair. Fixed widths, so
    // long text ends in "..." instead of running into the next one.
    lv_obj_t* gear = textButton(screen, LV_SYMBOL_SETTINGS, openGear, nullptr);
    lv_obj_set_size(gear, 64, 48);
    lv_obj_set_style_min_width(gear, 64, 0);
    lv_obj_set_style_min_height(gear, 48, 0);
    lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -8, 2);

    g_net = lv_label_create(screen);
    lv_obj_set_width(g_net, 240);
    lv_label_set_long_mode(g_net, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(g_net, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(g_net, lv_color_hex(0x8A929C), 0);
    lv_label_set_text(g_net, "");
    lv_obj_align(g_net, LV_ALIGN_TOP_RIGHT, -112, 12);

    g_status = lv_label_create(screen);
    lv_obj_set_width(g_status, board::LCD_WIDTH - 16 - 112 - 240 - 16);
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(g_status, lv_color_hex(0xC8CDD4), 0);
    lv_label_set_text(g_status, "Tap a Button");
    lv_obj_align(g_status, LV_ALIGN_TOP_LEFT, 16, 12);

    // Rows of 4 Buttons below the status line; two rows fit, more scroll.
    lv_obj_t* grid = g_grid = lv_obj_create(screen);
    lv_obj_set_size(grid, board::LCD_WIDTH, board::LCD_HEIGHT - 52);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 12, 0);
    lv_obj_set_style_pad_gap(grid, 12, 0);
    const int32_t w = (board::LCD_WIDTH - 24 - 3 * 12) / 4;
    const int32_t h = (board::LCD_HEIGHT - 52 - 24 - 12) / 2;

    for (size_t i = 0; i < g_phrases.size(); ++i) {
        lv_obj_t* button = lv_button_create(grid);
        lv_obj_set_size(button, w, h);
        lv_obj_add_event_cb(button, onTap, LV_EVENT_CLICKED, reinterpret_cast<void*>(i));
        lv_obj_t* label = lv_label_create(button);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, g_phrases.at(i).label.c_str());
        lv_obj_center(label);
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    Serial.println("phrasedeck panel: boot");
    Wire.begin(board::I2C_SDA, board::I2C_SCL, board::I2C_HZ);
    // The companion chip at 0x30 can come up after the ESP32 (seen at power-on on a V1.4).
    // Elecrow's V1.3+ example retries, sending the touch-activate byte, until it answers.
    for (int i = 0; i < 20 && !panel_mcu::begin(); ++i) {
        panel_mcu::activateTouch();
        delay(100);
    }
    if (!display_init()) {
        Serial.println("phrasedeck panel: no display, restarting in 5 s");
        delay(5000);
        esp_restart();
    }
    touch_init();
    loadPhrases();
    buildPage();
    if (!panel_store::begin()) Serial.println("phrasedeck panel: panel NVS unavailable");
    admin::begin();
    if (!net::start()) Serial.println("phrasedeck panel: net task didn't start");
    lv_timer_handler();
    panel_mcu::backlightOn();  // last, so the screen doesn't flash on boot
    Serial.printf("phrasedeck panel: ready, internal heap free %u, PSRAM free %u\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

#ifdef PANEL_SERIAL_COMMANDS
// Dev builds only (env crowpanel-dev): the same pair and Send the screen does, from serial.
void serialCommands() {
    static std::string line;
    while (Serial.available()) {
        const char ch = static_cast<char>(Serial.read());
        if (ch != '\n') {
            if (ch != '\r' && line.size() < 256) line += ch;
            continue;
        }
        if (line.rfind("pair ", 0) == 0) {
            char code[8] = {}, pin[24] = {};
            unsigned index = 0;
            const auto nearby = net::snapshot().nearby;
            if (sscanf(line.c_str() + 5, "%u %7s %23[0-9a-fA-F ]", &index, code, pin) == 3 && index < nearby.size()) {
                net::pair(nearby[index], code, pin);
            }
        } else if (line.rfind("type ", 0) == 0) {
            net::type(line.substr(5));
        } else if (line.rfind("tap ", 0) == 0) {
            tap(strtoul(line.c_str() + 4, nullptr, 10));  // as a finger would, "Send?" included
        } else if (line.rfind("edit ", 0) == 0) {  // stands in for the web page until it exists
            char* rest = nullptr;
            const size_t i = strtoul(line.c_str() + 5, &rest, 10);
            std::string text = rest + (*rest == ' ');
            for (size_t at; (at = text.find("\\n")) != std::string::npos;) text.replace(at, 2, "\n");
            for (size_t at; (at = text.find("\\t")) != std::string::npos;) text.replace(at, 2, "\t");
            if (i < g_phrases.size() && g_phrases.edit(i, g_phrases.at(i).label, text)) savePhrases();
        } else if (line == "shot") {
            // The screen, one line per row: "R" then hex (count, RGB565) runs. Copied whole first, so
            // the picture is one moment; one write per row, so log lines land between rows.
            static uint16_t* frame = static_cast<uint16_t*>(ps_malloc(board::LCD_WIDTH * board::LCD_HEIGHT * 2));
            static char out[2 + board::LCD_WIDTH * 6 + 1];
            if (frame != nullptr) display_lcd.readRect(0, 0, board::LCD_WIDTH, board::LCD_HEIGHT, frame);
            for (int y = 0; frame != nullptr && y < board::LCD_HEIGHT; ++y) {
                const uint16_t* row = frame + y * board::LCD_WIDTH;
                size_t len = 0;
                out[len++] = 'R';
                for (int x = 0; x < board::LCD_WIDTH;) {
                    int n = 1;
                    while (x + n < board::LCD_WIDTH && n < 255 && row[x + n] == row[x]) ++n;
                    len += snprintf(out + len, 7, "%02x%04x", n, row[x]);
                    x += n;
                }
                out[len++] = '\n';
                Serial.write(reinterpret_cast<const uint8_t*>(out), len);
            }
        } else if (line.rfind("setpw ", 0) == 0) {
            Serial.printf("setpw: %s\n", admin::set(line.substr(6)) ? "set" : "refused");
        } else if (line.rfind("checkpw ", 0) == 0) {
            uint32_t wait = 0;
            const uint32_t start = millis();
            const admin::Check c = admin::check(line.substr(8), wait);
            Serial.printf("checkpw: %s, wait %u ms, took %u ms\n",
                          c == admin::Check::Ok ? "ok" : c == admin::Check::Wrong ? "wrong" : "wait",
                          static_cast<unsigned>(wait), static_cast<unsigned>(millis() - start));
        } else if (line == "clearpw") {  // dev only: back to "no admin password", as on a new panel
            Preferences p;
            if (p.begin("admin", false, panel_store::kPartition)) p.clear();
            Serial.println("clearpw: done, reboot to load");
        } else if (line == "gear") {
            openGear(nullptr);
        } else if (line.rfind("pw ", 0) == 0 && g_lock != nullptr) {  // types into the password screen, then OK
            lv_textarea_set_text(g_pw, line.c_str() + 3);
            if (g_pw_again != nullptr) lv_textarea_set_text(g_pw_again, line.c_str() + 3);
            submitLock(nullptr);
        } else if (line.rfind("tab ", 0) == 0 && g_settings != nullptr) {
            lv_tabview_set_active(lv_obj_get_child(g_settings, 0), strtoul(line.c_str() + 4, nullptr, 10), LV_ANIM_OFF);
        } else if (line == "pairscreen") {
            openPair(nullptr);
        } else if (line.rfind("key ", 0) == 0 && g_pair != nullptr) {  // as the on-screen keys would
            for (char ch : line.substr(4)) lv_textarea_add_char(lv_keyboard_get_textarea(g_keyboard), ch);
        } else if (line == "ok" && g_pair != nullptr) {
            keyboardReady(nullptr);
        } else if (line == "send" && g_confirm != nullptr) {
            confirmSend(nullptr);
        }
        line.clear();
    }
}
#endif

void loop() {
#ifdef PANEL_SERIAL_COMMANDS
    serialCommands();
#endif
    touch_poll();
    showNet();
    lv_timer_handler();
    delay(5);
}
