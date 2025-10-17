#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

// OTA state machine states
typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_BEGIN,
    OTA_STATE_WRITING,
    OTA_STATE_VALIDATING,
    OTA_STATE_COMPLETE,
    OTA_STATE_ABORT,
    OTA_STATE_ERROR
} ota_state_t;

// OTA context structure
typedef struct {
    ota_state_t state;
    size_t total_size;
    size_t written_size;
    uint8_t progress_percent;
    char error_message[128];
    esp_ota_handle_t update_handle;
    const esp_partition_t* update_partition;
    uint32_t start_time;
} ota_context_t;

// Firmware information structure
typedef struct {
    char version[32];
    char build_date[32];
    char idf_version[32];
    uint32_t app_size;
    const char* partition_label;
    bool can_rollback;
} ota_info_t;

/**
 * @brief Initialize OTA handler
 */
void ota_handler_init(void);

/**
 * @brief Get current firmware information
 * 
 * @param info Pointer to ota_info_t structure to fill
 */
void ota_get_info(ota_info_t* info);

/**
 * @brief Start OTA update process
 * 
 * @param image_size Size of the firmware image (0 for unknown)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ota_begin_update(size_t image_size);

/**
 * @brief Write firmware chunk
 * 
 * @param data Pointer to firmware data
 * @param length Length of data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ota_write_chunk(const uint8_t* data, size_t length);

/**
 * @brief Finalize and validate update
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ota_end_update(void);

/**
 * @brief Abort update process
 */
void ota_abort_update(void);

/**
 * @brief Rollback to previous firmware
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ota_rollback(void);

/**
 * @brief Mark current firmware as valid (prevent auto-rollback)
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ota_mark_valid(void);

#ifdef __cplusplus
}
#endif

#endif // OTA_HANDLER_H
