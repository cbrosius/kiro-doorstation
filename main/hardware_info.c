#include "hardware_info.h"
#include "esp_system.h"
#include "esp_log.h"
#include "bootlog.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "hardware_info";

// Global cache for hardware information
hardware_info_t cached_hardware_info;
bool hardware_info_cached = false;

// Parse WiFi MAC address from bootlog
static void parse_mac_address_from_bootlog(const char* bootlog, char* mac_address, size_t mac_address_size)
{
    if (!bootlog || !mac_address) {
        if (mac_address && mac_address_size > 0) {
            strncpy(mac_address, "00:00:00:00:00:00", mac_address_size - 1);
            mac_address[mac_address_size - 1] = '\0';
        }
        return;
    }

    // Look for MAC address in bootlog - format: "MAC address: xx:xx:xx:xx:xx:xx"
    const char* mac_line = strstr(bootlog, "MAC address:");
    if (mac_line) {
        // Try to extract MAC address from the line
        char temp_mac[18];
        if (sscanf(mac_line, "MAC address: %17s", temp_mac) == 1) {
            strncpy(mac_address, temp_mac, mac_address_size - 1);
            mac_address[mac_address_size - 1] = '\0';
            ESP_LOGD(TAG, "Parsed MAC address from bootlog: %s", mac_address);
            return;
        }
    }

    // Fallback: look for other MAC address patterns
    const char* wifi_mac_line = strstr(bootlog, "WiFi MAC:");
    if (wifi_mac_line) {
        char temp_mac[18];
        if (sscanf(wifi_mac_line, "WiFi MAC: %17s", temp_mac) == 1) {
            strncpy(mac_address, temp_mac, mac_address_size - 1);
            mac_address[mac_address_size - 1] = '\0';
            ESP_LOGD(TAG, "Parsed WiFi MAC address from bootlog: %s", mac_address);
            return;
        }
    }

    // Another pattern: "wifi: MAC address: xx:xx:xx:xx:xx:xx"
    const char* wifi_pattern = strstr(bootlog, "wifi: MAC address:");
    if (wifi_pattern) {
        char temp_mac[18];
        if (sscanf(wifi_pattern, "wifi: MAC address: %17s", temp_mac) == 1) {
            strncpy(mac_address, temp_mac, mac_address_size - 1);
            mac_address[mac_address_size - 1] = '\0';
            ESP_LOGD(TAG, "Parsed wifi MAC address from bootlog: %s", mac_address);
            return;
        }
    }

    // If not found, use default
    ESP_LOGW(TAG, "MAC address not found in bootlog, using default");
    strncpy(mac_address, "00:00:00:00:00:00", mac_address_size - 1);
    mac_address[mac_address_size - 1] = '\0';
}

