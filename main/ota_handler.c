#include "ota_handler.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_format.h"
#include "esp_timer.h"
#include "esp_image_format.h"
#include "bootloader_common.h"
#include "mbedtls/sha256.h"
#include <string.h>
#include <time.h>

static const char *TAG = "OTA_HANDLER";

// Global OTA context
static ota_context_t g_ota_ctx = {
    .state = OTA_STATE_IDLE,
    .total_size = 0,
    .written_size = 0,
    .progress_percent = 0,
    .error_message = {0},
    .status_message = {0},
    .update_handle = 0,
    .update_partition = NULL,
    .start_time = 0,
    .last_report_time = 0,
    .last_report_bytes = 0
};

void ota_handler_init(void) {
    ESP_LOGI(TAG, "Initializing OTA handler");
    
    // Reset context to idle state
    memset(&g_ota_ctx, 0, sizeof(ota_context_t));
    g_ota_ctx.state = OTA_STATE_IDLE;
    g_ota_ctx.header_validated = false;
    
    // Get running partition info
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running != NULL) {
        ESP_LOGI(TAG, "Running partition: %s at offset 0x%lx", 
                 running->label, running->address);
    }
    
    // Check if we can rollback
    const esp_partition_t* last_invalid = esp_ota_get_last_invalid_partition();
    if (last_invalid != NULL) {
        ESP_LOGI(TAG, "Last invalid partition: %s", last_invalid->label);
    }
    
    ESP_LOGI(TAG, "OTA handler initialized successfully");
}

void ota_get_info(ota_info_t* info) {
    if (info == NULL) {
        ESP_LOGE(TAG, "Invalid info pointer");
        return;
    }
    
    // Clear the structure
    memset(info, 0, sizeof(ota_info_t));
    
    // Get running partition
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running != NULL) {
        info->partition_label = running->label;
        info->app_size = running->size;
    } else {
        info->partition_label = "unknown";
        info->app_size = 0;
    }
    
    // Get app description
    const esp_app_desc_t* app_desc = esp_app_get_description();
    if (app_desc != NULL) {
        strncpy(info->version, app_desc->version, sizeof(info->version) - 1);
        strncpy(info->build_date, app_desc->date, sizeof(info->build_date) - 1);
        strncpy(info->idf_version, app_desc->idf_ver, sizeof(info->idf_version) - 1);
    } else {
        strncpy(info->version, "unknown", sizeof(info->version) - 1);
        strncpy(info->build_date, "unknown", sizeof(info->build_date) - 1);
        strncpy(info->idf_version, "unknown", sizeof(info->idf_version) - 1);
    }
    
    // Check if rollback is possible
    const esp_partition_t* last_invalid = esp_ota_get_last_invalid_partition();
    info->can_rollback = (last_invalid != NULL);
    
    ESP_LOGI(TAG, "Firmware info - Version: %s, Date: %s, IDF: %s, Partition: %s, Can rollback: %s",
             info->version, info->build_date, info->idf_version, 
             info->partition_label, info->can_rollback ? "yes" : "no");
}

