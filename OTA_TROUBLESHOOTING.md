# OTA Upload Troubleshooting Guide

## Quick Diagnosis

### Error: "Invalid firmware image format: magic number 0xXX"

| Magic Number | File Type | Solution |
|--------------|-----------|----------|
| `0xE9` | ✅ Valid ESP32 firmware | This is correct! |
| `0x2D` | Text file starting with `-` | Upload the `.bin` file from `build/` directory |
| `0x23` | Shell script starting with `#` | Upload the `.bin` file, not a script |
| `0x7F` | ELF executable | Upload the `.bin` file, not the `.elf` |
| `0x3C` | HTML file starting with `<` | Upload the `.bin` file, not a web page |
| `0x2F` | Path or comment starting with `/` | Upload the `.bin` file |

### Error: "OTA update already in progress"

**Cause**: Previous upload failed and didn't clean up properly.

**Solution**: This is now fixed! The system automatically resets after failures. Just try uploading again.

**Manual Reset** (if needed):
```bash
# Restart the device
curl -X POST http://doorstation.local/api/system/restart
```

## Step-by-Step Upload Process

### 1. Build Your Firmware
```bash
cd /path/to/your/project
idf.py build
```

### 2. Find the Binary
Look for: `build/<project-name>.bin`

Example paths:
- `build/doorstation.bin`
- `build/main.bin`
- `build/firmware.bin`

### 3. Verify It's a Binary File
```bash
# On Linux/Mac
file build/doorstation.bin
# Should output: "build/doorstation.bin: data"

# Check first byte (should be E9)
xxd -l 16 build/doorstation.bin
# Should start with: e9 ...
```

### 4. Upload via Web Interface
1. Open `https://doorstation.local/`
2. Go to System → Firmware Update
3. Click "Choose File"
4. Select the `.bin` file
5. Click "Upload Firmware"

### 5. Monitor Progress
Watch the logs for:
```
I OTA_HANDLER: Validating firmware image header...
I OTA_HANDLER: ✓ Firmware image header validated successfully
I OTA_HANDLER: Magic: 0xE9, Chip: ESP32-S3, Segments: X
I OTA_HANDLER: OTA progress: 10% ...
I OTA_HANDLER: OTA progress: 100%
I OTA_HANDLER: OTA update completed successfully
```

## Common Issues

### Issue: Upload Fails Immediately
**Symptoms**: Error before any progress shown

**Possible Causes**:
1. Wrong file type (check magic number error)
2. File too large (>2MB)
3. Empty file
4. Network timeout

**Solutions**:
- Verify you're uploading the `.bin` file
- Check file size: `ls -lh build/*.bin`
- Ensure stable network connection

### Issue: Upload Starts Then Fails
**Symptoms**: Progress shown, then error

**Possible Causes**:
1. Corrupted binary
2. Network interruption
3. Insufficient flash space
4. Wrong chip target

**Solutions**:
- Rebuild firmware: `idf.py fullclean && idf.py build`
- Check build target is ESP32-S3
- Verify partition table has space

### Issue: Upload Succeeds But Device Won't Boot
**Symptoms**: Device restarts but doesn't come back online

**Solutions**:
1. Wait 30 seconds for full boot
2. Check serial monitor for boot errors
3. Use rollback feature if available
4. Flash via USB serial as fallback

## Rollback Procedure

If new firmware doesn't work:

### Via Web Interface (if accessible)
```bash
curl -X POST http://doorstation.local/api/ota/rollback
```

### Via Serial Console
```bash
# Connect via serial
idf.py monitor

# In the console, trigger rollback
# (Implementation depends on your menu system)
```

## Validation Checks

The OTA system validates:

1. **Magic Number** (`0xE9`) - ESP32 firmware signature
2. **Chip ID** - Must be ESP32-S3
3. **Segment Count** - Must be ≤ 16 segments
4. **SPI Configuration** - Valid flash settings
5. **File Size** - Must fit in partition
6. **Checksum** - SHA256 verification

## Build Configuration

Ensure your `sdkconfig` has:
```
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

Check partition table has OTA partitions:
```csv
# Name,   Type, SubType, Offset,  Size
ota_0,    app,  ota_0,   0x10000, 1536K
ota_1,    app,  ota_1,   0x190000,1536K
```

## Debug Commands

### Check Current Firmware
```bash
curl http://doorstation.local/api/ota/info
```

### Check OTA Status
```bash
curl http://doorstation.local/api/ota/status
```

### View System Info
```bash
curl http://doorstation.local/api/system/info
```

## Success Indicators

✅ **Upload Successful**:
```
I OTA_HANDLER: ✓ Firmware image header validated successfully
I OTA_HANDLER: OTA update completed successfully
I web_api: OTA update completed successfully
```

✅ **Boot Successful**:
```
I esp_image: Image verified successfully
I boot: Loaded app from partition at offset 0x190000
I main: Firmware version: vX.X.X
```

## Need More Help?

1. Check serial monitor output: `idf.py monitor`
2. Enable verbose OTA logging in `sdkconfig`
3. Verify build output: `idf.py build -v`
4. Check partition table: `idf.py partition-table`
