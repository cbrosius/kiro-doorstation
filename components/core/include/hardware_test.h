#ifndef HARDWARE_TEST_H
#define HARDWARE_TEST_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "gpio_handler.h"

/**
 * Hardware state structure for reporting current GPIO states
 */
typedef struct {
    bool door_relay_active;        // Door opener relay state
    bool light_relay_active;       // Light relay state
    bool bell1_pressed;            // Doorbell 1 button state
    bool bell2_pressed;            // Doorbell 2 button state
    uint32_t door_relay_remaining_ms;  // Remaining time for door opener test
} hardware_state_t;

/**
 * Initialize hardware test module
 * Must be called after gpio_handler_init()
 */
void hardware_test_init(void);

/**
 * Test doorbell button simulation
 * Simulates a physical button press and triggers SIP call if configured
 * 
 * @param bell Doorbell number (DOORBELL_1 or DOORBELL_2)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if bell number is invalid
 */
esp_err_t hardware_test_doorbell(doorbell_t bell);

/**
 * Test door opener relay with specified duration
 * Activates door relay for the specified duration with automatic timeout
 * 
 * @param duration_ms Duration in milliseconds (1000-10000)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if duration invalid,
 *         ESP_ERR_INVALID_STATE if test already active
 */
esp_err_t hardware_test_door_opener(uint32_t duration_ms);

/**
 * Test light relay toggle
 * Toggles the light relay state (on/off)
 * 
 * @param new_state Pointer to bool to store the new state (true=on, false=off)
 * @return ESP_OK on success
 */
esp_err_t hardware_test_light_toggle(bool* new_state);

/**
 * Get current hardware state
 * Returns the current state of all GPIO inputs and outputs
 * 
 * @param state Pointer to hardware_state_t structure to fill
 */
void hardware_test_get_state(hardware_state_t* state);

/**
 * Emergency stop - immediately deactivate all active tests
 * Stops door opener test and deactivates relay
 * 
 * @return ESP_OK on success
 */
esp_err_t hardware_test_stop_all(void);

#endif // HARDWARE_TEST_H
