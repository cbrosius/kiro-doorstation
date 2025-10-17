#include "ota_handler.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_format.h"
#include "esp_timer.h"
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
    .update_handle = 0,
    .update_partition = NULL,
    .start_time = 0
};

void ota_handler_init(void) {
    ESP_LOGI(TAG, "Initializing OTA handler");
    
    // Reset context to idle state
    memset(&g_ota_ctx, 0, sizeof(ota_context_t));
    g_ota_ctx.state = OTA_STATE_IDLE;
    
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
    memset(g_ota_ctx.error_message, 0, sizeof(g_ota_ctx.error_message));
    
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
    
    if (g_ota_ctx.total_size > 0) {
        uint8_t new_percent = (g_ota_ctx.written_size * 100) / g_ota_ctx.total_size;
        if (new_percent != g_ota_ctx.progress_percent) {
            g_ota_ctx.progress_percent = new_percent;
            ESP_LOGI(TAG, "OTA progress: %u%% (%u / %u bytes)", 
                     g_ota_ctx.progress_percent, g_ota_ctx.written_size, g_ota_ctx.total_size);
        }
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
    
    // Finalize OTA
    esp_err_t err = esp_ota_end(g_ota_ctx.update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        snprintf(g_ota_ctx.error_message, sizeof(g_ota_ctx.error_message),
                 "Validation failed: %s", esp_err_to_name(err));
        g_ota_ctx.state = OTA_STATE_ERROR;
        return err;
    }
    
    // Set boot partition
    err = esp_ota_set_boot_partition(g_ota_ctx.update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        snprintf(g_ota_ctx.error_message, sizeof(g_ota_ctx.error_message),
                 "Failed to set boot partition: %s", esp_err_to_name(err));
        g_ota_ctx.state = OTA_STATE_ERROR;
        return err;
    }
    
    g_ota_ctx.state = OTA_STATE_COMPLETE;
    g_ota_ctx.progress_percent = 100;
    
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
    
    // Reset context
    g_ota_ctx.state = OTA_STATE_ABORT;
    g_ota_ctx.update_handle = 0;
    g_ota_ctx.update_partition = NULL;
    
    ESP_LOGI(TAG, "OTA update aborted");
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
