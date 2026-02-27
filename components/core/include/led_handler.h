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
