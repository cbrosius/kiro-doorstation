# Implementation Plan

- [ ] 1. Create MQTT manager module
  - Create `main/mqtt_manager.h` and `main/mqtt_manager.c` files
  - Define MQTT configuration structure
  - Define MQTT status structure
  - Implement `mqtt_manager_init()` function
  - _Requirements: 1.1, 2.1_

- [ ] 2. Implement MQTT configuration management
  - Implement `mqtt_set_config()` to save configuration
  - Implement `mqtt_get_config()` to retrieve configuration
  - Store configuration in NVS
  - Validate broker hostname/IP format
  - Validate port number range
  - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6_

- [ ] 3. Implement MQTT connection management
  - Implement `mqtt_connect()` using esp_mqtt_client
  - Configure broker URI, port, username, password
  - Set client ID (default: doorstation-{MAC})
  - Implement connection event handler
  - Implement automatic reconnection on connection loss
  - _Requirements: 2.1, 2.2, 2.4_

- [ ] 4. Implement Last Will Testament
  - Configure LWT topic as "{base_topic}/status"
  - Set LWT payload to offline status JSON
  - Set LWT QoS to 1 and retained flag to true
  - Publish online status on successful connection
  - _Requirements: 2.2, 2.5, 5.4_

- [ ] 5. Implement MQTT publishing functions
  - Implement `mqtt_publish()` for generic message publishing
  - Implement `mqtt_publish_json()` for JSON payload publishing
  - Add QoS and retain flag support
  - Handle publish failures with error logging
  - Track published message count
  - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5_

- [ ] 6. Implement doorbell event publishing
  - Implement `mqtt_publish_doorbell()` function
  - Publish to topic "{base_topic}/bell{N}/pressed"
  - Include timestamp in ISO 8601 format
  - Include caller ID if available
  - Use QoS 1 for reliable delivery
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5_

- [ ] 7. Implement door opener event publishing
  - Implement `mqtt_publish_door_opened()` function
  - Publish to topic "{base_topic}/door/opened"
  - Include activation method (DTMF, web, button)
  - Include caller ID if available
  - Implement `mqtt_publish_door_closed()` for door closed event
  - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5_

- [ ] 8. Implement SIP call status publishing
  - Implement `mqtt_publish_sip_call_started()` function
  - Implement `mqtt_publish_sip_call_answered()` function
  - Implement `mqtt_publish_sip_call_ended()` function
  - Include call duration in call_ended message
  - Include target number in all call messages
  - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5_

- [ ] 9. Implement system status publishing
  - Implement `mqtt_publish_status()` function
  - Publish to topic "{base_topic}/status" with retained flag
  - Include firmware version, uptime, IP address in payload
  - Publish online status on connection
  - Configure LWT for offline status
  - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5_

- [ ] 10. Integrate MQTT with doorbell handler
  - Modify `gpio_doorbell_isr_handler()` to call `mqtt_publish_doorbell()`
  - Check if MQTT is connected before publishing
  - Pass bell number (1 or 2) to MQTT function
  - Extract caller ID from SIP if available
  - _Requirements: 3.1, 3.2_

- [ ] 11. Integrate MQTT with door opener handler
  - Modify door opener activation to call `mqtt_publish_door_opened()`
  - Pass activation method (DTMF, web, button)
  - Pass caller ID if activated via DTMF
  - Publish door closed event when relay deactivates
  - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5_

- [ ] 12. Integrate MQTT with SIP client
  - Modify SIP call state handler to publish MQTT events
  - Publish call_started when call is initiated
  - Publish call_answered when call is answered
  - Publish call_ended when call terminates
  - Include call duration in call_ended event
  - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5_

- [ ] 13. Implement MQTT command subscription
  - Implement `mqtt_subscribe_commands()` function
  - Subscribe to topic "{base_topic}/command/door"
  - Subscribe to topic "{base_topic}/command/light"
  - Implement command callback handler
  - Parse received command and payload
  - _Requirements: 7.1, 7.3_

- [ ] 14. Implement door command handler
  - Handle "OPEN" command on door topic
  - Activate door opener relay for 5 seconds
  - Publish door opened event after activation
  - Publish error message for invalid commands
  - _Requirements: 7.2, 7.5_

- [ ] 15. Implement light command handler
  - Handle "TOGGLE" command on light topic
  - Toggle light relay state
  - Publish light state change event
  - Publish error message for invalid commands
  - _Requirements: 7.4, 7.5_

