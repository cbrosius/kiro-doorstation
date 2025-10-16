# Implementation Plan

- [x] 1. Define RFC 4733 data structures and constants





  - Create telephone-event packet structure in rtp_handler.h
  - Define DTMF event code constants (0-15)
  - Add callback function pointer type for telephone-events
  - _Requirements: 1.1, 8.1_

- [x] 2. Implement RFC 4733 packet parsing in RTP handler





- [x] 2.1 Add telephone-event callback registration


  - Implement `rtp_set_telephone_event_callback()` function
  - Store callback pointer in static variable
  - _Requirements: 1.1, 8.1_

- [x] 2.2 Modify rtp_receive_audio() to route by payload type


  - Extract payload type from RTP header
  - Route payload type 101 to telephone-event parser
  - Route payload type 0/8 to existing audio decoder
  - _Requirements: 1.1, 8.5_

- [x] 2.3 Implement telephone-event packet parser


  - Create `rtp_process_telephone_event()` function
  - Parse event code, end bit, volume, and duration fields
  - Extract end bit from e_r_volume field (bit 7)
  - Validate packet size (minimum 4 bytes)
  - _Requirements: 8.1, 8.4_

- [x] 2.4 Add event deduplication logic


  - Track last processed RTP timestamp
  - Compare incoming timestamp with last processed
  - Only invoke callback when end bit is set and timestamp is new
  - _Requirements: 8.3_

- [x] 2.5 Map event codes to DTMF characters


  - Create mapping function for codes 0-15 to characters 0-9, *, #, A-D
  - Invoke callback with mapped character when end bit detected
  - _Requirements: 1.1, 8.2_

- [x] 2.6 Add error handling for malformed packets






  - Check packet size before parsing
  - Log errors for invalid packets
  - Continue processing without crashing
  - _Requirements: 8.4_

- [-] 3. Enhance DTMF decoder with security features


- [ ] 3.1 Define security configuration structures
  - Create dtmf_security_config_t structure
  - Create dtmf_command_state_t structure
  - Add security log entry structure
  - _Requirements: 2.1, 3.1, 5.1_

- [ ] 3.2 Implement NVS security configuration storage
  - Create `dtmf_load_security_config()` function
  - Create `dtmf_save_security_config()` function
  - Define NVS namespace "dtmf_security"
  - Store pin_enabled, pin_code, timeout_ms, max_attempts
  - Provide default values if NVS keys don't exist
  - _Requirements: 5.3, 5.4_

- [ ] 3.3 Implement telephone-event processing function
  - Create `dtmf_process_telephone_event()` function
  - Map event codes to DTMF characters
  - Accumulate characters in command buffer
  - Start timeout timer on first digit
  - _Requirements: 1.2, 1.5, 2.1_

- [ ] 3.4 Implement command timeout mechanism
  - Check elapsed time since first digit
  - Clear buffer if timeout exceeded
  - Log timeout events
  - Reset to idle state on timeout
  - _Requirements: 2.1, 2.2, 2.3, 2.4_

- [ ] 3.5 Implement command validation logic
  - Create `dtmf_validate_command()` function
  - Validate command format on '#' character
  - Check PIN code if PIN protection enabled
  - Support legacy "*1#" when PIN disabled
  - Recognize "*2#" for light toggle
  - _Requirements: 1.5, 5.1, 5.2, 5.4_

- [ ] 3.6 Implement rate limiting
  - Track failed attempts counter per call
  - Increment counter on invalid command
  - Block processing when max attempts reached
  - Log security alert when rate limit triggered
  - _Requirements: 3.1, 3.2, 3.3, 3.4_

- [ ] 3.7 Implement call state reset
  - Create `dtmf_reset_call_state()` function
  - Clear command buffer
  - Reset failed attempts counter
  - Clear rate limit flag
  - Cancel timeout timer
  - _Requirements: 2.5, 3.5_

- [ ] 3.8 Implement command execution
  - Create `dtmf_execute_command()` function
  - Execute door opener for valid PIN or legacy "*1#"
  - Execute light toggle for "*2#"
  - Clear buffer after successful execution
  - _Requirements: 1.5, 5.2_

- [ ] 4. Implement security audit logging
- [ ] 4.1 Create security log buffer
  - Define circular buffer for last 50 log entries
  - Create dtmf_security_log_t structure
  - Add mutex for thread-safe access
  - _Requirements: 4.1, 4.2, 4.3_

- [ ] 4.2 Log successful command executions
  - Record timestamp, command, action, caller ID
  - Use NTP timestamp when available
  - Add entry to circular buffer
  - _Requirements: 4.1, 4.4_

- [ ] 4.3 Log failed command attempts
  - Record timestamp, attempted command, reason
  - Include caller ID if available
  - Add entry to circular buffer
  - _Requirements: 4.2, 4.4_

- [ ] 4.4 Log rate limiting events
  - Record security alert when max attempts exceeded
  - Include number of failed attempts
  - Add caller ID
  - _Requirements: 4.3, 4.4_

- [ ] 4.5 Create log retrieval function
  - Implement function to get log entries since timestamp
  - Return entries in chronological order
  - Thread-safe access with mutex
  - _Requirements: 4.5_

