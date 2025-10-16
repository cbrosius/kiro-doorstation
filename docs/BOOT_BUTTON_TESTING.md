# BOOT Button Testing Feature

## Overview
The ESP32-S3's physical BOOT button (GPIO 0) has been configured as a doorbell simulator for easy testing without external hardware.

## Hardware
- **GPIO Pin**: GPIO 0 (built-in BOOT button on ESP32-S3)
- **Configuration**: Input with pull-up, interrupt on falling edge
- **Behavior**: Active low (triggers when pressed)

## Functionality
When the BOOT button is pressed:
1. Interrupt is triggered on falling edge (button press)
2. ISR handler logs the button press
3. SIP call is initiated to `apartment1@example.com`
4. Same behavior as physical doorbell 1 (GPIO 21)

## Usage
1. Ensure ESP32 is connected to WiFi
2. Ensure SIP client is registered
3. Press the BOOT button on your ESP32-S3 board
4. A SIP call will be initiated
5. Check the web interface SIP log for confirmation

## Code Implementation

### GPIO Configuration
```c
#define BOOT_BUTTON_PIN 0  // ESP32-S3 BOOT button

gpio_config_t boot_button_config = {
    .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_NEGEDGE
};
```

### ISR Handler
The ISR handler sends an event to a queue for processing in task context (ISR-safe):

```c
static void IRAM_ATTR boot_button_isr_handler(void* arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Send DOORBELL_1 event to queue
    doorbell_event_t event = { .bell = DOORBELL_1 };
    xQueueSendFromISR(doorbell_queue, &event, &xHigherPriorityTaskWoken);
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

### Task Handler
The actual call is made from a FreeRTOS task (not ISR context):

```c
static void doorbell_task(void *arg)
{
    doorbell_event_t event;
    
    while (1) {
        if (xQueueReceive(doorbell_queue, &event, portMAX_DELAY)) {
            if (event.bell == DOORBELL_1) {
                ESP_LOGI(TAG, "Doorbell 1 pressed");
                sip_client_make_call("apartment1@example.com");
            }
        }
    }
}
```

## Testing Workflow
1. **Initial Setup**
   - Configure WiFi credentials
   - Configure SIP server settings
   - Connect to SIP server

2. **Test Call**
   - Press BOOT button
   - Observe SIP log in web interface
   - Verify INVITE message is sent
   - Check for 100 Trying, 180 Ringing, 200 OK responses

3. **During Call**
   - Test DTMF codes (*1 for door, *2 for light)
   - Test call termination with #

## Benefits
- No external wiring required for testing
- Easy to trigger test calls
- Convenient for development and debugging
- Same code path as real doorbell buttons
- Built-in 2-second debounce prevents accidental double-presses

## Notes
- BOOT button is shared with bootloader entry (hold during reset)
- Normal press during runtime triggers doorbell function
- Does not interfere with normal ESP32 boot process
- Can be disabled in production by removing ISR handler registration
- 2-second debounce prevents multiple calls from single press
- Calls are only initiated when SIP is in IDLE or REGISTERED state

## Related Files
- `main/gpio_handler.c` - GPIO initialization and ISR handlers
- `main/gpio_handler.h` - Pin definitions
- `main/sip_client.c` - SIP call handling
