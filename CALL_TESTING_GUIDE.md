# SIP Call Testing Guide

## Quick Start

### Prerequisites
1. ESP32 with firmware flashed
2. WiFi connection configured
3. SIP account credentials
4. Target phone number/SIP URI

### Configuration Steps

1. **Access Web Interface**
   - Connect to ESP32's WiFi network or use local IP
   - Open browser: `http://192.168.x.x`

2. **Configure SIP Settings**
   - Navigate to SIP configuration section
   - Enter:
     - **Server**: Your SIP server (e.g., `sip.example.com`)
     - **Port**: Usually `5060`
     - **Username**: Your SIP username
     - **Password**: Your SIP password
     - **Apartment 1 URI**: Target SIP address (e.g., `sip:1001@example.com`)
     - **Apartment 2 URI**: Alternative target (optional)
   - Click "Save Configuration"

3. **Connect to SIP Server**
   - Click "Connect" button
   - Wait for "Registered" status
   - Check SIP logs for registration confirmation

### Making Test Calls

#### Method 1: Web Interface
1. Ensure SIP status shows "Registered"
2. Click "Test Call to Apartment 1" button
3. Monitor SIP logs for call progress:
   - "INVITE message sent"
   - "Server processing request (100 Trying)"
   - "Call ringing (180 Ringing)"
   - "Call accepted (200 OK)"
   - "ACK sent"
   - "RTP session started"
   - "Call connected"

#### Method 2: Physical Button
1. Press the doorbell button
2. System automatically calls Apartment 1
3. Same call flow as web interface

### Monitoring Call Progress

#### SIP Logs
Watch for these messages in sequence:
```
[info] Initiating call to sip:1001@example.com
[sent] INVITE message sent
[info] Server processing request (100 Trying)
[info] Call ringing (180 Ringing)
[info] Call accepted (200 OK)
[sent] ACK sent
[info] RTP session started
[info] Call connected - State: CONNECTED
```

#### State Transitions
```
REGISTERED → CALLING → RINGING → CONNECTED
```

### Audio Testing

#### With Audio Hardware
If you have I2S microphone and speaker connected:
1. Make a call
2. Speak into microphone
3. Listen for audio on remote phone
4. Remote party speaks
5. Hear audio from ESP32 speaker

#### Without Audio Hardware (Dummy Mode)
System works in simulation mode:
- Call flow is fully functional
- RTP packets are sent/received
- Audio functions return success
- No actual audio is captured/played

### Ending Calls

#### From ESP32
1. Click "Hang Up" button in web interface
2. System sends BYE message
3. RTP session stops
4. State returns to REGISTERED

#### From Remote Phone
1. Remote party hangs up
2. ESP32 receives BYE request
3. Sends 200 OK response
4. RTP session stops
5. State returns to REGISTERED

## Troubleshooting

### Call Not Connecting

**Symptom**: INVITE sent but no response
- **Check**: SIP server is reachable
- **Check**: Credentials are correct
- **Check**: Target URI is valid
- **Action**: Review SIP logs for error codes

**Symptom**: 403 Forbidden
- **Cause**: Authentication failed
- **Action**: Verify username/password
- **Action**: Check if account is active

**Symptom**: 404 Not Found
- **Cause**: Target URI doesn't exist
- **Action**: Verify target SIP address
- **Action**: Check for typos in URI

**Symptom**: 486 Busy Here
- **Cause**: Target is on another call
- **Action**: Try again later
- **Action**: Call alternative target

### No Audio

**Symptom**: Call connects but no audio
- **Check**: RTP session started (check logs)
- **Check**: Port 5004 is not blocked
- **Check**: Audio hardware is connected
- **Action**: Review RTP logs

**Symptom**: One-way audio
- **Check**: Firewall/NAT settings
- **Check**: RTP receive function
- **Action**: Test with different network

### Call Drops Immediately

**Symptom**: Call connects then drops
- **Check**: Network stability
- **Check**: SIP keep-alive
- **Action**: Monitor for BYE messages
- **Action**: Check socket errors

## Expected Behavior

### Successful Call Flow

1. **Registration** (automatic on startup)
   ```
   [info] SIP client ready
   [info] Auto-registration triggered
   [sent] REGISTER message sent
   [info] Authentication required
   [sent] Authenticated REGISTER sent
   [info] SIP registration successful
   ```

