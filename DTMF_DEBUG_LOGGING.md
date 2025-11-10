# DTMF Debug Logging Added

## Problem

DTMF packets were being received (payload_type=114) but not processed:

```
I (27705) RTP: RTP packet: payload_type=114, payload_size=4
I (29619) RTP: RTP packet: payload_type=114, payload_size=4
```

No "DTMF detected" or "Telephone-event" messages appeared.

## Debug Logging Added

Added comprehensive logging to trace the packet processing flow:

### 1. Payload Type Detection

```c
ESP_LOGI(TAG, "Detected telephone-event packet: payload_type=%d, size=%zu, expected_size=%zu", 
         payload_type, payload_size, sizeof(rtp_telephone_event_t));
```

Shows when a telephone-event packet is detected.

### 2. Processing Start

```c
ESP_LOGI(TAG, "Processing telephone-event...");
```

Confirms the packet is being routed to the telephone-event processor.

### 3. Event Details

```c
ESP_LOGI(TAG, "Telephone-event: code=%d, end=%d, volume=%d, duration=%d, ts=%u", 
         event->event, end_bit, volume, duration, rtp_timestamp);
```

Shows the parsed telephone-event fields:
- **code**: DTMF digit (0-15)
- **end**: End bit (1 = key released, 0 = key still pressed)
- **volume**: Signal volume
- **duration**: How long the key has been pressed
- **ts**: RTP timestamp for deduplication

### 4. End Bit Check

```c
if (!end_bit) {
    ESP_LOGD(TAG, "Telephone-event: end bit not set (ongoing event)");
}
```

RFC 4733 sends multiple packets while a key is pressed. Only the **last packet** (with end bit set) triggers DTMF processing.

### 5. Deduplication Check

```c
ESP_LOGD(TAG, "Telephone-event: duplicate timestamp %u (last=%u)", rtp_timestamp, last_telephone_event_timestamp);
```

Prevents processing the same event multiple times.

### 6. DTMF Detection

```c
ESP_LOGI(TAG, "DTMF detected: '%c' (event code %d)", dtmf_char, event->event);
```

Shows when a DTMF digit is successfully detected.

### 7. Callback Check

```c
if (telephone_event_callback) {
    ESP_LOGI(TAG, "Calling telephone_event_callback with event %d", event->event);
    telephone_event_callback(event->event);
} else {
    ESP_LOGW(TAG, "No telephone_event_callback registered!");
}
```

Verifies the callback is registered and being called.

## What to Look For

### Normal Operation (Key Press)

When you press a key, you should see:

```
I (xxxxx) RTP: RTP packet: payload_type=114, payload_size=4
I (xxxxx) RTP: Detected telephone-event packet: payload_type=114, size=4, expected_size=4
I (xxxxx) RTP: Processing telephone-event...
I (xxxxx) RTP: Telephone-event: code=5, end=0, volume=10, duration=160, ts=12345
D (xxxxx) RTP: Telephone-event: end bit not set (ongoing event)

I (xxxxx) RTP: RTP packet: payload_type=114, payload_size=4
I (xxxxx) RTP: Detected telephone-event packet: payload_type=114, size=4, expected_size=4
I (xxxxx) RTP: Processing telephone-event...
I (xxxxx) RTP: Telephone-event: code=5, end=0, volume=10, duration=320, ts=12345
D (xxxxx) RTP: Telephone-event: end bit not set (ongoing event)

I (xxxxx) RTP: RTP packet: payload_type=114, payload_size=4
I (xxxxx) RTP: Detected telephone-event packet: payload_type=114, size=4, expected_size=4
I (xxxxx) RTP: Processing telephone-event...
I (xxxxx) RTP: Telephone-event: code=5, end=1, volume=10, duration=480, ts=12345
I (xxxxx) RTP: DTMF detected: '5' (event code 5)
I (xxxxx) RTP: Calling telephone_event_callback with event 5
I (xxxxx) DTMF: Telephone-event received: '5' (code 5)
```

### Possible Issues

#### Issue 1: End Bit Never Set

```
I (xxxxx) RTP: Telephone-event: code=5, end=0, volume=10, duration=160, ts=12345
D (xxxxx) RTP: Telephone-event: end bit not set (ongoing event)
```

**Cause:** Phone/FritzBox not sending end bit  
**Solution:** Check phone DTMF settings

#### Issue 2: Callback Not Registered

```
I (xxxxx) RTP: DTMF detected: '5' (event code 5)
W (xxxxx) RTP: No telephone_event_callback registered!
```

**Cause:** `dtmf_decoder_init()` not called or callback not set  
**Solution:** Check initialization order

#### Issue 3: Wrong Payload Size

```
I (xxxxx) RTP: RTP packet: payload_type=114, payload_size=8
W (xxxxx) RTP: Payload type 114 but size 8 != 4 (expected telephone-event)
```

**Cause:** Not a telephone-event packet  
**Solution:** Check SDP negotiation

#### Issue 4: Invalid Event Code

```
E (xxxxx) RTP: Malformed telephone-event: invalid event code 16 (valid range: 0-15)
```

**Cause:** Corrupted packet or non-DTMF event  
**Solution:** Check network quality

## Next Steps

1. **Flash the updated firmware** with debug logging
2. **Make a call** and press a key
3. **Check the logs** to see which stage fails
4. **Report back** with the log output

The logs will tell us exactly where the processing stops and why DTMF isn't being detected.

## Expected Outcome

With proper operation, you should see:

1. ✅ Packet received (payload_type=114)
2. ✅ Detected as telephone-event
3. ✅ Processing started
4. ✅ Event details parsed
5. ✅ End bit set (on last packet)
6. ✅ DTMF detected
7. ✅ Callback called
8. ✅ DTMF decoder processes it

If any step fails, the logs will show exactly where and why.