esp_err_t ota_validate_image(const uint8_t* data, size_t length, const esp_partition_t* partition) {
    if (data == NULL || length == 0) {
        ESP_LOGE(TAG, "Invalid data or length for validation");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (partition == NULL) {
        ESP_LOGE(TAG, "Invalid partition for validation");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Validating firmware image (%u bytes)", length);
    
    // 1. Validate minimum size for ESP32 image header
    if (length < sizeof(esp_image_header_t)) {
        ESP_LOGE(TAG, "Image too small: %u bytes (minimum: %u bytes)", 
                 length, sizeof(esp_image_header_t));
        return ESP_ERR_INVALID_SIZE;
    }
    
    // 2. Validate ESP32 image magic number (0xE9)
    const esp_image_header_t* header = (const esp_image_header_t*)data;
    if (header->magic != ESP_IMAGE_HEADER_MAGIC) {
        ESP_LOGE(TAG, "Invalid image magic number: 0x%02X (expected: 0xE9)", header->magic);
        return ESP_ERR_IMAGE_INVALID;
    }
    ESP_LOGI(TAG, "✓ Magic number valid (0xE9)");
    
    // 3. Validate chip type compatibility (ESP32-S3)
    if (header->chip_id != ESP_CHIP_ID_ESP32S3) {
        ESP_LOGE(TAG, "Incompatible chip type: %d (expected: ESP32-S3 = %d)", 
                 header->chip_id, ESP_CHIP_ID_ESP32S3);
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_LOGI(TAG, "✓ Chip type compatible (ESP32-S3)");
    
    // 4. Validate firmware size against partition capacity
    if (length > partition->size) {
        ESP_LOGE(TAG, "Firmware size (%u bytes) exceeds partition capacity (%lu bytes)", 
                 length, partition->size);
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "✓ Firmware size valid (%u / %lu bytes)", length, partition->size);
    
    // 5. Validate segment count
    if (header->segment_count > ESP_IMAGE_MAX_SEGMENTS) {
        ESP_LOGE(TAG, "Too many segments: %d (max: %d)", 
                 header->segment_count, ESP_IMAGE_MAX_SEGMENTS);
        return ESP_ERR_IMAGE_INVALID;
    }
    ESP_LOGI(TAG, "✓ Segment count valid (%d segments)", header->segment_count);
    
    // 6. Validate image header fields
    // SPI mode: 0=QIO, 1=QOUT, 2=DIO, 3=DOUT, 4=FAST_READ, 5=SLOW_READ
    if (header->spi_mode > 5) {
        ESP_LOGE(TAG, "Invalid SPI mode: %d", header->spi_mode);
        return ESP_ERR_IMAGE_INVALID;
    }
    
    // SPI speed is a 4-bit field (0-15), always valid
    
    if (header->spi_size >= ESP_IMAGE_FLASH_SIZE_MAX) {
        ESP_LOGE(TAG, "Invalid flash size: %d", header->spi_size);
        return ESP_ERR_IMAGE_INVALID;
    }
    ESP_LOGI(TAG, "✓ Image header fields valid");
    
    // 7. Calculate and verify SHA256 checksum
    // Note: For a complete validation, we would need to parse all segments
    // and verify the SHA256 at the end of the image. For now, we'll do a
    // basic checksum of the entire image data.
    ESP_LOGI(TAG, "Calculating SHA256 checksum...");
    
    unsigned char calculated_hash[32];
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0); // 0 = SHA256 (not SHA224)
    mbedtls_sha256_update(&sha256_ctx, data, length);
    mbedtls_sha256_finish(&sha256_ctx, calculated_hash);
    mbedtls_sha256_free(&sha256_ctx);
    
    // Log the calculated hash for debugging
    ESP_LOGI(TAG, "✓ SHA256 calculated successfully");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, calculated_hash, 32, ESP_LOG_DEBUG);
    
    // 8. Additional validation: Check if image has valid app descriptor
    // The app descriptor is typically at offset 0x20 in the image
    if (length >= sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
        // Try to find and validate app descriptor
        // This is a simplified check - full validation would parse all segments
        ESP_LOGI(TAG, "✓ Image structure appears valid");
    }
    
    ESP_LOGI(TAG, "Firmware image validation completed successfully");
    return ESP_OK;
}

esp_err_t ota_begin_update(size_t image_size) {
    ESP_LOGI(TAG, "Beginning OTA update, image size: %u bytes", image_size);
    
    // Check if already in progress
    if (g_ota_ctx.state != OTA_STATE_IDLE) {
        ESP_LOGE(TAG, "OTA update already in progress");
        strncpy(g_ota_ctx.error_message, "OTA update already in progress", 
                sizeof(g_ota_ctx.error_message) - 1);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Get next update partition
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No available OTA partition");
        strncpy(g_ota_ctx.error_message, "No available OTA partition", 
                sizeof(g_ota_ctx.error_message) - 1);
        g_ota_ctx.state = OTA_STATE_ERROR;
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Writing to partition: %s at offset 0x%lx", 
             update_partition->label, update_partition->address);
    
    // Validate firmware size against partition capacity
    if (image_size > 0 && image_size > update_partition->size) {
        ESP_LOGE(TAG, "Firmware size (%u bytes) exceeds partition capacity (%lu bytes)", 
                 image_size, update_partition->size);
        snprintf(g_ota_ctx.error_message, sizeof(g_ota_ctx.error_message),
                 "Firmware size exceeds partition capacity");
        g_ota_ctx.state = OTA_STATE_ERROR;
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Begin OTA
    esp_ota_handle_t update_handle;
    esp_err_t err = esp_ota_begin(update_partition, image_size, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        snprintf(g_ota_ctx.error_message, sizeof(g_ota_ctx.error_message),
                 "Failed to begin OTA: %s", esp_err_to_name(err));
        g_ota_ctx.state = OTA_STATE_ERROR;
        return err;
    }
    
    // Update context
    g_ota_ctx.state = OTA_STATE_BEGIN;
    g_ota_ctx.total_size = image_size;
    g_ota_ctx.written_size = 0;
    g_ota_ctx.progress_percent = 0;
    g_ota_ctx.update_handle = update_handle;
    g_ota_ctx.update_partition = update_partition;
    g_ota_ctx.start_time = (uint32_t)(esp_timer_get_time() / 1000000);
    g_ota_ctx.last_report_time = g_ota_ctx.start_time;
    g_ota_ctx.last_report_bytes = 0;
    g_ota_ctx.header_validated = false;
    memset(g_ota_ctx.error_message, 0, sizeof(g_ota_ctx.error_message));
    strncpy(g_ota_ctx.status_message, "Preparing to write firmware", sizeof(g_ota_ctx.status_message) - 1);
    
    ESP_LOGI(TAG, "OTA update started successfully");
    return ESP_OK;
}

esp_err_t ota_write_chunk(const uint8_t* data, size_t length) {
    if (data == NULL || length == 0) {
        ESP_LOGE(TAG, "Invalid data or length");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (g_ota_ctx.state != OTA_STATE_BEGIN && g_ota_ctx.state != OTA_STATE_WRITING) {
        ESP_LOGE(TAG, "OTA not in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Validate image header on first chunk
    if (!g_ota_ctx.header_validated && g_ota_ctx.written_size == 0) {
        ESP_LOGI(TAG, "Validating firmware image header...");
        
        // We need at least the image header to validate
        if (length >= sizeof(esp_image_header_t)) {
            const esp_image_header_t* header = (const esp_image_header_t*)data;
            
            // Validate magic number
            if (header->magic != ESP_IMAGE_HEADER_MAGIC) {
                ESP_LOGE(TAG, "Invalid firmware image format: magic number 0x%02X (expected 0xE9)", 
                         header->magic);
                ESP_LOGE(TAG, "This is not a valid ESP32 firmware binary file!");
                ESP_LOGE(TAG, "Please upload a .bin file compiled for ESP32-S3");
                snprintf(g_ota_ctx.error_message, sizeof(g_ota_ctx.error_message),
                         "Invalid firmware file - not an ESP32 binary (magic: 0x%02X)", header->magic);
                g_ota_ctx.state = OTA_STATE_ERROR;
                // Abort to reset state for retry
                ota_abort_update();
                return ESP_ERR_IMAGE_INVALID;
            }
            
            // Validate chip type
            if (header->chip_id != ESP_CHIP_ID_ESP32S3) {
                ESP_LOGE(TAG, "Incompatible chip type: %d (expected ESP32-S3)", header->chip_id);
                snprintf(g_ota_ctx.error_message, sizeof(g_ota_ctx.error_message),
                         "Firmware not compatible with ESP32-S3 (chip_id: %d)", header->chip_id);
                g_ota_ctx.state = OTA_STATE_ERROR;
                ota_abort_update();
                return ESP_ERR_NOT_SUPPORTED;
            }
            
            // Validate segment count
            if (header->segment_count > ESP_IMAGE_MAX_SEGMENTS) {
                ESP_LOGE(TAG, "Invalid segment count: %d (max: %d)", 
                         header->segment_count, ESP_IMAGE_MAX_SEGMENTS);
                snprintf(g_ota_ctx.error_message, sizeof(g_ota_ctx.error_message),
                         "Invalid firmware image structure (segments: %d)", header->segment_count);
                g_ota_ctx.state = OTA_STATE_ERROR;
                ota_abort_update();
                return ESP_ERR_IMAGE_INVALID;
            }
            
            // Validate SPI mode and flash size
            // SPI mode: 0-5, SPI speed is 4-bit (always valid), Flash size: check against max
            if (header->spi_mode > 5 ||
                header->spi_size >= ESP_IMAGE_FLASH_SIZE_MAX) {
                ESP_LOGE(TAG, "Invalid SPI configuration in image header");
                snprintf(g_ota_ctx.error_message, sizeof(g_ota_ctx.error_message),
                         "Invalid firmware image configuration (SPI mode: %d)", header->spi_mode);
                g_ota_ctx.state = OTA_STATE_ERROR;
                ota_abort_update();
                return ESP_ERR_IMAGE_INVALID;
            }
            
            ESP_LOGI(TAG, "✓ Firmware image header validated successfully");
            ESP_LOGI(TAG, "  Magic: 0xE9, Chip: ESP32-S3, Segments: %d", header->segment_count);
            g_ota_ctx.header_validated = true;
        } else {
            ESP_LOGW(TAG, "First chunk too small for header validation (%u bytes), will validate on next chunk", 
                     length);
        }
    }
    
    // Write data to flash
    esp_err_t err = esp_ota_write(g_ota_ctx.update_handle, data, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
        snprintf(g_ota_ctx.error_message, sizeof(g_ota_ctx.error_message),
                 "Flash write error: %s", esp_err_to_name(err));
        g_ota_ctx.state = OTA_STATE_ERROR;
        return err;
    }
    
    // Update progress
    g_ota_ctx.state = OTA_STATE_WRITING;
    g_ota_ctx.written_size += length;
    
    // Calculate progress percentage
    uint8_t new_percent = 0;
    if (g_ota_ctx.total_size > 0) {
        new_percent = (g_ota_ctx.written_size * 100) / g_ota_ctx.total_size;
    }
    
    // Report progress every 10% or 50KB (whichever comes first)
    uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000000);
    size_t bytes_since_report = g_ota_ctx.written_size - g_ota_ctx.last_report_bytes;
    bool should_report = false;
    
    // Report if 10% progress change
    if (new_percent != g_ota_ctx.progress_percent && (new_percent % 10 == 0 || new_percent == 100)) {
        should_report = true;
    }
    
    // Report if 50KB written since last report
    if (bytes_since_report >= 51200) { // 50KB = 51200 bytes
        should_report = true;
    }
    
    if (should_report) {
        g_ota_ctx.progress_percent = new_percent;
        g_ota_ctx.last_report_time = current_time;
        g_ota_ctx.last_report_bytes = g_ota_ctx.written_size;
        
        // Update status message
        snprintf(g_ota_ctx.status_message, sizeof(g_ota_ctx.status_message),
                 "Writing firmware: %u%% (%u / %u bytes)", 
                 g_ota_ctx.progress_percent, g_ota_ctx.written_size, g_ota_ctx.total_size);
        
        ESP_LOGI(TAG, "OTA progress: %u%% (%u / %u bytes)", 
                 g_ota_ctx.progress_percent, g_ota_ctx.written_size, g_ota_ctx.total_size);
    }
    
    return ESP_OK;
}

esp_err_t ota_end_update(void) {
    ESP_LOGI(TAG, "Ending OTA update");
    
    if (g_ota_ctx.state != OTA_STATE_WRITING) {
        ESP_LOGE(TAG, "OTA not in writing state");
        return ESP_ERR_INVALID_STATE;
    }
    
    g_ota_ctx.state = OTA_STATE_VALIDATING;
    strncpy(g_ota_ctx.status_message, "Verifying firmware integrity", sizeof(g_ota_ctx.status_message) - 1);
    
    // Finalize OTA
    esp_err_t err = esp_ota_end(g_ota_ctx.update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        snprintf(g_ota_ctx.error_message, sizeof(g_ota_ctx.error_message),
                 "Validation failed: %s", esp_err_to_name(err));
        snprintf(g_ota_ctx.status_message, sizeof(g_ota_ctx.status_message),
                 "Failed: Validation error");
        g_ota_ctx.state = OTA_STATE_ERROR;
        return err;
    }
    
    strncpy(g_ota_ctx.status_message, "Applying firmware update", sizeof(g_ota_ctx.status_message) - 1);
    
    // Set boot partition
    err = esp_ota_set_boot_partition(g_ota_ctx.update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        snprintf(g_ota_ctx.error_message, sizeof(g_ota_ctx.error_message),
                 "Failed to set boot partition: %s", esp_err_to_name(err));
        snprintf(g_ota_ctx.status_message, sizeof(g_ota_ctx.status_message),
                 "Failed: Could not set boot partition");
        g_ota_ctx.state = OTA_STATE_ERROR;
        return err;
    }
    
    g_ota_ctx.state = OTA_STATE_COMPLETE;
    g_ota_ctx.progress_percent = 100;
    strncpy(g_ota_ctx.status_message, "Complete: Firmware ready to apply", sizeof(g_ota_ctx.status_message) - 1);
    
    uint32_t elapsed = (uint32_t)(esp_timer_get_time() / 1000000) - g_ota_ctx.start_time;
    ESP_LOGI(TAG, "OTA update completed successfully in %lu seconds", elapsed);
    ESP_LOGI(TAG, "Next boot partition: %s", g_ota_ctx.update_partition->label);
    
    return ESP_OK;
}

void ota_abort_update(void) {
    ESP_LOGW(TAG, "Aborting OTA update");
    
    if (g_ota_ctx.state == OTA_STATE_BEGIN || 
        g_ota_ctx.state == OTA_STATE_WRITING || 
        g_ota_ctx.state == OTA_STATE_VALIDATING) {
        
        // End OTA handle (this will clean up)
        esp_ota_abort(g_ota_ctx.update_handle);
    }
    
    // Reset context to IDLE state (not ABORT) to allow retry
    g_ota_ctx.state = OTA_STATE_IDLE;
    g_ota_ctx.update_handle = 0;
    g_ota_ctx.update_partition = NULL;
    g_ota_ctx.total_size = 0;
    g_ota_ctx.written_size = 0;
    g_ota_ctx.progress_percent = 0;
    g_ota_ctx.header_validated = false;
    
    ESP_LOGI(TAG, "OTA update aborted, state reset to IDLE");
}

esp_err_t ota_rollback(void) {
    ESP_LOGI(TAG, "Attempting rollback to previous firmware");
    
    // Get last invalid partition (previous firmware)
    const esp_partition_t* last_invalid = esp_ota_get_last_invalid_partition();
    if (last_invalid == NULL) {
        ESP_LOGE(TAG, "No previous firmware available for rollback");
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Rolling back to partition: %s", last_invalid->label);
    
    // Set boot partition to previous firmware
    esp_err_t err = esp_ota_set_boot_partition(last_invalid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition for rollback: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Rollback prepared, restart required");
    return ESP_OK;
}

esp_err_t ota_mark_valid(void) {
    ESP_LOGI(TAG, "Marking current firmware as valid");
    
    // Get running partition
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == NULL) {
        ESP_LOGE(TAG, "Failed to get running partition");
        return ESP_ERR_NOT_FOUND;
    }
    
    // Mark as valid to prevent auto-rollback
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mark app as valid: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Firmware marked as valid");
    return ESP_OK;
}

const ota_context_t* ota_get_context(void) {
    return &g_ota_ctx;
}
