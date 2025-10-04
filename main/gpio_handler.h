#ifndef GPIO_HANDLER_H
#define GPIO_HANDLER_H

#include "driver/gpio.h"

// Pin-Definitionen
#define DOORBELL_1_PIN      GPIO_NUM_21
#define DOORBELL_2_PIN      GPIO_NUM_22
#define DOOR_RELAY_PIN      GPIO_NUM_25
#define LIGHT_RELAY_PIN     GPIO_NUM_26

// I2S Audio Pins
#define I2S_SCK_PIN         GPIO_NUM_14
#define I2S_WS_PIN          GPIO_NUM_15
#define I2S_SD_OUT_PIN      GPIO_NUM_32
#define I2S_SD_IN_PIN       GPIO_NUM_33

typedef enum {
    DOORBELL_1 = 1,
    DOORBELL_2 = 2
} doorbell_t;

void gpio_handler_init(void);
void door_relay_activate(void);
void light_relay_toggle(void);
bool is_doorbell_pressed(doorbell_t bell);

#endif