- [ ] 5. Remove audio DTMF processing
- [ ] 5.1 Remove audio DTMF processing from SIP client
  - Locate `dtmf_process_audio(tx_buffer, samples_read)` call in sip_client.c
  - Remove the function call
  - Verify audio still transmitted via RTP normally
  - _Requirements: 1.3, 1.4_

- [ ] 5.2 Update dtmf_decoder to remove audio functions
  - Remove `dtmf_decode_buffer()` function
  - Remove `dtmf_process_audio()` function
  - Keep dtmf_decoder_init() and update to load security config
  - Update dtmf_decoder.h to remove audio function declarations
  - _Requirements: 1.3, 1.4_

- [ ] 6. Integrate telephone-event callback with DTMF decoder
- [ ] 6.1 Register callback during initialization
  - Call `rtp_set_telephone_event_callback()` from dtmf_decoder_init()
  - Pass `dtmf_process_telephone_event` as callback
  - _Requirements: 1.1, 1.2_

- [ ] 6.2 Add call state management
  - Call `dtmf_reset_call_state()` when call connects
  - Call `dtmf_reset_call_state()` when call ends
  - Integrate with existing SIP state machine in sip_client.c
  - _Requirements: 2.5, 3.5_

- [ ] 7. Add web interface endpoints for security configuration
- [ ] 7.1 Implement GET /api/dtmf/security endpoint
  - Return current security configuration as JSON
  - Include pin_enabled, pin_code, timeout_ms, max_attempts
  - _Requirements: 5.5, 6.5_

- [ ] 7.2 Implement POST /api/dtmf/security endpoint
  - Accept security configuration updates
  - Validate PIN format (digits only, 1-8 chars)
  - Validate timeout range (5000-30000 ms)
  - Save to NVS and update runtime configuration
  - _Requirements: 5.5, 6.5_

- [ ] 7.3 Implement GET /api/dtmf/logs endpoint
  - Return security log entries as JSON array
  - Support optional since_timestamp parameter
  - Include timestamp, type, command, action, caller, reason
  - _Requirements: 4.5, 6.5_

- [ ] 8. Update SDP negotiation for RFC 4733
- [ ] 8.1 Verify SDP includes RFC 4733 attributes
  - Confirm "a=rtpmap:101 telephone-event/8000" is present
  - Confirm "a=fmtp:101 0-15" is present
  - Check both INVITE and 200 OK responses
  - _Requirements: 7.1, 7.2, 7.5_

- [ ] 8.2 Add SDP response validation
  - Parse remote SDP for payload type 101 support
  - Log warning if remote party doesn't support RFC 4733
  - Continue call even without RFC 4733 support
  - _Requirements: 7.3, 7.4_

- [ ] 9. Create SECURITY.md documentation
- [ ] 9.1 Document audio DTMF replay attack vulnerability
  - Explain how audio DTMF can be replayed at the door
  - Describe the security risk of unauthorized access
  - Explain why audio DTMF processing was removed
  - _Requirements: 6.1, 6.2_

- [ ] 9.2 Document RFC 4733 compatibility
  - List supported devices (DECT bases, IP phones, softphones)
  - Explain that RFC 4733 is standard since 2006
  - State that all modern devices support it
  - _Requirements: 6.3_

- [ ] 9.3 Document legacy device guidance
  - Provide options for users with old equipment
  - Suggest firmware updates or device replacement
  - List compatible replacement devices
  - _Requirements: 6.4_

- [ ] 9.4 Document security features
  - Explain PIN code protection
  - Describe command timeout mechanism
  - Describe rate limiting
  - Explain audit logging
  - Provide configuration examples
  - _Requirements: 6.5_

- [ ] 10. Integration and system testing
- [ ] 10.1 Test RFC 4733 packet parsing
  - Send test telephone-event packets
  - Verify correct event code extraction
  - Verify end bit detection
  - Verify deduplication works
  - _Requirements: 8.1, 8.2, 8.3_

- [ ] 10.2 Test command execution with PIN
  - Configure PIN code via web interface
  - Make test call and send *[PIN]#
  - Verify door opener activates
  - Verify security log entry created
  - _Requirements: 5.1, 5.2, 4.1_

- [ ] 10.3 Test command timeout
  - Send partial command sequence
  - Wait for timeout period
  - Verify buffer cleared
  - Send new complete command
  - Verify successful execution
  - _Requirements: 2.1, 2.2, 2.3, 2.4_

- [ ] 10.4 Test rate limiting
  - Send 3 invalid commands
  - Verify rate limit triggered
  - Attempt valid command
  - Verify command blocked
  - Hang up and call again
  - Verify rate limit reset
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5_

- [ ] 10.5 Test audio DTMF removal
  - Play DTMF tones at doorstation microphone
  - Verify no command execution
  - Verify audio still transmitted normally
  - Verify no security log entries created
  - _Requirements: 1.3, 1.4_

- [ ] 10.6 Test with real SIP devices
  - Test with DECT phone (Gigaset or Yealink)
  - Test with IP phone
  - Test with softphone (Linphone)
  - Verify RFC 4733 events received correctly
  - _Requirements: 7.1, 7.2, 7.3_
