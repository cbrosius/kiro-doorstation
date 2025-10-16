# Design Document

## Overview

This design implements RFC 4733 RTP telephone-event processing to replace the insecure audio DTMF detection in the ESP32 SIP doorstation. The solution addresses a critical security vulnerability where audio DTMF tones could be replayed at the door to gain unauthorized access. The design focuses on parsing RFC 4733 packets from the RTP stream, implementing secure command processing with timeout and rate limiting, and providing comprehensive security logging.

The implementation will modify the existing RTP handler to detect and parse telephone-event packets (payload type 101), enhance the DTMF decoder to process digital events instead of audio samples, and add security features including configurable PIN codes, command timeouts, and rate limiting.

## Architecture

### Component Overview

```
┌─────────────────────────────────────────────────────────────┐
│                        SIP Client                            │
│  (Manages call state, triggers RTP session)                 │
└────────────────┬────────────────────────────────────────────┘
                 │
                 │ Call Connected
                 ▼
┌─────────────────────────────────────────────────────────────┐
│                      RTP Handler                             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  rtp_receive_audio()                                  │  │
│  │  • Receives RTP packets                               │  │
│  │  • Checks payload type                                │  │
│  │  • Routes to appropriate handler                      │  │
│  └──────────────┬───────────────────────┬─────────────────┘  │
│                 │                       │                    │
│    Payload=0/8  │                       │  Payload=101       │
│    (Audio)      │                       │  (Telephone-Event) │
│                 ▼                       ▼                    │
│  ┌──────────────────────┐  ┌──────────────────────────┐    │
│  │ Audio Decoder        │  │ RFC 4733 Parser          │    │
│  │ (μ-law/A-law)        │  │ • Parse event fields     │    │
│  │                      │  │ • Detect end bit         │    │
│  │                      │  │ • Deduplicate events     │    │
│  └──────────────────────┘  └──────────┬───────────────┘    │
└─────────────────────────────────────────┼───────────────────┘
                                          │
                                          │ DTMF Event
                                          ▼
┌─────────────────────────────────────────────────────────────┐
│                    DTMF Decoder                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  dtmf_process_telephone_event()                       │  │
│  │  • Accumulate digits in command buffer               │  │
│  │  • Manage command timeout                            │  │
│  │  • Validate command on '#'                           │  │
│  │  • Check rate limiting                               │  │
│  │  • Execute valid commands                            │  │
│  └──────────────┬───────────────────────────────────────┘  │
│                 │                                            │
│                 │ Valid Command                              │
│                 ▼                                            │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  Command Executor                                     │  │
│  │  • Door opener (*[PIN]#)                             │  │
│  │  • Light toggle (*2#)                                │  │
│  │  • Log all attempts                                  │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### Data Flow

1. **RTP Packet Reception**: RTP handler receives packets on UDP socket
2. **Payload Type Detection**: Check if payload type is 101 (telephone-event) or 0/8 (audio)
3. **RFC 4733 Parsing**: Extract event code, end bit, volume, and duration from telephone-event packets
4. **Event Deduplication**: Track last timestamp to prevent processing duplicate packets
5. **DTMF Accumulation**: Add digit to command buffer when end bit is set
6. **Command Validation**: On '#', validate complete command against known patterns
7. **Security Checks**: Verify rate limits and PIN codes before execution
8. **Command Execution**: Trigger relay activation or other actions
9. **Audit Logging**: Record all attempts with timestamps and caller info

## Components and Interfaces

### 1. RTP Handler Extensions

**File**: `main/rtp_handler.c`, `main/rtp_handler.h`

#### New Structures

```c
// RFC 4733 telephone-event packet structure
typedef struct {
    uint8_t event;        // Event code (0-15 for DTMF)
    uint8_t e_r_volume;   // E(1bit) R(1bit) Volume(6bits)
    uint16_t duration;    // Duration in timestamp units
} __attribute__((packed)) rtp_telephone_event_t;

// Callback for telephone-events
typedef void (*telephone_event_callback_t)(uint8_t event);
```

#### New Functions

```c
// Register callback for telephone-events
void rtp_set_telephone_event_callback(telephone_event_callback_t callback);

// Internal: Parse and process telephone-event packet
static void rtp_process_telephone_event(const uint8_t* payload, size_t payload_size);
```

#### Modified Functions

```c
// Enhanced to route packets by payload type
int rtp_receive_audio(int16_t* samples, size_t max_samples);
```

**Implementation Details**:
- Modify `rtp_receive_audio()` to check payload type in RTP header
- If payload type == 101, call `rtp_process_telephone_event()`
- If payload type == 0 or 8, continue with existing audio decoding
- Track last telephone-event timestamp to prevent duplicate processing
- Extract end bit from `e_r_volume` field: `(e_r_volume & 0x80) != 0`
- Map event codes 0-15 to DTMF characters: 0-9, *, #, A-D

### 2. DTMF Decoder Enhancements

**File**: `main/dtmf_decoder.c`, `main/dtmf_decoder.h`

#### New Structures

```c
// Security configuration
typedef struct {
    bool pin_enabled;           // PIN protection enabled
    char pin_code[9];           // PIN code (max 8 digits + null)
    uint32_t timeout_ms;        // Command timeout in milliseconds
    uint8_t max_attempts;       // Max failed attempts per call
} dtmf_security_config_t;

