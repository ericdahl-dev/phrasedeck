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
size_t g_pair_index = 0;
std::vector<net::Helper> g_pair_helpers;  // copied when the Pair screen opens, so it can't shift

// Shows Wi-Fi and the Nearby Helpers; runs on the LVGL loop, reading the net task's snapshot.
void showNet() {
    if (net::version() == g_net_version) return;
    const net::Snapshot s = net::snapshot();
    g_net_version = s.version;
    std::string text;
    if (!s.wifiConfigured) {
        text = "Wi-Fi: not set up";
    } else if (!s.wifiUp) {
        text = "Wi-Fi: connecting";
    } else {
        text = "Wi-Fi " + s.ip + "  |  Nearby: ";
        if (s.nearby.empty()) text += "none";
        for (size_t i = 0; i < s.nearby.size(); ++i) {
            text += (i ? ", " : "") + s.nearby[i].name;
        }
    }
    if (!s.paired.empty()) text += "  |  Paired: " + s.paired;
    lv_label_set_text(g_net, text.c_str());
    if (s.messageSeq != g_message_seq) {
        g_message_seq = s.messageSeq;
        g_busy = false;
        lv_label_set_text(g_status, s.message.c_str());
    }
}

void closePair(lv_event_t*) {
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

void submitPair(lv_event_t*) {
    const std::string code = lv_textarea_get_text(g_code);
    const std::string pin = lv_textarea_get_text(g_pin);
    if (code.size() != 6 || g_pair_index >= g_pair_helpers.size()) return;
    if (g_busy) {
        lv_label_set_text(g_status, "Busy: try again in a moment");
        return;
    }
    const net::Helper& h = g_pair_helpers[g_pair_index];
    startRequest(net::pair(h, code, pin), "Pairing with " + h.name + "...");
    closePair(nullptr);
}

void pickComputer(lv_event_t* e) {
    g_pair_index = reinterpret_cast<size_t>(lv_event_get_user_data(e));
    for (uint32_t i = 0; i < lv_obj_get_child_count(g_pair_list); ++i) {
        lv_obj_t* b = lv_obj_get_child(g_pair_list, i);
        if (i == g_pair_index) lv_obj_add_state(b, LV_STATE_CHECKED);
        else lv_obj_remove_state(b, LV_STATE_CHECKED);
    }
}

void focusField(lv_event_t* e) {
    lv_obj_t* ta = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_keyboard_set_textarea(g_keyboard, ta);
    lv_keyboard_set_mode(g_keyboard, ta == g_code ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
}

lv_obj_t* field(lv_obj_t* parent, const char* placeholder, const char* accepted, uint32_t max) {
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_accepted_chars(ta, accepted);
    lv_textarea_set_max_length(ta, max);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_28, 0);
    lv_obj_set_width(ta, 360);
    lv_obj_add_event_cb(ta, focusField, LV_EVENT_FOCUSED, nullptr);
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

void openPair(lv_event_t*) {
    if (g_pair != nullptr) return;
    g_pair = lv_obj_create(lv_screen_active());
    lv_obj_set_size(g_pair, board::LCD_WIDTH, board::LCD_HEIGHT);
    lv_obj_set_style_bg_color(g_pair, lv_color_hex(0x111418), 0);
    lv_obj_set_style_radius(g_pair, 0, 0);

    lv_obj_t* title = lv_label_create(g_pair);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    g_pair_helpers = net::snapshot().nearby;
    lv_label_set_text(title, g_pair_helpers.empty()
                                 ? "No Computer is Nearby. Start phrased with -lan, then try again."
                                 : "Run `phrased pair` on the Computer, then enter its code and Pin for a panel.");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    g_pair_list = lv_obj_create(g_pair);
    lv_obj_set_size(g_pair_list, 380, 150);
    lv_obj_align(g_pair_list, LV_ALIGN_TOP_LEFT, 0, 36);
    lv_obj_set_flex_flow(g_pair_list, LV_FLEX_FLOW_COLUMN);
    g_pair_index = 0;
    for (size_t i = 0; i < g_pair_helpers.size(); ++i) {
        lv_obj_t* b = textButton(g_pair_list, g_pair_helpers[i].name.c_str(), pickComputer, reinterpret_cast<void*>(i));
        lv_obj_add_flag(b, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_width(b, lv_pct(100));
        if (i == 0) lv_obj_add_state(b, LV_STATE_CHECKED);
    }

    g_code = field(g_pair, "Pairing code", "0123456789", 6);
    lv_obj_align(g_code, LV_ALIGN_TOP_RIGHT, 0, 36);
    g_pin = field(g_pair, "Pin for a panel", "0123456789abcdefABCDEF ", 19);
    lv_obj_align(g_pin, LV_ALIGN_TOP_RIGHT, 0, 100);

    lv_obj_t* cancel = textButton(g_pair, "Cancel", closePair, nullptr);
    lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, -130, 160);
    lv_obj_t* go = textButton(g_pair, "Pair", submitPair, nullptr);
    lv_obj_align(go, LV_ALIGN_TOP_RIGHT, 0, 160);

    g_keyboard = lv_keyboard_create(g_pair);
    lv_obj_set_size(g_keyboard, board::LCD_WIDTH - 32, 220);
    lv_obj_align(g_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(g_keyboard, g_code);
    lv_keyboard_set_mode(g_keyboard, LV_KEYBOARD_MODE_NUMBER);
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
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x111418), 0);

    g_status = lv_label_create(screen);
    lv_obj_set_style_text_font(g_status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_status, lv_color_hex(0xC8CDD4), 0);
    lv_label_set_text(g_status, "Phrasedeck panel: tap a Button");
    lv_obj_align(g_status, LV_ALIGN_TOP_LEFT, 16, 12);

    lv_obj_t* pairButton = textButton(screen, "Pair", openPair, nullptr);
    lv_obj_align(pairButton, LV_ALIGN_TOP_RIGHT, -8, 4);

    g_net = lv_label_create(screen);
    lv_obj_set_style_text_font(g_net, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_net, lv_color_hex(0x8A929C), 0);
    lv_label_set_text(g_net, "");
    lv_obj_align(g_net, LV_ALIGN_TOP_RIGHT, -100, 12);

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
