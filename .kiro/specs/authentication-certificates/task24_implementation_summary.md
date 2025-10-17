# Task 24: Password Reset Mechanism - Implementation Summary

## Overview
Implemented a physical password reset mechanism using the BOOT button (GPIO 0). When held for 10 seconds, the admin password is deleted from NVS, all sessions are invalidated, and the initial setup wizard is triggered on next web access.

## Changes Made

### 1. Authentication Manager (`auth_manager.h` / `auth_manager.c`)

#### New Function Added:
- `auth_reset_password()` - Deletes admin password and username from NVS

#### Implementation Details:
- Password reset deletes both password hash and username from NVS using `nvs_erase_key()`
- Invalidates all active sessions immediately
- Logs the password reset event to audit log with "physical-reset" as IP address
- After reset, `auth_is_password_set()` returns false, triggering the initial setup wizard
- No default password is set - user must go through setup wizard to create new password

### 2. GPIO Handler (`gpio_handler.h` / `gpio_handler.c`)

#### New Function:
- `gpio_start_reset_monitor()` - Starts a background task to monitor BOOT button

#### New Task:
- `reset_monitor_task()` - Polls BOOT button every 100ms
  - Detects when button is pressed (active low)
  - Counts consecutive presses (100 checks = 10 seconds)
  - **Short press (< 1 second)**: Triggers doorbell call on button release
  - **Long press (10 seconds)**: Triggers password deletion
  - Logs progress every second while button is held (after 1 second)
  - Waits for button release to prevent multiple resets
  - Logs success message indicating setup wizard will be triggered

#### Configuration Changes:
- BOOT button (GPIO 0) now configured without interrupt (polling mode)
- Removed ISR handler for BOOT button (now handled by polling task)
- Added `#include "auth_manager.h"` for password reset functionality
- Increased task stack size to 8192 bytes to support SIP operations for doorbell simulation

### 3. Main Application (`main.c`)

#### Initialization:
- Added call to `gpio_start_reset_monitor()` after GPIO initialization
- Reset monitor task starts early in boot sequence to be available immediately

### 4. Web Server (`web_server.c`)

#### No Changes Required:
- Login handler remains unchanged
- After password reset, `auth_is_password_set()` returns false
- Web server automatically redirects to setup wizard when no password is set
- This is handled by existing setup wizard logic

### 5. Setup Wizard (`setup.html`)

#### Live Password Validation Added:
- Added `validatePasswordMatch()` function for real-time visual validation
- Event listeners on both password fields trigger validation on input
- Confirm password field shows **green border** when passwords match
- Confirm password field shows **red border** when passwords don't match
- No error messages displayed during typing (non-intrusive)
- Validation only shows when confirm password field has content
- Improves user experience with subtle visual feedback

## Requirements Fulfilled

✅ **13.1** - Physical reset button option provided (BOOT button GPIO 0)
✅ **13.2** - Password deleted from NVS when button held for 10 seconds
✅ **13.3** - Password reset event logged to audit log
✅ **13.4** - Initial setup wizard triggered on next web access (password must be set)
✅ **13.5** - All sessions invalidated on password reset

## Security Considerations

1. **No Default Password**: Password is completely deleted from NVS, not set to a default value
2. **Physical Access Required**: Reset requires physical access to device (BOOT button)
3. **Setup Wizard Required**: User must go through initial setup wizard to set new password
4. **Session Invalidation**: All active sessions immediately invalidated
5. **Audit Logging**: Reset event logged with timestamp and "physical-reset" identifier
6. **Password Strength Enforced**: Setup wizard enforces password strength requirements (8+ chars, uppercase, lowercase, digit)

## User Experience

### Short Press (Doorbell Simulation):
1. User presses and releases BOOT button (hold < 1 second)
2. On release: "BOOT button short press - triggering doorbell call"
3. SIP call is initiated to SIP-Target1 (if configured)
4. Quick press and release - just like a real doorbell button
5. This allows testing doorbell functionality without physical doorbell button

### Long Press (Password Reset):
1. User presses and holds BOOT button
2. Console logs: "BOOT button pressed - hold for 10 seconds to reset password"
3. Progress logged every second after 1 second: "BOOT button held for X seconds..."
4. After 10 seconds: "Password deleted successfully"
5. Console logs: "Initial setup wizard will be triggered on next web access"
6. User releases button
7. All sessions invalidated

### Web Access After Reset:
1. User navigates to device web interface
2. System detects no password is set (`auth_is_password_set()` returns false)
3. User is automatically redirected to initial setup wizard
4. User must create new admin password through setup wizard
5. **Live validation**: Confirm password field shows green/red border as user types
6. Password must meet strength requirements (8+ chars, uppercase, lowercase, digit)
7. Visual feedback is subtle and non-intrusive (no error messages while typing)
8. After setup completes, user can log in with new credentials

## Testing Recommendations

1. **Manual Testing**:
   - Hold BOOT button for 10 seconds
   - Verify console logs show progress
   - Verify password deletion message appears
   - Access web interface
   - Verify setup wizard is displayed
   - Complete setup wizard with new password
   - Verify login works with new credentials

2. **Edge Cases**:
   - Release button before 10 seconds (should not reset)
   - Hold button for longer than 10 seconds (should reset once)
   - Multiple rapid presses (should not trigger reset)
   - Reset while sessions are active (should invalidate all)
   - Reset when already in setup mode (should work normally)

## Files Modified

1. `main/auth_manager.h` - Added `auth_reset_password()` function declaration
2. `main/auth_manager.c` - Implemented password deletion functionality
3. `main/gpio_handler.h` - Added reset monitor function declaration
4. `main/gpio_handler.c` - Implemented BOOT button monitoring task with dual functionality
5. `main/main.c` - Added reset monitor initialization
6. `main/setup.html` - Added live password match validation

## Notes

- The BOOT button now has dual functionality:
  - **Short press (< 1 second)**: Triggers doorbell call on release (for testing)
  - **Long press (10 seconds)**: Triggers password reset
- Reset monitor task runs continuously with low overhead (100ms polling interval)
- No default password is set - complete password deletion ensures maximum security
- Password deletion is permanent and requires going through setup wizard
- **Live password validation** in setup wizard provides subtle visual feedback (green/red border)
- Validation is non-intrusive - no error messages displayed while typing
- Implementation follows ESP32 best practices for GPIO polling and task management
- Existing setup wizard logic handles the "no password set" state automatically
- Task stack increased to 8192 bytes to support SIP operations for doorbell simulation
