# Requirements Document

## Introduction

The Hardware Testing Interface provides a web-based tool for installers and integrators to verify GPIO functionality, relay operations, and button inputs on the ESP32 SIP Door Station before physical installation. This feature enables quick validation of hardware connections without requiring physical button presses or external test equipment, reducing installation time and troubleshooting complexity.

## Glossary

- **System**: The ESP32 SIP Door Station hardware testing interface
- **Door_Relay**: The GPIO-controlled relay that activates the door opener mechanism
- **Light_Relay**: The GPIO-controlled relay that controls the light output
- **Doorbell_Button**: Physical button inputs (Bell 1 and Bell 2) that trigger SIP calls
- **Test_Duration**: The time period (in milliseconds) that the door relay remains active during testing
- **Hardware_State**: The current status of all GPIO inputs and outputs (relay states, button states)
- **Emergency_Stop**: A safety mechanism to immediately deactivate all active hardware tests

## Requirements

### Requirement 1: Hardware Test Module Initialization

**User Story:** As an installer, I want the hardware testing system to initialize safely on startup, so that I can reliably test hardware components without system crashes or conflicts.

#### Acceptance Criteria

1. WHEN THE System starts, THE System SHALL initialize the hardware test module with mutex protection
2. WHEN THE System initializes, THE System SHALL create the hardware state tracking structure
3. WHEN THE System initializes, THE System SHALL verify GPIO driver availability
4. WHERE the GPIO driver is unavailable, THE System SHALL log an error and disable hardware testing
5. WHEN initialization completes, THE System SHALL set the hardware test module to ready state

### Requirement 2: Doorbell Button Simulation

**User Story:** As an installer, I want to simulate doorbell button presses from the web interface, so that I can verify the doorbell triggers SIP calls correctly without physically pressing buttons.

#### Acceptance Criteria

1. WHEN THE installer requests a doorbell test, THE System SHALL validate the bell number is 1 or 2
2. IF the bell number is invalid, THEN THE System SHALL return an error message "Invalid bell number (must be 1 or 2)"
3. WHEN a valid doorbell test is requested, THE System SHALL trigger the same logic as a physical button press
4. WHEN a doorbell test executes, THE System SHALL initiate a SIP call if the system is configured
5. WHEN a doorbell test completes, THE System SHALL return a success message indicating which bell was tested

### Requirement 3: Door Opener Relay Test with Duration Control

**User Story:** As an installer, I want to activate the door opener relay for a specific duration, so that I can verify the relay operates correctly and the door mechanism responds as expected.

#### Acceptance Criteria

1. WHEN THE installer requests a door opener test, THE System SHALL validate the duration is between 1000ms and 10000ms
2. IF the duration is outside the valid range, THEN THE System SHALL return an error message "Duration must be between 1 and 10 seconds"
3. WHEN a valid door opener test is requested, THE System SHALL check if a door test is already active
4. IF a door test is already active, THEN THE System SHALL reject the request with error message "Door opener test already in progress"
5. WHEN a door opener test starts, THE System SHALL activate the Door_Relay GPIO pin
6. WHEN a door opener test starts, THE System SHALL create a timeout task to deactivate the relay after the specified duration
7. WHEN the Test_Duration expires, THE System SHALL deactivate the Door_Relay GPIO pin automatically
8. WHEN a door opener test completes, THE System SHALL update the hardware state to inactive

### Requirement 4: Light Relay Toggle Test

**User Story:** As an installer, I want to toggle the light relay on and off, so that I can verify the light output is wired correctly and responds to control signals.

#### Acceptance Criteria

1. WHEN THE installer requests a light toggle test, THE System SHALL read the current Light_Relay state
2. WHEN the current state is read, THE System SHALL toggle the Light_Relay GPIO pin to the opposite state
3. WHEN the Light_Relay is toggled, THE System SHALL return the new state (on or off) to the caller
4. WHEN the Light_Relay state changes, THE System SHALL update the hardware state tracking

### Requirement 5: Hardware State Tracking and Reporting

**User Story:** As an installer, I want to see the current state of all hardware components in real-time, so that I can monitor relay states, button presses, and test progress during installation.

#### Acceptance Criteria

1. WHEN THE web interface requests hardware state, THE System SHALL return the current Door_Relay state (active or inactive)
2. WHEN THE web interface requests hardware state, THE System SHALL return the current Light_Relay state (on or off)
3. WHEN THE web interface requests hardware state, THE System SHALL return the Doorbell_Button states for Bell 1 and Bell 2
4. WHILE a door opener test is active, THE System SHALL calculate and return the remaining time in milliseconds
5. WHEN no door opener test is active, THE System SHALL return zero for remaining time
6. WHEN THE web interface polls for state, THE System SHALL respond within 100ms