2. **Outgoing Call**
   ```
   [info] Initiating call to sip:1001@example.com
   [sent] INVITE message sent
   [info] Server processing request (100 Trying)
   [info] Call ringing (180 Ringing)
   [info] Call accepted (200 OK)
   [sent] ACK sent
   [info] RTP session started
   [info] Call connected - State: CONNECTED
   ```

3. **Active Call**
   ```
   [info] Audio recording started
   [info] Audio playback started
   [RTP packets being sent/received]
   ```

4. **Call Termination**
   ```
   [info] Sending BYE to end call
   [sent] BYE message sent
   [info] Call ended - State: REGISTERED
   [info] RTP session stopped
   [info] Audio recording stopped
   [info] Audio playback stopped
   ```

### Error Scenarios

#### Target Busy
```
[info] Initiating call
[sent] INVITE message sent
[info] Server processing request (100 Trying)
[info] SIP target busy
[info] State: REGISTERED
```

#### Target Not Found
```
[info] Initiating call
[sent] INVITE message sent
[info] Server processing request (100 Trying)
[error] SIP target not found
[info] State: REGISTERED
```

#### Network Error
```
[info] Initiating call
[error] DNS lookup failed for call
[error] State: ERROR
```

## Performance Metrics

### Normal Operation
- **Call Setup Time**: 2-5 seconds
- **Audio Latency**: 20-100ms
- **CPU Usage**: Low (< 10%)
- **Memory Usage**: ~50KB for call
- **Network Bandwidth**: ~64 kbps

### Resource Usage
- **SIP Messages**: ~1.5 KB each
- **RTP Packets**: 172 bytes @ 50 pps
- **Audio Buffers**: 320 bytes per frame

## Advanced Testing

### Multiple Calls
1. Make first call
2. Hang up
3. Make second call immediately
4. Verify state transitions correctly

### Long Duration Calls
1. Make call
2. Let it run for 5+ minutes
3. Verify audio quality remains stable
4. Check for memory leaks

### Network Interruption
1. Make call
2. Disconnect WiFi briefly
3. Reconnect
4. Verify call recovery or proper error handling

### Rapid Button Presses
1. Press button multiple times quickly
2. Verify only one call is initiated
3. Check debouncing works correctly

## Debug Commands

### View SIP Status
```
GET /api/sip/status
```
Returns current state, configuration, and connection status

### View SIP Logs
```
GET /api/sip/logs?since=0
```
Returns all SIP log entries

### Test Configuration
```
POST /api/sip/test
```
Tests SIP configuration without making a call

## Common Issues

### Issue: "Cannot make call - not in ready state"
- **Cause**: Not registered to SIP server
- **Solution**: Wait for registration or click "Connect"

### Issue: "Cannot make call - SIP not configured"
- **Cause**: No SIP credentials saved
- **Solution**: Configure SIP settings first

### Issue: "DNS lookup failed"
- **Cause**: Cannot resolve SIP server hostname
- **Solution**: Check WiFi connection and DNS settings

### Issue: "Failed to send INVITE"
- **Cause**: Socket error
- **Solution**: Check network connection, restart ESP32

## Success Indicators

✅ **Registration Successful**
- Status shows "Registered"
- Green indicator in web interface
- No error messages in logs

✅ **Call Connected**
- State shows "CONNECTED"
- RTP session active
- Audio recording/playback started

✅ **Audio Working**
- RTP packets being sent (check logs)
- RTP packets being received
- No socket errors

## Next Steps

After successful testing:
1. Configure both apartment targets
2. Test physical button integration
3. Adjust audio levels if needed
4. Set up automatic call on button press
5. Monitor long-term stability

## Support

For issues not covered here:
1. Check `SIP_CALL_IMPLEMENTATION.md` for technical details
2. Review `docs/SIP_SETUP.md` for configuration help
3. Examine serial console logs for detailed debugging
4. Check ESP32 system logs for crashes/errors

## Related Documentation
- `SIP_CALL_IMPLEMENTATION.md` - Technical implementation details
- `docs/SIP_SETUP.md` - SIP configuration guide
- `docs/SIP_CALL_TODO.md` - Implementation status
- `docs/BOOT_BUTTON_TESTING.md` - Button testing guide
- `TESTING_CHECKLIST.md` - Complete testing checklist
