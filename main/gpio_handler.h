#ifndef GPIO_HANDLER_H
#define GPIO_HANDLER_H

#include <stdbool.h>

// Pin-Definitionen
#define DOORBELL_1_PIN      21
#define DOORBELL_2_PIN      4
#define BOOT_BUTTON_PIN     0   // ESP32-S3 BOOT button for testing
#define DOOR_RELAY_PIN      5
#define LIGHT_RELAY_PIN     6

// I2S Audio Pins
#define I2S_SCK_PIN         14
#define I2S_WS_PIN          15
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

#endif