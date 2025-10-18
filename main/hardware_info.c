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

    // Collect partition information
    info->partition_count = 0;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);

    while (it != NULL && info->partition_count < 16) {
        const esp_partition_t *part = esp_partition_get(it);
        partition_info_t *part_info = &info->partitions[info->partition_count];

        // Copy partition details
        strncpy(part_info->label, part->label, sizeof(part_info->label) - 1);
        part_info->label[sizeof(part_info->label) - 1] = '\0';

        // Set type string
        const char *type_str = "unknown";
        if (part->type == ESP_PARTITION_TYPE_APP) {
            type_str = "app";
        } else if (part->type == ESP_PARTITION_TYPE_DATA) {
            type_str = "data";
        }
        strncpy(part_info->type, type_str, sizeof(part_info->type) - 1);

        // Set subtype string
        char subtype_str[16];
        if (part->type == ESP_PARTITION_TYPE_APP) {
            if (part->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
                strcpy(subtype_str, "factory");
            } else if (part->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
                strcpy(subtype_str, "ota_0");
            } else if (part->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
                strcpy(subtype_str, "ota_1");
            } else {
                snprintf(subtype_str, sizeof(subtype_str), "0x%02x", part->subtype);
            }
        } else if (part->type == ESP_PARTITION_TYPE_DATA) {
            if (part->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS) {
                strcpy(subtype_str, "nvs");
            } else if (part->subtype == ESP_PARTITION_SUBTYPE_DATA_PHY) {
                strcpy(subtype_str, "phy");
            } else if (part->subtype == ESP_PARTITION_SUBTYPE_DATA_FAT) {
                strcpy(subtype_str, "fat");
            } else if (part->subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS) {
                strcpy(subtype_str, "spiffs");
            } else {
                snprintf(subtype_str, sizeof(subtype_str), "0x%02x", part->subtype);
            }
        } else {
            snprintf(subtype_str, sizeof(subtype_str), "0x%02x", part->subtype);
        }
        strncpy(part_info->subtype, subtype_str, sizeof(part_info->subtype) - 1);

        part_info->address = part->address;
        part_info->size = part->size;

        // Try to get used space for NVS partitions
        part_info->used_bytes = -1; // -1 means unknown
        if (part->type == ESP_PARTITION_TYPE_DATA && part->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS) {
            // For NVS partitions, try to get statistics
            nvs_stats_t nvs_stats;
            nvs_handle_t nvs_handle;

            // Try to open the NVS partition
            esp_err_t err = nvs_open_from_partition(part->label, "storage", NVS_READONLY, &nvs_handle);
            if (err == ESP_OK) {
                nvs_close(nvs_handle);

                // Get NVS statistics
                err = nvs_get_stats(part->label, &nvs_stats);
                if (err == ESP_OK) {
                    // Calculate used space: (total - free) entries
                    size_t used_entries = nvs_stats.used_entries;
                    size_t total_entries = nvs_stats.total_entries;

                    // Estimate used bytes (rough approximation)
                    // Each entry uses approximately 32 bytes on average
                    part_info->used_bytes = (int32_t)(used_entries * 32);
                }
            }
        }

        info->partition_count++;
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);

    // Parse bootloader information from bootlog
    const char* bootlog = bootlog_get();
    if (bootlog) {
        // Parse ESP-IDF version
        const char* idf_line = strstr(bootlog, "ESP-IDF v");
        if (idf_line) {
            sscanf(idf_line, "ESP-IDF v%31s", info->bootloader_version);
        }

        // Parse compile time
        const char* compile_line = strstr(bootlog, "compile time");
        if (compile_line) {
            sscanf(compile_line, "compile time %31[^\n]", info->bootloader_compile_time);
        }

        // Parse chip revision
        const char* chip_rev_line = strstr(bootlog, "chip revision:");
        if (chip_rev_line) {
            sscanf(chip_rev_line, "chip revision: %15s", info->bootloader_chip_revision);
        }

        // Parse efuse revision
        const char* efuse_rev_line = strstr(bootlog, "efuse block revision:");
        if (efuse_rev_line) {
            sscanf(efuse_rev_line, "efuse block revision: %15s", info->bootloader_efuse_revision);
        }

        // Parse SPI speed
        const char* spi_speed_line = strstr(bootlog, "Boot SPI Speed :");
        if (spi_speed_line) {
            sscanf(spi_speed_line, "Boot SPI Speed : %15s", info->bootloader_spi_speed);
        }

        // Parse SPI mode
        const char* spi_mode_line = strstr(bootlog, "SPI Mode       :");
        if (spi_mode_line) {
            sscanf(spi_mode_line, "SPI Mode       : %7s", info->bootloader_spi_mode);
        }

        // Parse flash size
        const char* flash_size_line = strstr(bootlog, "SPI Flash Size :");
        if (flash_size_line) {
            sscanf(flash_size_line, "SPI Flash Size : %15s", info->bootloader_flash_size);
        }
    }

    // Bootlog
    info->bootlog = bootlog;

    ESP_LOGI(TAG, "Hardware info collected successfully, including %d partitions and bootloader info", info->partition_count);
    return true;
}