# SIP INFO DTMF Troubleshooting Guide

## Overview

The doorstation already has **full support for SIP INFO DTMF** (RFC 2976) in addition to RFC 4733 RTP telephone-events. This guide helps you troubleshoot why DTMF might not be working.

## What's Already Implemented

The doorstation supports **two methods** for receiving DTMF:

1. **RFC 4733 (RTP Telephone-Events)** - DTMF sent as special RTP packets (payload type 101)
2. **RFC 2976 (SIP INFO)** - DTMF sent as SIP INFO messages with `application/dtmf-relay` content

Both methods are processed through the same `dtmf_process_telephone_event()` function, so they work identically.

## Checking What Your Phone is Sending

### Step 1: Check SIP Logs

Look at the SIP logs in the web interface (`/api/sip/log`) or ESP32 console for these messages:

**If using RFC 4733 (RTP):**
```
I (xxxxx) RTP: RTP packet received: payload_type=101
I (xxxxx) RTP: Telephone-event: code=X, end=1
I (xxxxx) RTP: DTMF detected: 'X' (event code X)
```

**If using SIP INFO:**
```
I (xxxxx) SIP: INFO request received
I (xxxxx) SIP: Processing DTMF relay INFO message
I (xxxxx) SIP: DTMF relay: Signal=X, Duration=XXX
I (xxxxx) SIP: DTMF from INFO: X
I (xxxxx) SIP: DTMF processed via RFC 2976 INFO method
```

### Step 2: Check RTP Logs

Enable verbose RTP logging to see all packets:

```
I (xxxxx) RTP: RTP packet received: payload_type=0, payload_size=160
```

- **payload_type=0 or 8** = Audio (PCMU/PCMA)
- **payload_type=101** = RFC 4733 telephone-events

If you only see payload_type=0 or 8, your phone is NOT sending RFC 4733.

### Step 3: Check for INFO Messages

If you see:
```
I (xxxxx) SIP: RFC 2976 INFO method received
```

Your phone IS sending SIP INFO messages.

## Common Issues and Solutions

### Issue 1: No DTMF Messages at All

**Symptoms:**
- No RTP payload_type=101 packets
- No SIP INFO messages
- Pressing keys on phone does nothing

**Possible Causes:**
1. Phone is using **in-band DTMF** (audio tones) - NOT SUPPORTED for security reasons
2. Phone DTMF is disabled
3. Phone is not in a connected call state

**Solutions:**
1. Check phone DTMF settings:
   - Look for "DTMF Mode" or "DTMF Type" setting
   - Enable "RFC 2833" or "RTP DTMF" (for RFC 4733)
   - OR enable "SIP INFO" (for RFC 2976)
   - Disable "In-band" or "Audio" DTMF

2. Verify call is connected:
   - DTMF only works during an active call
   - Check SIP state is "CONNECTED"

### Issue 2: Phone Sends INFO but Wrong Content-Type

**Symptoms:**
```
I (xxxxx) SIP: INFO method not supported for non-DTMF purposes
I (xxxxx) SIP: 501 Not Implemented response sent
```

**Cause:** Phone is sending INFO with wrong Content-Type (not `application/dtmf-relay`)

**Solution:** Check phone settings for DTMF content type. Some phones support:
- `application/dtmf-relay` (standard, supported)
- `application/dtmf` (also supported by doorstation)

### Issue 3: SDP Negotiation Issue

**Symptoms:**
- Call connects but no DTMF works
- Phone doesn't know doorstation supports INFO or RFC 4733

**Check SDP in Call Setup:**

The doorstation advertises:
```
Allow: INVITE, ACK, BYE, CANCEL, OPTIONS, INFO
Accept: application/sdp, application/dtmf-relay
a=rtpmap:101 telephone-event/8000
a=fmtp:101 0-15
```

If your phone doesn't see these, there may be a SIP proxy/firewall issue.

### Issue 4: DTMF Received but Not Processed

**Symptoms:**
```
I (xxxxx) SIP: DTMF from INFO: 5
```
But no command execution.

