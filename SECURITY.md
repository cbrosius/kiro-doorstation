# Security Documentation

## Overview

This document describes the security architecture of the ESP32 SIP Doorstation, with particular focus on why audio DTMF processing is not supported and the security measures implemented to protect against unauthorized access.

## Audio DTMF Replay Attack Vulnerability

### The Problem

Traditional audio DTMF (Dual-Tone Multi-Frequency) detection analyzes audio samples from the microphone to detect button presses. While this approach works, it creates a **critical security vulnerability** in doorstation applications:

**Attack Scenario:**
1. An authorized user calls the doorstation and enters the door code (e.g., "*1234#")
2. An attacker at the door records the audio conversation using a smartphone
3. The attacker extracts the DTMF tones from the recording
4. Later, the attacker calls the doorstation and plays back the recorded DTMF tones
5. The doorstation's microphone picks up the tones
6. The DTMF decoder processes the tones and opens the door
7. **Unauthorized access granted**

This is known as a **replay attack** and is a well-documented security vulnerability in systems that process commands from untrusted audio sources.

### Why This Is Serious

- **No authentication required**: Anyone with a recording can gain access
- **Easy to execute**: Smartphone apps can generate DTMF tones
- **Physical security breach**: Directly compromises building access control
- **Difficult to detect**: Appears as legitimate command in logs
- **Scalable attack**: One recording can be used repeatedly

### Real-World Impact

In a doorstation scenario:
- The microphone is physically accessible to anyone at the door
- Audio can be played directly at the microphone
- There is no way to distinguish between legitimate button presses from a phone and replayed audio
- The attack requires no technical sophistication

## Why Audio DTMF Is Not Supported

**This doorstation does not process audio DTMF tones for security reasons.**

The firmware has been designed to:
- ✅ **Accept commands only via RFC 4733 RTP telephone-events** (digital signaling)
- ❌ **Reject audio DTMF tones** played at the doorstation microphone
- ✅ **Prevent replay attacks** by processing only authenticated RTP streams

### Technical Implementation

The doorstation processes DTMF commands from the **RTP stream** (network packets), not from the **audio stream** (microphone input):

```
┌─────────────────────────────────────────────────────────┐
│  Caller's Phone                                          │
│  • User presses button (e.g., "1")                      │
│  • Phone sends RFC 4733 packet over network             │
└────────────────────┬────────────────────────────────────┘
                     │
                     │ RTP Packet (Payload Type 101)
                     │ Encrypted/Authenticated by SIP
                     ▼
┌─────────────────────────────────────────────────────────┐
│  Doorstation                                             │
│  • Receives RFC 4733 packet from network                │
│  • Processes digital event (not audio)                  │
│  • Executes command if valid                            │
│                                                          │
│  ❌ Microphone audio is NOT processed for DTMF          │
└─────────────────────────────────────────────────────────┘
```

**Key Security Properties:**
- Commands come from the authenticated SIP session, not from ambient audio
- Attacker at the door cannot inject commands by playing sounds
- Audio recorded from previous calls cannot be replayed to gain access
- The RTP stream is part of the authenticated SIP call

## RFC 4733 Compatibility

### What Is RFC 4733?

RFC 4733 (formerly RFC 2833) is the IETF standard for transmitting DTMF digits and other telephony signals as RTP packets. Instead of sending audio tones, the phone sends digital event codes over the network.

**Advantages over audio DTMF:**
- ✅ **Secure**: Not vulnerable to replay attacks
- ✅ **Reliable**: Not affected by codec compression or audio quality
- ✅ **Fast**: Lower latency than audio tone detection
- ✅ **Efficient**: Less CPU usage than FFT/Goertzel analysis
- ✅ **Standard**: Supported by all modern SIP devices

### Device Compatibility

RFC 4733 has been an industry standard since **2006**. All modern SIP devices support it:

#### ✅ Supported Devices (Partial List)

**DECT Base Stations:**
- Gigaset N510, N670, N870 IP DECT bases
- Yealink W60B, W70B, W80B DECT bases
- Snom M series DECT bases
- Panasonic KX-TGP600 DECT base
- AVM Fritz!Box with DECT

