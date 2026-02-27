#include "audio_handler.h"
#include "gpio_handler.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2s_std.h"

static const char *TAG = "AUDIO";
static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;
static bool audio_hardware_present = false;

void audio_handler_init(void)
{
    ESP_LOGW(TAG, "Audio handler initializing - hardware not connected, using dummy mode");
    audio_hardware_present = false;
    ESP_LOGI(TAG, "Audio handler initialized (dummy mode)");
}

void audio_start_recording(void)
{
    if (audio_hardware_present) {
        ESP_LOGI(TAG, "Audio recording started");
        if (rx_handle) {
            i2s_channel_enable(rx_handle);
        }
    } else {
        ESP_LOGW(TAG, "Audio recording started (dummy - hardware not connected)");
    }
}

void audio_stop_recording(void)
{
    if (audio_hardware_present) {
        ESP_LOGI(TAG, "Audio recording stopped");
        if (rx_handle) {
            i2s_channel_disable(rx_handle);
        }
    } else {
        ESP_LOGW(TAG, "Audio recording stopped (dummy - hardware not connected)");
    }
}

void audio_start_playback(void)
{
    if (audio_hardware_present) {
        ESP_LOGI(TAG, "Audio playback started");
        if (tx_handle) {
            i2s_channel_enable(tx_handle);
        }
    } else {
        ESP_LOGW(TAG, "Audio playback started (dummy - hardware not connected)");
    }
}

void audio_stop_playback(void)
{
    if (audio_hardware_present) {
        ESP_LOGI(TAG, "Audio playback stopped");
        if (tx_handle) {
            i2s_channel_disable(tx_handle);
        }
    } else {
        ESP_LOGW(TAG, "Audio playback stopped (dummy - hardware not connected)");
    }
}

size_t audio_read(int16_t *buffer, size_t length)
{
    if (audio_hardware_present) {
        size_t bytes_read = 0;
        if (rx_handle) {
            esp_err_t ret = i2s_channel_read(rx_handle, buffer, length * sizeof(int16_t), &bytes_read, portMAX_DELAY);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "I2S read error: %s", esp_err_to_name(ret));
                return 0;
            }
        }
        return bytes_read / sizeof(int16_t);
    } else {
        ESP_LOGW(TAG, "Audio read (dummy - hardware not connected) - returning silence");
        memset(buffer, 0, length * sizeof(int16_t));
        return length;
    }
}

size_t audio_write(const int16_t *buffer, size_t length)
{
    if (audio_hardware_present) {
        size_t bytes_written = 0;
        if (tx_handle) {
            esp_err_t ret = i2s_channel_write(tx_handle, buffer, length * sizeof(int16_t), &bytes_written, portMAX_DELAY);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "I2S write error: %s", esp_err_to_name(ret));
                return 0;
            }
        }
        return bytes_written / sizeof(int16_t);
    } else {
        ESP_LOGW(TAG, "Audio write (dummy - hardware not connected) - ignoring data");
        return length;
    }
}