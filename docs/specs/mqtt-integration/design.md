# MQTT Integration - Design Document

## Overview

This feature enables the ESP32 SIP Door Station to integrate with home automation systems through MQTT messaging. It publishes doorbell events, door opener status, SIP call status, and system health information while also accepting commands for remote control.

## Architecture

### High-Level Architecture

```
┌─────────────────┐
│  Door Station   │
│  - Doorbell     │
│  - Door Opener  │
│  - SIP Client   │
└────────┬────────┘
         │ Events
         ▼
┌─────────────────┐
│  MQTT Client    │
│  - Publish      │
│  - Subscribe    │
└────────┬────────┘
         │ MQTT Protocol
         ▼
┌─────────────────┐
│  MQTT Broker    │
│  (Mosquitto)    │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Home Automation │
│  - Home Assist. │
│  - Node-RED     │
│  - OpenHAB      │
└─────────────────┘
```

### Topic Structure

```
doorstation/
├── status (retained)
├── bell1/
│   └── pressed
├── bell2/
│   └── pressed
├── door/
│   ├── opened
│   └── closed
├── sip/
│   ├── call_started
│   ├── call_answered
│   └── call_ended
└── command/
    ├── door (subscribed)
    ├── light (subscribed)
    └── error (published)
```

## Components and Interfaces

### 1. MQTT Manager Module (`mqtt_manager.c/h`)

**Purpose**: Manages MQTT connection, publishing, and subscription.

**Public API**:
```c
// Initialize MQTT manager
void mqtt_manager_init(void);

// Configuration
typedef struct {
    bool enabled;
    char broker_host[64];
    uint16_t broker_port;
    char username[32];
    char password[64];
    char client_id[32];
    char base_topic[32];
    bool use_tls;
    bool ha_discovery;
    uint8_t qos;  // 0, 1, or 2
} mqtt_config_t;

esp_err_t mqtt_set_config(const mqtt_config_t* config);
esp_err_t mqtt_get_config(mqtt_config_t* config);

// Connection management
esp_err_t mqtt_connect(void);
void mqtt_disconnect(void);
bool mqtt_is_connected(void);

typedef struct {
    bool connected;
    char broker_host[64];
    uint16_t broker_port;
    uint32_t connected_since;
    uint32_t last_error_time;
    char last_error[128];
    uint32_t messages_published;
    uint32_t messages_failed;
} mqtt_status_t;

void mqtt_get_status(mqtt_status_t* status);

// Publishing
esp_err_t mqtt_publish(const char* topic, const char* payload, uint8_t qos, bool retain);
esp_err_t mqtt_publish_json(const char* topic, cJSON* json, uint8_t qos, bool retain);

// Event publishing helpers
esp_err_t mqtt_publish_doorbell(uint8_t bell_number, const char* caller_id);
esp_err_t mqtt_publish_door_opened(const char* method, const char* caller_id);
esp_err_t mqtt_publish_door_closed(void);
esp_err_t mqtt_publish_sip_call_started(const char* target);
esp_err_t mqtt_publish_sip_call_answered(const char* target);
esp_err_t mqtt_publish_sip_call_ended(const char* target, uint32_t duration);
esp_err_t mqtt_publish_status(const char* status);

// Subscription
typedef void (*mqtt_command_callback_t)(const char* command, const char* payload);
esp_err_t mqtt_subscribe_commands(mqtt_command_callback_t callback);

// Home Assistant Discovery
esp_err_t mqtt_publish_ha_discovery(void);

// Message history
typedef struct {
    uint32_t timestamp;
    char topic[64];
    char payload[256];
    uint8_t qos;
    bool success;
    char error[64];
} mqtt_message_history_t;

int mqtt_get_message_history(mqtt_message_history_t* history, int max_entries);
```

### 2. MQTT Event Integration

**Doorbell Integration**:
```c
// In gpio_handler.c
void gpio_doorbell_isr_handler(doorbell_t bell) {
    // Existing doorbell logic...
    
    // Publish MQTT event
    if (mqtt_is_connected()) {
        mqtt_publish_doorbell(bell == DOORBELL_1 ? 1 : 2, NULL);
    }
}
```

**Door Opener Integration**:
```c
// In dtmf_decoder.c
void dtmf_execute_door_open(const char* caller_id) {
    // Existing door open logic...
    
    // Publish MQTT event
    if (mqtt_is_connected()) {
        mqtt_publish_door_opened("DTMF", caller_id);
    }
}
```

