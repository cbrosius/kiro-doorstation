# Testing Checklist - SIP Logging and Connection Management

## Pre-Testing Setup

### Hardware Requirements
- [ ] ESP32 DevKit connected
- [ ] USB cable connected to computer
- [ ] Power supply adequate (5V, 500mA minimum)

### Software Requirements
- [ ] ESP-IDF environment set up
- [ ] Project compiled successfully (`idf.py build`)
- [ ] Firmware flashed to ESP32 (`idf.py flash`)
- [ ] Serial monitor available (`idf.py monitor`)

### Network Requirements
- [ ] WiFi network available
- [ ] ESP32 connected to WiFi
- [ ] ESP32 IP address known
- [ ] Computer on same network as ESP32

### SIP Server Requirements
- [ ] SIP server accessible (e.g., Asterisk, FreePBX, or cloud SIP provider)
- [ ] SIP credentials available (server, username, password, URI)
- [ ] SIP server port accessible (default: 5060 UDP)
- [ ] Firewall rules configured if needed

## Test Suite

### 1. Web Interface Access
- [ ] Open browser
- [ ] Navigate to `http://[ESP32_IP]`
- [ ] Verify page loads correctly
- [ ] Verify all sections visible
- [ ] Verify no JavaScript errors in console

### 2. System Status Display
- [ ] Verify WiFi status shows "Connected"
- [ ] Verify SIP status shows "Not Registered" initially
- [ ] Verify system information displays:
  - [ ] Uptime
  - [ ] Free heap memory
  - [ ] IP address
  - [ ] Firmware version

### 3. SIP Configuration
- [ ] Enter SIP URI (e.g., `sip:user@example.com`)
- [ ] Enter SIP Server (e.g., `sip.example.com`)
- [ ] Enter Username
- [ ] Enter Password
- [ ] Click "Save Configuration"
- [ ] Verify toast notification: "SIP configuration saved successfully!"
- [ ] Refresh page
- [ ] Verify configuration persists (fields still filled)

### 4. SIP Test Function
- [ ] Click "Test Configuration" button
- [ ] Button shows "Testing..." during test
- [ ] Verify test result appears below form
- [ ] Verify toast notification appears
- [ ] Check serial monitor for test messages

### 5. Connect to SIP Button
- [ ] Click "Connect to SIP" button
- [ ] Button shows "Connecting..." briefly
- [ ] Verify toast notification: "SIP connection initiated!"
- [ ] Verify SIP status card updates (may take 2-3 seconds)
- [ ] Check serial monitor for connection messages

