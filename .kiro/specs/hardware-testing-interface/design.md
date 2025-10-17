# Hardware Testing Interface - Design Document

## Overview

This feature provides a web-based hardware testing interface that allows installers and integrators to verify GPIO functionality, relay operations, and button inputs before mounting the ESP32 SIP Door Station. The interface enables quick validation of hardware connections without requiring physical button presses or external test equipment.

## Architecture

### High-Level Architecture

```
┌─────────────────┐
│  Web Interface  │
│   (Browser)     │
└────────┬────────┘
         │ HTTP POST /api/hardware/test
         │
         ▼
┌─────────────────┐
│  Web Server     │
│  Test Endpoints │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Hardware Test   │
│    Handler      │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  GPIO Handler   │
│  (Existing)     │
└─────────────────┘
```

### Component Interaction

```
Web UI → Test API → Hardware Test Handler → GPIO Handler → Physical Hardware
   ↑                                                              ↓
   └──────────────── Status/Feedback ←──────────────────────────┘
```

## Components and Interfaces

### 1. Hardware Test Handler Module (`hardware_test.c/h`)

**Purpose**: Provides safe, controlled hardware testing functions with state tracking and safety timeouts.

**Public API**:
```c
// Initialize hardware test module
void hardware_test_init(void);

// Test doorbell button simulation
esp_err_t hardware_test_doorbell(doorbell_t bell);

// Test door opener relay with duration
esp_err_t hardware_test_door_opener(uint32_t duration_ms);

// Test light relay toggle
esp_err_t hardware_test_light_toggle(void);

// Get current hardware state
typedef struct {
    bool door_relay_active;
    bool light_relay_active;
    bool bell1_pressed;
    bool bell2_pressed;
    uint32_t door_relay_remaining_ms;
} hardware_state_t;

void hardware_test_get_state(hardware_state_t* state);

// Safety: Stop all active tests
void hardware_test_stop_all(void);
```

**Safety Features**:
- Maximum door opener duration: 10 seconds
- Automatic timeout and relay deactivation
- Prevent concurrent door opener activations
- State tracking to prevent conflicts with normal operation

### 2. Web Server Test Endpoints

**Endpoint**: `POST /api/hardware/test/doorbell`
- **Body**: `{"bell": 1}` or `{"bell": 2}`
- **Response**: `{"success": true, "message": "Doorbell 1 test triggered"}`
- **Action**: Simulates doorbell button press (triggers SIP call if configured)

**Endpoint**: `POST /api/hardware/test/door`
- **Body**: `{"duration": 5000}` (milliseconds, 1000-10000)
- **Response**: `{"success": true, "duration": 5000}`
- **Action**: Activates door opener relay for specified duration

**Endpoint**: `POST /api/hardware/test/light`
- **Body**: `{}` (empty)
- **Response**: `{"success": true, "state": "on"}` or `{"state": "off"}`
- **Action**: Toggles light relay state

**Endpoint**: `GET /api/hardware/state`
- **Response**: Current hardware state (relay states, button states)
- **Polling**: Updated every 500ms by web interface

**Endpoint**: `POST /api/hardware/test/stop`
- **Body**: `{}` (empty)
- **Response**: `{"success": true, "message": "All tests stopped"}`
- **Action**: Emergency stop for all active tests

### 3. Web Interface Components

**Hardware Testing Section** (new sidebar menu item):
- Real-time hardware state indicators
- Doorbell test buttons (Bell 1, Bell 2)
- Door opener test with duration slider (1-10 seconds)
- Light relay toggle button
- Emergency stop button
- Test result feedback area
- Safety warnings

## Data Models

### Hardware Test Context
```c
typedef struct {
    bool door_test_active;
    uint32_t door_test_start_time;
    uint32_t door_test_duration;
    bool light_state;
    SemaphoreHandle_t test_mutex;
} hardware_test_context_t;
```

### Test Result Structure
```c
typedef struct {
    bool success;
    char message[128];
    uint32_t timestamp;
} test_result_t;
```

## Error Handling

### Validation Checks
1. **Duration Validation**
   - Minimum: 1000ms (1 second)
   - Maximum: 10000ms (10 seconds)
   - Reject invalid durations with error message

2. **State Validation**
   - Check if door opener is already active
   - Prevent concurrent door opener tests
   - Validate bell number (1 or 2)

3. **Safety Checks**
   - Verify GPIO initialization complete
   - Check for hardware faults
   - Validate relay driver state

### Error Messages
```c
#define HW_TEST_ERR_INVALID_BELL     "Invalid bell number (must be 1 or 2)"
#define HW_TEST_ERR_INVALID_DURATION "Duration must be between 1 and 10 seconds"
#define HW_TEST_ERR_DOOR_ACTIVE      "Door opener test already in progress"
#define HW_TEST_ERR_NOT_INITIALIZED  "Hardware not initialized"
#define HW_TEST_ERR_GPIO_FAULT       "GPIO hardware fault detected"
```

