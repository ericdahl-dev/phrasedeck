// Phrasedeck panel (#68, #98): the panel's own Buttons, saved on the panel. A tap types the
// Button's Phrase on the paired Computer; a Phrase changed on the web page asks "Send?" first.

#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include <string>
#include <vector>

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
        if (g_pairing_now) {  // also after Cancel and reopen, so a reopened screen still gets its result
            g_pairing_now = false;
            if (g_pair != nullptr) pairResult(s);
        }
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

void sendPhrase(size_t i) {
    const phrases::Button& b = g_phrases.at(i);
    startRequest(net::type(b.text, phraseKey(b.id), b.edit), "Sending: " + b.label);
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

// Eric (#98): a Phrase added or edited on the web page shows its text and asks first.
void openConfirm(size_t i) {
    g_confirm_id = g_phrases.at(i).id;
    g_confirm_edit = g_phrases.at(i).edit;
    g_confirm = lv_obj_create(lv_screen_active());
    lv_obj_set_size(g_confirm, board::LCD_WIDTH, board::LCD_HEIGHT);
    lv_obj_center(g_confirm);
    lv_obj_t* title = lv_label_create(g_confirm);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_label_set_text(title, "This Button changed on the web page. Send it?");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t* text = lv_label_create(g_confirm);
    lv_obj_set_style_text_font(text, &lv_font_montserrat_28, 0);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(text, board::LCD_WIDTH - 48, board::LCD_HEIGHT - 160);
    lv_obj_add_flag(text, LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_text(text, g_phrases.at(i).text.c_str());
    lv_obj_align(text, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_obj_align(textButton(g_confirm, "Cancel", closeConfirm, nullptr), LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_align(textButton(g_confirm, "Send", confirmSend, nullptr), LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

void tap(size_t i) {
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

void buildPage() {
    // LVGL's default theme is light; dark, so every screen and overlay matches without per-object colors.
    lv_display_set_theme(display_lvgl(), lv_theme_default_init(display_lvgl(), lv_palette_main(LV_PALETTE_BLUE),
                                                                lv_palette_main(LV_PALETTE_RED), true,
                                                                &lv_font_montserrat_20));
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x111418), 0);

    // Header: status on the left, then Wi-Fi and the paired Computer, then Pair. Fixed widths, so
    // long text ends in "..." instead of running into the next one.
    lv_obj_t* pairButton = textButton(screen, "Pair", openPair, nullptr);
    lv_obj_align(pairButton, LV_ALIGN_TOP_RIGHT, -8, 4);

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
    lv_obj_t* grid = lv_obj_create(screen);
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
            if (i < g_phrases.size() && g_phrases.edit(i, g_phrases.at(i).label, rest + (*rest == ' '))) savePhrases();
        } else if (line == "shot") {
            // The screen, one line per row: "R" then hex (count, RGB565) runs. One write per row, so
            // the net task's log lines land between rows, never inside one.
            static uint16_t row[board::LCD_WIDTH];
            static char out[2 + board::LCD_WIDTH * 6 + 1];
            for (int y = 0; y < board::LCD_HEIGHT; ++y) {
                display_lcd.readRect(0, y, board::LCD_WIDTH, 1, row);
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