**SIP Call Integration**:
```c
// In sip_client.c
void sip_call_state_changed(sip_call_state_t state, const char* target) {
    // Existing state change logic...
    
    // Publish MQTT event
    if (mqtt_is_connected()) {
        switch (state) {
            case SIP_CALL_STARTED:
                mqtt_publish_sip_call_started(target);
                break;
            case SIP_CALL_ANSWERED:
                mqtt_publish_sip_call_answered(target);
                break;
            case SIP_CALL_ENDED:
                mqtt_publish_sip_call_ended(target, call_duration);
                break;
        }
    }
}
```

### 3. MQTT Command Handler

**Command Processing**:
```c
static void mqtt_command_handler(const char* command, const char* payload) {
    if (strcmp(command, "door") == 0) {
        if (strcmp(payload, "OPEN") == 0) {
            // Activate door opener
            door_opener_activate(5000);  // 5 second activation
            mqtt_publish_door_opened("MQTT", NULL);
        }
    } else if (strcmp(command, "light") == 0) {
        if (strcmp(payload, "TOGGLE") == 0) {
            // Toggle light relay
            light_relay_toggle();
        }
    } else {
        // Unknown command
        cJSON* error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Unknown command");
        cJSON_AddStringToObject(error, "command", command);
        mqtt_publish_json("command/error", error, 0, false);
        cJSON_Delete(error);
    }
}
```

## Data Models

### MQTT Message Payloads

**Doorbell Event**:
```json
{
  "event": "doorbell_pressed",
  "bell": 1,
  "timestamp": "2025-10-17T10:30:00Z",
  "caller_id": "sip:apartment1@example.com"
}
```

**Door Opener Event**:
```json
{
  "event": "door_opened",
  "method": "DTMF",
  "timestamp": "2025-10-17T10:30:05Z",
  "caller_id": "sip:apartment1@example.com"
}
```

**SIP Call Event**:
```json
{
  "event": "call_started",
  "target": "sip:apartment1@example.com",
  "timestamp": "2025-10-17T10:30:00Z"
}
```

**Status Message**:
```json
{
  "status": "online",
  "firmware_version": "1.2.0",
  "uptime_seconds": 86400,
  "ip_address": "192.168.1.100",
  "timestamp": "2025-10-17T10:30:00Z"
}
```

### Home Assistant Discovery

**Binary Sensor (Doorbell)**:
```json
{
  "name": "Doorbell 1",
  "unique_id": "doorstation_aabbccddee_bell1",
  "state_topic": "doorstation/bell1/pressed",
  "device_class": "occupancy",
  "payload_on": "pressed",
  "payload_off": "released",
  "device": {
    "identifiers": ["doorstation_aabbccddee"],
    "name": "ESP32 Door Station",
    "model": "ESP32-S3",
    "manufacturer": "Custom",
    "sw_version": "1.2.0"
  }
}
```

**Switch (Door Opener)**:
```json
{
  "name": "Door Opener",
  "unique_id": "doorstation_aabbccddee_door",
  "command_topic": "doorstation/command/door",
  "state_topic": "doorstation/door/opened",
  "payload_on": "OPEN",
  "payload_off": "CLOSE",
  "device": {
    "identifiers": ["doorstation_aabbccddee"],
    "name": "ESP32 Door Station",
    "model": "ESP32-S3",
    "manufacturer": "Custom",
    "sw_version": "1.2.0"
  }
}
```

## Error Handling

### Connection Errors
```c
#define MQTT_ERR_CONNECTION_FAILED   "Failed to connect to broker"
#define MQTT_ERR_AUTH_FAILED         "Authentication failed"
#define MQTT_ERR_TIMEOUT             "Connection timeout"
#define MQTT_ERR_TLS_FAILED          "TLS handshake failed"
```

### Publishing Errors
```c
#define MQTT_ERR_NOT_CONNECTED       "Not connected to broker"
#define MQTT_ERR_PUBLISH_FAILED      "Failed to publish message"
#define MQTT_ERR_INVALID_TOPIC       "Invalid topic format"
```

## Testing Strategy

### Unit Tests
1. **Message Formatting Tests**
   - Test JSON payload generation
   - Test topic construction
   - Test timestamp formatting

2. **Configuration Tests**
   - Test config validation
   - Test NVS storage/retrieval

### Integration Tests
1. **Connection Tests**
   - Test broker connection
   - Test authentication
   - Test TLS connection
   - Test reconnection logic

2. **Publishing Tests**
   - Test event publishing
   - Test QoS levels
   - Test retained messages

