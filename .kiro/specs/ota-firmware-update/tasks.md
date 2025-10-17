# Implementation Plan

- [x] 1. Create OTA handler module with core functionality





  - Create `main/ota_handler.h` and `main/ota_handler.c` files
  - Implement `ota_handler_init()` function
  - Implement `ota_get_info()` to retrieve current firmware information (version, build date, partition label)
  - Add OTA context structure with state machine (IDLE, BEGIN, WRITING, VALIDATING, COMPLETE, ABORT, ERROR)
  - _Requirements: 3.1.2, 3.1.4_

- [ ] 2. Implement OTA update process functions
  - Implement `ota_begin_update()` to initialize OTA partition and handle
  - Implement `ota_write_chunk()` to write firmware data to flash
  - Implement `ota_end_update()` to finalize and validate the update
  - Implement `ota_abort_update()` to clean up on error
  - Add progress tracking (bytes written, percentage complete)
  - _Requirements: 3.1.4, 3.1.8_

- [ ] 3. Add firmware validation and safety checks
  - Implement ESP32 image header validation (magic number 0xE9)
  - Add firmware size validation against partition capacity
  - Implement SHA256 checksum verification
  - Add chip type compatibility check (ESP32-S3)
  - Implement error handling for validation failures
  - _Requirements: 3.1.4, 3.1.9_

- [ ] 4. Implement rollback functionality
  - Implement `ota_rollback()` to revert to previous firmware
  - Implement `ota_mark_valid()` to prevent auto-rollback
  - Add logic to detect and enable rollback button when previous firmware exists
  - Test rollback mechanism with valid previous partition
  - _Requirements: 3.1.9_

- [ ] 5. Create web server OTA endpoints
  - Add `POST /api/ota/upload` endpoint with multipart/form-data handling
  - Add `GET /api/ota/info` endpoint to return current firmware information
  - Add `POST /api/ota/rollback` endpoint to trigger rollback
  - Add `GET /api/ota/status` endpoint for real-time update status
  - Implement chunked data reception for large firmware files
  - _Requirements: 3.1.3, 3.1.4_

- [ ] 6. Add OTA section to web interface HTML
  - Add OTA Update section in Dashboard or System menu
  - Display current firmware version, build date, and ESP-IDF version
  - Create file upload interface with file input and upload button
  - Add progress bar container with percentage display
  - Add status message area for upload/validation/apply states
  - Add rollback button (initially disabled)
  - _Requirements: 3.1.2, 3.1.3, 3.1.5_

- [ ] 7. Implement web interface JavaScript for OTA upload
  - Create `uploadFirmware()` function with file validation (.bin extension)
  - Implement FormData creation and XMLHttpRequest upload
  - Add upload progress event handler to update progress bar
  - Implement confirmation dialog before starting update
  - Add success handler with automatic page reload after 5 seconds
  - Add error handler with user-friendly error messages
  - _Requirements: 3.1.3, 3.1.5, 3.1.6, 3.1.7_

- [ ] 8. Implement real-time status updates during OTA
  - Add progress reporting every 10% or 50KB
  - Display status messages: "Uploading", "Verifying", "Applying", "Complete", "Failed"
  - Update progress bar percentage in real-time
  - Show estimated time remaining during upload
  - Display detailed error messages on failure
  - _Requirements: 3.1.5, 3.1.8_

- [ ] 9. Add safety warnings and user guidance
  - Display warning about device restart during update process
  - Add warning about not powering off device during update
  - Show estimated update duration based on file size
  - Add confirmation dialog for destructive actions (update, rollback)
  - Display success message with countdown before automatic restart
  - _Requirements: 3.1.6, 3.1.7, 3.1.11_

- [ ] 10. Implement automatic restart after successful update
  - Add 5-second countdown after successful update
  - Call `esp_restart()` to reboot device with new firmware
  - Display reconnection instructions to user
  - Implement automatic page reload attempt after restart
  - _Requirements: 3.1.7_

- [ ] 11. Add rollback UI and functionality
  - Enable rollback button when previous firmware partition exists
  - Implement `rollbackFirmware()` JavaScript function
  - Add confirmation dialog for rollback action
  - Call `/api/ota/rollback` endpoint
  - Display rollback status and restart countdown
  - _Requirements: 3.1.9_

- [ ] 12. Update CMakeLists.txt to include OTA handler
  - Add `ota_handler.c` to `main/CMakeLists.txt` SRCS list
  - Ensure `esp_ota_ops` component is linked
  - Verify partition table configuration supports dual OTA partitions
  - _Requirements: 3.1.2_

- [ ]* 13. Add comprehensive error handling and logging
  - Log all OTA operations with timestamps
  - Add detailed error messages for each failure scenario
  - Implement timeout handling (5 minutes max upload time)
  - Add validation error logging with specific failure reasons
  - Log successful updates with version information
  - _Requirements: 3.1.8, 3.1.9_

- [ ]* 14. Create integration tests for OTA functionality
  - Test upload of valid firmware file
  - Test rejection of invalid file formats
  - Test rejection of oversized firmware
  - Test progress reporting accuracy
  - Test rollback functionality
  - Test automatic restart after update
  - _Requirements: 3.1.4, 3.1.9_
