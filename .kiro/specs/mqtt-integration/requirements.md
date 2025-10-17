# Requirements Document

## Introduction

The MQTT Integration feature enables the ESP32 SIP Door Station to publish events and status updates to an MQTT broker, allowing integration with home automation systems like Home Assistant, OpenHAB, and Node-RED. This feature provides real-time notifications of doorbell presses, door openings, and system status changes.

## Glossary

- **System**: The ESP32 SIP Door Station MQTT integration system
- **MQTT_Broker**: Server that receives and distributes MQTT messages
- **MQTT_Topic**: Hierarchical message channel (e.g., doorstation/bell1/pressed)
- **MQTT_Publish**: Sending a message to an MQTT topic
- **MQTT_Subscribe**: Receiving messages from an MQTT topic
- **QoS**: Quality of Service level for message delivery (0, 1, or 2)
- **Retained_Message**: Message stored by broker and sent to new subscribers
- **Last_Will_Testament**: Message sent by broker when client disconnects unexpectedly

## Requirements

### Requirement 1: MQTT Broker Configuration

**User Story:** As a system administrator, I want to configure MQTT broker connection settings, so that the door station can connect to my home automation MQTT broker.

#### Acceptance Criteria

1. WHEN THE administrator configures MQTT, THE System SHALL require broker hostname or IP address
2. WHEN broker is configured, THE System SHALL require port number (default 1883)
3. WHEN broker is configured, THE System SHALL optionally accept username and password
4. WHEN broker is configured, THE System SHALL optionally accept client ID (default: doorstation-{MAC})
5. WHEN configuration is saved, THE System SHALL store settings in NVS
6. WHEN configuration is updated, THE System SHALL reconnect to broker with new settings

### Requirement 2: MQTT Connection Management

**User Story:** As a system administrator, I want automatic MQTT connection management, so that the door station maintains connection to the broker without manual intervention.

#### Acceptance Criteria

1. WHEN THE System starts with MQTT enabled, THE System SHALL connect to configured broker
2. WHEN connection succeeds, THE System SHALL publish online status message
3. WHEN connection fails, THE System SHALL retry connection every 30 seconds
4. WHEN connection is lost, THE System SHALL automatically attempt reconnection
5. WHEN System shuts down gracefully, THE System SHALL publish offline status message

### Requirement 3: Doorbell Event Publishing

**User Story:** As a home automation user, I want doorbell press events published to MQTT, so that I can trigger automations when someone rings the doorbell.

#### Acceptance Criteria

1. WHEN THE doorbell 1 is pressed, THE System SHALL publish message to topic "doorstation/bell1/pressed"
2. WHEN doorbell 2 is pressed, THE System SHALL publish message to topic "doorstation/bell2/pressed"
3. WHEN doorbell event is published, THE System SHALL include timestamp in message payload
4. WHEN doorbell event is published, THE System SHALL use QoS 1 for reliable delivery
5. WHEN doorbell event is published, THE System SHALL include caller ID if available from SIP

### Requirement 4: Door Opener Event Publishing

**User Story:** As a home automation user, I want door opener events published to MQTT, so that I can log when the door is opened and by whom.

#### Acceptance Criteria

1. WHEN THE door opener is activated, THE System SHALL publish message to topic "doorstation/door/opened"
2. WHEN door opener event is published, THE System SHALL include activation method (DTMF, web interface, button)
3. WHEN door opener event is published, THE System SHALL include timestamp
4. WHEN door opener event is published, THE System SHALL include caller ID if activated via DTMF
5. WHEN door opener deactivates, THE System SHALL publish message to topic "doorstation/door/closed"

### Requirement 5: System Status Publishing

**User Story:** As a home automation user, I want system status updates published to MQTT, so that I can monitor the door station's health and connectivity.

#### Acceptance Criteria