**IP Phones:**
- Cisco SPA series
- Yealink T series
- Grandstream GXP series
- Snom D series
- Polycom VVX series

**Softphones:**
- Linphone (desktop and mobile)
- Zoiper
- Bria
- MicroSIP
- Groundwire (iOS)
- CSipSimple (Android)

**PBX Systems:**
- Asterisk
- FreeSWITCH
- Kamailio
- 3CX
- All major hosted VoIP providers

#### ❌ Incompatible Devices

Devices that **only** support in-band audio DTMF:
- Very old SIP phones (pre-2010)
- Analog phones connected via very old ATAs (pre-2008)
- Custom/proprietary implementations that don't follow standards

**These devices are extremely rare in modern deployments.**

### How to Check Your Device

1. **Check device specifications**: Look for "RFC 4733", "RFC 2833", or "RTP telephone-event" support
2. **Check SIP INVITE**: Look for `a=rtpmap:101 telephone-event/8000` in the SDP
3. **Test with this doorstation**: If DTMF commands work, your device supports RFC 4733

## Guidance for Legacy Equipment

If you have a device that only supports in-band audio DTMF:

### Option 1: Firmware Update
Many older devices gained RFC 4733 support through firmware updates. Check the manufacturer's website for the latest firmware.

### Option 2: Replace the Device
Modern SIP devices are affordable and provide better security:
- **DECT bases**: €50-150 (Gigaset N510, Yealink W60B)
- **IP phones**: €40-100 (Yealink T31P, Grandstream GXP1625)
- **Softphones**: Free (Linphone, Zoiper)

### Option 3: Use a Different Phone
If you have multiple phones, use one that supports RFC 4733 for doorstation access.

### Why We Won't Add Audio DTMF Support

The security risk of replay attacks outweighs the benefit of supporting 15+ year old equipment. Adding audio DTMF would:
- ❌ Create a critical security vulnerability
- ❌ Allow unauthorized physical access to buildings
- ❌ Undermine all other security measures
- ❌ Violate security best practices

**Security cannot be compromised for backward compatibility.**

## Implemented Security Features

Beyond removing audio DTMF, this doorstation implements multiple layers of security:

### 1. PIN Code Protection

**Feature**: Configurable PIN codes for door access

**Configuration**:
```json
{
  "pin_enabled": true,
  "pin_code": "1234"
}
```

**Usage**:
- Without PIN: `*1#` opens the door (legacy mode)
- With PIN: `*1234#` opens the door (PIN mode)

**Security Properties**:
- PIN stored in encrypted NVS storage
- Constant-time comparison prevents timing attacks
- PIN length: 1-8 digits
- Can be changed via web interface

### 2. Command Timeout

**Feature**: Commands must be completed within a time window

**Configuration**:
```json
{
  "timeout_ms": 10000
}
```

**Behavior**:
- Timer starts on first DTMF digit
- Command must be completed within timeout period (default: 10 seconds)
- Partial commands are cleared after timeout
- Prevents slow brute-force attacks

**Security Properties**:
- Limits attack window
- Prevents command buffer pollution
- Automatic cleanup of incomplete sequences

### 3. Rate Limiting

**Feature**: Maximum failed attempts per call

**Configuration**:
```json
{
  "max_attempts": 3
}
```

**Behavior**:
- Failed attempts are counted per call
- After max attempts, all further commands are blocked
- Counter resets on new call
- Security alert logged when triggered

**Security Properties**:
- Prevents brute-force PIN guessing
- Limits damage from compromised credentials
- Forces attacker to make multiple calls (more detectable)

### 4. Audit Logging

**Feature**: All access attempts are logged with timestamps

**Log Entry Example**:
```json
{
  "timestamp": 1697462400000,
  "type": "success",
  "command": "*1234#",
  "action": "door_open",
  "caller": "sip:user@domain.com"
}
```

**Logged Events**:
- ✅ Successful door openings
- ❌ Failed PIN attempts
- 🚨 Rate limiting triggers
- ⏱️ Command timeouts

