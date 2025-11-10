# FritzBox DTMF Configuration Guide

## Overview

FritzBox routers act as SIP proxies and can modify DTMF signaling between your DECT phone and the doorstation. This guide helps you configure FritzBox to properly pass DTMF signals.

## The Problem

When using a FritzBox as a SIP server/proxy:
- Your DECT phone (Fritz!Fon) sends DTMF to the FritzBox
- FritzBox must relay DTMF to the doorstation
- FritzBox may convert DTMF format (RFC 2833 ↔ SIP INFO ↔ In-band)

## FritzBox DTMF Settings

### Step 1: Access FritzBox Web Interface

1. Open browser to `http://fritz.box` or `http://192.168.178.1`
2. Login with your FritzBox password
3. Go to **Telephony** → **Telephony Devices**

### Step 2: Configure SIP Trunk (Doorstation)

If you've configured the doorstation as a SIP device on the FritzBox:

1. Go to **Telephony** → **Telephony Devices**
2. Find your doorstation entry
3. Click **Edit**
4. Look for DTMF settings (may be under "Advanced" or "Expert Settings")

**Recommended Settings:**
- **DTMF Method**: RFC 2833 (preferred) or SIP INFO
- **Do NOT use**: In-band DTMF

### Step 3: Check FritzBox Telephony Settings

1. Go to **Telephony** → **Telephony Connections**
2. Check your SIP trunk settings
3. Ensure DTMF passthrough is enabled

### Step 4: Fritz!Fon (DECT Phone) Settings

Unfortunately, Fritz!Fon phones don't have user-accessible DTMF settings. The FritzBox controls how DTMF is sent.

## Common FritzBox Scenarios

### Scenario 1: Doorstation Registered Directly to FritzBox

**Setup:**
```
Fritz!Fon (DECT) → FritzBox → Doorstation (SIP device on FritzBox)
```

**Configuration:**
1. Register doorstation as SIP device on FritzBox
2. Assign internal number (e.g., **621)
3. FritzBox will handle DTMF conversion automatically
4. Usually works with RFC 2833 by default

**Testing:**
- Call **621 from your Fritz!Fon
- Press keys - FritzBox should relay as RFC 2833 or SIP INFO

### Scenario 2: Doorstation as External SIP Server

**Setup:**
```
Fritz!Fon (DECT) → FritzBox → Internet/LAN → Doorstation (external SIP)
```

**Configuration:**
1. Configure doorstation IP as external SIP trunk
2. Set up call routing rules
3. May need to enable "SIP ALG" or "SIP Helper" in FritzBox

### Scenario 3: Direct SIP Registration (Bypass FritzBox)

**Setup:**
```
Fritz!Fon (DECT) → FritzBox (network only)
Doorstation registers directly to external SIP provider
```

**Note:** This won't work for DTMF from Fritz!Fon to doorstation since they're on different SIP servers.

## Debugging FritzBox DTMF Issues

### Check FritzBox Call Logs

1. Go to **Telephony** → **Call List**
2. Make a test call to doorstation
3. Check if call connects successfully

### Enable FritzBox SIP Trace (Advanced)

FritzBox has hidden debug features:

1. Dial `#96*5*` on your Fritz!Fon to enable SIP trace
2. Make test call and press keys
3. Dial `#96*4*` to disable trace
4. Download trace from FritzBox web interface

**Note:** This is an advanced feature and may not be available on all models.

### Check Doorstation Logs

While connected to doorstation, check ESP32 console or web logs for:

```
I (xxxxx) SIP: INFO request received
I (xxxxx) SIP: DTMF via INFO: signal='5', duration=160 ms
```

OR

```
I (xxxxx) RTP: RTP packet received: payload_type=101
I (xxxxx) RTP: DTMF detected: '5' (event code 5)
```

## FritzBox DTMF Conversion

FritzBox can convert between DTMF methods:

| From Phone | FritzBox Sends | Doorstation Receives |
|------------|----------------|---------------------|
| RFC 2833   | RFC 2833       | ✅ Works            |
| RFC 2833   | SIP INFO       | ✅ Works            |
| In-band    | RFC 2833       | ✅ Works            |
| In-band    | SIP INFO       | ✅ Works            |

**The doorstation supports both RFC 2833 and SIP INFO**, so FritzBox conversion should work.

## Common Issues and Solutions

### Issue 1: No DTMF Received at All

**Symptoms:**
- Keys pressed on Fritz!Fon
- No logs on doorstation
- No DTMF events

