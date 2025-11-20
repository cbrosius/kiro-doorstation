#ifndef GPIO_HANDLER_H
#define GPIO_HANDLER_H
#define I2S_SD_OUT_PIN      32
#define I2S_SD_IN_PIN       33

typedef enum {
    DOORBELL_1 = 1,
    DOORBELL_2 = 2
} doorbell_t;

void gpio_handler_init(void);
void door_relay_activate(void);
void light_relay_toggle(void);
bool is_doorbell_pressed(doorbell_t bell);

/**
 * @brief Start monitoring BOOT button for password reset and doorbell simulation
 * 
 * Monitors the BOOT button (GPIO 0):
 * - Short press (1 second): Triggers doorbell call (for testing)
 * - Long press (10 seconds): Triggers password reset
 */
void gpio_start_reset_monitor(void);

// Doorbell queue for hardware testing access
extern QueueHandle_t doorbell_queue;

#endif