- [ ] 16. Implement Home Assistant MQTT Discovery
  - Implement `mqtt_publish_ha_discovery()` function
  - Publish binary sensor config for doorbell 1
  - Publish binary sensor config for doorbell 2
  - Publish switch config for door opener
  - Include device information (name, model, manufacturer, firmware)
  - Use MAC address for unique IDs
  - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5, 8.6_

- [ ] 17. Implement MQTT TLS/SSL support
  - Add TLS enable option to configuration
  - Configure MQTT client to use port 8883 when TLS enabled
  - Load and validate broker certificate
  - Support custom CA certificate upload
  - Handle TLS handshake failures with error logging
  - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.5_

- [ ] 18. Implement MQTT topic customization
  - Add base topic field to configuration
  - Use base topic prefix for all published topics
  - Update subscribed command topics with base topic
  - Republish Home Assistant discovery when base topic changes
  - Store base topic in NVS
  - _Requirements: 10.1, 10.2, 10.3, 10.4, 10.5_

- [ ] 19. Implement MQTT message history
  - Create circular buffer for last 50 published messages
  - Store timestamp, topic, payload, QoS, success/failure status
  - Implement `mqtt_get_message_history()` to retrieve history
  - Record publish failures with error reason
  - _Requirements: 16.1, 16.2, 16.3, 16.4, 16.5_

- [ ] 20. Create MQTT configuration API endpoints
  - Add `GET /api/mqtt/config` endpoint to retrieve configuration
  - Add `POST /api/mqtt/config` endpoint to update configuration
  - Add `GET /api/mqtt/status` endpoint to get connection status
  - Add `POST /api/mqtt/test` endpoint to test connection
  - Parse JSON request bodies and return JSON responses
  - _Requirements: 14.1, 14.2, 14.3, 14.4, 14.5, 14.6_

- [ ] 21. Implement MQTT test connection
  - Create temporary MQTT client for testing
  - Attempt connection to broker with provided settings
  - Return success or specific error message (connection refused, auth failed, timeout)
  - Disconnect test client after test completes
  - Display loading indicator during test
  - _Requirements: 15.1, 15.2, 15.3, 15.4, 15.5_

- [ ] 22. Create MQTT web interface section
  - Add "MQTT" menu item to sidebar navigation
  - Display MQTT connection status (Connected/Disconnected)
  - Show broker address and port when connected
  - Show last error message when disconnected
  - Display time since last successful connection
  - _Requirements: 12.1, 12.2, 12.3, 12.4, 12.5, 13.1_

- [ ] 23. Implement MQTT configuration form
  - Add enable/disable toggle checkbox
  - Add broker hostname/IP input field
  - Add port number input field (default 1883)
  - Add username and password input fields
  - Add base topic input field (default "doorstation")
  - Add Home Assistant discovery toggle
  - Add TLS enable toggle
  - _Requirements: 13.2, 13.3, 13.4, 13.5, 13.6, 13.7, 13.8_

- [ ] 24. Implement MQTT configuration JavaScript
  - Create function to load MQTT configuration
  - Create function to save MQTT configuration
  - Create function to test MQTT connection
  - Display success/error messages for configuration operations
  - Update connection status display in real-time
  - _Requirements: 14.2, 15.1_

- [ ] 25. Implement MQTT message history viewer
  - Create table to display recent published messages
  - Show timestamp, topic, payload, QoS, and delivery status
  - Highlight failed messages in red
  - Display error reason for failed messages
  - Auto-refresh history every 5 seconds
  - _Requirements: 16.2, 16.3, 16.4, 16.5_

- [ ] 26. Update CMakeLists.txt to include MQTT module
  - Add `mqtt_manager.c` to `main/CMakeLists.txt` SRCS list
  - Verify mqtt component is linked
  - Verify cJSON component is linked
  - Ensure mbedtls component is available for TLS
  - _Requirements: 1.1_

- [ ]* 27. Add comprehensive MQTT logging
  - Log all connection attempts and results
  - Log all published messages with topics
  - Log all received commands
  - Log all publish failures with error reasons
  - Log Home Assistant discovery publications
  - _Requirements: 2.2, 2.3, 2.4_

- [ ]* 28. Create integration tests for MQTT
  - Test MQTT connection to broker
  - Test doorbell event publishing
  - Test door opener event publishing
  - Test SIP call status publishing
  - Test command reception and execution
  - Test Home Assistant discovery
  - Test TLS connection
  - Test automatic reconnection
  - _Requirements: 2.1, 2.4, 3.1, 4.1, 6.1, 7.2, 8.1, 9.1_
