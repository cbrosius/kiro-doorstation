#ifndef HARDWARE_INFO_H
#define HARDWARE_INFO_H

#include <stdint.h>
#include <stdbool.h>

// Hardware information structure
typedef struct {
    // Chip information
    char chip_model[32];
    uint32_t chip_revision;
    uint32_t cpu_cores;
    uint32_t cpu_freq_mhz;

    // Memory information
    uint32_t flash_size_mb;
    uint32_t flash_total_bytes;
    uint32_t flash_used_bytes;
    uint32_t flash_available_bytes;

    // MAC address
    char mac_address[18];

    // Firmware version
    char firmware_version[32];

    // ESP-IDF version
    char idf_version[32];

    // Build date
    char build_date[32];

    // Bootlog (pointer to captured log)
    const char* bootlog;
} hardware_info_t;

/**
 * Collect static hardware information
 * This function gathers all hardware specifications that don't change during runtime
 *
 * @param info Pointer to hardware_info_t structure to fill
 * @return true on success, false on failure
 */
bool hardware_info_collect(hardware_info_t* info);

#endif // HARDWARE_INFO_H