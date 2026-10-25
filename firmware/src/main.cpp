// Phrasedeck panel (#68), slice 1a: a hard-coded Page of Buttons on the screen. A tap shows
// the Button's phrase in the status line and logs it to serial; nothing is sent yet.

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "board_pins.h"
#include "display_driver.h"
#include "panel_mcu.h"
#include "touch.h"

namespace {

// shortcut: a fixed Page until the Deck comes from the Helper (#68's open problem).
const char* const kPhrases[] = {
    "Thanks!", "On my way", "LGTM", "Can you take a look?",
    "Running late", "Back in 5", "Ship it", "Let's sync tomorrow",
};

lv_obj_t* g_status = nullptr;

void onTap(lv_event_t* e) {
    const char* phrase = static_cast<const char*>(lv_event_get_user_data(e));
    lv_label_set_text_fmt(g_status, "Tapped: %s", phrase);
    Serial.printf("tap: \"%s\" at %d,%d\n", phrase, touch_x(), touch_y());
}

void buildPage() {
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x111418), 0);

    g_status = lv_label_create(screen);
    lv_obj_set_style_text_font(g_status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_status, lv_color_hex(0xC8CDD4), 0);
    lv_label_set_text(g_status, "Phrasedeck panel: tap a Button");
    lv_obj_align(g_status, LV_ALIGN_TOP_MID, 0, 12);

    // 4 x 2 Buttons below the status line.
    static const int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_t* grid = lv_obj_create(screen);
    lv_obj_set_size(grid, board::LCD_WIDTH, board::LCD_HEIGHT - 52);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_grid_dsc_array(grid, cols, rows);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 12, 0);
    lv_obj_set_style_pad_gap(grid, 12, 0);

    for (int i = 0; i < 8; ++i) {
        lv_obj_t* button = lv_button_create(grid);
        lv_obj_set_grid_cell(button, LV_GRID_ALIGN_STRETCH, i % 4, 1, LV_GRID_ALIGN_STRETCH, i / 4, 1);
        lv_obj_add_event_cb(button, onTap, LV_EVENT_CLICKED, const_cast<char*>(kPhrases[i]));
        lv_obj_t* label = lv_label_create(button);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, kPhrases[i]);
        lv_obj_center(label);
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    Serial.println("phrasedeck panel: boot");
    Wire.begin(board::I2C_SDA, board::I2C_SCL, board::I2C_HZ);
    panel_mcu::begin();
    if (!display_init()) {
        Serial.println("phrasedeck panel: no display, halting");
        while (true) delay(1000);
    }
    touch_init();
    buildPage();
    lv_timer_handler();
    panel_mcu::backlightOn();  // last, so the screen doesn't flash on boot
    Serial.println("phrasedeck panel: ready");
}

void loop() {
    touch_poll();
    lv_timer_handler();
    delay(5);
}
