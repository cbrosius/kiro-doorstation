# RFC 4733 Testing Checklist

Quick reference checklist for manual testing. Check off items as you complete them.

## Pre-Test Setup
- [ ] Doorstation powered on and connected to network
- [ ] Web interface accessible at: `http://_______________`
- [ ] SIP registration successful
- [ ] Test phone available and registered
- [ ] Serial console connected (optional but recommended)

## Test 8.1: RFC 4733 Packet Parsing
- [ ] Made test call successfully
- [ ] Pressed single DTMF digit
- [ ] Verified event logged (no duplicates)
- [ ] Tested multiple digits (1,2,3,4,5)
- [ ] All digits received in correct order
- [ ] **Result**: ✅ PASS / ❌ FAIL

**Issues**: _______________________________________________

---

## Test 8.2: Command Execution with PIN
- [ ] Configured PIN in web interface (PIN: ________)
- [ ] Tested valid PIN command: `*[PIN]#`
- [ ] Door opener activated
- [ ] Security log entry created (success)
- [ ] Tested invalid PIN: `*9999#`
- [ ] Door did NOT open
- [ ] Security log entry created (failed)
- [ ] Disabled PIN and tested legacy `*1#`
- [ ] Legacy command worked
- [ ] **Result**: ✅ PASS / ❌ FAIL

**Issues**: _______________________________________________

---

## Test 8.3: Command Timeout
- [ ] Set timeout to 10 seconds
- [ ] Sent partial command: `*12`
- [ ] Waited 11 seconds
- [ ] Buffer cleared (logged)
- [ ] Sent complete command after timeout
- [ ] Command executed successfully
- [ ] Tested with 5 second timeout
- [ ] Timeout occurred correctly
- [ ] **Result**: ✅ PASS / ❌ FAIL

**Issues**: _______________________________________________

---

## Test 8.4: Rate Limiting
- [ ] Set max attempts to 3
- [ ] Sent 1st invalid command: `*0000#`
- [ ] Sent 2nd invalid command: `*1111#`
- [ ] Sent 3rd invalid command: `*2222#`
- [ ] Rate limit triggered (logged)
- [ ] Sent valid PIN while rate limited
- [ ] Door did NOT open (blocked)
- [ ] Hung up call
- [ ] Made new call
- [ ] Sent valid PIN
- [ ] Door opened (rate limit reset)
- [ ] Security alert in logs
- [ ] **Result**: ✅ PASS / ❌ FAIL

**Issues**: _______________________________________________

---

## Test 8.5: Audio DTMF Removal
- [ ] Made call to doorstation
- [ ] Played DTMF tones at microphone
- [ ] Door did NOT open
- [ ] No security log entries
- [ ] Verified audio transmission works
- [ ] Tested replay attack (recorded DTMF)
- [ ] Replay did NOT trigger command
- [ ] Pressed DTMF on phone keypad (RFC 4733)
- [ ] Door opened normally
- [ ] **Result**: ✅ PASS / ❌ FAIL

**Issues**: _______________________________________________

---

## Test 8.6: Device Compatibility

### DECT Phone
- [ ] Device: _________________ (model)
- [ ] Call connected
- [ ] DTMF command worked
- [ ] SDP includes payload 101
- [ ] **Result**: ✅ PASS / ❌ FAIL / ⊘ N/A

### IP Phone
- [ ] Device: _________________ (model)
- [ ] Call connected
- [ ] DTMF command worked
- [ ] SDP includes payload 101
- [ ] **Result**: ✅ PASS / ❌ FAIL / ⊘ N/A

### Softphone
- [ ] Software: _________________ (name)
- [ ] Call connected
- [ ] DTMF command worked
- [ ] SDP includes payload 101
- [ ] **Result**: ✅ PASS / ❌ FAIL / ⊘ N/A

### Mobile App
- [ ] App: _________________ (name)
- [ ] Call connected
- [ ] DTMF command worked
- [ ] SDP includes payload 101
- [ ] **Result**: ✅ PASS / ❌ FAIL / ⊘ N/A

**Issues**: _______________________________________________

---

## Additional Verification

### Light Toggle Command
- [ ] Tested `*2#` command
- [ ] Light relay toggled
- [ ] Works without PIN
- [ ] **Result**: ✅ PASS / ❌ FAIL

### NTP Timestamps
- [ ] NTP synchronized
- [ ] Log timestamps accurate
- [ ] **Result**: ✅ PASS / ❌ FAIL

### Configuration Persistence
- [ ] Configured settings
- [ ] Rebooted doorstation
- [ ] Settings persisted
- [ ] **Result**: ✅ PASS / ❌ FAIL

### Call State Reset
- [ ] Sent partial command
- [ ] Hung up immediately
- [ ] Made new call
- [ ] Sent complete command
- [ ] Command worked (state reset)
- [ ] **Result**: ✅ PASS / ❌ FAIL

---

## Final Summary

**Date**: _______________
**Tester**: _______________
**Firmware Version**: _______________

**Overall Result**: ✅ ALL PASS / ⚠️ MINOR ISSUES / ❌ CRITICAL ISSUES

**Critical Issues Found**:
1. _______________________________________________
2. _______________________________________________
3. _______________________________________________

**Minor Issues Found**:
1. _______________________________________________
2. _______________________________________________
3. _______________________________________________

**Recommendations**:
_______________________________________________
_______________________________________________
_______________________________________________

**Sign-off**: _______________________________________________
