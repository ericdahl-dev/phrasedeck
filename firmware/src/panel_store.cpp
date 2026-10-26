#include "panel_store.h"

#include <Arduino.h>
#include <nvs_flash.h>

namespace panel_store {

bool begin() {
    esp_err_t err = nvs_flash_init_partition(kPartition);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("panel_store: formatting the panel NVS (first boot on this table)");
        if (nvs_flash_erase_partition(kPartition) != ESP_OK) return false;
        err = nvs_flash_init_partition(kPartition);
    }
    if (err != ESP_OK) Serial.printf("panel_store: %s\n", esp_err_to_name(err));
    return err == ESP_OK;
}

bool erase() { return nvs_flash_erase_partition(kPartition) == ESP_OK; }

}  // namespace panel_store
