# SIP Call Implementation

## Overview
Full SIP call functionality has been implemented for the ESP32 doorbell project. The system now supports:
- Outgoing calls (INVITE)
- Call establishment (100 Trying, 180 Ringing, 200 OK, ACK)
- RTP audio streaming (G.711 μ-law)
- Call termination (BYE)
- Incoming call handling
- Error handling (busy, declined, not found, etc.)

## Architecture

### Components

#### 1. SIP Client (`main/sip_client.c`)
Handles SIP signaling:
- **INVITE**: Initiates outgoing calls
- **ACK**: Confirms call establishment after 200 OK
- **BYE**: Terminates active calls
- **Response Handling**: Processes all SIP responses (100, 180, 200, 4xx, etc.)

#### 2. RTP Handler (`main/rtp_handler.c`)
Manages real-time audio transport:
- **G.711 μ-law codec**: Encodes/decodes audio
- **RTP packet handling**: Builds and parses RTP headers
- **UDP socket management**: Sends/receives audio packets
- **Sequence numbering**: Maintains packet order
- **Timestamp management**: Synchronizes audio streams

#### 3. Audio Handler (`main/audio_handler.c`)
Interfaces with audio hardware:
- **Recording**: Captures microphone input
- **Playback**: Outputs to speaker
- **Dummy mode**: Works without physical hardware for testing

## Call Flow

### Outgoing Call

```
ESP32 Doorbell                    SIP Server                    Remote Phone
      |                                |                                |
      |--- INVITE ------------------>  |                                |
      |                                |--- INVITE ------------------>  |
      |<-- 100 Trying ---------------|  |                                |
      |                                |<-- 180 Ringing --------------|  |
      |<-- 180 Ringing --------------|  |                                |
      |                                |<-- 200 OK -------------------|  |
      |<-- 200 OK -------------------|  |                                |
      |--- ACK --------------------->  |                                |
      |                                |--- ACK --------------------->  |
      |                                                                  |
      |<==================== RTP Audio Stream ==========================>|
      |                                                                  |
      |--- BYE --------------------->  |                                |
      |                                |--- BYE --------------------->  |
      |<-- 200 OK -------------------|  |                                |
```

### Incoming Call

```
Remote Phone                      SIP Server                    ESP32 Doorbell
      |                                |                                |
      |--- INVITE ------------------>  |                                |
      |                                |--- INVITE ------------------>  |
      |                                |<-- 200 OK -------------------|  |
      |<-- 200 OK -------------------|  |                                |
      |--- ACK --------------------->  |                                |
      |                                |--- ACK --------------------->  |
      |                                                                  |
      |<==================== RTP Audio Stream ==========================>|
      |                                                                  |
      |--- BYE --------------------->  |                                |
      |                                |--- BYE --------------------->  |
      |                                |<-- 200 OK -------------------|  |
```

## Implementation Details

### INVITE Message
```c
INVITE sip:user@domain SIP/2.0
Via: SIP/2.0/UDP local_ip:5060;branch=z9hG4bK[random]
From: <sip:username@server>;tag=[random]
To: <sip:target@server>
Call-ID: [random]@local_ip
CSeq: 1 INVITE
Contact: <sip:username@local_ip:5060>
Content-Type: application/sdp
Content-Length: [length]

v=0
o=- [session_id] 0 IN IP4 [local_ip]
s=ESP32 Doorbell Call
c=IN IP4 [local_ip]
t=0 0
m=audio 5004 RTP/AVP 0 8 101
a=rtpmap:0 PCMU/8000
a=rtpmap:8 PCMA/8000
a=rtpmap:101 telephone-event/8000
a=sendrecv
```

### ACK Message
Sent after receiving 200 OK to complete the 3-way handshake:
```c
ACK sip:user@server SIP/2.0
Via: SIP/2.0/UDP local_ip:5060;branch=z9hG4bK[random]
From: <sip:username@server>;tag=[from_tag]
To: <sip:username@server>;tag=[to_tag]
Call-ID: [call_id]
CSeq: 1 ACK
Content-Length: 0
```

### BYE Message
Terminates an active call:
```c
BYE sip:user@server SIP/2.0
Via: SIP/2.0/UDP local_ip:5060;branch=z9hG4bK[random]
From: <sip:username@server>;tag=[tag]
To: <sip:username@server>
Call-ID: [call_id]
CSeq: 2 BYE
Content-Length: 0
```

### RTP Packet Structure
```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|X|  CC   |M|     PT      |       Sequence Number         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           Timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           Synchronization Source (SSRC) identifier            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Payload (μ-law)                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

## State Machine

```
IDLE/REGISTERED
    |
    | sip_client_make_call()
    v
CALLING
    |
    | 180 Ringing
    v
RINGING
    |
    | 200 OK + ACK
    v
CONNECTED (RTP active)
    |
    | BYE (sent or received)
    v
