# Incoming Call Fix

## Problem

Incoming calls were not working properly. The system would detect the INVITE but wouldn't properly respond, causing the call to fail.

## Root Cause

The original `sip_client_answer_call()` function only changed the internal state and started audio, but **did not send a SIP 200 OK response** to the INVITE request. Without this response, the calling party doesn't know the call was answered.

### What Was Missing

1. **No 200 OK Response**: The SIP protocol requires a 200 OK response to INVITE
2. **No SDP in Response**: The response must include SDP describing audio capabilities
3. **No Header Extraction**: Need to extract Call-ID, From, To, Via, CSeq from INVITE
4. **No RTP Session**: RTP session wasn't being started for incoming calls

## Solution

### 1. Proper INVITE Handling ✅

When an INVITE is received, the system now:

1. **Extracts SIP Headers**:
   - Call-ID
   - From
   - To
   - Via
   - CSeq

2. **Builds 200 OK Response**:
   - Includes all required headers
   - Adds tag to To header
   - Includes SDP with audio capabilities

3. **Sends Response**:
   - Sends 200 OK via UDP socket
   - Logs the action

4. **Starts RTP Session**:
   - Establishes RTP on port 5004
   - Starts audio recording/playback

5. **Updates State**:
   - Changes to CONNECTED state
   - Clears call timeout

### 2. Implementation Details

#### Header Extraction
```c
// Extract Call-ID
char* callid_start = strstr(buffer, "Call-ID:");
if (callid_start) {
    callid_start += 8;
    while (*callid_start == ' ') callid_start++;
    char* callid_end = strstr(callid_start, "\r\n");
    if (callid_end) {
        int len = callid_end - callid_start;
        strncpy(call_id, callid_start, len);
        call_id[len] = '\0';
    }
}
```

Similar extraction for From, To, Via, and CSeq headers.

#### 200 OK Response
```c
snprintf(response, sizeof(response),
         "SIP/2.0 200 OK\r\n"
         "Via: %s\r\n"
         "From: %s\r\n"
         "To: %s;tag=%d\r\n"  // Add tag if not present
         "Call-ID: %s\r\n"
         "CSeq: %d INVITE\r\n"
         "Contact: <sip:%s@%s:5060>\r\n"
         "Content-Type: application/sdp\r\n"
         "Content-Length: %d\r\n\r\n%s",
         via_header, from_header, to_header, rand(),
         call_id, cseq_num,
         sip_config.username, local_ip,
         strlen(sdp), sdp);
```

#### SDP Content
```
v=0
o=- [session_id] 0 IN IP4 [local_ip]
s=ESP32 Doorbell
c=IN IP4 [local_ip]
t=0 0
m=audio 5004 RTP/AVP 0 8 101
a=rtpmap:0 PCMU/8000
a=rtpmap:8 PCMA/8000
a=rtpmap:101 telephone-event/8000
a=sendrecv
```

## Call Flow

### Before (Broken)
```
Caller                    ESP32 Doorbell
  |                              |
  |--- INVITE -----------------> |
  |                              | (Detects INVITE)
  |                              | (Changes state to RINGING)
  |                              | (Starts audio)
  |                              | ❌ No 200 OK sent
  |                              |
  |<-- (timeout) ----------------|
  |                              |
  Call fails ❌
```

### After (Fixed)
```
Caller                    ESP32 Doorbell
  |                              |
  |--- INVITE -----------------> |
  |                              | (Extracts headers)
  |                              | (Builds 200 OK with SDP)
  |<-- 200 OK ------------------|
  |                              |
  |--- ACK --------------------> |
  |                              | (Starts RTP session)
  |                              | (Starts audio)
  |                              |
  |<====== RTP Audio ==========> |
  |                              |
  Call connected ✅
```

## Testing

### Test Incoming Call

1. **Register ESP32**:
   - Configure SIP settings
   - Connect to SIP server
   - Verify REGISTERED state

2. **Make Call to ESP32**:
   - From another phone, call the ESP32's SIP address
   - Example: `sip:doorbell@192.168.60.90`

3. **Expected Behavior**:
   ```
   [received] INVITE message
   [info] Incoming call detected
   [sent] 200 OK response to INVITE
   [info] RTP session started
   [info] Incoming call answered - State: CONNECTED
   [info] Audio recording started
   [info] Audio playback started
   ```

4. **Verify**:
   - ESP32 state shows CONNECTED (green)
   - Audio streams bidirectionally
   - Call can be ended from either side

### Test Call Termination