**Possible Causes:**
1. FritzBox not relaying DTMF
2. Call not properly established
3. FritzBox using in-band DTMF (audio tones) - doorstation ignores these for security

**Solutions:**
1. Check FritzBox SIP device settings for doorstation
2. Try calling doorstation from a different phone (mobile SIP app)
3. Check if doorstation is in CONNECTED state during call

### Issue 2: FritzBox Sends In-Band DTMF

**Symptoms:**
- Doorstation logs show only audio packets (payload_type=0 or 8)
- No RFC 2833 or SIP INFO messages

**Cause:** FritzBox configured to send DTMF as audio tones

**Solution:**
- Change FritzBox SIP trunk settings to RFC 2833 or SIP INFO
- This is a security requirement - in-band DTMF is disabled on doorstation

### Issue 3: SIP INFO with Wrong Content-Type

**Symptoms:**
```
I (xxxxx) SIP: INFO method not supported for non-DTMF purposes
```

**Cause:** FritzBox sending INFO with wrong Content-Type

**Solution:**
- Check FritzBox SIP trunk configuration
- Ensure it's set to send `application/dtmf-relay`

### Issue 4: NAT/Firewall Issues

**Symptoms:**
- Call connects but no audio or DTMF
- One-way audio

**Solutions:**
1. Enable STUN on doorstation
2. Configure port forwarding on FritzBox (UDP 5060 for SIP, UDP 10000-20000 for RTP)
3. Check FritzBox firewall settings
4. Enable "SIP ALG" or "SIP Helper" in FritzBox (under Internet → Permit Access)

## Recommended FritzBox Configuration

### For Best DTMF Compatibility:

1. **Register doorstation as SIP device on FritzBox**
   - Telephony → Telephony Devices → Configure New Device
   - Choose "Telephone (SIP)"
   - Enter doorstation IP and credentials

2. **Assign internal number** (e.g., **621)

3. **Set DTMF to RFC 2833** (if option available)

4. **Enable SIP ALG/Helper**
   - Internet → Permit Access → SIP ALG (enable)

5. **Test from Fritz!Fon**
   - Call **621
   - Press keys
   - Check doorstation logs

## Testing Without FritzBox

To verify the doorstation works independently:

1. Use a SIP softphone (Linphone, Zoiper) on your computer/phone
2. Configure it to register directly to doorstation
3. Make a call and test DTMF
4. This confirms doorstation DTMF works

If it works without FritzBox but not through FritzBox, the issue is FritzBox configuration.

## FritzBox Models and DTMF Support

| Model | RFC 2833 | SIP INFO | Notes |
|-------|----------|----------|-------|
| FritzBox 7590 | ✅ | ✅ | Full support |
| FritzBox 7530 | ✅ | ✅ | Full support |
| FritzBox 7490 | ✅ | ✅ | Full support |
| FritzBox 7430 | ✅ | ✅ | Full support |
| FritzBox 7390 | ✅ | ⚠️ | May need firmware update |
| Older models | ⚠️ | ⚠️ | Check firmware version |

**Recommendation:** Update FritzBox to latest firmware (Fritz!OS 7.x or newer)

## Quick Diagnostic Steps

1. **Make a call** from Fritz!Fon to doorstation
2. **Check doorstation state**: Should show "CONNECTED" in logs
3. **Press a key** (e.g., 5)
4. **Check doorstation logs** for:
   - `INFO request received` (SIP INFO method)
   - OR `RTP: payload_type=101` (RFC 2833 method)
5. **If nothing appears**: FritzBox is not relaying DTMF correctly

## Getting Help

If DTMF still doesn't work:

1. Check FritzBox firmware version (should be 7.x or newer)
2. Try registering doorstation directly (bypass FritzBox)
3. Test with a different SIP client (softphone)
4. Check FritzBox support forums for your specific model
5. Consider using doorstation's direct SIP registration instead of through FritzBox

## Summary

- **FritzBox acts as SIP proxy** between Fritz!Fon and doorstation
- **Doorstation supports both RFC 2833 and SIP INFO** - FritzBox should work
- **Most common issue**: FritzBox not configured to relay DTMF properly
- **Solution**: Register doorstation as SIP device on FritzBox with RFC 2833 enabled
- **Test**: Check doorstation logs to see if DTMF messages arrive

The doorstation implementation is complete and working. The issue is likely FritzBox configuration or DTMF relay settings.
