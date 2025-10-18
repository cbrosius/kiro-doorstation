#ifndef HARDWARE_INFO_H
#define HARDWARE_INFO_H

#include <stdint.h>
#include <stdbool.h>

// Hardware information structure
typedef struct hardware_info_t hardware_info_t;

// Partition information structure
typedef struct {
    char label[16];
    char type[16];
    char subtype[16];
    uint32_t address;
    uint32_t size;
    int32_t used_bytes;  // -1 if unknown
} partition_info_t;

// Hardware information structure definition
struct hardware_info_t {
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
    uint32_t psram_size_mb;  // PSRAM size in MB (0 if not available)

    // MAC address
    char mac_address[18];

    // Firmware version
    char firmware_version[32];

    // ESP-IDF version
    char idf_version[32];

    // Build date
    char build_date[32];

    // Bootloader information (from bootlog parsing)
    char bootloader_version[32];
    char bootloader_compile_time[32];
    char bootloader_chip_revision[16];
    char bootloader_efuse_revision[16];
    char bootloader_spi_speed[16];
    char bootloader_spi_mode[8];
    char bootloader_flash_size[16];

    // Partition layout
    partition_info_t partitions[16];  // Support up to 16 partitions
    uint32_t partition_count;

    // Bootlog (pointer to captured log)
    const char* bootlog;
};

// Cached hardware information structure
extern hardware_info_t cached_hardware_info;
extern bool hardware_info_cached;

/**
 * Collect static hardware information
 * This function gathers all hardware specifications that don't change during runtime
 *
 * @param info Pointer to hardware_info_t structure to fill
 * @return true on success, false on failure
 */
bool hardware_info_collect(hardware_info_t* info);

/**
 * Initialize hardware info cache
 * This function should be called once at startup to populate the cache
 * All subsequent calls to hardware_info_get() will return cached data
 *
 * @return true on success, false on failure
 */
bool hardware_info_init_cache(void);

/**
 * Get cached hardware information
 * Returns the cached hardware information without re-parsing bootlog
 * This is much faster and safer than hardware_info_collect()
 *
 * @param info Pointer to hardware_info_t structure to fill with cached data
 * @return true if cache is available, false if not initialized
 */
bool hardware_info_get(hardware_info_t* info);

#endif // HARDWARE_INFO_H