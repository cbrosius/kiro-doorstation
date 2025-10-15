# SIP Call Implementation TODO

## Current Status
The `sip_client_make_call()` function is currently a **stub implementation** that:
- ✅ Validates state (IDLE or REGISTERED)
- ✅ Validates configuration
- ✅ Creates INVITE message with SDP
- ✅ Logs the call attempt
- ❌ Does NOT actually send the INVITE
- ❌ Does NOT handle responses
- ❌ Does NOT establish RTP audio

## What's Missing

### 1. Send INVITE Message
```c
// Need to send via UDP socket to SIP server
struct sockaddr_in server_addr;
server_addr.sin_family = AF_INET;
server_addr.sin_port = htons(sip_config.port);
// ... resolve hostname and send
sendto(sip_socket, invite_msg, strlen(invite_msg), 0, ...);
```

### 2. Handle SIP Responses
- **100 Trying** - Server received request
- **180 Ringing** - Callee is being alerted
- **200 OK** - Call accepted
- **486 Busy Here** - Callee is busy
- **603 Decline** - Call rejected

### 3. Send ACK
After receiving 200 OK, must send ACK to complete 3-way handshake

### 4. RTP Audio Stream
- Establish RTP session on negotiated port
- Send audio from microphone
- Receive audio to speaker
- Handle DTMF in-band or via SIP INFO

### 5. Call Termination
- Send BYE message
- Handle BYE from remote
- Clean up RTP session

## Why It's Not Implemented Yet

This is a **doorbell testing project** focused on:
1. ✅ SIP registration and authentication
2. ✅ Button press detection
3. ✅ Web interface
4. ✅ NTP time synchronization
5. ✅ Logging and debugging

Full SIP call handling with RTP audio is complex and requires:
- RTP/RTCP protocol implementation
- Audio codec (G.711, etc.)
- Echo cancellation
- Jitter buffer
- NAT traversal (STUN/TURN)

## Current Behavior

When doorbell button is pressed:
1. Button ISR queues event
2. Task calls `sip_client_make_call()`
3. INVITE message is created
4. Log entry shows "Call simulation complete"
5. State returns to REGISTERED/IDLE
6. **No actual call is made**

## Next Steps for Full Implementation

1. **Phase 1: Send INVITE**
   - Send INVITE via socket
   - Parse responses
   - Handle basic call flow

2. **Phase 2: Audio Setup**
   - Implement RTP sender/receiver
   - Add G.711 codec
   - Connect to I2S audio

3. **Phase 3: Call Management**
   - Handle call duration
   - Implement BYE
   - Add call history

4. **Phase 4: Advanced Features**
   - DTMF sending
   - Call transfer
   - Multiple lines

## Testing Without Full Implementation

You can still test:
- ✅ Button press detection (works)
- ✅ Debouncing (works)
- ✅ State management (works)
- ✅ Logging (works)
- ✅ Web interface (works)
- ✅ SIP registration (works)

The system logs show when a call would be initiated, which is sufficient for testing the doorbell hardware and button logic.

## Related Files
- `main/sip_client.c` - SIP client implementation
- `main/gpio_handler.c` - Button handling
- `main/audio_handler.c` - Audio I/O (stub)
- `docs/SIP_SETUP.md` - SIP configuration guide
