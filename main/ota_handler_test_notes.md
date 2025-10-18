# OTA Rollback Functionality - Test Notes

## Implementation Summary

The rollback functionality has been successfully implemented in `ota_handler.c` with the following functions:

### 1. `ota_rollback()` (lines 402-420)
- **Purpose**: Revert to previous firmware version
- **Logic**:
  - Calls `esp_ota_get_last_invalid_partition()` to find previous firmware
  - Returns `ESP_ERR_NOT_FOUND` if no previous firmware exists
  - Calls `esp_ota_set_boot_partition()` to set previous partition as boot partition
  - Logs all operations for debugging
  - Returns `ESP_OK` on success, error code on failure

### 2. `ota_mark_valid()` (lines 422-438)
- **Purpose**: Mark current firmware as valid to prevent automatic rollback
- **Logic**:
  - Gets running partition using `esp_ota_get_running_partition()`
  - Calls `esp_ota_mark_app_valid_cancel_rollback()` to mark firmware as valid
  - This prevents ESP-IDF's automatic rollback mechanism from reverting
  - Returns `ESP_OK` on success, error code on failure

### 3. Rollback Detection in `ota_get_info()` (line 82)
- **Purpose**: Detect if rollback is available
- **Logic**:
  - Calls `esp_ota_get_last_invalid_partition()` to check for previous firmware
  - Sets `info->can_rollback = (last_invalid != NULL)`
  - Web interface can use this flag to enable/disable rollback button

## Testing Approach

### Manual Testing Steps (Requires Hardware)

1. **Initial Setup**:
   - Flash firmware version 1.0 to device
   - Device boots from ota_0 partition
   - Verify `can_rollback = false` (no previous firmware)

2. **First OTA Update**:
   - Upload firmware version 1.1 via web interface
   - Firmware writes to ota_1 partition
   - Device reboots and boots from ota_1
   - Verify `can_rollback = true` (ota_0 has version 1.0)

3. **Test Rollback**:
   - Call `ota_rollback()` via web API
   - Verify function returns `ESP_OK`
   - Device reboots
   - Verify device boots from ota_0 (version 1.0)
   - Verify `can_rollback = true` (ota_1 has version 1.1)

4. **Test Mark Valid**:
   - After successful boot, call `ota_mark_valid()`
   - Verify function returns `ESP_OK`
   - Simulate crash or power loss
   - Verify device does NOT auto-rollback (stays on current firmware)

5. **Test Auto-Rollback** (ESP-IDF Feature):
   - Upload firmware with intentional boot failure
   - Device attempts to boot 3 times
   - ESP-IDF automatically rolls back to previous valid firmware
   - Verify device boots from previous partition

### Expected Behavior

#### Rollback Scenarios:
- **Scenario A**: No previous firmware
  - `ota_rollback()` returns `ESP_ERR_NOT_FOUND`
  - Log: "No previous firmware available for rollback"

- **Scenario B**: Previous firmware exists
  - `ota_rollback()` returns `ESP_OK`
  - Log: "Rolling back to partition: ota_X"
  - Device requires restart to boot from previous partition

#### Mark Valid Scenarios:
- **Scenario C**: Normal operation
  - `ota_mark_valid()` returns `ESP_OK`
  - Log: "Firmware marked as valid"
  - Prevents automatic rollback on subsequent boots

- **Scenario D**: Error case
  - If `esp_ota_get_running_partition()` fails
  - Returns `ESP_ERR_NOT_FOUND`
  - Log: "Failed to get running partition"

## Code Review Checklist

- [x] `ota_rollback()` checks for NULL partition before attempting rollback
- [x] `ota_rollback()` returns appropriate error codes
- [x] `ota_rollback()` logs all operations for debugging
- [x] `ota_mark_valid()` validates running partition exists
- [x] `ota_mark_valid()` calls correct ESP-IDF API
- [x] `ota_get_info()` correctly detects rollback availability
- [x] All functions have proper error handling
- [x] All functions have descriptive log messages

## Integration Points

### Web API Integration (Task 5):
- `POST /api/ota/rollback` endpoint will call `ota_rollback()`
- `GET /api/ota/info` endpoint will call `ota_get_info()` to get `can_rollback` flag
- Web interface will enable/disable rollback button based on `can_rollback`

### Automatic Validation:
- After successful OTA update and reboot, system should call `ota_mark_valid()`
- Recommended: Call `ota_mark_valid()` after 60 seconds of successful operation
- This prevents auto-rollback if firmware is stable

## ESP-IDF OTA Partition Behavior

The ESP-IDF OTA system uses the following logic:

1. **Boot Counter**: Tracks failed boot attempts
2. **OTA Data Partition**: Stores which partition to boot from
3. **Automatic Rollback**: After 3 failed boots, ESP-IDF automatically rolls back
4. **Mark Valid**: Calling `esp_ota_mark_app_valid_cancel_rollback()` resets boot counter

### Partition States:
- **Valid**: Firmware marked as valid, will boot normally
- **Pending Validation**: New firmware, not yet marked valid
- **Invalid**: Failed firmware, will not boot

## Verification Without Hardware

Since we cannot run the firmware without ESP32 hardware, we verify correctness through:

1. **Code Review**: All functions follow ESP-IDF OTA API patterns
2. **Error Handling**: All error cases are handled appropriately
3. **Logging**: Comprehensive logging for debugging
4. **API Compliance**: Uses correct ESP-IDF functions:
   - `esp_ota_get_last_invalid_partition()`
   - `esp_ota_set_boot_partition()`
   - `esp_ota_get_running_partition()`
   - `esp_ota_mark_app_valid_cancel_rollback()`

## Conclusion

The rollback functionality is **fully implemented and ready for testing** on hardware. The implementation follows ESP-IDF best practices and includes:

- Manual rollback via `ota_rollback()`
- Firmware validation via `ota_mark_valid()`
- Rollback detection via `ota_get_info()`
- Comprehensive error handling and logging
- Integration points for web API (Task 5)

**Status**: ✅ Implementation Complete - Ready for Hardware Testing
