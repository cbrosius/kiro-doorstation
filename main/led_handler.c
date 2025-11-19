#include "led_handler.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "LED_HANDLER";

// RMT and LED configuration
#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution, 1 tick = 0.1us
#define RMT_LED_STRIP_GPIO_NUM      48
#define LED_STRIP_LENGTH            1

// --- RMT specific configuration for WS2812B ---
// These values are based on the WS2812B datasheet
#define WS2812_T0H_NS 400
#define WS2812_T0L_NS 850
#define WS2812_T1H_NS 800
#define WS2812_T1L_NS 450
#define WS2812_RESET_US 280

// --- Pattern Timing ---
#define LED_SOLID_INTERVAL_MS       1000 // Update solid color every second
#define LED_SLOW_BLINK_INTERVAL_MS  1000 // 1s on, 1s off
#define LED_FAST_BLINK_INTERVAL_MS  250  // 250ms on, 250ms off
#define LED_PULSE_INTERVAL_MS       20   // Update pulse every 20ms for smoothness

// --- State Variables ---
static TaskHandle_t s_led_task_handle = NULL;
static led_state_t s_current_state = LED_STATE_INIT;
static SemaphoreHandle_t s_state_mutex = NULL;

// RMT channel and encoder handles
static rmt_channel_handle_t s_led_chan = NULL;
static rmt_encoder_handle_t s_led_bytes_encoder = NULL;

// --- RMT Transmit Function ---
static esp_err_t rmt_transmit_led(uint8_t red, uint8_t green, uint8_t blue) {
    if (!s_led_chan || !s_led_bytes_encoder) {
        return ESP_FAIL;
    }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0, // No loop
    };

    uint8_t led_data[] = {green, red, blue}; // WS2812B order is GRB

    return rmt_transmit(s_led_chan, s_led_bytes_encoder, led_data, sizeof(led_data), &tx_config);
}

// --- LED Control Task ---
static void led_control_task(void *pvParameters) {
    uint32_t tick_count = 0;
    bool led_on = true;
    float pulse_brightness = 0.0;

    // Colors for each state (10% brightness)
    const uint8_t color_init[] = {26, 26, 0};   // Yellow
    const uint8_t color_wifi[] = {0, 0, 26};     // Blue
    const uint8_t color_sip[] = {26, 17, 0};    // Orange
    const uint8_t color_registered[] = {0, 26, 0}; // Green
    const uint8_t color_call[] = {26, 0, 26};   // Magenta
    const uint8_t color_active[] = {26, 0, 0};   // Red
    const uint8_t color_error[] = {26, 0, 0};    // Red
    const uint8_t color_idle[] = {0, 26, 0};     // Green

    led_state_t local_state = LED_STATE_INIT;

    while (1) {
        if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
            local_state = s_current_state;
            xSemaphoreGive(s_state_mutex);
        }

        tick_count++;

        switch (local_state) {
            case LED_STATE_INIT: // Yellow, slow blink
                led_on = (tick_count % (LED_SLOW_BLINK_INTERVAL_MS / LED_PULSE_INTERVAL_MS * 2)) < (LED_SLOW_BLINK_INTERVAL_MS / LED_PULSE_INTERVAL_MS);
                if (led_on) {
                    rmt_transmit_led(color_init[0], color_init[1], color_init[2]);
                } else {
                    rmt_transmit_led(0, 0, 0);
                }
                break;

            case LED_STATE_WIFI_CONNECTING: // Blue, fast blink
                led_on = (tick_count % (LED_FAST_BLINK_INTERVAL_MS / LED_PULSE_INTERVAL_MS * 2)) < (LED_FAST_BLINK_INTERVAL_MS / LED_PULSE_INTERVAL_MS);
                if (led_on) {
                    rmt_transmit_led(color_wifi[0], color_wifi[1], color_wifi[2]);
                } else {
                    rmt_transmit_led(0, 0, 0);
                }
                break;

            case LED_STATE_WIFI_CONNECTED: // Blue, solid
                rmt_transmit_led(color_wifi[0], color_wifi[1], color_wifi[2]);
                break;

            case LED_STATE_SIP_CONNECTING: // Orange, slow blink
                led_on = (tick_count % (LED_SLOW_BLINK_INTERVAL_MS / LED_PULSE_INTERVAL_MS * 2)) < (LED_SLOW_BLINK_INTERVAL_MS / LED_PULSE_INTERVAL_MS);
                if (led_on) {
                    rmt_transmit_led(color_sip[0], color_sip[1], color_sip[2]);
                } else {
                    rmt_transmit_led(0, 0, 0);
                }
                break;

            case LED_STATE_SIP_REGISTERED: // Green, solid
                rmt_transmit_led(color_registered[0], color_registered[1], color_registered[2]);
                break;

            case LED_STATE_CALL_INCOMING:
            case LED_STATE_CALL_OUTGOING: // Magenta, fast blink
                led_on = (tick_count % (LED_FAST_BLINK_INTERVAL_MS / LED_PULSE_INTERVAL_MS * 2)) < (LED_FAST_BLINK_INTERVAL_MS / LED_PULSE_INTERVAL_MS);
                if (led_on) {
                    rmt_transmit_led(color_call[0], color_call[1], color_call[2]);
                } else {
                    rmt_transmit_led(0, 0, 0);
                }
                break;
            
            case LED_STATE_RINGING: // Magenta, slow blink
                led_on = (tick_count % (LED_SLOW_BLINK_INTERVAL_MS / LED_PULSE_INTERVAL_MS * 2)) < (LED_SLOW_BLINK_INTERVAL_MS / LED_PULSE_INTERVAL_MS);
                if (led_on) {
                    rmt_transmit_led(color_call[0], color_call[1], color_call[2]);
                } else {
                    rmt_transmit_led(0, 0, 0);
                }
                break;

            case LED_STATE_CALL_ACTIVE: // Red, solid
                rmt_transmit_led(color_active[0], color_active[1], color_active[2]);
                break;

            case LED_STATE_ERROR: // Red, fast blink
                led_on = (tick_count % (LED_FAST_BLINK_INTERVAL_MS / LED_PULSE_INTERVAL_MS * 2)) < (LED_FAST_BLINK_INTERVAL_MS / LED_PULSE_INTERVAL_MS);
                if (led_on) {
                    rmt_transmit_led(color_error[0], color_error[1], color_error[2]);
                } else {
                    rmt_transmit_led(0, 0, 0);
                }
                break;

            case LED_STATE_IDLE: // Green, pulsing
                // Simple sine wave for breathing effect
                pulse_brightness = (sin(tick_count * 0.05) + 1.0) / 2.0;
                rmt_transmit_led(
                    (uint8_t)(color_idle[0] * pulse_brightness),
                    (uint8_t)(color_idle[1] * pulse_brightness),
                    (uint8_t)(color_idle[2] * pulse_brightness)
                );
                break;

            default:
                rmt_transmit_led(0, 0, 0); // Off for unknown states
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(LED_PULSE_INTERVAL_MS));
    }
}

