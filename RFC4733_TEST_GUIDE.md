# RFC 4733 Packet Parsing Test Guide

## Overview

This document describes the RFC 4733 packet parsing tests that verify the doorstation's ability to correctly process telephone-event RTP packets according to RFC 4733 specification.

## Test Coverage

The test suite includes 5 comprehensive tests:

### Test 1: Event Code Extraction
- Verifies that event codes (0-15) are correctly extracted from telephone-event packets
- Tests the mapping of event codes to DTMF characters

### Test 2: End Bit Detection
- Verifies that the end bit (bit 7 of e_r_volume field) is correctly detected
- Tests both end bit set (1) and not set (0) scenarios

### Test 3: Deduplication
- Verifies that duplicate packets with the same RTP timestamp are properly deduplicated
- Ensures only unique events are processed

### Test 4: All Event Codes
- Tests all 16 DTMF event codes (0-9, *, #, A-D)
- Verifies correct parsing for the complete DTMF range

### Test 5: Malformed Packet Handling
- Tests handling of packets that are too small (< 4 bytes payload)
- Tests handling of invalid event codes (> 15)
- Verifies the system doesn't crash on malformed input

## Running the Tests

### Via Web API

The tests can be triggered via the web interface. **Authentication is required** - you must be logged in to run the tests.

```bash
POST /api/rfc4733/test
```

**Response:**
```json
{
  "success": true,
  "message": "All RFC 4733 tests passed"
}
```

### Via cURL

You need to authenticate first and obtain a session cookie:

```bash
# Step 1: Login to get session cookie
curl -X POST https://doorstation-ip/api/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"your_password"}' \
  -c cookies.txt -k

# Step 2: Run the test with the session cookie
curl -X POST https://doorstation-ip/api/rfc4733/test \
  -b cookies.txt -k
```

**Note:** The `-k` flag is used to skip SSL certificate verification for self-signed certificates.

### Viewing Test Results

Test results are logged to the ESP32 console with the tag `RFC4733_TEST`. Each test reports:
- Test name and number
- PASS/FAIL status
- Detailed failure information if applicable
- Final summary with pass/fail count

## Test Implementation

The tests are implemented in:
- `main/rfc4733_test.c` - Test implementation
- `main/rfc4733_test.h` - Test interface
- `main/web_api.c` - Web API endpoint handler

## Requirements Verified

These tests verify the following requirements from the spec:

- **Requirement 8.1**: Correct event code extraction from RTP packets
- **Requirement 8.2**: End bit detection for complete DTMF digits
- **Requirement 8.3**: Deduplication to prevent double-detection
- **Requirement 8.4**: Malformed packet handling without crashes

## Expected Output

When all tests pass, you should see:

```
I (12345) RFC4733_TEST: === Starting RFC 4733 Packet Parsing Tests ===
I (12346) RFC4733_TEST: Test 1: Event code extraction
I (12347) RFC4733_TEST: PASS: Event code extraction works correctly
I (12348) RFC4733_TEST: Test 2: End bit detection
I (12349) RFC4733_TEST: PASS: End bit detection works correctly
I (12350) RFC4733_TEST: Test 3: Deduplication
I (12351) RFC4733_TEST: PASS: Deduplication logic verified
I (12352) RFC4733_TEST: Test 4: All DTMF event codes
I (12353) RFC4733_TEST: PASS: All 16 DTMF event codes verified
I (12354) RFC4733_TEST: Test 5: Malformed packet handling
I (12355) RFC4733_TEST: PASS: Malformed packet handling verified
I (12356) RFC4733_TEST: === Test Summary ===
I (12357) RFC4733_TEST: Passed: 5/5
I (12358) RFC4733_TEST: === ALL TESTS PASSED ===
```

## Notes

- **Authentication Required**: You must be logged in to the web interface to run these tests
- These are unit tests that verify packet parsing logic
- They do not require an active SIP call or RTP session
- Tests create synthetic RTP packets to verify parsing behavior
- The tests are safe to run at any time without affecting system operation