### 6. SIP Logging Display - Initial State
- [ ] Verify "SIP Connection Log" section visible
- [ ] Verify dark theme console display (#1e1e1e background)
- [ ] Verify control buttons present:
  - [ ] "Refresh Log" button
  - [ ] "Clear Display" button
  - [ ] "Auto-refresh" checkbox (checked by default)

### 7. SIP Logging - Auto-Refresh
- [ ] Verify auto-refresh checkbox is checked
- [ ] Click "Connect to SIP"
- [ ] Verify log entries appear automatically
- [ ] Verify entries appear within 500ms
- [ ] Verify no manual refresh needed
- [ ] Verify auto-scroll to bottom works

### 8. SIP Logging - Log Entry Format
- [ ] Verify timestamp format: `[HH:MM:SS]`
- [ ] Verify type format: `[INFO]`, `[ERROR]`, `[SENT]`, `[RECEIVED]`
- [ ] Verify message content readable
- [ ] Verify no HTML injection (special characters escaped)

### 9. SIP Logging - Color Coding
- [ ] Verify INFO messages in cyan (#4ec9b0)
- [ ] Verify ERROR messages in red (#f48771)
- [ ] Verify SENT messages in yellow (#dcdcaa)
- [ ] Verify RECEIVED messages in blue (#9cdcfe)
- [ ] Verify timestamp in gray (#888)

### 10. SIP Logging - Manual Refresh
- [ ] Uncheck "Auto-refresh" checkbox
- [ ] Verify log stops updating automatically
- [ ] Click "Refresh Log" button
- [ ] Verify new entries appear
- [ ] Re-check "Auto-refresh" checkbox
- [ ] Verify auto-refresh resumes

### 11. SIP Logging - Clear Display
- [ ] Click "Clear Display" button
- [ ] Verify log display clears
- [ ] Verify message: "Log cleared. Waiting for new entries..."
- [ ] Verify new entries still appear after clearing
- [ ] Verify old entries not re-fetched

### 12. SIP Connection Flow - No Authentication
If your SIP server doesn't require authentication:
- [ ] Click "Connect to SIP"
- [ ] Verify log shows:
  - [ ] `[INFO] SIP connection requested`
  - [ ] `[SENT] REGISTER sip:...`
  - [ ] `[RECEIVED] SIP/2.0 200 OK`
  - [ ] `[INFO] SIP registration successful`
- [ ] Verify SIP status card shows "Registered" (green)

### 13. SIP Connection Flow - With Authentication
If your SIP server requires authentication:
- [ ] Click "Connect to SIP"
- [ ] Verify log shows:
  - [ ] `[INFO] SIP connection requested`
  - [ ] `[SENT] REGISTER sip:...` (initial)
  - [ ] `[RECEIVED] SIP/2.0 401 Unauthorized`
  - [ ] `[INFO] Authentication required`
  - [ ] `[SENT] REGISTER sip:...` (with Authorization header)
  - [ ] `[RECEIVED] SIP/2.0 200 OK`
  - [ ] `[INFO] SIP registration successful`
- [ ] Verify SIP status card shows "Registered" (green)

### 14. SIP Connection Flow - Error Cases
Test with invalid configuration:
- [ ] Enter invalid SIP server hostname
- [ ] Click "Connect to SIP"
- [ ] Verify log shows:
  - [ ] `[ERROR] Cannot resolve SIP server hostname`
  - [ ] `[ERROR] SIP registration failed`
- [ ] Verify SIP status card shows error state (red)

### 15. Disconnect from SIP
- [ ] Ensure SIP is connected (status shows "Registered")
- [ ] Click "Disconnect" button
- [ ] Button shows "Disconnecting..." briefly
- [ ] Verify toast notification: "Disconnected from SIP"
- [ ] Verify log shows:
  - [ ] `[INFO] SIP disconnection requested`
  - [ ] `[INFO] Disconnected from SIP server`
- [ ] Verify SIP status card shows "Not Registered"

### 16. Reconnect After Disconnect
- [ ] Click "Disconnect" (if connected)
- [ ] Wait 2 seconds
- [ ] Click "Connect to SIP"
- [ ] Verify connection works again
- [ ] Verify log shows new connection sequence

### 17. Multiple Connect Attempts
- [ ] Click "Connect to SIP" while already connected
- [ ] Verify log shows: `[INFO] Already registered to SIP server`
- [ ] Verify no duplicate registration attempts

### 18. Log Buffer Overflow
- [ ] Generate more than 50 log entries (connect/disconnect multiple times)
- [ ] Verify oldest entries are overwritten
- [ ] Verify no crashes or errors
- [ ] Verify log display still works correctly

### 19. Long-Running Stability
- [ ] Leave auto-refresh enabled
- [ ] Leave page open for 5 minutes
- [ ] Verify log continues to update
- [ ] Verify no memory leaks (check free heap in system info)
- [ ] Verify no browser slowdown

### 20. Page Refresh Behavior
- [ ] Refresh browser page (F5)
- [ ] Verify configuration persists
- [ ] Verify log display resets (starts fresh)
- [ ] Verify auto-refresh starts automatically
- [ ] Verify connection status loads correctly

### 21. Multiple Browser Windows
- [ ] Open web interface in two browser windows
- [ ] Click "Connect to SIP" in first window
- [ ] Verify both windows show log updates
- [ ] Verify both windows show status updates
- [ ] Verify no conflicts or errors

### 22. Mobile Responsiveness
- [ ] Open web interface on mobile device
- [ ] Verify layout adapts to small screen
- [ ] Verify all buttons accessible
- [ ] Verify log display readable
- [ ] Verify touch interactions work

### 23. Serial Monitor Verification
- [ ] Open serial monitor (`idf.py monitor`)
- [ ] Click "Connect to SIP"
- [ ] Verify ESP_LOGI messages appear:
  - [ ] `SIP: SIP connection requested`
  - [ ] `SIP: SIP task started on core 1`
  - [ ] `SIP: SIP message received:`
- [ ] Verify no errors or warnings

### 24. Network Interruption Recovery
- [ ] Connect to SIP successfully
- [ ] Disconnect WiFi temporarily
- [ ] Verify log shows network errors
- [ ] Reconnect WiFi
- [ ] Click "Connect to SIP" again
- [ ] Verify connection recovers

### 25. System Restart
- [ ] Click "Restart System" button
- [ ] Confirm restart dialog
- [ ] Wait for ESP32 to reboot (~5 seconds)
- [ ] Refresh browser page
- [ ] Verify system comes back online
- [ ] Verify configuration persists
- [ ] Verify log starts fresh

## Performance Tests

### 26. Log Refresh Performance
- [ ] Open browser developer tools (F12)
- [ ] Go to Network tab
- [ ] Enable auto-refresh
- [ ] Monitor `/api/sip/log` requests
- [ ] Verify requests every ~500ms
- [ ] Verify response time < 100ms
- [ ] Verify response size reasonable (< 5 KB typically)

### 27. Memory Usage
- [ ] Check initial free heap (system info)
- [ ] Generate 100+ log entries
- [ ] Check free heap again
- [ ] Verify no significant memory leak
- [ ] Verify free heap stable over time

### 28. CPU Usage
- [ ] Monitor serial output
- [ ] Enable auto-refresh
- [ ] Verify no watchdog timer resets
- [ ] Verify WiFi remains stable
- [ ] Verify no "Task watchdog" errors

## Edge Cases

### 29. Empty Configuration
- [ ] Clear all SIP configuration fields
- [ ] Click "Connect to SIP"
- [ ] Verify error message: "Cannot connect: SIP not configured"
- [ ] Verify log shows error

### 30. Very Long Messages
- [ ] Configure SIP with very long server name (> 200 chars)
- [ ] Click "Connect to SIP"
- [ ] Verify log truncates message with "..."
- [ ] Verify no buffer overflow
- [ ] Verify display remains readable

### 31. Special Characters
- [ ] Enter SIP password with special characters: `<>&"'`
- [ ] Save configuration
- [ ] Verify characters properly escaped in display
- [ ] Verify no HTML injection
- [ ] Verify no JavaScript errors

### 32. Rapid Button Clicks
- [ ] Rapidly click "Connect to SIP" 10 times
- [ ] Verify no crashes
- [ ] Verify button disables during operation
- [ ] Verify only one connection attempt

### 33. Browser Compatibility
Test in multiple browsers:
- [ ] Chrome/Edge (Chromium)
- [ ] Firefox
- [ ] Safari (if available)
- [ ] Mobile Chrome
- [ ] Mobile Safari

## Final Verification

### 34. Complete User Workflow
- [ ] Start with fresh ESP32 (factory reset if needed)
- [ ] Connect to WiFi via web interface
- [ ] Configure SIP settings
- [ ] Save configuration
- [ ] Test configuration
- [ ] Connect to SIP
- [ ] Monitor logs for successful registration
- [ ] Verify status shows "Registered"
- [ ] Disconnect from SIP
- [ ] Reconnect to SIP
- [ ] Verify everything works end-to-end

### 35. Documentation Verification
- [ ] Read IMPLEMENTATION_SUMMARY.md
- [ ] Read docs/SIP_LOGGING_AND_CONNECTION.md
- [ ] Read docs/SIP_LOGGING_ARCHITECTURE.md
- [ ] Read docs/WEB_INTERFACE_PREVIEW.md
- [ ] Verify documentation matches implementation
- [ ] Verify all features documented

## Test Results Summary

### Pass/Fail Criteria
- **PASS**: All critical tests pass (1-25)
- **PASS WITH NOTES**: All critical tests pass, some optional tests fail
- **FAIL**: Any critical test fails

### Critical Tests (Must Pass)
- Tests 1-15: Basic functionality
- Tests 16-20: Stability and reliability
- Test 34: Complete workflow

### Optional Tests (Nice to Have)
- Tests 21-33: Edge cases and advanced scenarios
- Test 35: Documentation

## Bug Report Template

If any test fails, document:

```
Test Number: [X]
Test Name: [Name]
Expected Result: [What should happen]
Actual Result: [What actually happened]
Steps to Reproduce:
1. [Step 1]
2. [Step 2]
3. [Step 3]

Serial Monitor Output:
[Paste relevant output]

Browser Console Errors:
[Paste any JavaScript errors]

Screenshots:
[Attach if relevant]

Environment:
- ESP32 Board: [Model]
- ESP-IDF Version: [Version]
- Browser: [Name and version]
- SIP Server: [Type and version]
```

## Sign-Off

### Tester Information
- **Name**: _______________
- **Date**: _______________
- **Environment**: _______________

### Test Results
- **Total Tests**: 35
- **Tests Passed**: _____ / 35
- **Tests Failed**: _____ / 35
- **Tests Skipped**: _____ / 35

### Overall Status
- [ ] ✅ PASS - Ready for production
- [ ] ⚠️ PASS WITH NOTES - Minor issues, ready for production
- [ ] ❌ FAIL - Critical issues, not ready for production

### Notes
```
[Add any additional notes, observations, or recommendations]
```

---

**Good luck with testing! 🚀**
