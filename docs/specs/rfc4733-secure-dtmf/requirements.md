# Requirements Document

## Introduction

This feature implements secure user command input for the SIP doorstation using RFC 4733 RTP telephone-events, replacing the insecure audio DTMF processing. The implementation addresses a critical security vulnerability where audio DTMF tones could be replayed by unauthorized individuals at the door to gain access. This feature ensures that only authenticated callers can send commands to control the door opener and other functions through standardized, secure RTP signaling.

## Glossary

- **RFC 4733**: IETF standard for RTP Payload for DTMF Digits, Telephony Tones, and Telephony Signals
- **RTP Telephone-Event**: Digital representation of DTMF button presses sent as RTP packets (payload type 101)
- **Audio DTMF**: Legacy method of detecting DTMF tones from audio samples using frequency analysis
- **Doorstation**: The ESP32-based SIP intercom device that controls door access
- **Command Sequence**: A series of DTMF digits ending with '#' that triggers an action (e.g., "*1#" for door open)
- **Replay Attack**: Security vulnerability where recorded audio can be played back to trigger unauthorized actions
- **RTP Handler**: The component responsible for sending and receiving Real-time Transport Protocol packets
- **DTMF Decoder**: The component that processes DTMF input and executes commands
- **PIN Code**: Personal Identification Number used as part of command sequences for authentication
- **Rate Limiting**: Security mechanism that restricts the number of command attempts within a time period
- **Command Buffer**: Temporary storage for accumulating DTMF digits before command execution

## Requirements

### Requirement 1

**User Story:** As a security-conscious system administrator, I want the doorstation to only accept DTMF commands via RFC 4733 RTP telephone-events, so that unauthorized individuals cannot replay audio tones to gain access.

#### Acceptance Criteria

1. WHEN the Doorstation receives an RTP packet with payload type 101, THE Doorstation SHALL decode the telephone-event according to RFC 4733 specification
2. WHEN the Doorstation is in a connected call state, THE Doorstation SHALL process telephone-events from the RTP stream to build command sequences
3. THE Doorstation SHALL NOT process audio samples from the microphone input for DTMF detection
4. WHEN audio DTMF processing code is present, THE Doorstation SHALL remove all calls to dtmf_process_audio() on transmitted audio buffers
5. THE Doorstation SHALL maintain backward compatibility with the existing command structure (e.g., "*1#" for door open, "*2#" for light toggle)

### Requirement 2

**User Story:** As a doorstation operator, I want commands to have a timeout period, so that partial command sequences cannot be exploited or cause confusion.

#### Acceptance Criteria

1. WHEN a telephone-event is received, THE DTMF Decoder SHALL start a command timeout timer if not already running
2. WHEN the command timeout period expires, THE DTMF Decoder SHALL clear the Command Buffer and reset to idle state
3. THE Doorstation SHALL use a configurable timeout value between 5 and 30 seconds with a default of 10 seconds
4. WHEN a complete command sequence is executed, THE DTMF Decoder SHALL immediately clear the timeout timer
5. WHEN the call ends, THE DTMF Decoder SHALL clear the Command Buffer and cancel any active timeout timer

### Requirement 3

**User Story:** As a security administrator, I want to limit the number of failed command attempts, so that brute-force attacks are prevented.

#### Acceptance Criteria

1. WHEN a command sequence is completed with '#', THE DTMF Decoder SHALL validate the command against known patterns
2. WHEN an invalid command is detected, THE DTMF Decoder SHALL increment a failed attempt counter for the current call
3. WHEN the failed attempt counter reaches 3 attempts, THE DTMF Decoder SHALL block further command processing for the remainder of the call
4. WHEN rate limiting is triggered, THE Doorstation SHALL log a security warning with timestamp and caller information
5. WHEN a new call is established, THE DTMF Decoder SHALL reset the failed attempt counter to zero

### Requirement 4

**User Story:** As a system administrator, I want all door access attempts to be logged with detailed information, so that I can audit security events and investigate unauthorized access attempts.

#### Acceptance Criteria

1. WHEN a valid command is executed, THE Doorstation SHALL log the event with timestamp, command type, and caller identifier
2. WHEN an invalid command is attempted, THE Doorstation SHALL log the failed attempt with timestamp and the attempted command sequence
3. WHEN rate limiting is triggered, THE Doorstation SHALL log a security alert with the number of failed attempts
4. THE Doorstation SHALL include NTP-synchronized timestamps in all security logs when NTP is available
5. THE Doorstation SHALL make security logs accessible through the web interface for review

### Requirement 5

**User Story:** As a doorstation administrator, I want to configure custom PIN codes for door access, so that I can enhance security beyond the default command sequences.

#### Acceptance Criteria

1. THE Doorstation SHALL support configurable PIN codes of 1 to 8 digits for door opener commands
2. WHEN PIN code protection is enabled, THE Doorstation SHALL require the format "*[PIN]#" to activate the door opener
3. THE Doorstation SHALL store PIN codes in NVS (Non-Volatile Storage) with secure handling
4. WHEN no PIN is configured, THE Doorstation SHALL accept the legacy "*1#" command for backward compatibility
5. THE Doorstation SHALL provide a web interface option to enable/disable and configure PIN codes

### Requirement 6

**User Story:** As a developer, I want clear documentation explaining why audio DTMF is not supported, so that users understand the security rationale and compatibility requirements.

#### Acceptance Criteria

1. THE Doorstation project SHALL include a SECURITY.md document in the root directory
2. THE SECURITY.md document SHALL explain the replay attack vulnerability of audio DTMF processing
3. THE SECURITY.md document SHALL document that RFC 4733 is supported by all modern SIP devices manufactured after 2010
4. THE SECURITY.md document SHALL provide guidance for users with legacy equipment
5. THE SECURITY.md document SHALL describe all implemented security features and their configuration

### Requirement 7

**User Story:** As a doorstation operator, I want the system to properly negotiate RFC 4733 support during call setup, so that both parties know telephone-events are available.

#### Acceptance Criteria

1. WHEN the Doorstation sends an INVITE or responds to an INVITE, THE Doorstation SHALL include "a=rtpmap:101 telephone-event/8000" in the SDP
2. WHEN the Doorstation sends an INVITE or responds to an INVITE, THE Doorstation SHALL include "a=fmtp:101 0-15" in the SDP to indicate support for DTMF events 0-15
3. WHEN the Doorstation receives an SDP response, THE Doorstation SHALL verify that the remote party supports payload type 101
4. WHEN the remote party does not support RFC 4733, THE Doorstation SHALL log a warning but continue the call
5. THE Doorstation SHALL maintain the existing SDP structure for audio codecs (PCMU/PCMA)

### Requirement 8

**User Story:** As a system integrator, I want the RTP handler to correctly parse RFC 4733 packets, so that telephone-events are reliably detected regardless of the sending device.

#### Acceptance Criteria

1. WHEN an RTP packet with payload type 101 is received, THE RTP Handler SHALL extract the event code, end bit, volume, and duration fields
2. WHEN the end bit is set in a telephone-event packet, THE RTP Handler SHALL signal a complete DTMF digit to the DTMF Decoder
3. THE RTP Handler SHALL ignore duplicate telephone-event packets with the same timestamp to prevent double-detection
4. WHEN a telephone-event packet is malformed, THE RTP Handler SHALL log an error and discard the packet without crashing
5. THE RTP Handler SHALL continue processing audio RTP packets (payload type 0 and 8) normally alongside telephone-event packets