### Error Recovery
- **Timeout**: Automatically deactivate relay after maximum duration
- **Concurrent Request**: Reject with clear error message
- **Hardware Fault**: Disable testing, log error, notify user
- **Emergency Stop**: Immediately deactivate all relays

## Testing Strategy

### Unit Tests
1. **Hardware Test Handler Tests**
   - Test duration validation
   - Test state tracking
   - Test timeout mechanism
   - Test concurrent request rejection

2. **Safety Tests**
   - Verify maximum duration enforcement
   - Test emergency stop functionality
   - Verify automatic timeout
   - Test mutex protection

### Integration Tests
1. **Web Interface Tests**
   - Test doorbell button simulation
   - Test door opener with various durations
   - Test light toggle
   - Test state polling
   - Test emergency stop

2. **End-to-End Tests**
   - Verify physical relay activation
   - Measure actual relay timing accuracy
   - Test button press simulation triggers SIP call
   - Verify state indicators update correctly

### Safety Tests
1. **Timeout Tests**
   - Verify relay deactivates after timeout
   - Test maximum duration enforcement
   - Verify no relay stuck in active state

2. **Concurrent Access Tests**
   - Multiple simultaneous door opener requests
   - Rapid button press simulations
   - Verify mutex protection works

## Security Considerations

### Current Implementation (Phase 1)
- No authentication (web interface is open)
- Rate limiting on test endpoints (max 1 request per second)
- Maximum duration enforcement (10 seconds)
- Emergency stop always available

### Future Enhancements (Phase 2+)
- Add web interface authentication
- Implement test session tokens
- Add audit logging for all hardware tests
- Implement IP-based access control

## Performance Considerations

### Response Time
- Test trigger: <50ms
- State update: <100ms
- Relay activation: <10ms (hardware dependent)

### Memory Usage
- Hardware test context: ~100 bytes
- Test result buffer: ~200 bytes
- Total additional heap: <1KB

### Timing Accuracy
- Door opener duration accuracy: ±50ms
- State polling interval: 500ms
- Timeout check interval: 100ms

## Implementation Notes

### GPIO Safety
```c
// Ensure relay is deactivated after timeout
void hardware_test_door_timeout_task(void* arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
        
        if (test_ctx.door_test_active) {
            uint32_t elapsed = xTaskGetTickCount() - test_ctx.door_test_start_time;
            if (elapsed >= test_ctx.door_test_duration) {
                gpio_set_level(DOOR_RELAY_GPIO, 0);
                test_ctx.door_test_active = false;
            }
        }
    }
}
```

### Doorbell Simulation
```c
// Simulate doorbell press by calling existing handler
esp_err_t hardware_test_doorbell(doorbell_t bell) {
    if (bell != DOORBELL_1 && bell != DOORBELL_2) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Trigger the same logic as physical button press
    // This will initiate SIP call if configured
    gpio_doorbell_isr_handler(bell);
    
    return ESP_OK;
}
```

### State Tracking
```c
void hardware_test_get_state(hardware_state_t* state) {
    state->door_relay_active = test_ctx.door_test_active;
    state->light_relay_active = gpio_get_level(LIGHT_RELAY_GPIO);
    state->bell1_pressed = is_doorbell_pressed(DOORBELL_1);
    state->bell2_pressed = is_doorbell_pressed(DOORBELL_2);
    
    if (test_ctx.door_test_active) {
        uint32_t elapsed = xTaskGetTickCount() - test_ctx.door_test_start_time;
        state->door_relay_remaining_ms = test_ctx.door_test_duration - elapsed;
    } else {
        state->door_relay_remaining_ms = 0;
    }
}
```

## Web Interface Design

### Hardware Testing Section UI

```html
<div class="hardware-test-section">
  <h3>Hardware Testing</h3>
  
  <div class="warning-box">
    <strong>⚠️ Warning:</strong> Testing will physically activate relays and may trigger SIP calls.
    Ensure proper wiring before testing.
  </div>
  
  <div class="hardware-state">
    <h4>Current State</h4>
    <div class="state-grid">
      <div class="state-item">
        <span class="state-label">Door Relay:</span>
        <span class="state-value" id="door-relay-state">Inactive</span>
      </div>
      <div class="state-item">
        <span class="state-label">Light Relay:</span>
        <span class="state-value" id="light-relay-state">Off</span>
      </div>
      <div class="state-item">
        <span class="state-label">Bell 1:</span>
        <span class="state-value" id="bell1-state">Not Pressed</span>
      </div>
      <div class="state-item">
        <span class="state-label">Bell 2:</span>
        <span class="state-value" id="bell2-state">Not Pressed</span>
      </div>
    </div>
  </div>
  
  <div class="test-controls">
    <h4>Doorbell Tests</h4>
    <p class="test-description">Simulate doorbell button press (will trigger SIP call if configured)</p>
    <button onclick="testDoorbell(1)" class="test-btn">Test Doorbell 1</button>
    <button onclick="testDoorbell(2)" class="test-btn">Test Doorbell 2</button>
  </div>
  
  <div class="test-controls">
    <h4>Door Opener Test</h4>
    <p class="test-description">Activate door opener relay for specified duration</p>
    <label for="door-duration">Duration: <span id="door-duration-value">5</span> seconds</label>
    <input type="range" id="door-duration" min="1" max="10" value="5" 
           oninput="updateDurationDisplay(this.value)">
    <button onclick="testDoorOpener()" class="test-btn">Activate Door Opener</button>
    <div id="door-countdown" style="display:none;">
      Remaining: <span id="door-remaining">0</span>s
    </div>
  </div>
  
  <div class="test-controls">
    <h4>Light Relay Test</h4>
    <p class="test-description">Toggle light relay on/off</p>
    <button onclick="testLightToggle()" class="test-btn">Toggle Light</button>
  </div>
  
  <div class="test-controls">
    <button onclick="emergencyStop()" class="emergency-btn">🛑 Emergency Stop All Tests</button>
  </div>
  
  <div id="test-feedback" class="feedback-area"></div>
</div>
```