1. **End from Caller**:
   - Caller hangs up
   - ESP32 receives BYE
   - ESP32 sends 200 OK to BYE
   - State returns to REGISTERED

2. **End from ESP32**:
   - Click "Hang Up" button
   - ESP32 sends BYE
   - State returns to REGISTERED

## Automatic Answer

The system automatically answers incoming calls immediately. This is appropriate for a doorbell system where:
- Calls should be answered automatically
- No user interaction needed
- Immediate two-way communication

If manual answer is desired in the future, the code can be modified to:
1. Send 180 Ringing instead of 200 OK
2. Wait for user to click "Answer" button
3. Then send 200 OK

## SIP Message Examples

### Incoming INVITE
```
INVITE sip:doorbell@192.168.60.90 SIP/2.0
Via: SIP/2.0/UDP 192.168.60.100:5060;branch=z9hG4bKabcd1234
From: <sip:caller@192.168.60.90>;tag=12345
To: <sip:doorbell@192.168.60.90>
Call-ID: unique-call-id@192.168.60.100
CSeq: 1 INVITE
Contact: <sip:caller@192.168.60.100:5060>
Content-Type: application/sdp
Content-Length: 200

v=0
o=- 123456 0 IN IP4 192.168.60.100
s=Call
c=IN IP4 192.168.60.100
t=0 0
m=audio 5004 RTP/AVP 0
a=rtpmap:0 PCMU/8000
```

### ESP32 Response (200 OK)
```
SIP/2.0 200 OK
Via: SIP/2.0/UDP 192.168.60.100:5060;branch=z9hG4bKabcd1234
From: <sip:caller@192.168.60.90>;tag=12345
To: <sip:doorbell@192.168.60.90>;tag=67890
Call-ID: unique-call-id@192.168.60.100
CSeq: 1 INVITE
Contact: <sip:doorbell@192.168.60.90:5060>
Content-Type: application/sdp
Content-Length: 180

v=0
o=- 789012 0 IN IP4 192.168.60.90
s=ESP32 Doorbell
c=IN IP4 192.168.60.90
t=0 0
m=audio 5004 RTP/AVP 0 8 101
a=rtpmap:0 PCMU/8000
a=rtpmap:8 PCMA/8000
a=rtpmap:101 telephone-event/8000
a=sendrecv
```

## Known Limitations

### 1. Simplified Header Handling
- Assumes single Via header
- Doesn't handle multiple Contact headers
- Basic header parsing

### 2. Fixed RTP Port
- Always uses port 5004
- Doesn't parse SDP from INVITE to get caller's RTP port
- Works for most scenarios but not ideal

### 3. No 180 Ringing
- Immediately sends 200 OK
- Doesn't send provisional responses
- No "ringing" indication to caller

### 4. No Call Waiting
- Can only handle one call at a time
- New INVITE during active call is ignored
- No call hold/transfer

## Future Enhancements

### 1. Parse Caller's SDP
Extract RTP port from incoming INVITE:
```c
char* m_line = strstr(sdp_start, "m=audio ");
if (m_line) {
    m_line += 8;
    remote_rtp_port = atoi(m_line);
}
```

### 2. Send 180 Ringing
Before 200 OK, send provisional response:
```c
snprintf(response, sizeof(response),
         "SIP/2.0 180 Ringing\r\n"
         "Via: %s\r\n"
         "From: %s\r\n"
         "To: %s\r\n"
         "Call-ID: %s\r\n"
         "CSeq: %d INVITE\r\n"
         "Content-Length: 0\r\n\r\n",
         via_header, from_header, to_header, call_id, cseq_num);
```

### 3. Manual Answer Option
Add configuration option:
```c
if (sip_config.auto_answer) {
    // Send 200 OK immediately
} else {
    // Send 180 Ringing
    // Wait for user to click "Answer"
}
```

### 4. Caller ID Display
Extract caller information and display in web interface:
```c
// Parse From header
char caller_name[64];
char caller_number[64];
// Display in UI
```

## Related Files

- `main/sip_client.c` - Incoming call handling
- `main/rtp_handler.c` - RTP session management
- `INCOMING_CALL_FIX.md` - This document
- `SIP_CALL_IMPLEMENTATION.md` - Overall implementation

## Conclusion

Incoming calls now work properly by:
- ✅ Extracting headers from INVITE
- ✅ Sending proper 200 OK response with SDP
- ✅ Starting RTP session
- ✅ Establishing bidirectional audio
- ✅ Handling call termination

The system can now receive calls from any SIP client and establish two-way voice communication.
