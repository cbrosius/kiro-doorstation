# Requirements Document

## Introduction

The OTA (Over-The-Air) Firmware Update feature implements remote firmware updates for the ESP32 SIP Door Station, allowing administrators to update device firmware through the web interface without physical access. The implementation uses ESP-IDF's native OTA partition system with dual app partitions for safe rollback capability.

## Glossary

- **System**: The ESP32 SIP Door Station OTA update system
- **OTA_Update**: Over-The-Air firmware update process
- **Firmware_Image**: Binary file containing new firmware (.bin file)
- **OTA_Partition**: Flash memory partition for storing alternate firmware
- **Rollback**: Reverting to previous firmware version after failed update
- **Validation**: Verification that firmware image is valid and compatible
- **Boot_Partition**: The partition from which the device boots

## Requirements

### Requirement 1: Firmware Information Display

**User Story:** As a system administrator, I want to see current firmware information, so that I can verify the installed version before updating.

#### Acceptance Criteria

1. WHEN THE web interface loads, THE System SHALL display current firmware version
2. WHEN firmware info is displayed, THE System SHALL show build date
3. WHEN firmware info is displayed, THE System SHALL show ESP-IDF version
4. WHEN firmware info is displayed, THE System SHALL show current partition label (ota_0 or ota_1)
5. WHEN firmware info is displayed, THE System SHALL indicate if rollback is available

### Requirement 2: Firmware Upload Interface

**User Story:** As a system administrator, I want to upload firmware files through the web interface, so that I can update the device remotely.

#### Acceptance Criteria

1. WHEN THE web interface loads, THE System SHALL provide file upload interface
2. WHEN file is selected, THE System SHALL validate file extension is .bin
3. WHEN file is selected, THE System SHALL display file size
4. WHEN upload button is clicked, THE System SHALL display confirmation dialog with warnings
5. WHEN upload is confirmed, THE System SHALL begin firmware upload with progress indicator

### Requirement 3: Firmware Image Validation

**User Story:** As a system administrator, I want the system to validate firmware images before flashing, so that I can avoid bricking the device with invalid firmware.

#### Acceptance Criteria

1. WHEN THE System receives firmware image, THE System SHALL verify ESP32 image magic number (0xE9)
2. WHEN magic number is verified, THE System SHALL validate image header structure
3. WHEN header is validated, THE System SHALL check firmware size fits in OTA partition
4. WHEN size is validated, THE System SHALL verify image segments are not corrupted
5. WHEN segments are validated, THE System SHALL calculate and verify SHA256 checksum
6. IF validation fails, THEN THE System SHALL abort update and return specific error message

### Requirement 4: OTA Update Process

**User Story:** As a system administrator, I want a reliable firmware update process, so that the device updates safely without data loss or bricking.

#### Acceptance Criteria

1. WHEN THE System begins OTA update, THE System SHALL identify next available OTA partition
2. WHEN partition is identified, THE System SHALL erase the partition
3. WHEN partition is erased, THE System SHALL write firmware data in chunks
4. WHEN all data is written, THE System SHALL verify written data integrity
5. WHEN verification succeeds, THE System SHALL set new partition as boot partition
6. WHEN boot partition is set, THE System SHALL restart the device
7. IF any step fails, THEN THE System SHALL abort update and preserve current firmware

### Requirement 5: Upload Progress Reporting

**User Story:** As a system administrator, I want to see upload progress, so that I know the update is proceeding and estimate completion time.

#### Acceptance Criteria

1. WHEN THE firmware upload begins, THE System SHALL display progress bar
2. WHILE upload is in progress, THE System SHALL update progress percentage every 10% or 50KB
3. WHEN upload completes, THE System SHALL display "Validating firmware..." message
4. WHEN validation completes, THE System SHALL display "Flashing firmware..." message
5. WHEN flashing completes, THE System SHALL display "Update successful, restarting..." message

### Requirement 6: Rollback Functionality

**User Story:** As a system administrator, I want to rollback to previous firmware if the new version has issues, so that I can recover from problematic updates.

#### Acceptance Criteria

1. WHEN THE System has two valid firmware partitions, THE System SHALL enable rollback button
2. WHEN rollback button is clicked, THE System SHALL display confirmation dialog
3. WHEN rollback is confirmed, THE System SHALL set previous partition as boot partition
4. WHEN boot partition is changed, THE System SHALL restart the device
5. WHEN device restarts, THE System SHALL boot from previous firmware version

### Requirement 7: Automatic Rollback on Boot Failure

**User Story:** As a system administrator, I want automatic rollback if new firmware fails to boot, so that the device remains functional even if an update introduces boot failures.