1. WHEN THE System connects to MQTT broker, THE System SHALL publish online status to topic "doorstation/status"
2. WHEN System publishes status, THE System SHALL set retained flag so new subscribers receive status
3. WHEN System publishes status, THE System SHALL include firmware version, uptime, and IP address
4. WHEN System disconnects, THE System SHALL have Last Will Testament set to publish offline status
5. WHEN System status changes, THE System SHALL publish updated status within 5 seconds

### Requirement 6: SIP Call Status Publishing

**User Story:** As a home automation user, I want SIP call status published to MQTT, so that I can track when calls are in progress and their outcomes.

#### Acceptance Criteria

1. WHEN THE SIP call is initiated, THE System SHALL publish message to topic "doorstation/sip/call_started"
2. WHEN SIP call is answered, THE System SHALL publish message to topic "doorstation/sip/call_answered"
3. WHEN SIP call ends, THE System SHALL publish message to topic "doorstation/sip/call_ended"
4. WHEN call status is published, THE System SHALL include call duration
5. WHEN call status is published, THE System SHALL include target number

### Requirement 7: MQTT Command Subscription

**User Story:** As a home automation user, I want to send commands to the door station via MQTT, so that I can trigger door opening and other actions from my automation system.

#### Acceptance Criteria

1. WHEN THE System connects to MQTT broker, THE System SHALL subscribe to topic "doorstation/command/door"
2. WHEN message "OPEN" is received on door command topic, THE System SHALL activate door opener
3. WHEN System connects to broker, THE System SHALL subscribe to topic "doorstation/command/light"
4. WHEN message "TOGGLE" is received on light command topic, THE System SHALL toggle light relay
5. WHEN invalid command is received, THE System SHALL publish error message to "doorstation/command/error"

### Requirement 8: Home Assistant MQTT Discovery

**User Story:** As a Home Assistant user, I want automatic device discovery, so that the door station appears in Home Assistant without manual configuration.

#### Acceptance Criteria

1. WHEN THE System connects to MQTT broker with Home Assistant discovery enabled, THE System SHALL publish discovery messages
2. WHEN discovery is published, THE System SHALL publish to topic "homeassistant/binary_sensor/doorstation_bell1/config"
3. WHEN discovery is published, THE System SHALL publish to topic "homeassistant/binary_sensor/doorstation_bell2/config"
4. WHEN discovery is published, THE System SHALL publish to topic "homeassistant/switch/doorstation_door/config"
5. WHEN discovery is published, THE System SHALL include device information (name, model, manufacturer, firmware version)
6. WHEN discovery is published, THE System SHALL include unique IDs based on device MAC address

### Requirement 9: MQTT TLS/SSL Support

**User Story:** As a security-conscious administrator, I want TLS/SSL encryption for MQTT connections, so that messages are protected from eavesdropping.

#### Acceptance Criteria

1. WHEN THE administrator enables MQTT TLS, THE System SHALL connect to broker using port 8883
2. WHEN TLS is enabled, THE System SHALL validate broker certificate
3. WHEN TLS is enabled, THE System SHALL optionally accept custom CA certificate
4. WHEN TLS handshake fails, THE System SHALL log error and retry connection
5. WHEN TLS is disabled, THE System SHALL use unencrypted connection on port 1883

### Requirement 10: MQTT Topic Customization

**User Story:** As a system administrator, I want to customize MQTT topic prefixes, so that I can organize topics according to my home automation structure.

#### Acceptance Criteria

1. WHEN THE administrator configures MQTT, THE System SHALL allow custom base topic (default: "doorstation")
2. WHEN base topic is changed, THE System SHALL use new prefix for all published topics
3. WHEN base topic is changed, THE System SHALL update subscribed command topics
4. WHEN base topic is changed, THE System SHALL republish Home Assistant discovery with new topics
5. WHEN base topic is saved, THE System SHALL store in NVS

### Requirement 11: MQTT Message Payload Format

