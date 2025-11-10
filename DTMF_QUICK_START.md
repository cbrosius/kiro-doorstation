# DTMF Quick Start Guide

## Your Setup: FritzBox + Fritz!Fon DECT

### What You Need to Know

✅ **Your doorstation already supports DTMF** via:
- RFC 4733 (RTP telephone-events)
- SIP INFO (RFC 2976)

✅ **No code changes needed** - implementation is complete

⚠️ **The issue is likely FritzBox configuration**

## Quick Fix Steps

### Step 1: Check How Doorstation is Connected

**Option A: Doorstation registered to FritzBox as SIP device**
```
Fritz!Fon → FritzBox → Doorstation (internal **621)
```
This is the recommended setup.

**Option B: Doorstation as external SIP server**
```
Fritz!Fon → FritzBox → Network → Doorstation
```
May need additional configuration.

### Step 2: Configure FritzBox

1. Open `http://fritz.box`
2. Go to **Telephony** → **Telephony Devices**
3. Find your doorstation (or add it as new SIP device)
4. Ensure DTMF is set to **RFC 2833** or **SIP INFO** (NOT in-band)

### Step 3: Test

1. Call doorstation from Fritz!Fon
2. Wait for call to connect (you should hear audio)
3. Press a key (e.g., 5)
4. Check doorstation logs

### Step 4: Check Logs

Open doorstation web interface → SIP Logs, look for:

**If using RFC 4733:**
```
RTP packet received: payload_type=101
DTMF detected: '5' (event code 5)
```

**If using SIP INFO:**
```
INFO request received
DTMF via INFO: signal='5', duration=160 ms
```

**If you see nothing:**
- FritzBox is not relaying DTMF
- Check FritzBox SIP device settings
- Ensure call is in CONNECTED state

## Common FritzBox Issues

### Issue: FritzBox sends in-band DTMF (audio tones)

**Symptom:** Only see `RTP packet received: payload_type=0` (audio)

**Fix:** Change FritzBox SIP trunk to RFC 2833 or SIP INFO

### Issue: Call connects but no DTMF

**Symptom:** Can talk, but keys don't work

**Fix:** 
1. Check doorstation is in CONNECTED state
2. Verify FritzBox DTMF relay is enabled
3. Try different DTMF mode in FritzBox

### Issue: One-way audio or no audio

**Symptom:** Can't hear doorstation or doorstation can't hear you

**Fix:**
1. Check NAT/firewall settings
2. Enable STUN on doorstation
3. Enable SIP ALG in FritzBox

## Testing Without FritzBox

To verify doorstation works:

1. Install Linphone on your phone/computer
2. Configure to register directly to doorstation IP
3. Make call and test DTMF
4. If it works → FritzBox is the issue
5. If it doesn't work → Check doorstation configuration

## What to Check Right Now

Run through this checklist:

- [ ] Doorstation is registered as SIP device on FritzBox
- [ ] Doorstation has internal number assigned (e.g., **621)
- [ ] Can make call from Fritz!Fon to doorstation
- [ ] Call shows CONNECTED state in doorstation logs
- [ ] FritzBox DTMF is set to RFC 2833 or SIP INFO (not in-band)
- [ ] Pressed a key during call
- [ ] Checked doorstation logs for INFO or RTP payload_type=101

## Next Steps

1. **Read detailed guide:** `FRITZBOX_DTMF_SETUP.md`
2. **Check troubleshooting:** `SIP_INFO_DTMF_TROUBLESHOOTING.md`
3. **Test RFC 4733:** `RFC4733_TEST_GUIDE.md`

## Quick Diagnostic

Make a call and press 5, then check logs:

**Good (RFC 4733):**
```
I (12345) RTP: RTP packet received: payload_type=101, payload_size=4
I (12346) RTP: Telephone-event: code=5, end=1, volume=10, duration=160
I (12347) RTP: DTMF detected: '5' (event code 5)
I (12348) DTMF: Telephone-event received: '5' (code 5)
```

**Good (SIP INFO):**
```
I (12345) SIP: INFO request received
I (12346) SIP: DTMF via INFO: signal='5', duration=160 ms
I (12347) SIP: 200 OK response to INFO
I (12348) DTMF: Telephone-event received: '5' (code 5)
```

**Bad (No DTMF):**
```
I (12345) RTP: RTP packet received: payload_type=0, payload_size=160
I (12346) RTP: RTP packet received: payload_type=0, payload_size=160
```
Only audio packets, no DTMF → FritzBox not relaying DTMF

## Summary

- ✅ Doorstation supports both RFC 4733 and SIP INFO
- ✅ Implementation is complete and tested
- ⚠️ FritzBox must be configured to relay DTMF
- 🔧 Check FritzBox SIP device settings for doorstation
- 📝 Look at doorstation logs to see what's being received

**Most likely fix:** Configure FritzBox to send RFC 2833 or SIP INFO instead of in-band DTMF.