// Parse a single partition line from bootlog
static void parse_partition_line(const char* line, size_t* total_used, hardware_info_t* info)
{
    if (!line || strlen(line) < 10) return;

    // Skip header line and end marker
    if (strstr(line, "## Label") || strstr(line, "End of partition table")) {
        return;
    }

    // Expected format: "  0 nvs              WiFi data        01 02 00009000 00010000"
    // Format: "index label             usage            type subtype address  size"
    int index;
    char label[32];
    unsigned int type, subtype, address, size;

    // Try to parse the actual partition table format with fixed positions
    // The format is: "  0 nvs              WiFi data        01 02 00009000 00010000"
    if (sscanf(line, "%d %s %*s %*s %*s %*s %x %x %x %x",
               &index, label, &type, &subtype, &address, &size) >= 4) {

        // Trim trailing spaces from label
        char *end = label + strlen(label) - 1;
        while (end > label && *end == ' ') {
            *end = '\0';
            end--;
        }

        // Convert type and subtype codes to strings
        char type_str[16] = "unknown";
        char subtype_str[16] = "unknown";

        // Convert type to string
        if (type == 0x00) {
            strcpy(type_str, "app");
        } else if (type == 0x01) {
            strcpy(type_str, "data");
        } else {
            snprintf(type_str, sizeof(type_str), "0x%02x", type);
        }

        // Convert subtype to string based on type
        if (type == 0x00) { // ESP_PARTITION_TYPE_APP
            if (subtype == 0x10) strcpy(subtype_str, "ota_0");
            else if (subtype == 0x11) strcpy(subtype_str, "ota_1");
            else if (subtype == 0x00) strcpy(subtype_str, "factory");
            else snprintf(subtype_str, sizeof(subtype_str), "0x%02x", subtype);
        } else if (type == 0x01) { // ESP_PARTITION_TYPE_DATA
            if (subtype == 0x02) strcpy(subtype_str, "nvs");
            else if (subtype == 0x01) strcpy(subtype_str, "phy");
            else if (subtype == 0x82) strcpy(subtype_str, "spiffs");
            else if (subtype == 0x00) strcpy(subtype_str, "otadata");
            else snprintf(subtype_str, sizeof(subtype_str), "0x%02x", subtype);
        } else {
            snprintf(subtype_str, sizeof(subtype_str), "0x%02x", subtype);
        }

        if (total_used) {
            *total_used += size;
        }

        if (info && info->partition_count < 16) {
            partition_info_t *part_info = &info->partitions[info->partition_count];

            // Copy partition details
            strncpy(part_info->label, label, sizeof(part_info->label) - 1);
            part_info->label[sizeof(part_info->label) - 1] = '\0';

            strncpy(part_info->type, type_str, sizeof(part_info->type) - 1);
            part_info->type[sizeof(part_info->type) - 1] = '\0';

            strncpy(part_info->subtype, subtype_str, sizeof(part_info->subtype) - 1);
            part_info->subtype[sizeof(part_info->subtype) - 1] = '\0';

            part_info->address = address;
            part_info->size = size;
            part_info->used_bytes = -1; // -1 means unknown

            info->partition_count++;

            ESP_LOGD(TAG, "Parsed partition: %s (%s:%s) at 0x%x, size 0x%x",
                     label, type_str, subtype_str, address, size);
        }
    }
}

// Parse partition information from bootlog text
// This replaces the need for esp_partition_find() calls that could cause deadlocks
static void parse_partition_info_from_bootlog(const char* bootlog, size_t* total_used, hardware_info_t* info)
{
    if (!bootlog) return;

    // Look for partition table in bootlog
    const char* partition_section = strstr(bootlog, "Partition Table:");
    if (!partition_section) {
        ESP_LOGW(TAG, "No partition table found in bootlog");
        ESP_LOGD(TAG, "Bootlog content around partition area: %.200s", bootlog + strlen(bootlog) - 200);
        return;
    }

    ESP_LOGD(TAG, "Found partition table section at offset: %d", partition_section - bootlog);

    // Skip "Partition Table:" line and header line, start parsing partition data
    const char* line_start = partition_section + strlen("Partition Table:");
    char line[256];
    int line_pos = 0;
    bool found_header = false;

    ESP_LOGD(TAG, "Partition section found, starting to parse lines");

    // Parse each partition line
    while (*line_start && (info ? info->partition_count < 16 : true)) {
        if (*line_start == '\n' || *line_start == '\r') {
            if (line_pos > 0) {
                line[line_pos] = '\0';

                // Skip the header line "## Label..."
                if (strstr(line, "## Label")) {
                    found_header = true;
                    line_pos = 0;
                    line_start++;
                    continue;
                }

                // Skip "End of partition table" line
                if (strstr(line, "End of partition table")) {
                    break;
                }

                // Only parse actual partition lines (after header)
                if (found_header && strlen(line) > 10) {
                    ESP_LOGD(TAG, "Parsing partition line: %s", line);
                    parse_partition_line(line, total_used, info);
                }
                line_pos = 0;
            }
        } else if (line_pos < sizeof(line) - 1) {
            line[line_pos++] = *line_start;
        }
        line_start++;
    }

    // Handle last line if no newline
    if (line_pos > 0 && found_header && (info ? info->partition_count < 16 : true)) {
        line[line_pos] = '\0';
        if (strlen(line) > 10 && !strstr(line, "End of partition table")) {
            parse_partition_line(line, total_used, info);
        }
    }
}