// Command state tracking
typedef struct {
    char buffer[16];            // Command buffer
    uint8_t buffer_index;       // Current position in buffer
    uint32_t start_time_ms;     // Command start timestamp
    uint8_t failed_attempts;    // Failed attempts this call
    bool rate_limited;          // Rate limit triggered
    uint32_t last_event_ts;     // Last RTP timestamp processed
} dtmf_command_state_t;
```

#### New Functions

```c
// Process telephone-event (replaces audio processing)
void dtmf_process_telephone_event(uint8_t event);

// Load security configuration from NVS
void dtmf_load_security_config(void);

// Save security configuration to NVS
void dtmf_save_security_config(const dtmf_security_config_t* config);

// Reset state for new call
void dtmf_reset_call_state(void);

// Check if command timeout expired
static bool dtmf_check_timeout(void);

// Validate complete command
static bool dtmf_validate_command(const char* command);

// Execute validated command
static void dtmf_execute_command(const char* command);
```

#### Removed Functions

```c
// REMOVED: dtmf_process_audio() - no longer used
// REMOVED: dtmf_decode_buffer() - no longer used
```

**Implementation Details**:
- `dtmf_process_telephone_event()` receives event codes 0-15 from RTP handler
- Map event codes to characters: 0-9 (0x00-0x09), * (0x0A), # (0x0B), A-D (0x0C-0x0F)
- Accumulate characters in command buffer until '#' received
- Start timeout timer on first digit
- On '#', validate command format and check PIN if enabled
- Increment failed attempts counter on invalid command
- Block further processing if max attempts exceeded
- Clear buffer and reset timeout after successful execution or timeout

### 3. Security Configuration Storage

**File**: `main/dtmf_decoder.c` (NVS operations)

#### NVS Schema

```
Namespace: "dtmf_security"
Keys:
  - "pin_enabled"  : uint8_t (0 or 1)
  - "pin_code"     : string (max 8 chars)
  - "timeout_ms"   : uint32_t (default 10000)
  - "max_attempts" : uint8_t (default 3)
```

**Implementation Details**:
- Load configuration during `dtmf_decoder_init()`
- Provide defaults if NVS keys don't exist
- Validate PIN code format (digits only, 1-8 characters)
- Validate timeout range (5000-30000 ms)

### 4. Web Interface Integration

**File**: `main/http_server.c` (new endpoints)

#### New HTTP Endpoints

```
GET  /api/dtmf/security
POST /api/dtmf/security
GET  /api/dtmf/logs
```

**Request/Response Formats**:

```json
// GET /api/dtmf/security
{
  "pin_enabled": true,
  "pin_code": "1234",
  "timeout_ms": 10000,
  "max_attempts": 3
}

// POST /api/dtmf/security
{
  "pin_enabled": true,
  "pin_code": "5678",
  "timeout_ms": 15000,
  "max_attempts": 5
}

