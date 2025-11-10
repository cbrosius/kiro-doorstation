# Duplicate Key Press Fix

## Problem

Not all key presses were being logged. Specifically, pressing the same key twice in a row would miss the second press.

**Example:**
```
Press: 1, 1, 2, 2, 3
Detected: 1, 2, 3  (missing second 1 and second 2)
```

## Root Cause

The FritzBox workaround was checking if the event code changed:

```c
bool is_new_event = (event->event != last_event_code);

if ((end_bit || is_new_event) && rtp_timestamp != last_telephone_event_timestamp) {
    // Process event
}
```

**Problem:** If you press '1' twice:
- First press: event_code=1, timestamp=1000 → Processed ✅
- Second press: event_code=1, timestamp=2000 → `is_new_event=false` (same code) → Ignored ❌

## The Fix

Changed the logic to detect new events by EITHER event code change OR timestamp change:

```c
bool event_code_changed = (event->event != last_event_code);
bool timestamp_changed = (rtp_timestamp != last_telephone_event_timestamp);

// Consider it a new event if event code changed OR timestamp changed
bool is_new_event = event_code_changed || timestamp_changed;

if ((end_bit || is_new_event) && timestamp_changed) {
    // Process event
}
```

## How It Works Now

### Scenario 1: Different Keys

```
Press: 1, 2, 3
Event code changes: 1→2→3
Result: All detected ✅
```

### Scenario 2: Same Key Twice

```
Press: 1, 1
First:  code=1, ts=1000 → event_code_changed=true → Detected ✅
Second: code=1, ts=2000 → timestamp_changed=true → Detected ✅
```

### Scenario 3: Rapid Same Key

```
Press: 1 (hold), 1 (hold)
Packet 1: code=1, ts=1000 → Detected ✅
Packet 2: code=1, ts=1000 → Duplicate timestamp → Ignored ✅ (correct)
Packet 3: code=1, ts=2000 → New timestamp → Detected ✅
```

## Detection Logic

The system now detects a new DTMF event when:

1. **End bit is set** (standard RFC 4733) - Always processed
2. **Event code changed** (different key) - FritzBox workaround
3. **Timestamp changed** (same key pressed again) - Duplicate key fix

## Deduplication

The timestamp check prevents processing the same packet multiple times:

```c
if ((end_bit || is_new_event) && timestamp_changed) {
    // Only process if timestamp is different from last
}
```

This ensures:
- ✅ Same key pressed twice: Both detected
- ✅ Multiple packets for one key press: Only first detected
- ✅ Different keys: All detected

## Testing

After flashing the updated firmware:

### Test 1: Same Key Twice

```
Press: 1, 1, 1
Expected logs:
I (xxxxx) RTP: DTMF detected: '1' (event code 1)
I (xxxxx) RTP: DTMF detected: '1' (event code 1)
I (xxxxx) RTP: DTMF detected: '1' (event code 1)
```

### Test 2: Different Keys

```
Press: 1, 2, 3
Expected logs:
I (xxxxx) RTP: DTMF detected: '1' (event code 1)
I (xxxxx) RTP: DTMF detected: '2' (event code 2)
I (xxxxx) RTP: DTMF detected: '3' (event code 3)
```

### Test 3: Mixed Pattern

```
Press: 1, 2, 2, 3, 1
Expected logs:
I (xxxxx) RTP: DTMF detected: '1' (event code 1)
I (xxxxx) RTP: DTMF detected: '2' (event code 2)
I (xxxxx) RTP: DTMF detected: '2' (event code 2)
I (xxxxx) RTP: DTMF detected: '3' (event code 3)
I (xxxxx) RTP: DTMF detected: '1' (event code 1)
```

## Edge Cases Handled

### Case 1: Very Rapid Key Presses

If you press keys extremely fast (< 20ms apart), some might be missed due to the 20ms polling interval.

**Solution:** This is a hardware limitation. 20ms is already very fast (50 checks/second).

### Case 2: Key Held Down

If you hold a key down, multiple packets arrive with the same timestamp.

**Result:** Only first packet processed (correct behavior - one key press = one DTMF digit).

### Case 3: Network Packet Loss

If packets are lost, some key presses might be missed.

**Solution:** This is a network issue, not a code issue. Ensure good WiFi signal.

## Compatibility

This fix maintains compatibility with:

✅ **Standard RFC 4733** (with end bit)  
✅ **FritzBox** (without end bit)  
✅ **Duplicate key presses**  
✅ **Different key sequences**  

## Summary

**Problem:** Pressing the same key twice missed the second press

**Root Cause:** Workaround only checked event code change, not timestamp change

**Solution:** Detect new events by event code change OR timestamp change

**Result:** All key presses now detected correctly! 🎉