### JavaScript Test Functions

```javascript
// Poll hardware state every 500ms
setInterval(updateHardwareState, 500);

async function updateHardwareState() {
  try {
    const response = await fetch('/api/hardware/state');
    const state = await response.json();
    
    document.getElementById('door-relay-state').textContent = 
      state.door_relay_active ? 'Active' : 'Inactive';
    document.getElementById('door-relay-state').className = 
      state.door_relay_active ? 'state-active' : 'state-inactive';
    
    document.getElementById('light-relay-state').textContent = 
      state.light_relay_active ? 'On' : 'Off';
    document.getElementById('light-relay-state').className = 
      state.light_relay_active ? 'state-active' : 'state-inactive';
    
    if (state.door_relay_active && state.door_relay_remaining_ms > 0) {
      document.getElementById('door-countdown').style.display = 'block';
      document.getElementById('door-remaining').textContent = 
        Math.ceil(state.door_relay_remaining_ms / 1000);
    } else {
      document.getElementById('door-countdown').style.display = 'none';
    }
    
  } catch (error) {
    console.error('Failed to update hardware state:', error);
  }
}

async function testDoorbell(bell) {
  try {
    const response = await fetch('/api/hardware/test/doorbell', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({bell: bell})
    });
    
    const result = await response.json();
    showFeedback(result.success ? 'success' : 'error', result.message);
    
  } catch (error) {
    showFeedback('error', 'Test failed: ' + error.message);
  }
}

async function testDoorOpener() {
  const duration = parseInt(document.getElementById('door-duration').value) * 1000;
  
  if (!confirm(`Activate door opener for ${duration/1000} seconds?`)) {
    return;
  }
  
  try {
    const response = await fetch('/api/hardware/test/door', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({duration: duration})
    });
    
    const result = await response.json();
    showFeedback(result.success ? 'success' : 'error', 
                 result.message || `Door opener activated for ${duration/1000}s`);
    
  } catch (error) {
    showFeedback('error', 'Test failed: ' + error.message);
  }
}

async function testLightToggle() {
  try {
    const response = await fetch('/api/hardware/test/light', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({})
    });
    
    const result = await response.json();
    showFeedback('success', `Light relay ${result.state}`);
    
  } catch (error) {
    showFeedback('error', 'Test failed: ' + error.message);
  }
}

async function emergencyStop() {
  try {
    const response = await fetch('/api/hardware/test/stop', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({})
    });
    
    const result = await response.json();
    showFeedback('success', result.message);
    
  } catch (error) {
    showFeedback('error', 'Emergency stop failed: ' + error.message);
  }
}

function updateDurationDisplay(value) {
  document.getElementById('door-duration-value').textContent = value;
}

function showFeedback(type, message) {
  const feedback = document.getElementById('test-feedback');
  feedback.className = 'feedback-area feedback-' + type;
  feedback.textContent = message;
  feedback.style.display = 'block';
  
  setTimeout(() => {
    feedback.style.display = 'none';
  }, 5000);
}
```

## Dependencies

### ESP-IDF Components
- `driver/gpio`: GPIO control
- `freertos/FreeRTOS`: Task management and timers
- `freertos/semphr`: Mutex for thread safety

### Project Components
- `gpio_handler`: Existing GPIO control functions
- `web_server`: HTTP server for test endpoints
- `sip_client`: For doorbell simulation (triggers calls)

## Rollout Plan

### Phase 1: Basic Testing (MVP)
- Implement hardware test handler module
- Add web server test endpoints
- Create web interface testing section
- Implement doorbell and relay tests
- Add basic safety timeouts

### Phase 2: Enhanced Safety
- Add emergency stop functionality
- Implement comprehensive state tracking
- Add test result logging
- Enhanced error handling

### Phase 3: Advanced Features
- Add automated test sequences
- Implement test result history
- Add hardware diagnostics
- GPIO voltage monitoring (if hardware supports)

## References

- [ESP-IDF GPIO API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html)
- [FreeRTOS Task Management](https://www.freertos.org/taskandcr.html)
- Existing `gpio_handler.c/h` implementation
