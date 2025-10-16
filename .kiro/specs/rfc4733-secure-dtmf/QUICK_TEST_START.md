# Quick Start - RFC 4733 Testing

Get started testing in 5 minutes!

## Step 1: Verify Doorstation is Ready (2 min)

1. **Power on** the ESP32 doorstation
2. **Connect to serial console** (optional but helpful):
   ```bash
   # Windows
   # Use PuTTY or similar at 115200 baud
   
   # Linux/Mac
   screen /dev/ttyUSB0 115200
   ```
3. **Find IP address**:
   - Check serial console output, or
   - Check your router's DHCP client list, or
   - Use network scanner

4. **Open web interface**: `http://[doorstation-ip]`

## Step 2: Check SIP Registration (1 min)

1. In web interface, go to **SIP Status**
2. Verify status shows: **"Registered"** or **"Connected"**
3. If not registered:
   - Click **"Connect"**
   - Wait 10 seconds
   - Refresh status

## Step 3: Configure DTMF Security (1 min)

1. In web interface, go to **DTMF Security** section
2. Set initial configuration:
   - **PIN Enabled**: ✅ Yes
   - **PIN Code**: `1234`
   - **Timeout**: `10000` ms
   - **Max Attempts**: `3`
3. Click **"Save"**

## Step 4: Make Your First Test Call (1 min)

1. **From your phone**, call the doorstation SIP address
2. **Wait for connection** (you should hear audio)
3. **Press DTMF digits**: `*` `1` `2` `3` `4` `#`
4. **Listen/Watch**: Door opener relay should activate

### Expected Results:
- ✅ Call connects successfully
- ✅ You hear audio from doorstation
- ✅ Door opener activates after pressing `*1234#`
- ✅ Log entry appears in web interface

### If it doesn't work:
- Check that your phone supports RFC 4733 (most modern phones do)
- Verify SIP registration is active
- Check serial console for error messages
- Try the legacy command `*1#` with PIN disabled

---

## Quick Test Scenarios

### Test 1: Valid PIN (30 seconds)
```
Call → Press: * 1 2 3 4 # → Door opens ✅
```

### Test 2: Invalid PIN (30 seconds)
```
Call → Press: * 9 9 9 9 # → Door stays closed ✅
```

### Test 3: Timeout (15 seconds)
```
Call → Press: * 1 2 → Wait 11 seconds → Press: * 1 2 3 4 # → Door opens ✅
```

### Test 4: Rate Limiting (2 minutes)
```
Call → Press: * 0 0 0 0 # (fail 1)
     → Press: * 1 1 1 1 # (fail 2)
     → Press: * 2 2 2 2 # (fail 3, rate limited)
     → Press: * 1 2 3 4 # (blocked, door stays closed ✅)
Hang up → Call again → Press: * 1 2 3 4 # → Door opens ✅
```

### Test 5: Audio DTMF (Security Test) (1 minute)
```
Call → Play DTMF tones at doorstation microphone → Door stays closed ✅
     → Press DTMF on phone keypad: * 1 2 3 4 # → Door opens ✅
```

---

## Checking Logs

### Via Web Interface:
1. Open `http://[doorstation-ip]`
2. Navigate to **Security Logs** section
3. View recent entries

### Via Serial Console:
Look for messages like:
```
DTMF: Received telephone-event: code=1
DTMF: Command executed: *1234#
DTMF: Security log: success, door_open
```

### Via API:
```bash
# Get all logs
curl http://[doorstation-ip]/api/dtmf/logs

# Get logs since timestamp
curl "http://[doorstation-ip]/api/dtmf/logs?since=1697462400000"
```

---

## Common Issues & Quick Fixes

### Issue: No DTMF events received
**Fix**: Check phone DTMF settings
- Look for "RFC 2833" or "RTP DTMF" option
- Disable "SIP INFO" DTMF mode
- Disable "Inband" DTMF mode

### Issue: Door doesn't open with correct PIN
**Fix**: Verify configuration
```bash
# Check config via API
curl http://[doorstation-ip]/api/dtmf/security
```

### Issue: Can't connect to web interface
**Fix**: Find correct IP address
- Check serial console: `I (12345) WIFI: Got IP: 192.168.1.xxx`
- Check router DHCP leases
- Try: `ping esp32-doorstation.local` (if mDNS enabled)

### Issue: Call doesn't connect
**Fix**: Check SIP registration
1. Web interface → SIP Status
2. If not registered, click "Connect"
3. Check SIP server settings
4. Verify network connectivity

---

## Next Steps

Once basic testing works:

1. **Test with different devices**:
   - DECT phone
   - IP phone
   - Softphone (Linphone, Zoiper)
   - Mobile SIP app

2. **Test edge cases**:
   - Very fast DTMF input
   - Very slow DTMF input
   - Hanging up mid-command
   - Multiple rapid calls

3. **Security testing**:
   - Replay attack prevention
   - Rate limiting effectiveness
   - Log accuracy

4. **Configuration testing**:
   - Different PIN lengths (1-8 digits)
   - Different timeout values (5-30 seconds)
   - Different max attempts (1-10)

---

## API Quick Reference

### Get Security Config
```bash
curl http://[ip]/api/dtmf/security
```

### Update Security Config
```bash
curl -X POST http://[ip]/api/dtmf/security \
  -H "Content-Type: application/json" \
  -d '{
    "pin_enabled": true,
    "pin_code": "5678",
    "timeout_ms": 15000,
    "max_attempts": 5
  }'
```

### Get Security Logs
```bash
curl http://[ip]/api/dtmf/logs
```

### Get SIP Status
```bash
curl http://[ip]/api/sip/status
```

---

## Ready to Test?

1. ✅ Doorstation powered and connected
2. ✅ Web interface accessible
3. ✅ SIP registered
4. ✅ DTMF security configured
5. ✅ Test phone ready

**Let's start with Test 1: Valid PIN!**

Call the doorstation and press: `*` `1` `2` `3` `4` `#`

Good luck! 🚀
