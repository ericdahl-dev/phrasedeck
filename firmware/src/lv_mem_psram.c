// LVGL's allocator (LV_USE_STDLIB_MALLOC=LV_STDLIB_CUSTOM), backed by PSRAM so its heap doesn't
// take the internal RAM that Wi-Fi and TLS need (#91 review).
#include <esp_heap_caps.h>
#include <lvgl.h>

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
void* lv_malloc_core(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM); }
void* lv_realloc_core(void* p, size_t new_size) { return heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM); }
void lv_free_core(void* p) { heap_caps_free(p); }
void lv_mem_monitor_core(lv_mem_monitor_t* mon_p) { LV_UNUSED(mon_p); }
lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }
