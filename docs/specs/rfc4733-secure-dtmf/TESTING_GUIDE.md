# RFC 4733 Secure DTMF - Manual Testing Guide

This guide will walk you through testing the RFC 4733 secure DTMF implementation on your ESP32 doorstation. Each test corresponds to a subtask in the implementation plan.

## Prerequisites

Before starting, ensure you have:
- [ ] ESP32 doorstation with RFC 4733 firmware flashed
- [ ] Doorstation connected to network and registered with SIP server
- [ ] Web interface accessible (check IP address)
- [ ] At least one SIP device for testing (DECT phone, IP phone, or softphone)
- [ ] Access to doorstation logs (via web interface or serial console)

## Test Environment Setup

### 1. Access Web Interface
1. Find your doorstation's IP address (check router or serial console)
2. Open browser to `http://[doorstation-ip]`
3. Navigate to DTMF Security settings

### 2. Check Current Configuration
Before testing, note the current settings:
- PIN enabled: Yes/No
- PIN code: ____
- Timeout: ____ ms
- Max attempts: ____

---

## Test 8.1: RFC 4733 Packet Parsing

**Objective**: Verify that the doorstation correctly receives and parses RFC 4733 telephone-event packets.

### What We're Testing
- Event code extraction (0-15 for DTMF digits)
- End bit detection (signals complete digit)
- Deduplication (prevents double-detection)

### Test Procedure

#### Step 1: Enable Debug Logging (Optional)
If you have serial console access:
1. Connect to ESP32 via USB
2. Open serial monitor (115200 baud)
3. Look for log messages starting with "RTP:" or "DTMF:"

#### Step 2: Make a Test Call
1. From your SIP device, call the doorstation
2. Wait for call to connect (you should hear audio)
3. Press a single DTMF digit (e.g., "1")
4. Observe the logs

#### Step 3: Verify Event Reception
**Expected behavior**:
- Log should show: "Received telephone-event: code=1"
- Log should show: "End bit detected"
- No duplicate processing messages for same digit

#### Step 4: Test Multiple Digits
1. Press digits: 1, 2, 3, 4, 5
2. Each digit should be logged separately
3. No digits should be missed or duplicated