### Requirement 6: Emergency Stop Functionality

**User Story:** As an installer, I want an emergency stop button that immediately deactivates all tests, so that I can quickly stop hardware operations if something goes wrong during testing.

#### Acceptance Criteria

1. WHEN THE installer activates emergency stop, THE System SHALL immediately deactivate the Door_Relay
2. WHEN THE installer activates emergency stop, THE System SHALL cancel any active timeout tasks
3. WHEN THE installer activates emergency stop, THE System SHALL reset the hardware test context state
4. WHEN emergency stop completes, THE System SHALL return a success message "All tests stopped"
5. WHEN emergency stop is activated, THE System SHALL complete the operation within 50ms

### Requirement 7: Web Server Hardware Test Endpoints

**User Story:** As an installer, I want RESTful API endpoints for hardware testing, so that the web interface can trigger tests and retrieve hardware state information.

#### Acceptance Criteria

1. WHEN THE System starts, THE System SHALL register the POST /api/hardware/test/doorbell endpoint
2. WHEN THE System starts, THE System SHALL register the POST /api/hardware/test/door endpoint
3. WHEN THE System starts, THE System SHALL register the POST /api/hardware/test/light endpoint
4. WHEN THE System starts, THE System SHALL register the GET /api/hardware/state endpoint
5. WHEN THE System starts, THE System SHALL register the POST /api/hardware/test/stop endpoint
6. WHEN an endpoint receives a request, THE System SHALL parse JSON body data correctly
7. WHEN an endpoint processes a request, THE System SHALL return appropriate JSON responses with success or error status

### Requirement 8: Web Interface Hardware Testing Section

**User Story:** As an installer, I want a dedicated hardware testing section in the web interface, so that I can access all testing controls and view hardware state from a single location.

#### Acceptance Criteria

1. WHEN THE web interface loads, THE System SHALL display a "Hardware Testing" menu item in the sidebar
2. WHEN the hardware testing section is displayed, THE System SHALL show a warning about physical relay activation
3. WHEN the hardware testing section is displayed, THE System SHALL show the current state of Door_Relay, Light_Relay, and both Doorbell_Buttons
4. WHEN the hardware testing section is displayed, THE System SHALL provide buttons to test Bell 1 and Bell 2
5. WHEN the hardware testing section is displayed, THE System SHALL provide a duration slider (1-10 seconds) and button for door opener test
6. WHEN the hardware testing section is displayed, THE System SHALL provide a button to toggle the light relay
7. WHEN the hardware testing section is displayed, THE System SHALL provide an emergency stop button
8. WHEN the hardware testing section is displayed, THE System SHALL provide a feedback area for test result messages

### Requirement 9: Real-Time State Updates and Visual Feedback

**User Story:** As an installer, I want to see real-time updates of hardware state with visual indicators, so that I can immediately see the results of my tests without manually refreshing.

#### Acceptance Criteria

1. WHEN the hardware testing section is active, THE System SHALL poll the hardware state every 500ms
2. WHEN the Door_Relay state changes, THE System SHALL update the visual indicator with color coding (Active/Inactive)
3. WHEN the Light_Relay state changes, THE System SHALL update the visual indicator with color coding (On/Off)
4. WHEN a Doorbell_Button state changes, THE System SHALL update the button state indicator
5. WHILE a door opener test is active, THE System SHALL display a countdown timer showing remaining seconds
6. WHEN the door opener test completes, THE System SHALL hide the countdown timer
7. WHEN a test completes, THE System SHALL display a feedback message for 5 seconds

### Requirement 10: Safety Warnings and Confirmations

**User Story:** As an installer, I want clear safety warnings and confirmation dialogs, so that I understand the physical implications of tests before activating relays.

#### Acceptance Criteria

1. WHEN the hardware testing section loads, THE System SHALL display a warning box about physical relay activation
2. WHEN the warning box is displayed, THE System SHALL include text about proper wiring verification
3. WHEN the installer clicks the door opener test button, THE System SHALL display a confirmation dialog
4. WHEN the confirmation dialog is displayed, THE System SHALL show the selected duration
5. IF the installer cancels the confirmation, THEN THE System SHALL not activate the door opener test

### Requirement 11: Build Configuration Integration

**User Story:** As a developer, I want the hardware test module to be properly integrated into the build system, so that it compiles correctly with all required dependencies.

#### Acceptance Criteria

1. WHEN the project builds, THE System SHALL include hardware_test.c in the compilation
2. WHEN the project builds, THE System SHALL link the GPIO driver component
3. WHEN the project builds, THE System SHALL link the FreeRTOS components
4. WHEN the project builds, THE System SHALL verify all dependencies are available
5. IF any required component is missing, THEN THE System SHALL fail the build with a clear error message
