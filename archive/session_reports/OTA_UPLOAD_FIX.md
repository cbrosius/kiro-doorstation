# OTA Upload Fix - Invalid Firmware File

## Problem Identified

The OTA upload was failing with error:
```
E (159968) OTA_HANDLER: Invalid firmware image format: magic number 0x2D (expected 0xE9)
```

**Root Cause**: You're uploading the wrong file type. The magic number `0x2D` is ASCII for '-', which suggests you're uploading a text file or non-binary file instead of the compiled firmware binary.

## What Was Fixed

### 1. State Reset After Failed Upload
**Issue**: After a failed upload, the OTA state remained in ERROR state, causing "OTA update already in progress" errors on subsequent attempts.

**Fix**: Modified `ota_abort_update()` to properly reset the state to `OTA_STATE_IDLE` and clear all context variables, allowing immediate retry after a failed upload.

### 2. Better Error Messages
**Issue**: Generic error messages didn't help identify the actual problem.

**Fix**: Enhanced error messages to clearly indicate:
- The actual magic number found vs expected
- Specific guidance to upload a `.bin` file compiled for ESP32-S3
- More detailed validation failure reasons

### 3. Automatic Cleanup on Validation Failure
**Issue**: Validation failures left the OTA system in an inconsistent state.

**Fix**: All validation failures now call `ota_abort_update()` to ensure clean state reset.

## How to Upload Firmware Correctly

### Step 1: Build the Firmware
```bash
# In your ESP-IDF project directory
idf.py build
```

This creates a `.bin` file in the `build` directory.

### Step 2: Locate the Correct Binary File
The firmware binary will be at:
```
build/<project-name>.bin
```

For example:
- `build/doorstation.bin`
- `build/firmware.bin`

**DO NOT upload**:
- `.elf` files
- `.map` files  
- Text files
- Partition tables
- Bootloader files

### Step 3: Upload via Web Interface
1. Navigate to the OTA update page
2. Click "Choose File"
3. Select the `.bin` file from the `build` directory
4. Click "Upload"

### Step 4: Verify Upload
The logs should show:
```
I (xxxxx) OTA_HANDLER: Validating firmware image header...
I (xxxxx) OTA_HANDLER: ✓ Firmware image header validated successfully
I (xxxxx) OTA_HANDLER: Magic: 0xE9, Chip: ESP32-S3, Segments: X
```

## Common Mistakes

### ❌ Wrong: Uploading Source Code
```
Error: magic number 0x23 (# character - shell script)
Error: magic number 0x2F (/ character - path)
```

### ❌ Wrong: Uploading ELF File
```
Error: magic number 0x7F (ELF header)
```

### ❌ Wrong: Uploading Text File
```
Error: magic number 0x2D (- character)
Error: magic number 0x3C (< character - HTML)
```

### ✅ Correct: Uploading .bin File
```
Success: magic number 0xE9 (ESP32 firmware)
```

## Testing the Fix

1. Try uploading a wrong file type - you should get a clear error message
2. Try uploading again immediately - it should work without "already in progress" error
3. Upload the correct `.bin` file - it should validate and flash successfully

## Additional Notes

- Maximum firmware size: 2MB
- The system now automatically resets after validation failures
- Error messages include the actual magic number found for debugging
- State is properly cleaned up to allow immediate retry

## Build Command Reference

```bash
# Full clean build
idf.py fullclean
idf.py build

# Quick rebuild
idf.py build

# Build and flash via serial (for development)
idf.py flash monitor

# Just build the binary for OTA
idf.py app
```

The OTA binary will always be in `build/<project-name>.bin` after a successful build.
