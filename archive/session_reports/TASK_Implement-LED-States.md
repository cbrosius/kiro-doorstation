# Tasks to Implement Multicolor LED State Display on ESP32-S3

## Overview
Implement control of the integrated WS2812B RGB LED connected to GPIO48 on the ESP32-S3 development board using esp-idf. The LED will indicate the current state of the device by changing color or blinking patterns.

---

## Implementation Summary

### Hardware Configuration
- **LED Type**: WS2812B RGB LED (addressable RGB LED)
- **GPIO Pin**: GPIO48 (data pin for WS2812B protocol)
- **Control Method**: RMT (Remote Control) peripheral for precise timing
- **esp-idf Version**: 5.5.x. The legacy RMT driver is deprecated; use `driver/rmt_tx.h` and the corresponding new API.

### Device State to LED Mapping
- **Init**: Yellow, slow blink
- **WiFi Connecting**: Blue, fast blink
- **WiFi Connected**: Blue, solid
- **SIP Connecting**: Orange, slow blink
- **SIP Registered**: Green, solid
- **Call Incoming/Outgoing**: Magenta, fast blink
- **Call Active**: Red, solid
- **Error**: Red, fast blink
- **Ringing**: Magenta, slow blink
- **Connected (Idle)**: Green, pulsing (breathing effect)

### Files Created/Modified

#### New Files
- `main/led_handler.h` - LED control module header
- `main/led_handler.c` - LED control module implementation

#### Modified Files
- `main/CMakeLists.txt` - Add `led_handler.c` to build sources.
- `main/main.c` - Add LED initialization and state updates for boot and WiFi.
- `main/sip_client.c` - Add LED state updates on SIP events.

---

## Detailed API and File Structure

### `main/led_handler.h`
```c
#ifndef LED_HANDLER_H
#define LED_HANDLER_H

#include "esp_err.h"
#include <stdint.h>

// Enum for all possible device states the LED can represent
typedef enum {
    LED_STATE_INIT,
    LED_STATE_WIFI_CONNECTING,
    LED_STATE_WIFI_CONNECTED,
    LED_STATE_SIP_CONNECTING,
    LED_STATE_SIP_REGISTERED,
    LED_STATE_CALL_INCOMING,
    LED_STATE_CALL_OUTGOING,
    LED_STATE_CALL_ACTIVE,
    LED_STATE_RINGING,
    LED_STATE_ERROR,
    LED_STATE_IDLE, // Registered and waiting
} led_state_t;

/**
 * @brief Initializes the LED handler, RMT peripheral, and the LED control task.
 *
 * @return esp_err_t ESP_OK on success, or an error code from the RMT driver.
 */
esp_err_t led_handler_init(void);

/**
 * @brief Sets the current state of the LED.
 * This function is thread-safe and can be called from any task.
 *
 * @param state The new state to display on the LED.
 */
void led_handler_set_state(led_state_t state);

/**
 * @brief Gets the current state of the LED.
 *
 * @return led_state_t The current LED state.
 */
led_state_t led_handler_get_current_state(void);

#endif // LED_HANDLER_H
```

### `main/led_handler.c` Structure
This file will contain the RMT configuration and the FreeRTOS task for managing patterns.

```c
#include "led_handler.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"

static const char *TAG = "LED_HANDLER";

// RMT and LED configuration
#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution
#define RMT_LED_STRIP_GPIO_NUM      48
#define LED_STRIP_LENGTH            1

// FreeRTOS task handle
static TaskHandle_t s_led_task_handle = NULL;
static led_state_t s_current_state = LED_STATE_INIT;

// RMT channel and encoder handles
static rmt_channel_handle_t s_led_chan = NULL;
static rmt_encoder_handle_t s_led_encoder = NULL;

// Main task for updating LED patterns (blinking, pulsing)
static void led_control_task(void *pvParameters) {
    // Task loop
    while (1) {
        // 1. Determine color and pattern based on s_current_state
        // 2. Implement logic for blinking, pulsing, or solid color
        // 3. Use the RMT driver to transmit the color data to the WS2812B LED
        // 4. Use vTaskDelay for timing (e.g., 50ms for smooth pulsing)
    }
}

esp_err_t led_handler_init(void) {
    ESP_LOGI(TAG, "Initializing WS2812B LED control");

    // 1. Configure RMT TX channel
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = RMT_LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    // Create channel...

    // 2. Install the WS2812B encoder
    // See ESP-IDF documentation for `rmt_new_bytes_encoder` and `rmt_new_led_strip_encoder`
    
    // 3. Enable the RMT channel

    // 4. Create the FreeRTOS task
    xTaskCreate(led_control_task, "led_control_task", 2048, NULL, 5, &s_led_task_handle);

    return ESP_OK;
}

void led_handler_set_state(led_state_t state) {
    // This function just updates the shared state variable.
    // The control task will see the change and update the LED pattern.
    s_current_state = state;
}

// ... other functions ...
```