// GET /api/dtmf/logs
{
  "logs": [
    {
      "timestamp": 1697462400000,
      "type": "success",
      "command": "*1234#",
      "action": "door_open",
      "caller": "sip:user@domain.com"
    },
    {
      "timestamp": 1697462450000,
      "type": "failed",
      "command": "*9999#",
      "reason": "invalid_pin",
      "caller": "sip:unknown@domain.com"
    }
  ]
}
```

## Data Models

### RFC 4733 Event Codes

```c
// DTMF event codes (RFC 4733 Section 3.2)
#define DTMF_EVENT_0    0
#define DTMF_EVENT_1    1
#define DTMF_EVENT_2    2
#define DTMF_EVENT_3    3
#define DTMF_EVENT_4    4
#define DTMF_EVENT_5    5
#define DTMF_EVENT_6    6
#define DTMF_EVENT_7    7
#define DTMF_EVENT_8    8
#define DTMF_EVENT_9    9
#define DTMF_EVENT_STAR 10  // *
#define DTMF_EVENT_HASH 11  // #
#define DTMF_EVENT_A    12
#define DTMF_EVENT_B    13
#define DTMF_EVENT_C    14
#define DTMF_EVENT_D    15
```

### Command Patterns

```c
// Valid command patterns
typedef enum {
    CMD_DOOR_OPEN,      // *[PIN]# or *1# (legacy)
    CMD_LIGHT_TOGGLE,   // *2#
    CMD_INVALID
} dtmf_command_type_t;
```

### Security Log Entry

```c
typedef struct {
    uint64_t timestamp;         // NTP timestamp in milliseconds
    dtmf_command_type_t type;   // Command type
    bool success;               // Execution success
    char command[16];           // Full command string
    char caller_id[64];         // SIP caller identifier
    char reason[32];            // Failure reason (if applicable)
} dtmf_security_log_t;
```

## Error Handling

### RTP Handler Errors

1. **Malformed Telephone-Event Packet**
   - Log error with packet details
   - Discard packet and continue processing
   - Do not crash or affect audio stream

2. **Unknown Payload Type**
   - Log warning
   - Discard packet
   - Continue normal operation

3. **Socket Errors**
   - Existing error handling remains unchanged
   - Log errors and attempt recovery

### DTMF Decoder Errors

1. **Command Timeout**
   - Clear command buffer
   - Log timeout event
   - Reset to idle state
   - Do not increment failed attempts counter

2. **Invalid Command**
   - Increment failed attempts counter
   - Log failed attempt with command details
   - Clear buffer and wait for next command

3. **Rate Limit Exceeded**
   - Set rate_limited flag
   - Log security alert
   - Ignore all further DTMF input for remainder of call
   - Reset on new call

4. **NVS Read/Write Errors**
   - Use default configuration values
   - Log error
   - Continue operation with defaults

### Security Considerations

1. **Buffer Overflow Prevention**
   - Limit command buffer to 15 characters
   - Reject additional input when buffer full
   - Clear buffer on timeout or execution

2. **Timing Attack Mitigation**
   - Use constant-time comparison for PIN validation
   - Do not reveal PIN length in error messages

3. **Replay Attack Prevention**
   - Only process telephone-events from active RTP session
   - Verify call is in CONNECTED state
   - Clear all state when call ends

## Testing Strategy

### Unit Tests

1. **RFC 4733 Packet Parsing**
   - Test valid telephone-event packets
   - Test malformed packets (too short, invalid fields)
   - Test end bit detection
   - Test event code mapping

2. **Command Buffer Management**
   - Test digit accumulation
   - Test buffer overflow handling
   - Test timeout expiration
   - Test buffer clearing

3. **PIN Validation**
   - Test correct PIN acceptance
   - Test incorrect PIN rejection
   - Test PIN with various lengths (1-8 digits)
   - Test legacy mode (no PIN)

4. **Rate Limiting**
   - Test failed attempt counting
   - Test rate limit trigger
   - Test reset on new call

### Integration Tests

1. **End-to-End Command Execution**
   - Establish SIP call
   - Send RFC 4733 telephone-events
   - Verify door opener activation
   - Verify security logging

2. **Timeout Behavior**
   - Send partial command
   - Wait for timeout
   - Verify buffer cleared
   - Send new command successfully

3. **Rate Limiting**
   - Send 3 invalid commands
   - Verify rate limit triggered
   - Verify valid command blocked
   - Hang up and call again
   - Verify rate limit reset

4. **Audio DTMF Removal**
   - Play DTMF tones at doorstation microphone
   - Verify no command execution
   - Verify audio still transmitted normally

### Compatibility Tests

1. **SIP Device Compatibility**
   - Test with Gigaset DECT base
   - Test with Yealink IP phone
   - Test with Linphone softphone
   - Test with mobile SIP apps

2. **SDP Negotiation**
   - Verify payload type 101 advertised
   - Verify fmtp parameters correct
   - Test with devices that don't support RFC 4733

### Security Tests

1. **Replay Attack Prevention**
   - Record audio DTMF tones
   - Play back at doorstation
   - Verify no command execution

2. **Brute Force Prevention**
   - Attempt multiple invalid PINs
   - Verify rate limiting activates
   - Verify security alerts logged

3. **Timing Analysis**
   - Measure PIN validation time
   - Verify constant-time comparison
   - Check for timing side channels

## Implementation Notes

### Memory Considerations

- ESP32 has limited RAM; avoid large buffers
- Use static allocation for command state (not heap)
- Reuse existing RTP packet buffer for parsing
- Limit security log to last 50 entries (circular buffer)

### Performance Considerations

- Telephone-event processing is lightweight (no FFT/Goertzel)
- Command validation is O(1) for known patterns
- PIN comparison should be constant-time but fast
- No impact on audio latency

### Backward Compatibility

- Maintain existing SDP structure
- Keep legacy "*1#" command if PIN disabled
- Preserve existing command structure for "*2#"
- No changes to SIP signaling or call flow

### Migration Path

1. Deploy new firmware with RFC 4733 support
2. Audio DTMF removed immediately (security fix)
3. Users with legacy devices must upgrade or replace
4. Provide clear documentation in SECURITY.md
5. Web interface shows RFC 4733 status in diagnostics