esp_err_t led_handler_init(void) {
    ESP_LOGI(TAG, "Initializing WS2812B LED control");

    s_state_mutex = xSemaphoreCreateMutex();
    if (!s_state_mutex) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_FAIL;
    }

    // 1. Configure RMT TX channel
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = RMT_LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 64, // 64 * 4B = 256B of RAM
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &s_led_chan));
    ESP_LOGI(TAG, "RMT TX channel created");

    // 2. Install the WS2812B encoder
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = WS2812_T0H_NS / (1000000000 / RMT_LED_STRIP_RESOLUTION_HZ),
            .level1 = 0,
            .duration1 = WS2812_T0L_NS / (1000000000 / RMT_LED_STRIP_RESOLUTION_HZ),
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = WS2812_T1H_NS / (1000000000 / RMT_LED_STRIP_RESOLUTION_HZ),
            .level1 = 0,
            .duration1 = WS2812_T1L_NS / (1000000000 / RMT_LED_STRIP_RESOLUTION_HZ),
        },
        .flags.msb_first = 1,
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&bytes_encoder_config, &s_led_bytes_encoder));
    ESP_LOGI(TAG, "RMT bytes encoder created for WS2812B");

    // 3. Enable the RMT channel
    ESP_ERROR_CHECK(rmt_enable(s_led_chan));
    ESP_LOGI(TAG, "RMT channel enabled");

    // 4. Create the FreeRTOS task
    xTaskCreate(led_control_task, "led_control_task", 2048, NULL, 5, &s_led_task_handle);
    if (s_led_task_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create LED control task");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "LED control task created");

    return ESP_OK;
}

void led_handler_set_state(led_state_t state) {
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_current_state = state;
        xSemaphoreGive(s_state_mutex);
    }
}

led_state_t led_handler_get_current_state(void) {
    led_state_t state;
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        state = s_current_state;
        xSemaphoreGive(s_state_mutex);
    } else {
        // Should not happen, but return a safe default
        state = s_current_state;
    }
    return state;
}