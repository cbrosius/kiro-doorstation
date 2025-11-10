# Rate Limit Reset Guide

## What Happened

You're seeing:
```
W (xxxxx) DTMF: Rate limited - ignoring event
```

This means you've had **3 failed command attempts** during the current call, and the security system has blocked further DTMF input to prevent brute-force attacks.

## Why This Happened

The DTMF decoder has security features:
- **Max attempts**: 3 failed commands per call (default)
- **Rate limiting**: After 3 failures, all DTMF is blocked for the rest of the call
- **Reset on new call**: Counter resets when you hang up and call again

## Quick Fix: Reset Rate Limit

### Method 1: Hang Up and Call Again

1. **Hang up** the current call
2. **Wait a few seconds**
3. **Call again** - rate limit is reset
4. **Try your command** again

### Method 2: Check Your PIN Configuration

The rate limit was triggered because your commands were invalid. Check:

1. **Is PIN enabled?**
   - Go to web interface → DTMF Security settings
   - Check if PIN protection is enabled

2. **What's the correct command?**
   - **If PIN is disabled**: Use `*1#` (legacy command)
   - **If PIN is enabled**: Use `*[YOUR_PIN]#` (e.g., `*1234#`)

## Check Current Configuration

### Via Web Interface

1. Open `https://doorstation-ip/`
2. Go to **DTMF Security** settings
3. Check:
   - PIN enabled: Yes/No
   - PIN code: (if enabled)
   - Max attempts: 3 (default)

### Via API

```bash
curl -X GET https://doorstation-ip/api/dtmf/security \
  -H "Cookie: session_id=YOUR_SESSION" -k
```

Response:
```json
{
  "pin_enabled": false,
  "pin_code": "",
  "timeout_ms": 10000,
  "max_attempts": 3
}
```

## Configure PIN (If Needed)

### Disable PIN (Use Legacy Mode)

```bash
curl -X POST https://doorstation-ip/api/dtmf/security \
  -H "Content-Type: application/json" \
  -H "Cookie: session_id=YOUR_SESSION" \
  -d '{"pin_enabled": false}' -k
```

Then use command: `*1#`

### Enable PIN

```bash
curl -X POST https://doorstation-ip/api/dtmf/security \
  -H "Content-Type: application/json" \
  -H "Cookie: session_id=YOUR_SESSION" \
  -d '{"pin_enabled": true, "pin_code": "1234"}' -k
```

Then use command: `*1234#`

## Testing Commands

### Test 1: Legacy Command (No PIN)

1. **Disable PIN** in web interface
2. **Call doorstation**
3. **Press**: `*` `1` `#`
4. **Expected**: Door opens

### Test 2: PIN Command

1. **Enable PIN** and set to `1234`
2. **Call doorstation**
3. **Press**: `*` `1` `2` `3` `4` `#`
4. **Expected**: Door opens

### Test 3: Light Toggle

1. **Call doorstation**
2. **Press**: `*` `2` `#`
3. **Expected**: Light toggles (no PIN required)

## Understanding Failed Attempts

Commands that trigger failed attempts:

❌ `*5#` - Invalid command  
❌ `*9999#` - Wrong PIN  
❌ `*#` - Incomplete command  
❌ `123#` - Doesn't start with `*`  

Valid commands:

✅ `*1#` - Door open (legacy, no PIN)  
✅ `*1234#` - Door open (with PIN 1234)  
✅ `*2#` - Light toggle  

## Increase Max Attempts (Optional)

If you want to allow more attempts:

```bash
curl -X POST https://doorstation-ip/api/dtmf/security \
  -H "Content-Type: application/json" \
  -H "Cookie: session_id=YOUR_SESSION" \
  -d '{"max_attempts": 5}' -k
```

**Note:** Higher values reduce security.

## Check Security Logs

See what commands were attempted:

```bash
curl -X GET https://doorstation-ip/api/dtmf/logs \
  -H "Cookie: session_id=YOUR_SESSION" -k
```

This shows:
- All command attempts
- Which ones failed
- Why they failed (invalid_pin, invalid_command, etc.)

## Troubleshooting

### Issue: Rate Limited Immediately

**Cause:** Previous call had 3 failures, new call started but rate limit persists

**Solution:** Check logs - rate limit should reset on new call. If not, there's a bug.

### Issue: Don't Know My PIN

**Solution:** 
1. Access web interface
2. Go to DTMF Security
3. Either:
   - View current PIN
   - Set new PIN
   - Disable PIN

### Issue: Commands Still Don't Work

**Possible causes:**
1. Wrong PIN
2. Wrong command format
3. Timeout (10 seconds between first and last digit)

**Check logs:**
```
I (xxxxx) DTMF: Telephone-event received: '1' (code 1)
I (xxxxx) DTMF: Buffer: *1
I (xxxxx) DTMF: Command complete: *1#
W (xxxxx) DTMF: Invalid legacy command: *1
```

This tells you exactly what went wrong.

## Summary

**Problem:** Rate limited after 3 failed attempts

**Quick Fix:** Hang up and call again

**Proper Fix:** 
1. Check PIN configuration
2. Use correct command format
3. Test with `*1#` (no PIN) or `*[PIN]#` (with PIN)

**Your DTMF is working!** You just need to use the correct command format. 🎉
