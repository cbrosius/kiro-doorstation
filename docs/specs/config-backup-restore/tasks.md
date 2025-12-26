# Implementation Plan

- [ ] 1. Create configuration manager module structure
  - Create `main/config_manager.h` and `main/config_manager.c` files
  - Define `device_config_t` structure with all configuration sections (WiFi, SIP, NTP, DTMF, Hardware)
  - Define `config_metadata_t` structure for backup metadata
  - Implement `config_manager_init()` function
  - _Requirements: 11.4.1, 11.4.2_

- [ ] 2. Implement configuration reading from NVS
  - Create `config_read_all()` function to read all settings from NVS
  - Read WiFi configuration (SSID, password, static IP settings)
  - Read SIP configuration (server, port, username, password, targets)
  - Read NTP configuration (server, timezone)
  - Read DTMF security configuration (PIN, timeout, max attempts)
  - Read hardware configuration (GPIO pin assignments)
  - _Requirements: 11.4.2, 11.4.3_

- [ ] 3. Implement JSON serialization for backup
  - Implement `config_backup_to_json()` function using cJSON library
  - Create JSON structure with metadata (version, timestamp, firmware version)
  - Serialize WiFi configuration section
  - Serialize SIP configuration section
  - Serialize NTP configuration section
  - Serialize DTMF security configuration section
  - Serialize hardware configuration section
  - Add option to exclude passwords (replace with null or "********")
  - _Requirements: 11.4.2, 11.4.3, 11.4.9_

- [ ] 4. Implement JSON deserialization for restore
  - Implement `config_restore_from_json()` function using cJSON library
  - Parse JSON and extract metadata
  - Parse WiFi configuration section
  - Parse SIP configuration section
  - Parse NTP configuration section
  - Parse DTMF security configuration section
  - Parse hardware configuration section
  - Handle missing optional fields gracefully
  - _Requirements: 11.4.5, 11.4.6_

- [ ] 5. Implement configuration validation
  - Implement `config_validate_json()` function
  - Validate JSON syntax and structure
  - Check required fields are present
  - Validate IP address formats
  - Validate port number ranges (1-65535)
  - Validate PIN code format (1-8 digits)
  - Validate SSID length (1-32 characters)
  - Check configuration version compatibility
  - Return detailed validation errors
  - _Requirements: 11.4.5, 11.4.8_

- [ ] 6. Implement configuration writing to NVS with rollback
  - Create `config_write_all()` function to write all settings to NVS
  - Backup current configuration before applying new configuration
  - Write WiFi configuration to NVS
  - Write SIP configuration to NVS
  - Write NTP configuration to NVS
  - Write DTMF security configuration to NVS
  - Write hardware configuration to NVS
  - Implement rollback mechanism on write failure
  - _Requirements: 11.4.6, 11.4.7, 11.4.8_

- [ ] 7. Implement factory reset functionality
  - Implement `config_factory_reset()` function
  - Erase all NVS entries
  - Reset to default configuration values
  - Log factory reset operation
  - _Requirements: 11.4.6_

- [ ] 8. Create web server backup/restore endpoints
  - Add `GET /api/config/backup` endpoint with query parameter for password inclusion
  - Set Content-Disposition header for file download
  - Generate filename with timestamp (doorstation-config-YYYY-MM-DD-HHMMSS.json)
  - Add `POST /api/config/restore` endpoint with JSON body
  - Add `POST /api/config/validate` endpoint for pre-restore validation
  - Add `POST /api/config/factory-reset` endpoint with confirmation
  - _Requirements: 11.4.1, 11.4.4, 11.4.5_

- [ ] 9. Add Configuration Backup/Restore section to web interface
  - Add "Configuration" or "Backup/Restore" menu item to sidebar
  - Create configuration summary display (last modified, firmware version)
  - Add backup controls with "Include passwords" checkbox
  - Add "Download Backup" button
  - Add restore controls with file input (accept .json files)
  - Add "Preview Restore" button
  - Add preview area with JSON display and apply/cancel buttons
  - Add factory reset section with warning message
  - Add feedback area for status messages
  - _Requirements: 11.4.1, 11.4.4, 11.4.5_

- [ ] 10. Implement web interface JavaScript for backup
  - Create `backupConfiguration()` function
  - Read "Include passwords" checkbox state
  - Call `/api/config/backup` endpoint with appropriate query parameter
  - Generate filename with current timestamp
  - Trigger browser download of JSON file
  - Display success feedback message
  - _Requirements: 11.4.2, 11.4.3_

- [ ] 11. Implement web interface JavaScript for restore preview
  - Create `previewRestore()` function
  - Read selected file from file input
  - Parse JSON file content
  - Call `/api/config/validate` endpoint
  - Display validation errors if invalid
  - Show JSON preview in preview area if valid
  - Store configuration for later restore
  - _Requirements: 11.4.4, 11.4.5_

- [ ] 12. Implement web interface JavaScript for restore apply
  - Create `applyRestore()` function
  - Display confirmation dialog before applying
  - Call `/api/config/restore` endpoint with configuration JSON
  - Display success message and countdown
  - Automatically reload page after 3 seconds
  - Handle and display restore errors
  - _Requirements: 11.4.6, 11.4.7_

- [ ] 13. Implement factory reset UI and JavaScript
  - Create `factoryReset()` function
  - Display confirmation prompt requiring "RESET" text input
  - Call `/api/config/factory-reset` endpoint
  - Display success message
  - Redirect to AP mode IP (192.168.4.1) after 3 seconds
  - _Requirements: 11.4.6_

- [ ] 14. Add password handling options
  - Implement password exclusion logic (default behavior)
  - Implement password inclusion logic (when checkbox selected)
  - Handle null password fields during restore (don't overwrite existing)
  - Display warning when including passwords in backup
  - _Requirements: 11.4.9, 11.4.10_

- [ ] 15. Update CMakeLists.txt to include configuration manager
  - Add `config_manager.c` to `main/CMakeLists.txt` SRCS list
  - Ensure cJSON component is linked (part of ESP-IDF)
  - Verify NVS flash component is available
  - _Requirements: 11.4.1_

- [ ]* 16. Add comprehensive error handling and logging
  - Log all backup operations with timestamps
  - Log all restore operations with success/failure status
  - Log validation errors with detailed messages
  - Log factory reset operations
  - Add error recovery for NVS write failures
  - Implement timeout handling for large configurations
  - _Requirements: 11.4.8_

- [ ]* 17. Create integration tests for backup/restore
  - Test complete backup/restore cycle
  - Test backup with passwords excluded
  - Test backup with passwords included
  - Test restore with missing optional fields
  - Test restore with invalid JSON
  - Test validation catches all error types
  - Test rollback on restore failure
  - Test factory reset functionality
  - _Requirements: 11.4.2, 11.4.5, 11.4.6, 11.4.8_