**Security Properties**:
- NTP-synchronized timestamps for accurate forensics
- Caller ID tracking for accountability
- Accessible via web interface
- Last 50 events retained in memory

### 5. Call State Validation

**Feature**: Commands only accepted during active calls

**Behavior**:
- DTMF processing only active in CONNECTED state
- All state cleared when call ends
- No command processing outside of calls

**Security Properties**:
- Prevents out-of-band attacks
- Ensures caller authentication via SIP
- Automatic cleanup on disconnect

## Security Best Practices

### For Administrators

1. **Enable PIN protection**: Don't rely on `*1#` alone
2. **Use strong PINs**: Avoid sequential digits (1234) or repeated digits (1111)
3. **Change PINs regularly**: Especially if shared with multiple users
4. **Monitor logs**: Review access attempts regularly
5. **Use SIP authentication**: Ensure your SIP server requires registration
6. **Enable TLS/SRTP**: Encrypt SIP signaling and RTP media (if supported)

### For Users

1. **Don't share PINs**: Each user should have their own code (if supported by your setup)
2. **Don't write down PINs**: Memorize your access code
3. **Be aware of surroundings**: Don't enter PIN if someone is watching/recording
4. **Report suspicious activity**: Check logs if you suspect unauthorized access

### For Integrators

1. **Use modern equipment**: Ensure all devices support RFC 4733
2. **Test thoroughly**: Verify DTMF works before deployment
3. **Document configuration**: Keep records of PIN codes and settings
4. **Plan for updates**: Ensure firmware can be updated remotely
5. **Consider additional security**: Add video verification, time-based access, etc.

## Threat Model

### In Scope

This doorstation protects against:
- ✅ Audio replay attacks (DTMF tones played at microphone)
- ✅ Brute-force PIN guessing (rate limiting)
- ✅ Unauthorized access attempts (audit logging)
- ✅ Timing attacks on PIN validation (constant-time comparison)

### Out of Scope

This doorstation does NOT protect against:
- ❌ Compromised SIP credentials (use strong passwords)
- ❌ Man-in-the-middle attacks (use TLS/SRTP)
- ❌ Physical attacks on the device (use tamper-resistant enclosure)
- ❌ Social engineering (user education required)
- ❌ Network-level attacks (use firewall and VLANs)

### Assumptions

This security model assumes:
- SIP server provides authentication
- Network is reasonably secure (not public internet)
- Physical device is in a protected location
- Users follow basic security practices

## Reporting Security Issues

If you discover a security vulnerability in this doorstation:

1. **Do not disclose publicly** until a fix is available
2. **Contact the maintainer** via private channel
3. **Provide details**: Steps to reproduce, impact assessment
4. **Allow time for fix**: Coordinate disclosure timeline

## Compliance and Standards

This implementation follows:
- **RFC 4733**: RTP Payload for DTMF Digits, Telephony Tones, and Telephony Signals
- **RFC 3261**: SIP: Session Initiation Protocol
- **RFC 3550**: RTP: A Transport Protocol for Real-Time Applications
- **OWASP**: Secure coding practices for embedded systems

## Changelog

### Version 2.0 (Current)
- ✅ Removed audio DTMF processing (security fix)
- ✅ Implemented RFC 4733 telephone-event processing
- ✅ Added PIN code protection
- ✅ Added command timeout
- ✅ Added rate limiting
- ✅ Added audit logging

### Version 1.0 (Legacy)
- ❌ Audio DTMF processing (vulnerable to replay attacks)
- ❌ No PIN protection
- ❌ No rate limiting
- ❌ Limited logging

**Upgrade strongly recommended for all deployments.**

## References

- [RFC 4733 - RTP Payload for DTMF Digits](https://tools.ietf.org/html/rfc4733)
- [RFC 3261 - SIP: Session Initiation Protocol](https://tools.ietf.org/html/rfc3261)
- [OWASP Embedded Application Security](https://owasp.org/www-project-embedded-application-security/)
- [NIST Guidelines on Securing VoIP](https://csrc.nist.gov/publications/detail/sp/800-58/final)

## License

This security documentation is provided as-is for informational purposes. The maintainers make no warranties about the security of this implementation. Use at your own risk.
