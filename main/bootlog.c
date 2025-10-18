#include "bootlog.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static const char *TAG = "bootlog";

// Bootlog buffer
#define BOOTLOG_MAX_SIZE 8192  // 8KB should be enough for boot logs
static char bootlog_buffer[BOOTLOG_MAX_SIZE];
static size_t bootlog_length = 0;
static bool bootlog_active = false;
static SemaphoreHandle_t bootlog_mutex = NULL;

// Custom log handler to capture ESP_LOG messages
static vprintf_like_t original_vprintf = NULL;

static int bootlog_vprintf(const char *format, va_list args)
{
    // First, call the original vprintf to maintain normal logging
    if (original_vprintf) {
        original_vprintf(format, args);
    }

    // If bootlog capture is active, also capture to our buffer
    if (bootlog_active && bootlog_mutex) {
        if (xSemaphoreTake(bootlog_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            // Check if we have space for more data
            if (bootlog_length < BOOTLOG_MAX_SIZE - 256) {  // Leave some margin
                int written = vsnprintf(bootlog_buffer + bootlog_length,
                                      BOOTLOG_MAX_SIZE - bootlog_length,
                                      format, args);

                if (written > 0 && bootlog_length + written < BOOTLOG_MAX_SIZE) {
                    bootlog_length += written;

                    // Check if this log message indicates we should stop capturing
                    // Look for "MAIN: All components initialized"
                    char temp_buffer[256];
                    vsnprintf(temp_buffer, sizeof(temp_buffer), format, args);
                    if (strstr(temp_buffer, "MAIN: All components initialized")) {
                        bootlog_active = false;
                        ESP_LOGI(TAG, "Bootlog capture stopped at 'MAIN: All components initialized'");
                    }
                }
            }
            xSemaphoreGive(bootlog_mutex);
        }
    }

    return 0;  // Return value not used by ESP-IDF
}

void bootlog_init(void)
{
    ESP_LOGI(TAG, "Initializing bootlog capture");

    // Create mutex for thread safety
    bootlog_mutex = xSemaphoreCreateMutex();
    if (bootlog_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create bootlog mutex");
        return;
    }

    // Initialize buffer
    memset(bootlog_buffer, 0, sizeof(bootlog_buffer));
    bootlog_length = 0;
    bootlog_active = true;

    // Install our custom vprintf handler
    original_vprintf = esp_log_set_vprintf(bootlog_vprintf);

    ESP_LOGI(TAG, "Bootlog capture initialized, capturing all ESP_LOG messages");
}

const char* bootlog_get(void)
{
    if (bootlog_length == 0) {
        return NULL;
    }

    // Ensure buffer is null-terminated
    if (bootlog_length < BOOTLOG_MAX_SIZE) {
        bootlog_buffer[bootlog_length] = '\0';
    }

    return bootlog_buffer;
}

void bootlog_finalize(void)
{
    ESP_LOGI(TAG, "Finalizing bootlog capture");

    // Restore original vprintf handler BEFORE setting active to false
    if (original_vprintf) {
        esp_log_set_vprintf(original_vprintf);
        original_vprintf = NULL;
    }

    bootlog_active = false;

    // Ensure buffer is null-terminated
    if (bootlog_length < BOOTLOG_MAX_SIZE) {
        bootlog_buffer[bootlog_length] = '\0';
    }

    ESP_LOGI(TAG, "Bootlog capture finalized, captured %d bytes of log data", bootlog_length);
}