REGISTERED
```

## Audio Processing

### Transmission (ESP32 → Remote)
1. `audio_read()` - Capture 160 samples (20ms @ 8kHz)
2. `dtmf_process_audio()` - Detect DTMF tones
3. `linear_to_mulaw()` - Encode to G.711 μ-law
4. `rtp_send_audio()` - Send via RTP/UDP

### Reception (Remote → ESP32)
1. `rtp_receive_audio()` - Receive RTP packet
2. `mulaw_decode_table[]` - Decode G.711 μ-law
3. `audio_write()` - Play to speaker

## Configuration

### SIP Settings
- **Server**: SIP server hostname/IP
- **Port**: Usually 5060 (UDP)
- **Username**: SIP account username
- **Password**: SIP account password
- **Target URIs**: Apartment 1 and 2 SIP addresses

### RTP Settings
- **Local Port**: 5004 (configurable)
- **Remote Port**: Extracted from SDP (usually 5004)
- **Codec**: G.711 μ-law (payload type 0)
- **Sample Rate**: 8000 Hz
- **Frame Size**: 160 samples (20ms)

## API Functions

### Call Control
```c
// Initiate outgoing call
void sip_client_make_call(const char* uri);

// Terminate active call
void sip_client_hangup(void);

// Answer incoming call
void sip_client_answer_call(void);

// Send DTMF digit during call
void sip_client_send_dtmf(char dtmf_digit);
```

### RTP Control
```c
// Initialize RTP handler
void rtp_init(void);

// Start RTP session
bool rtp_start_session(const char* remote_ip, uint16_t remote_port, uint16_t local_port);

// Stop RTP session
void rtp_stop_session(void);

// Check if RTP is active
bool rtp_is_active(void);
```

## Error Handling

### SIP Responses
- **100 Trying**: Server processing request
- **180 Ringing**: Remote phone ringing
- **200 OK**: Call accepted
- **403 Forbidden**: Authentication failed
- **404 Not Found**: Target not found
- **408 Request Timeout**: No response
- **486 Busy Here**: Target busy
- **603 Decline**: Call rejected

### Recovery
- Failed calls return to REGISTERED state
- Socket errors trigger reconnection
- RTP failures are logged but don't crash

## Testing

### Manual Testing
1. Configure SIP settings via web interface
2. Press doorbell button or use "Test Call" button
3. Monitor SIP logs for INVITE, 180, 200 OK, ACK
4. Verify RTP session starts
5. Check audio transmission (if hardware connected)
6. Hang up and verify BYE message

### Without Audio Hardware
The system works in "dummy mode":
- Audio functions return success
- No actual audio is captured/played
- RTP packets are still sent/received
- Call flow is fully functional

## Performance

### Memory Usage
- SIP messages: ~1.5 KB per message
- RTP packets: ~172 bytes (12 byte header + 160 byte payload)
- Audio buffers: 320 bytes per frame (160 samples × 2 bytes)

### CPU Usage
- SIP task: Low (1 second polling interval)
- RTP processing: Moderate (20ms frame rate)
- Audio codec: Minimal (table lookup)

### Network Bandwidth
- RTP: ~64 kbps (G.711 μ-law)
- SIP: Minimal (only during call setup/teardown)

## Limitations

### Current Implementation
- **No STUN/TURN**: May not work behind NAT
- **No SRTP**: Audio is not encrypted
- **No jitter buffer**: May have audio quality issues
- **No echo cancellation**: May have echo on calls
- **Fixed codec**: Only G.711 μ-law supported
- **No call hold/transfer**: Basic call control only

### Future Enhancements
1. Add STUN support for NAT traversal
2. Implement jitter buffer for better audio quality
3. Add SRTP for secure audio
4. Support additional codecs (G.722, Opus)
5. Implement call hold and transfer
6. Add echo cancellation
7. Support multiple simultaneous calls

## Troubleshooting

### Call Not Connecting
- Check SIP registration status
- Verify target URI is correct
- Check firewall/NAT settings
- Review SIP logs for error responses

### No Audio
- Verify RTP session started
- Check RTP port (5004) is not blocked
- Confirm audio hardware is connected
- Review RTP logs for packet transmission

### Call Drops
- Check network stability
- Verify SIP keep-alive (re-registration)
- Monitor for BYE messages
- Check for socket errors

## Related Files
- `main/sip_client.c` - SIP signaling
- `main/sip_client.h` - SIP API
- `main/rtp_handler.c` - RTP audio transport
- `main/rtp_handler.h` - RTP API
- `main/audio_handler.c` - Audio I/O
- `main/dtmf_decoder.c` - DTMF detection
- `docs/SIP_CALL_TODO.md` - Previous TODO (now completed)
- `docs/SIP_SETUP.md` - Configuration guide

## Conclusion

The SIP call functionality is now fully implemented and functional. The system can:
- Make outgoing calls to configured targets
- Handle incoming calls
- Stream audio bidirectionally via RTP
- Properly terminate calls
- Handle errors gracefully

This implementation provides a solid foundation for a SIP-based doorbell system, with room for future enhancements as needed.