---

## Integration Examples

### `main/main.c`
In `app_main`, you'll initialize the handler and set states during the boot process.

```c
// At the top of app_main
#include "led_handler.h"

void app_main(void) {
    // ... other initializations ...
    
    // Initialize LED Handler
    led_handler_init();
    led_handler_set_state(LED_STATE_INIT);

    // ... NVS, GPIO init ...

    // Before starting WiFi Manager
    led_handler_set_state(LED_STATE_WIFI_CONNECTING);
    wifi_manager_init();

    // After waiting for IP address
    while (!wifi_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    led_handler_set_state(LED_STATE_WIFI_CONNECTED);

    // ... rest of app_main ...
}
```

### `main/sip_client.c`
In the `sip_task`, update the LED state whenever the `current_state` of the SIP client changes.

```c
// Include the header
#include "led_handler.h"

// Inside the sip_task, whenever `current_state` is updated:
// Example 1: Registration starts
current_state = SIP_STATE_REGISTERING;
led_handler_set_state(LED_STATE_SIP_CONNECTING);

// Example 2: Registration succeeds
current_state = SIP_STATE_REGISTERED;
led_handler_set_state(LED_STATE_SIP_REGISTERED); // Or LED_STATE_IDLE if that's preferred

// Example 3: Call becomes active
current_state = SIP_STATE_CONNECTED;
led_handler_set_state(LED_STATE_CALL_ACTIVE);

// Example 4: Authentication fails
current_state = SIP_STATE_AUTH_FAILED;
led_handler_set_state(LED_STATE_ERROR);
```

---

## Updated Task Completion Status

- [ ] Analyze ESP32-S3 LED hardware and GPIO48 configuration.
- [ ] Create `main/led_handler.h` with the defined API and enums.
- [ ] Create `main/led_handler.c` with RMT configuration and function stubs.
- [ ] **Implement the `led_control_task` FreeRTOS task for animations (blink, pulse).**
- [ ] Map each `led_state_t` to a specific color and pattern inside the control task.
- [ ] Add `led_handler.c` to `main/CMakeLists.txt`.
- [ ] Integrate `led_handler_init()` and `led_handler_set_state()` calls into `main.c`.
- [ ] Integrate `led_handler_set_state()` calls into `sip_client.c` for all relevant state changes.
- [ ] Test all defined LED states on the physical ESP32-S3 board.
- [ ] Debug and fine-tune LED timing and colors as needed.
- [ ] Document the final implementation and state mappings.

---

## Testing Instructions

1. Build the project with ESP-IDF.
2. Flash to ESP32-S3 board with WS2812B LED on GPIO48.
3. Observe LED behavior during:
   - Device boot (`Init` state)
   - WiFi connection (`WiFi Connecting` -> `WiFi Connected`)
   - SIP registration (`SIP Connecting` -> `SIP Registered` -> `Idle`)
   - Outgoing and incoming calls (`Call Outgoing`/`Ringing` -> `Call Active`)
   - Error conditions (e.g., SIP auth failure)

---

## Future Enhancements

- Add configurable LED brightness levels.
- Implement custom color schemes via web interface.
- Add LED test patterns for hardware validation.
- Support for multiple WS2812B LEDs in a strip.

---