3. **Subscription Tests**
   - Test command reception
   - Test command execution

## Security Considerations

### TLS/SSL
- Support TLS 1.2 minimum
- Validate broker certificate
- Support custom CA certificates
- Encrypt username/password

### Authentication
- Support username/password authentication
- Store credentials encrypted in NVS
- Never log passwords

## Performance Considerations

### Memory Usage
- MQTT client: ~10KB
- Message buffers: ~2KB
- Message history: ~15KB (50 messages × 300 bytes)
- Total: ~30KB

### Network Usage
- Typical event: ~200 bytes
- Status update: ~300 bytes
- Keepalive: ~50 bytes every 60 seconds

## Implementation Notes

### ESP-IDF MQTT Client

```c
#include "mqtt_client.h"

static esp_mqtt_client_handle_t mqtt_client = NULL;

void mqtt_connect(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://broker.example.com:1883",
        .credentials.username = "doorstation",
        .credentials.authentication.password = "password",
        .session.last_will.topic = "doorstation/status",
        .session.last_will.msg = "{\"status\":\"offline\"}",
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                               int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected");
            mqtt_publish_status("online");
            mqtt_subscribe_commands(mqtt_command_handler);
            if (mqtt_config.ha_discovery) {
                mqtt_publish_ha_discovery();
            }
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT Disconnected");
            break;
            
        case MQTT_EVENT_DATA:
            // Handle received command
            char topic[64];
            char payload[256];
            snprintf(topic, sizeof(topic), "%.*s", event->topic_len, event->topic);
            snprintf(payload, sizeof(payload), "%.*s", event->data_len, event->data);
            
            // Extract command from topic
            char* command = strrchr(topic, '/');
            if (command) {
                command++;  // Skip '/'
                mqtt_command_handler(command, payload);
            }
            break;
    }
}
```

### Web Interface Design

```html
<div class="mqtt-section">
    <h3>MQTT Configuration</h3>
    
    <div class="mqtt-status">
        <p><strong>Status:</strong> <span id="mqtt-status">Disconnected</span></p>
        <p><strong>Broker:</strong> <span id="mqtt-broker">-</span></p>
        <p><strong>Messages Published:</strong> <span id="mqtt-msg-count">0</span></p>
    </div>
    
    <div class="mqtt-config">
        <label>
            <input type="checkbox" id="mqtt-enabled"> Enable MQTT
        </label>
        
        <label>Broker Host:
            <input type="text" id="mqtt-host" placeholder="broker.example.com">
        </label>
        
        <label>Port:
            <input type="number" id="mqtt-port" value="1883">
        </label>
        
        <label>Username:
            <input type="text" id="mqtt-username">
        </label>
        
        <label>Password:
            <input type="password" id="mqtt-password">
        </label>
        
        <label>Base Topic:
            <input type="text" id="mqtt-base-topic" value="doorstation">
        </label>
        
        <label>
            <input type="checkbox" id="mqtt-tls"> Enable TLS/SSL
        </label>
        
        <label>
            <input type="checkbox" id="mqtt-ha-discovery"> Home Assistant Discovery
        </label>
        
        <button onclick="testMqttConnection()">Test Connection</button>
        <button onclick="saveMqttConfig()">Save Configuration</button>
    </div>
    
    <div class="mqtt-history">
        <h4>Recent Messages</h4>
        <table id="mqtt-history-table">
            <thead>
                <tr>
                    <th>Time</th>
                    <th>Topic</th>
                    <th>Payload</th>
                    <th>Status</th>
                </tr>
            </thead>
            <tbody></tbody>
        </table>
    </div>
</div>
```

## Dependencies

### ESP-IDF Components
- `mqtt`: MQTT client library
- `cJSON`: JSON formatting
- `mbedtls`: TLS support

### Project Components
- `gpio_handler`: Doorbell events
- `dtmf_decoder`: Door opener events
- `sip_client`: Call status events
- `web_server`: Configuration API

## Rollout Plan

### Phase 1: Basic MQTT (MVP)
- Implement MQTT connection
- Publish doorbell events
- Publish door opener events
- Add basic configuration

### Phase 2: Advanced Features
- Add command subscription
- Implement Home Assistant discovery
- Add TLS support
- Add message history

### Phase 3: Enhanced Integration
- Add SIP call status publishing
- Add system health monitoring
- Add custom topic templates
- Add MQTT-based firmware updates

## References

- [ESP-IDF MQTT Client](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html)
- [MQTT Protocol](https://mqtt.org/)
- [Home Assistant MQTT Discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery)
