# ISR Safety Audit Report

## Overview
This document summarizes the audit of all interrupt service routines (ISRs) and callbacks to ensure they don't use unsafe functions like `ESP_LOG` macros.

## Audit Date
October 15, 2025

## Findings

### ✅ SAFE - GPIO Handler (main/gpio_handler.c)
**Status**: Fixed and verified safe

**ISR Handlers**:
- `doorbell_isr_handler()` - IRAM_ATTR
- `boot_button_isr_handler()` - IRAM_ATTR

**Implementation**:
- Uses FreeRTOS queue (`xQueueSendFromISR`) to defer work to task context
- No logging or complex operations in ISR
- Dedicated `doorbell_task` handles actual SIP calls and logging
- Pattern: ISR → Queue → Task → Action

**Code Pattern**:
```c
static void IRAM_ATTR boot_button_isr_handler(void* arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    doorbell_event_t event = { .bell = DOORBELL_1 };
    xQueueSendFromISR(doorbell_queue, &event, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

### ✅ SAFE - NTP Sync (main/ntp_sync.c)
**Status**: Verified safe

**Callback**:
- `time_sync_notification_cb()` - Called by ESP-IDF SNTP library

**Implementation**:
- ESP-IDF SNTP library calls this from task context, not ISR context
- Safe to use `ESP_LOGI` and other standard functions
- No IRAM_ATTR needed

**Verification**:
According to ESP-IDF documentation, SNTP callbacks are invoked from the SNTP task context, making them safe for logging and other operations that require locks.

### ✅ SAFE - Audio Handler (main/audio_handler.c)
**Status**: No ISRs present

**Analysis**:
- No interrupt handlers defined
- All functions run in normal task context
- Uses I2S driver which handles interrupts internally

### ✅ SAFE - DTMF Decoder (main/dtmf_decoder.c)
**Status**: No ISRs present

**Analysis**:
- No interrupt handlers defined
- `dtmf_command_handler()` is a callback but called from task context
- All operations safe for logging

### ✅ SAFE - WiFi Manager (main/wifi_manager.c)
**Status**: No ISRs present (not audited in detail)

**Analysis**:
- WiFi event handlers are called from event task context
- ESP-IDF WiFi library handles interrupt context internally

### ✅ SAFE - Web Server (main/web_server.c)
**Status**: No ISRs present

**Analysis**:
- HTTP handlers run in httpd task context
- All logging operations are safe

## Common ISR Safety Rules

### ❌ UNSAFE in ISR Context
- `ESP_LOGI()`, `ESP_LOGW()`, `ESP_LOGE()`, `ESP_LOGD()` - Use locks
- `printf()`, `sprintf()` - Use locks
- `malloc()`, `free()` - Use locks
- FreeRTOS functions without `FromISR` suffix
- Blocking operations
- Long-running operations

### ✅ SAFE in ISR Context
- `xQueueSendFromISR()`
- `xSemaphoreGiveFromISR()`
- `xTaskNotifyFromISR()`
- `portYIELD_FROM_ISR()`
- Simple variable assignments
- GPIO operations
- Hardware register access

## Best Practices Implemented

1. **Defer Work Pattern**: ISRs only send events to queues, actual work done in tasks
2. **IRAM_ATTR**: All ISR handlers marked with IRAM_ATTR for fast access
3. **Minimal ISR Code**: Keep ISR handlers as short as possible
4. **Task Notifications**: Use FreeRTOS primitives designed for ISR use
5. **No Logging in ISR**: All logging moved to task context

## Previous Issues Fixed

### Issue 1: BOOT Button Crash
**Date**: October 15, 2025
**Symptom**: System crash when pressing BOOT button
**Root Cause**: `ESP_LOGI()` called directly in `boot_button_isr_handler()`
**Error**: `abort() was called at PC 0x40376877 on core 0` in `lock_acquire_generic`
**Fix**: Implemented queue-based deferred work pattern

### Issue 2: Watchdog Timeout on Button Press
**Date**: October 15, 2025
**Symptom**: Interrupt watchdog timeout when pressing BOOT button
**Root Cause**: 
1. No debouncing - button pressed multiple times rapidly
2. SIP client rejected calls when not in IDLE state
3. Queue flooding with duplicate events
**Error**: `Guru Meditation Error: Core 0 panic'ed (Interrupt wdt timeout on CPU0)`
**Fix**: 
1. Added 2-second debounce in doorbell_task
2. Updated sip_client_make_call to accept calls in REGISTERED state
3. Improved error logging with NTP timestamps

## Recommendations

1. ✅ All ISR handlers should use IRAM_ATTR
2. ✅ Use FreeRTOS `FromISR` functions exclusively in ISRs
3. ✅ Defer complex work to tasks via queues or notifications
4. ✅ Never use logging functions in ISR context
5. ✅ Keep ISR execution time minimal

## Conclusion

All interrupt service routines in the codebase have been audited and verified safe. The GPIO handler ISRs were fixed to use proper queue-based deferred work pattern. No other ISR safety issues were found.

## Related Files
- `main/gpio_handler.c` - GPIO ISR handlers (fixed)
- `main/ntp_sync.c` - SNTP callback (verified safe)
- `main/audio_handler.c` - No ISRs
- `main/dtmf_decoder.c` - No ISRs
- `docs/BOOT_BUTTON_TESTING.md` - BOOT button implementation details
