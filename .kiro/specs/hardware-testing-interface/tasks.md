# Implementation Plan

- [x] 1. Create hardware test handler module





  - Create `main/hardware_test.h` and `main/hardware_test.c` files
  - Define hardware test context structure with state tracking
  - Implement `hardware_test_init()` function with mutex initialization
  - Define `hardware_state_t` structure for state reporting
  - _Requirements: 8.1.1_

- [x] 2. Implement doorbell test functionality




  - Implement `hardware_test_doorbell()` function with bell number validation
  - Add logic to simulate doorbell button press by calling existing GPIO handler
  - Verify doorbell test triggers SIP call when configured
  - Add error handling for invalid bell numbers
  - _Requirements: 8.1.2_

- [x] 3. Implement door opener relay test with duration control





  - Implement `hardware_test_door_opener()` function with duration parameter
  - Add duration validation (1000ms to 10000ms range)
  - Implement GPIO relay activation logic
  - Create timeout task to automatically deactivate relay after duration
  - Add mutex protection to prevent concurrent door opener tests
  - _Requirements: 8.1.3, 8.1.6_

- [x] 4. Implement light relay toggle test





  - Implement `hardware_test_light_toggle()` function
  - Add logic to read current light relay state
  - Toggle light relay GPIO pin
  - Return new state (on/off) to caller
  - _Requirements: 8.1.3_

- [x] 5. Implement hardware state tracking and reporting





  - Implement `hardware_test_get_state()` function
  - Read current door relay state (active/inactive)
  - Read current light relay state (on/off)
  - Read doorbell button states (pressed/not pressed)
  - Calculate remaining time for active door opener test
  - _Requirements: 8.1.4, 8.1.6_
-

- [x] 6. Implement emergency stop functionality




  - Implement `hardware_test_stop_all()` function
  - Deactivate door opener relay immediately
  - Cancel any active timeout tasks
  - Reset hardware test context state
  - _Requirements: 8.1.5_

- [x] 7. Create web server hardware test endpoints





  - Add `POST /api/hardware/test/doorbell` endpoint with JSON body parsing
  - Add `POST /api/hardware/test/door` endpoint with duration parameter
  - Add `POST /api/hardware/test/light` endpoint
  - Add `GET /api/hardware/state` endpoint for state polling
  - Add `POST /api/hardware/test/stop` endpoint for emergency stop
  - _Requirements: 8.1.1, 8.1.2, 8.1.3_

- [x] 8. Add Hardware Testing section to web interface





  - Add "Hardware Testing" menu item to sidebar navigation
  - Create hardware testing section HTML structure
  - Add warning box about physical relay activation
  - Create current state display grid (door relay, light relay, bell states)
  - Add doorbell test buttons (Bell 1, Bell 2)
  - Add door opener test controls with duration slider (1-10 seconds)
  - Add light toggle button
  - Add emergency stop button
  - Add test feedback area for result messages
  - _Requirements: 8.1.1, 8.1.2, 8.1.3, 8.1.5_

- [x] 9. Implement web interface JavaScript for hardware testing





  - Create `updateHardwareState()` function with 500ms polling interval
  - Implement `testDoorbell()` function with bell parameter
  - Implement `testDoorOpener()` function with duration from slider
  - Implement `testLightToggle()` function
  - Implement `emergencyStop()` function
  - Add `updateDurationDisplay()` function for slider value display
  - Create `showFeedback()` function for test result messages
  - _Requirements: 8.1.2, 8.1.3, 8.1.4_

- [x] 10. Add real-time state indicators and countdown




  - Update door relay state indicator (Active/Inactive) with color coding
  - Update light relay state indicator (On/Off) with color coding
  - Update doorbell button state indicators
  - Display countdown timer for active door opener test
  - Hide countdown when door opener is inactive
  - _Requirements: 8.1.4, 8.1.6_

- [ ] 11. Add safety warnings and confirmations
  - Display warning box about physical relay activation
  - Add confirmation dialog before door opener activation
  - Display safety warning about proper wiring
  - Show duration in confirmation dialog
  - _Requirements: 8.1.5_

- [ ] 12. Update CMakeLists.txt to include hardware test module
  - Add `hardware_test.c` to `main/CMakeLists.txt` SRCS list
  - Verify GPIO driver component is linked
  - Ensure FreeRTOS components are available
  - _Requirements: 8.1.1_

- [ ]* 13. Add comprehensive error handling and validation
  - Validate bell number (1 or 2) in doorbell test
  - Validate duration range (1000-10000ms) in door opener test
  - Check for concurrent door opener test attempts
  - Add error messages for all validation failures
  - Log all hardware test operations with timestamps
  - _Requirements: 8.1.2, 8.1.3_

- [ ]* 14. Create integration tests for hardware testing
  - Test doorbell simulation triggers SIP call
  - Test door opener activation and timeout
  - Test light relay toggle
  - Test state polling accuracy
  - Test emergency stop functionality
  - Test concurrent request rejection
  - Verify timeout accuracy (±50ms)
  - _Requirements: 8.1.2, 8.1.3, 8.1.4_