#### Acceptance Criteria

1. WHEN THE device boots with new firmware, THE System SHALL mark firmware as "pending validation"
2. WHEN firmware runs successfully, THE System SHALL mark firmware as "valid" after 60 seconds
3. IF device fails to boot 3 times, THEN THE System SHALL automatically rollback to previous firmware
4. WHEN automatic rollback occurs, THE System SHALL log rollback event
5. WHEN device boots after rollback, THE System SHALL display warning about failed update

### Requirement 8: OTA State Management

**User Story:** As a developer, I want proper OTA state management, so that the system handles concurrent requests and errors correctly.

#### Acceptance Criteria

1. WHEN THE System is idle, THE System SHALL accept new OTA update requests
2. WHEN OTA update is in progress, THE System SHALL reject new update requests with error message
3. WHEN OTA update fails, THE System SHALL return to idle state
4. WHEN OTA update succeeds, THE System SHALL restart device
5. WHEN System restarts, THE System SHALL return to idle state

### Requirement 9: Error Handling and Recovery

**User Story:** As a system administrator, I want clear error messages and recovery options, so that I can troubleshoot failed updates.

#### Acceptance Criteria

1. WHEN firmware validation fails, THE System SHALL return error message "Invalid firmware image format"
2. WHEN firmware size exceeds partition, THE System SHALL return error message "Firmware size exceeds partition capacity"
3. WHEN flash write fails, THE System SHALL return error message "Flash write error"
4. WHEN upload is interrupted, THE System SHALL abort update and clean up partition
5. WHEN any error occurs, THE System SHALL preserve current working firmware

### Requirement 10: Web Server OTA Endpoints

**User Story:** As a developer, I want RESTful API endpoints for OTA operations, so that the web interface can perform firmware updates.

#### Acceptance Criteria

1. WHEN THE System starts, THE System SHALL register POST /api/ota/upload endpoint
2. WHEN THE System starts, THE System SHALL register GET /api/ota/info endpoint
3. WHEN THE System starts, THE System SHALL register POST /api/ota/rollback endpoint
4. WHEN THE System starts, THE System SHALL register GET /api/ota/status endpoint
5. WHEN endpoints receive requests, THE System SHALL parse multipart form data correctly
6. WHEN endpoints process requests, THE System SHALL return appropriate JSON responses

### Requirement 11: Upload Security and Validation

**User Story:** As a system administrator, I want firmware uploads to be validated for security, so that only legitimate firmware can be installed.

#### Acceptance Criteria

1. WHEN THE System receives firmware upload, THE System SHALL verify file is not empty
2. WHEN file is received, THE System SHALL check file size is reasonable (< 2MB)
3. WHEN file is validated, THE System SHALL verify ESP32 chip type matches
4. WHEN chip type matches, THE System SHALL verify partition table compatibility
5. IF any security check fails, THEN THE System SHALL reject upload with specific error

### Requirement 12: Post-Update Verification

**User Story:** As a system administrator, I want the system to verify firmware works after update, so that I can be confident the update was successful.

#### Acceptance Criteria

1. WHEN THE device boots after update, THE System SHALL verify all critical components initialize
2. WHEN components initialize, THE System SHALL verify WiFi connectivity
3. WHEN WiFi connects, THE System SHALL verify web server starts
4. WHEN web server starts, THE System SHALL mark firmware as valid
5. IF verification fails, THEN THE System SHALL trigger automatic rollback

### Requirement 13: Update Timeout Protection

**User Story:** As a system administrator, I want upload timeouts to prevent hung updates, so that the system doesn't remain in update state indefinitely.

#### Acceptance Criteria

1. WHEN THE firmware upload begins, THE System SHALL start 5-minute timeout timer
2. IF upload does not complete within timeout, THEN THE System SHALL abort update
3. WHEN timeout occurs, THE System SHALL clean up partial firmware data
4. WHEN timeout occurs, THE System SHALL return error message "Upload timeout"
5. WHEN timeout occurs, THE System SHALL return to idle state

### Requirement 14: Memory Management During Update

**User Story:** As a developer, I want efficient memory management during OTA updates, so that the system doesn't run out of memory during large firmware uploads.

#### Acceptance Criteria

1. WHEN THE System processes firmware upload, THE System SHALL use 4KB write buffer
2. WHEN buffer is full, THE System SHALL write to flash immediately
3. WHEN write completes, THE System SHALL reuse buffer for next chunk
4. WHEN update completes, THE System SHALL free all allocated memory
5. WHEN update is aborted, THE System SHALL free all allocated memory
