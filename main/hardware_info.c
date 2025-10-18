#include "hardware_info.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "bootlog.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "hardware_info";

bool hardware_info_collect(hardware_info_t* info)
{
    if (!info) {
        ESP_LOGE(TAG, "Invalid info pointer");
        return false;
    }

    memset(info, 0, sizeof(hardware_info_t));

    // Chip model (hardcoded for ESP32-S3)
    strncpy(info->chip_model, "ESP32-S3", sizeof(info->chip_model) - 1);
    info->chip_revision = 0;  // Not easily accessible
    info->cpu_cores = 2;
    info->cpu_freq_mhz = 240;

    // Flash size calculation from partitions
    size_t max_address = 0;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL) {
        const esp_partition_t *part = esp_partition_get(it);
        size_t part_end = part->address + part->size;
        if (part_end > max_address) {
            max_address = part_end;
        }
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);

    // Determine flash size from max partition address
    if (max_address <= 2 * 1024 * 1024) {
        info->flash_size_mb = 2;
    } else if (max_address <= 4 * 1024 * 1024) {
        info->flash_size_mb = 4;
    } else if (max_address <= 8 * 1024 * 1024) {
        info->flash_size_mb = 8;
    } else if (max_address <= 16 * 1024 * 1024) {
        info->flash_size_mb = 16;
    } else {
        info->flash_size_mb = 32;
    }

    info->flash_total_bytes = info->flash_size_mb * 1024 * 1024;

    // Calculate used flash space
    size_t total_used = 0;
    it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL) {
        const esp_partition_t *part = esp_partition_get(it);
        total_used += part->size;
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);

    info->flash_used_bytes = total_used;
    info->flash_available_bytes = (info->flash_total_bytes > total_used) ?
                                  (info->flash_total_bytes - total_used) : 0;

    // MAC address
    uint8_t mac[6] = {0};
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (ret == ESP_OK) {
        snprintf(info->mac_address, sizeof(info->mac_address),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        strncpy(info->mac_address, "00:00:00:00:00:00", sizeof(info->mac_address) - 1);
    }

    // Firmware version (hardcoded for now)
    strncpy(info->firmware_version, "v1.0.0", sizeof(info->firmware_version) - 1);

    // ESP-IDF version
    const char* idf_ver = esp_get_idf_version();
    if (idf_ver) {
        strncpy(info->idf_version, idf_ver, sizeof(info->idf_version) - 1);
    } else {
        strncpy(info->idf_version, "unknown", sizeof(info->idf_version) - 1);
    }

    // Build date (hardcoded for now - could be set by build system)
    strncpy(info->build_date, __DATE__ " " __TIME__, sizeof(info->build_date) - 1);

    // Bootlog
    info->bootlog = bootlog_get();

    ESP_LOGI(TAG, "Hardware info collected successfully");
    return true;
}