### Success Criteria
- ✅ Each DTMF digit press generates exactly one event
- ✅ Event codes match pressed digits (0-9, *, #)
- ✅ No duplicate events for single button press
- ✅ All digits are received in correct order

### Troubleshooting
- **No events received**: Check SDP negotiation - both sides must support payload type 101
- **Duplicate events**: Check timestamp deduplication logic
- **Wrong digits**: Verify event code to character mapping

---

## Test 8.2: Command Execution with PIN

**Objective**: Verify that PIN-protected commands work correctly and are logged.

### What We're Testing
- PIN code validation
- Door opener activation
- Security audit logging

### Test Procedure

#### Step 1: Configure PIN Protection
1. Open web interface → DTMF Security
2. Enable PIN protection
3. Set PIN code (e.g., "1234")
4. Set timeout to 10000 ms
5. Set max attempts to 3
6. Click "Save"

#### Step 2: Test Valid PIN Command
1. Make a call to the doorstation
2. Wait for connection
3. Press: `*` `1` `2` `3` `4` `#`
4. Observe door opener relay

**Expected behavior**:
- Door opener relay activates (GPIO goes HIGH)
- Relay stays active for configured duration
- Command buffer clears after execution

#### Step 3: Verify Security Log
1. Open web interface → Security Logs
2. Find the most recent entry

**Expected log entry**:
```json
{
  "timestamp": [current time],
  "type": "success",
  "command": "*1234#",
  "action": "door_open",
  "caller": "sip:yourphone@domain.com"
}
```

#### Step 4: Test Invalid PIN
1. Make another call
2. Press: `*` `9` `9` `9` `9` `#`
3. Observe that door does NOT open

**Expected log entry**:
```json
{
  "timestamp": [current time],
  "type": "failed",
  "command": "*9999#",
  "reason": "invalid_pin",
  "caller": "sip:yourphone@domain.com"
}
```

#### Step 5: Test Legacy Mode
1. Disable PIN protection in web interface
2. Make a call
3. Press: `*` `1` `#`
4. Door should open (backward compatibility)

### Success Criteria
- ✅ Correct PIN opens door
- ✅ Incorrect PIN does not open door
- ✅ All attempts are logged with timestamps
- ✅ Caller ID is captured in logs
- ✅ Legacy "*1#" works when PIN disabled

### Troubleshooting
- **Door doesn't open with correct PIN**: Check PIN configuration in NVS
- **No log entries**: Verify web server endpoints are working
- **Wrong caller ID**: Check SIP headers for From/Remote-Party-ID

---

## Test 8.3: Command Timeout

**Objective**: Verify that partial commands are cleared after timeout period.

### What We're Testing
- Timeout timer starts on first digit
- Buffer clears after timeout
- New commands work after timeout

### Test Procedure

#### Step 1: Configure Timeout
1. Web interface → DTMF Security
2. Set timeout to 10000 ms (10 seconds)
3. Save configuration

#### Step 2: Send Partial Command
1. Make a call to doorstation
2. Press: `*` `1` `2`
3. **STOP** - Do not press any more digits
4. Wait 11 seconds (longer than timeout)

**Expected behavior**:
- Log shows: "Command timeout - buffer cleared"
- No command execution
- No failed attempt logged (timeout is not a failure)

#### Step 3: Send Complete Command After Timeout
1. Still in same call
2. Press: `*` `1` `2` `3` `4` `#` (complete valid command)
3. Door should open normally

**Expected behavior**:
- Command executes successfully
- Previous partial command did not interfere

#### Step 4: Test Timeout with Different Durations
1. Set timeout to 5000 ms (5 seconds)
2. Repeat test with partial command
3. Verify timeout occurs after 5 seconds

### Success Criteria
- ✅ Partial commands clear after timeout
- ✅ Timeout does not increment failed attempts
- ✅ New commands work after timeout
- ✅ Timeout is logged for debugging
- ✅ Configurable timeout values work (5-30 seconds)

### Troubleshooting
- **Timeout too fast/slow**: Check system time and timer implementation
- **Buffer not clearing**: Verify timeout handler calls buffer clear function
- **Commands after timeout fail**: Check state reset logic

---

## Test 8.4: Rate Limiting

**Objective**: Verify that brute-force attacks are prevented by rate limiting.

### What We're Testing
- Failed attempt counter
- Rate limit trigger after max attempts
- Valid commands blocked when rate limited
- Reset on new call

### Test Procedure

#### Step 1: Configure Rate Limiting
1. Web interface → DTMF Security
2. Enable PIN protection with PIN "1234"
3. Set max attempts to 3
4. Save configuration

#### Step 2: Trigger Rate Limit
1. Make a call to doorstation
2. Send invalid command #1: `*` `0` `0` `0` `0` `#`
3. Wait 1 second
4. Send invalid command #2: `*` `1` `1` `1` `1` `#`
5. Wait 1 second
6. Send invalid command #3: `*` `2` `2` `2` `2` `#`

**Expected behavior**:
- Each invalid command logs a failed attempt
- After 3rd attempt, log shows: "Rate limit triggered - blocking further commands"
- Security alert logged

#### Step 3: Attempt Valid Command While Rate Limited
1. Still in same call
2. Send correct PIN: `*` `1` `2` `3` `4` `#`

**Expected behavior**:
- Door does NOT open
- Log shows: "Command blocked - rate limited"
- No command execution

#### Step 4: Verify Rate Limit Reset on New Call
1. Hang up the call
2. Wait 5 seconds
3. Make a new call
4. Send correct PIN: `*` `1` `2` `3` `4` `#`

**Expected behavior**:
- Door DOES open
- Rate limit counter reset to 0
- Normal operation resumed

#### Step 5: Check Security Logs
Review logs for the rate limit event:
```json
{
  "timestamp": [time],
  "type": "security_alert",
  "reason": "rate_limit_exceeded",
  "failed_attempts": 3,
  "caller": "sip:yourphone@domain.com"
}
```

### Success Criteria
- ✅ Failed attempts are counted correctly
- ✅ Rate limit triggers after max attempts
- ✅ Valid commands blocked when rate limited
- ✅ Security alert logged with details
- ✅ Rate limit resets on new call
- ✅ Configurable max attempts works

### Troubleshooting
- **Rate limit not triggering**: Check failed attempt counter increment
- **Rate limit persists across calls**: Verify call state reset function
- **Wrong attempt count**: Check counter logic and reset conditions

---

## Test 8.5: Audio DTMF Removal

**Objective**: Verify that audio DTMF tones do NOT trigger commands (security fix).

### What We're Testing
- Audio DTMF processing removed
- Replay attack prevention
- Audio transmission still works

### Test Procedure

#### Step 1: Test with Physical DTMF Tones
1. Make a call to doorstation
2. Play DTMF tones at the doorstation's microphone using:
   - Another phone held near the microphone
   - DTMF tone generator app
   - Pre-recorded DTMF audio
3. Play the sequence: `*` `1` `2` `3` `4` `#`

**Expected behavior**:
- Door does NOT open
- No command execution
- No security log entries created
- Audio is transmitted normally to caller

#### Step 2: Verify Audio Transmission
1. During same call, speak into doorstation microphone
2. Verify caller can hear your voice clearly
3. Verify no audio quality degradation

#### Step 3: Test Replay Attack Prevention
1. Record audio of someone pressing DTMF on their phone
2. Play recording at doorstation microphone
3. Verify no command execution

**Expected behavior**:
- Recorded DTMF tones do NOT trigger commands
- Only RFC 4733 telephone-events work
- Security vulnerability is fixed

#### Step 4: Verify RFC 4733 Still Works
1. In same call, press DTMF on your phone keypad
2. Send: `*` `1` `2` `3` `4` `#`
3. Door should open normally

**Expected behavior**:
- RFC 4733 events work
- Audio DTMF does not work
- Clear distinction between the two

### Success Criteria
- ✅ Audio DTMF tones do NOT trigger commands
- ✅ Replay attacks are prevented
- ✅ Audio transmission quality unchanged
- ✅ RFC 4733 telephone-events still work
- ✅ No security log entries for audio DTMF

### Troubleshooting
- **Audio DTMF still works**: Check that dtmf_process_audio() calls are removed
- **No audio transmission**: Verify audio path not broken
- **RFC 4733 not working**: Check SDP negotiation and payload type routing

---

## Test 8.6: Real SIP Device Compatibility

**Objective**: Verify RFC 4733 works with various SIP devices.

### What We're Testing
- SDP negotiation with different devices
- RFC 4733 support detection
- Compatibility with common SIP phones

### Test Devices

#### Device 1: DECT Phone (Gigaset or Yealink)
1. Register DECT handset to base station
2. Call doorstation from DECT phone
3. Test DTMF command: `*` `1` `2` `3` `4` `#`

**Expected behavior**:
- Call connects successfully
- DTMF digits transmitted as RFC 4733
- Door opens on valid command

**Check SDP**:
- Look for "a=rtpmap:101 telephone-event/8000"
- Look for "a=fmtp:101 0-15"

#### Device 2: IP Phone (Yealink, Cisco, etc.)
1. Register IP phone to SIP server
2. Call doorstation
3. Test DTMF command

**Expected behavior**:
- Same as DECT phone
- RFC 4733 negotiated in SDP
- Commands work correctly

#### Device 3: Softphone (Linphone, Zoiper, etc.)
1. Install softphone on computer/mobile
2. Configure SIP account
3. Call doorstation
4. Test DTMF command

**Expected behavior**:
- Call connects
- DTMF works via RFC 4733
- Check softphone settings for "RFC 2833" or "RTP DTMF"

#### Device 4: Mobile SIP App
1. Install SIP app (Linphone, Zoiper, etc.)
2. Configure account
3. Test from mobile device

**Expected behavior**:
- Mobile apps typically support RFC 4733
- Commands work correctly
- Test both WiFi and cellular connections

### SDP Verification

For each device, capture SDP exchange and verify:

**Doorstation INVITE SDP**:
```
m=audio [port] RTP/AVP 0 8 101
a=rtpmap:0 PCMU/8000
a=rtpmap:8 PCMA/8000
a=rtpmap:101 telephone-event/8000
a=fmtp:101 0-15
```

**Doorstation 200 OK SDP** (for incoming calls):
```
m=audio [port] RTP/AVP 0 8 101
a=rtpmap:0 PCMU/8000
a=rtpmap:8 PCMA/8000
a=rtpmap:101 telephone-event/8000
a=fmtp:101 0-15
```

### Success Criteria
- ✅ All tested devices support RFC 4733
- ✅ SDP negotiation successful with each device
- ✅ DTMF commands work from all devices
- ✅ No compatibility issues found
- ✅ Both incoming and outgoing calls work

### Troubleshooting
- **Device doesn't support RFC 4733**: Check device settings for "RFC 2833" or "RTP DTMF"
- **SDP missing payload 101**: Verify doorstation SDP generation
- **DTMF not working**: Check device DTMF mode settings (should be RFC 2833/4733, not SIP INFO or inband)

---

## Additional Verification Tests

### Test: Light Toggle Command
1. Make a call
2. Press: `*` `2` `#`
3. Verify light relay toggles

**Expected**: Light command works without PIN (as per design)

### Test: NTP Timestamp Logging
1. Ensure NTP is synchronized (check web interface)
2. Execute a command
3. Check security log timestamp

**Expected**: Timestamp should be accurate NTP time, not uptime

### Test: Configuration Persistence
1. Configure PIN and settings
2. Reboot doorstation
3. Check settings after reboot

**Expected**: Settings persist in NVS

### Test: Concurrent Call Handling
1. Make a call, send partial command
2. Hang up immediately
3. Make new call quickly
4. Send complete command

**Expected**: State properly reset between calls

---

## Test Results Template

Use this template to document your test results:

```markdown
## Test Results - [Date]

### Test 8.1: RFC 4733 Packet Parsing
- Status: ✅ PASS / ❌ FAIL
- Notes: 
- Issues found:

### Test 8.2: Command Execution with PIN
- Status: ✅ PASS / ❌ FAIL
- Notes:
- Issues found:

### Test 8.3: Command Timeout
- Status: ✅ PASS / ❌ FAIL
- Notes:
- Issues found:

### Test 8.4: Rate Limiting
- Status: ✅ PASS / ❌ FAIL
- Notes:
- Issues found:

### Test 8.5: Audio DTMF Removal
- Status: ✅ PASS / ❌ FAIL
- Notes:
- Issues found:

### Test 8.6: Device Compatibility
- DECT Phone: ✅ PASS / ❌ FAIL
- IP Phone: ✅ PASS / ❌ FAIL
- Softphone: ✅ PASS / ❌ FAIL
- Mobile App: ✅ PASS / ❌ FAIL
- Notes:
- Issues found:

### Overall Assessment
- All tests passed: YES / NO
- Critical issues: 
- Minor issues:
- Recommendations:
```

---

## Quick Reference: Common Commands

### Door Opener Commands
- With PIN: `*[PIN]#` (e.g., `*1234#`)
- Legacy (no PIN): `*1#`

### Light Toggle
- `*2#` (no PIN required)

### Web Interface URLs
- Security Config: `http://[ip]/api/dtmf/security`
- Security Logs: `http://[ip]/api/dtmf/logs`

---

## Need Help?

If you encounter issues during testing:
1. Check serial console logs for detailed error messages
2. Verify SIP registration status
3. Check network connectivity
4. Review SDP negotiation in SIP traces
5. Consult SECURITY.md for additional information

Let me know which test you'd like to start with, and I'll guide you through it step by step!
```