**Possible Causes:**
1. Not in CONNECTED state
2. Rate limiting triggered (3 failed attempts)
3. Invalid command sequence
4. Command timeout

**Check DTMF Decoder Logs:**
```
I (xxxxx) DTMF: Telephone-event received: '5' (code 5)
I (xxxxx) DTMF: Buffer: *5
I (xxxxx) DTMF: Command complete: *1234#
I (xxxxx) DTMF: Valid PIN - door opener authorized
```

## Testing DTMF Methods

### Test RFC 4733 (RTP)

1. Make a call to the doorstation
2. Press a key on your phone
3. Look for: `RTP: DTMF detected: 'X'`

### Test SIP INFO

1. Make a call to the doorstation
2. Press a key on your phone
3. Look for: `SIP: DTMF from INFO: X`

### Manual Test with cURL

You can manually send a SIP INFO message to test:

```bash
# This requires the doorstation to be in a call
# Replace with your actual Call-ID, From, To, etc.

curl -X POST http://doorstation-ip:5060 \
  -H "Content-Type: text/plain" \
  --data-binary "INFO sip:doorstation@192.168.1.100 SIP/2.0
Via: SIP/2.0/UDP 192.168.1.50:5060;branch=z9hG4bKtest
From: <sip:phone@192.168.1.50>;tag=test123
To: <sip:doorstation@192.168.1.100>;tag=doorbell456
Call-ID: test-call-id@192.168.1.50
CSeq: 1 INFO
Content-Type: application/dtmf-relay
Content-Length: 24

Signal=5
Duration=160"
```

## Phone Configuration Examples

### FritzBox with Fritz!Fon (DECT)
**See detailed guide: FRITZBOX_DTMF_SETUP.md**

Quick setup:
1. Register doorstation as SIP device on FritzBox
2. Assign internal number (e.g., **621)
3. FritzBox handles DTMF conversion automatically
4. Ensure RFC 2833 or SIP INFO is enabled in FritzBox SIP trunk settings

**Note:** Fritz!Fon phones don't have user DTMF settings - FritzBox controls this.

### Gigaset DECT Phones
- Menu → Settings → Telephony → DTMF
- Set to "RFC 2833" or "SIP INFO"

### Yealink IP Phones
- Web Interface → Account → Advanced
- DTMF Type: "RFC2833" or "SIP INFO"

### Linphone (Softphone)
- Settings → Network → DTMF
- Enable "Use RFC 2833" or "Use SIP INFO"

### Grandstream Phones
- Web Interface → Account → Call Settings
- DTMF: "in audio" (disable), "via RTP (RFC2833)" (enable), or "via SIP INFO" (enable)

## Verification Checklist

- [ ] Phone DTMF mode is set to RFC 2833 or SIP INFO (NOT in-band)
- [ ] Call is in CONNECTED state
- [ ] SIP logs show INFO messages or RTP payload_type=101
- [ ] DTMF decoder logs show events being received
- [ ] No rate limiting active (< 3 failed attempts)
- [ ] Valid command sequence (e.g., *1234# with correct PIN)

## Security Notes

**Why In-Band DTMF is Disabled:**

In-band DTMF (audio tones) is disabled for security. Anyone at the door could play recorded DTMF tones to gain unauthorized access. Only RFC 4733 and SIP INFO are supported because:

1. They're sent as signaling data, not audio
2. They can't be replayed by playing sounds at the microphone
3. They're the standard methods used by all modern SIP devices

## Getting More Help

If DTMF still doesn't work:

1. Enable verbose logging on your phone
2. Capture SIP traffic with Wireshark
3. Check the doorstation SIP logs for any error messages
4. Verify your phone firmware is up to date
5. Try a different DTMF mode (switch between RFC 2833 and SIP INFO)

## Summary

The doorstation **already supports both RFC 4733 and SIP INFO DTMF**. If keys aren't working:

1. Check your phone's DTMF settings
2. Verify the call is connected
3. Look at the logs to see what (if anything) is being received
4. Make sure you're not using in-band DTMF

The implementation is complete and working - the issue is likely in the phone configuration.