// Initialize hardware info cache
// This function should be called once at startup to populate the cache
bool hardware_info_init_cache(void)
{
    ESP_LOGI(TAG, "Initializing hardware info cache");

    if (hardware_info_cached) {
        ESP_LOGI(TAG, "Hardware info cache already initialized");
        return true;
    }

    // Collect hardware information once
    if (!hardware_info_collect(&cached_hardware_info)) {
        ESP_LOGE(TAG, "Failed to collect hardware information for cache");
        return false;
    }

    hardware_info_cached = true;
    ESP_LOGI(TAG, "Hardware info cache initialized successfully");
    return true;
}

// Get cached hardware information
// Returns the cached hardware information without re-parsing bootlog
bool hardware_info_get(hardware_info_t* info)
{
    if (!hardware_info_cached) {
        ESP_LOGE(TAG, "Hardware info cache not initialized");
        return false;
    }

    if (!info) {
        ESP_LOGE(TAG, "Invalid info pointer");
        return false;
    }

    // Copy cached data to provided structure
    memcpy(info, &cached_hardware_info, sizeof(hardware_info_t));
    return true;
}

bool hardware_info_collect(hardware_info_t* info)
{
    if (!info) {
        ESP_LOGE(TAG, "Invalid info pointer");
        return false;
    }

    memset(info, 0, sizeof(hardware_info_t));

    // Get bootlog first - enhanced parsing approach
    const char* bootlog = bootlog_get();
    if (!bootlog) {
        ESP_LOGE(TAG, "No bootlog available");
        return false;
    }

    ESP_LOGD(TAG, "Bootlog length: %d bytes", strlen(bootlog));
    ESP_LOGD(TAG, "Bootlog preview: %.100s...", bootlog);

    // ===== ENHANCED CHIP INFORMATION PARSING =====
    // Parse chip model from bootlog with multiple patterns
    if (strstr(bootlog, "ESP32-S3")) {
        strncpy(info->chip_model, "ESP32-S3", sizeof(info->chip_model) - 1);
    } else {
        strncpy(info->chip_model, "ESP32", sizeof(info->chip_model) - 1);
    }

    // Enhanced chip revision parsing
    const char* chip_rev_line_main = strstr(bootlog, "chip revision:");
    if (chip_rev_line_main) {
        char rev_str[16];
        if (sscanf(chip_rev_line_main, "chip revision: v%15s", rev_str) == 1) {
            char* dot = strchr(rev_str, '.');
            if (dot) {
                info->chip_revision = atoi(dot + 1);
            }
        }
    }

    // CPU cores and frequency (ESP32-S3 specific)
    info->cpu_cores = 2;
    info->cpu_freq_mhz = 240;

    // ===== ENHANCED FLASH INFORMATION PARSING =====
    const char* flash_size_line_main = strstr(bootlog, "SPI Flash Size :");
    if (flash_size_line_main) {
        char size_str[16];
        if (sscanf(flash_size_line_main, "SPI Flash Size : %15s", size_str) == 1) {
            ESP_LOGD(TAG, "Found flash size string: %s", size_str);
            if (strstr(size_str, "2MB")) {
                info->flash_size_mb = 2;
            } else if (strstr(size_str, "4MB")) {
                info->flash_size_mb = 4;
            } else if (strstr(size_str, "8MB")) {
                info->flash_size_mb = 8;
            } else if (strstr(size_str, "16MB")) {
                info->flash_size_mb = 16;
            } else if (strstr(size_str, "32MB")) {
                info->flash_size_mb = 32;
            }
        } else {
            ESP_LOGW(TAG, "Failed to parse flash size from line: %s", flash_size_line_main);
        }
    } else {
        ESP_LOGW(TAG, "Flash size pattern 'SPI Flash Size :' not found in bootlog");
    }

    info->flash_total_bytes = info->flash_size_mb * 1024 * 1024;

    // If flash size is still 0, try alternative detection methods
    if (info->flash_size_mb == 0) {
        ESP_LOGD(TAG, "Flash size still 0, trying alternative detection");

        // Try to detect from partition table
        if (info->partition_count > 0) {
            uint32_t max_address = 0;
            for (uint32_t i = 0; i < info->partition_count; i++) {
                uint32_t partition_end = info->partitions[i].address + info->partitions[i].size;
                if (partition_end > max_address) {
                    max_address = partition_end;
                }
            }

            ESP_LOGD(TAG, "Max partition address: 0x%x (%d bytes)", max_address, max_address);

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
                info->flash_size_mb = 32; // Assume 32MB for larger sizes
            }

            info->flash_total_bytes = info->flash_size_mb * 1024 * 1024;
            ESP_LOGD(TAG, "Detected flash size from partitions: %d MB", info->flash_size_mb);
        }
    }

    // ===== ENHANCED PARTITION INFORMATION PARSING =====
    size_t total_used = 0;
    ESP_LOGD(TAG, "Enhanced partition parsing from bootlog");
    parse_partition_info_from_bootlog(bootlog, &total_used, info);
    ESP_LOGI(TAG, "Enhanced parsing found %d partitions, total used: %d bytes", info->partition_count, total_used);

    info->flash_used_bytes = total_used;
    info->flash_available_bytes = (info->flash_total_bytes > total_used) ?
                                  (info->flash_total_bytes - total_used) : 0;

    // ===== ENHANCED PSRAM INFORMATION PARSING =====
    // Multiple detection patterns for PSRAM
    const char* psram_line = strstr(bootlog, "PSRAM Size :");
    if (psram_line) {
        char psram_size_str[16];
        if (sscanf(psram_line, "PSRAM Size : %15s", psram_size_str) == 1) {
            ESP_LOGD(TAG, "Found PSRAM size string: %s", psram_size_str);
            if (strstr(psram_size_str, "8MB")) {
                info->psram_size_mb = 8;
            } else if (strstr(psram_size_str, "4MB")) {
                info->psram_size_mb = 4;
            } else if (strstr(psram_size_str, "2MB")) {
                info->psram_size_mb = 2;
            } else if (strstr(psram_size_str, "16MB")) {
                info->psram_size_mb = 16;
            } else {
                info->psram_size_mb = 0;
            }
        } else {
            ESP_LOGW(TAG, "Failed to parse PSRAM size from line: %s", psram_line);
        }
    } else {
        ESP_LOGD(TAG, "PSRAM size pattern 'PSRAM Size :' not found in bootlog");
        // Enhanced fallback patterns
        if (strstr(bootlog, "PSRAM") && strstr(bootlog, "8MB")) {
            ESP_LOGD(TAG, "Found PSRAM fallback pattern (PSRAM + 8MB)");
            info->psram_size_mb = 8;
        } else if (strstr(bootlog, "octal_psram: vendor id")) {
            ESP_LOGD(TAG, "Found PSRAM octal_psram pattern");
            // PSRAM initialization logs found, try to detect size
            const char* psram_vendor = strstr(bootlog, "octal_psram: vendor id");
            if (psram_vendor) {
                const char* psram_density = strstr(psram_vendor, "density      : 0x03");
                if (psram_density && strstr(psram_vendor, "64 Mbit")) {
                    ESP_LOGD(TAG, "Found PSRAM density pattern (64 Mbit = 8 MB)");
                    info->psram_size_mb = 8; // 64 Mbit = 8 MB
                }
            }
        } else {
            ESP_LOGD(TAG, "No PSRAM patterns found in bootlog");
            info->psram_size_mb = 0;
        }
    }

    // ===== ENHANCED MAC ADDRESS PARSING =====
    parse_mac_address_from_bootlog(bootlog, info->mac_address, sizeof(info->mac_address));

    // ===== SYSTEM INFORMATION =====
    strncpy(info->firmware_version, "v1.0.0", sizeof(info->firmware_version) - 1);

    const char* idf_ver = esp_get_idf_version();
    if (idf_ver) {
        strncpy(info->idf_version, idf_ver, sizeof(info->idf_version) - 1);
    } else {
        strncpy(info->idf_version, "unknown", sizeof(info->idf_version) - 1);
    }

    strncpy(info->build_date, __DATE__ " " __TIME__, sizeof(info->build_date) - 1);

    // ===== ENHANCED BOOTLOADER INFORMATION PARSING =====
    // Parse ESP-IDF version
    const char* idf_line = strstr(bootlog, "ESP-IDF v");
    if (idf_line) {
        sscanf(idf_line, "ESP-IDF v%31s", info->bootloader_version);
        ESP_LOGD(TAG, "Found IDF version: %s", info->bootloader_version);
    } else {
        ESP_LOGD(TAG, "IDF version pattern 'ESP-IDF v' not found in bootlog");
    }

    // Parse compile time
    const char* compile_line = strstr(bootlog, "compile time");
    if (compile_line) {
        sscanf(compile_line, "compile time %31[^\n]", info->bootloader_compile_time);
        ESP_LOGD(TAG, "Found compile time: %s", info->bootloader_compile_time);
    } else {
        ESP_LOGD(TAG, "Compile time pattern 'compile time' not found in bootlog");
    }

    // Parse chip revision
    const char* chip_rev_line_bootloader = strstr(bootlog, "chip revision:");
    if (chip_rev_line_bootloader) {
        sscanf(chip_rev_line_bootloader, "chip revision: %15s", info->bootloader_chip_revision);
        ESP_LOGD(TAG, "Found bootloader chip revision: %s", info->bootloader_chip_revision);
    } else {
        ESP_LOGD(TAG, "Bootloader chip revision pattern 'chip revision:' not found in bootlog");
    }

    // Parse efuse revision
    const char* efuse_rev_line = strstr(bootlog, "efuse block revision:");
    if (efuse_rev_line) {
        sscanf(efuse_rev_line, "efuse block revision: %15s", info->bootloader_efuse_revision);
        ESP_LOGD(TAG, "Found efuse revision: %s", info->bootloader_efuse_revision);
    } else {
        ESP_LOGD(TAG, "Efuse revision pattern 'efuse block revision:' not found in bootlog");
    }

    // Parse SPI speed
    const char* spi_speed_line = strstr(bootlog, "Boot SPI Speed :");
    if (spi_speed_line) {
        sscanf(spi_speed_line, "Boot SPI Speed : %15s", info->bootloader_spi_speed);
        ESP_LOGD(TAG, "Found SPI speed: %s", info->bootloader_spi_speed);
    } else {
        ESP_LOGD(TAG, "SPI speed pattern 'Boot SPI Speed :' not found in bootlog");
    }

    // Parse SPI mode
    const char* spi_mode_line = strstr(bootlog, "SPI Mode       :");
    if (spi_mode_line) {
        sscanf(spi_mode_line, "SPI Mode       : %7s", info->bootloader_spi_mode);
        ESP_LOGD(TAG, "Found SPI mode: %s", info->bootloader_spi_mode);
    } else {
        ESP_LOGD(TAG, "SPI mode pattern 'SPI Mode       :' not found in bootlog");
    }

    // Parse flash size
    const char* flash_size_line_bootloader = strstr(bootlog, "SPI Flash Size :");
    if (flash_size_line_bootloader) {
        sscanf(flash_size_line_bootloader, "SPI Flash Size : %15s", info->bootloader_flash_size);
        ESP_LOGD(TAG, "Found bootloader flash size: %s", info->bootloader_flash_size);
    } else {
        ESP_LOGD(TAG, "Bootloader flash size pattern 'SPI Flash Size :' not found in bootlog");
    }

    info->bootlog = bootlog;

    static bool logged_once = false;
    if (!logged_once) {
        ESP_LOGI(TAG, "Enhanced hardware info collected: %d partitions, %dMB flash, %dMB PSRAM, chip %s rev %d",
                 info->partition_count, info->flash_size_mb, info->psram_size_mb,
                 info->chip_model, info->chip_revision);
        logged_once = true;
    }
    return true;
}