**User Story:** As a developer, I want consistent JSON message payloads, so that I can easily parse MQTT messages in my automations.

#### Acceptance Criteria

1. WHEN THE System publishes event message, THE System SHALL use JSON format
2. WHEN JSON is published, THE System SHALL include "timestamp" field in ISO 8601 format
3. WHEN JSON is published, THE System SHALL include "event" field describing event type
4. WHEN JSON is published, THE System SHALL include relevant data fields (caller_id, duration, method)
5. WHEN JSON is published, THE System SHALL ensure valid JSON syntax

### Requirement 12: MQTT Connection Status Display

**User Story:** As a system administrator, I want to see MQTT connection status in the web interface, so that I can verify the door station is connected to the broker.

#### Acceptance Criteria

1. WHEN THE web interface loads, THE System SHALL display MQTT connection status (Connected/Disconnected)
2. WHEN MQTT is connected, THE System SHALL display broker address and port
3. WHEN MQTT is disconnected, THE System SHALL display last error message
4. WHEN MQTT is disconnected, THE System SHALL display time since last successful connection
5. WHEN connection status changes, THE System SHALL update display within 5 seconds

### Requirement 13: MQTT Configuration Web Interface

**User Story:** As a system administrator, I want a web interface section for MQTT configuration, so that I can easily configure broker settings without editing files.

#### Acceptance Criteria

1. WHEN THE web interface loads, THE System SHALL display "MQTT" menu item in sidebar
2. WHEN MQTT section is displayed, THE System SHALL show enable/disable toggle
3. WHEN MQTT section is displayed, THE System SHALL show broker hostname/IP input field
4. WHEN MQTT section is displayed, THE System SHALL show port input field
5. WHEN MQTT section is displayed, THE System SHALL show username and password input fields
6. WHEN MQTT section is displayed, THE System SHALL show base topic input field
7. WHEN MQTT section is displayed, THE System SHALL show Home Assistant discovery toggle
8. WHEN MQTT section is displayed, THE System SHALL show TLS enable toggle

### Requirement 14: MQTT API Endpoints

**User Story:** As a developer, I want RESTful API endpoints for MQTT management, so that the web interface can configure and monitor MQTT.

#### Acceptance Criteria

1. WHEN THE System starts, THE System SHALL register GET /api/mqtt/config endpoint
2. WHEN THE System starts, THE System SHALL register POST /api/mqtt/config endpoint
3. WHEN THE System starts, THE System SHALL register GET /api/mqtt/status endpoint
4. WHEN THE System starts, THE System SHALL register POST /api/mqtt/test endpoint
5. WHEN endpoints receive requests, THE System SHALL parse JSON bodies correctly
6. WHEN endpoints process requests, THE System SHALL return appropriate JSON responses

### Requirement 15: MQTT Test Connection

**User Story:** As a system administrator, I want to test MQTT connection, so that I can verify broker settings are correct before saving.

#### Acceptance Criteria

1. WHEN THE administrator clicks "Test Connection" button, THE System SHALL attempt connection to broker
2. WHEN test connection succeeds, THE System SHALL return success message
3. WHEN test connection fails, THE System SHALL return specific error message (connection refused, authentication failed, timeout)
4. WHEN test completes, THE System SHALL disconnect test connection
5. WHEN test is in progress, THE System SHALL display loading indicator

### Requirement 16: MQTT Event History

**User Story:** As a system administrator, I want to see recent MQTT published messages, so that I can verify events are being published correctly.

#### Acceptance Criteria

1. WHEN THE System publishes MQTT message, THE System SHALL store message in circular buffer (last 50 messages)
2. WHEN administrator views MQTT section, THE System SHALL display recent published messages
3. WHEN messages are displayed, THE System SHALL show topic, payload, timestamp, and QoS
4. WHEN messages are displayed, THE System SHALL show delivery status (success/failed)
5. WHEN message fails to publish, THE System SHALL display error reason
