# ESP32 SIP Door Station

An ESP32-based SIP door station project with the following features:

- 2 doorbell buttons for different apartments/areas
- SIP telephony for intercom
- DTMF control for light and door opener
- Audio input/output via I2S
- Web interface for configuration
- NTP time synchronization with real timestamps
- SIP Digest Authentication (MD5)

## Requirements

### Software
- **ESP-IDF v5.5.1 or later** (required)
- Python 3.8 or later
- Git

### Hardware
- ESP32-S3 DevKit (or ESP32)
- I2S Audio Codec (e.g., MAX98357A for output)
- I2S Microphone (e.g., INMP441)
- 2x Push buttons for doorbells
- Relays for door opener and light
- Speaker and microphone

## Pin-Belegung

| Funktion | GPIO |
|----------|------|
| Klingel 1 | GPIO 21 |
| Klingel 2 | GPIO 22 |
| Türöffner Relais | GPIO 25 |
| Licht Relais | GPIO 26 |
| I2S SCK | GPIO 14 |
| I2S WS | GPIO 15 |
| I2S SD (OUT) | GPIO 32 |
| I2S SD (IN) | GPIO 33 |

## DTMF-Codes

- `*1` - Türöffner aktivieren
- `*2` - Licht ein/aus
- `#` - Gespräch beenden

## Development

This project uses ESP-IDF with VS Code and devcontainer for a consistent development environment.

### Prerequisites

Install ESP-IDF v5.5.1 or later:
```bash
# Follow official ESP-IDF installation guide
# https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/
```

### Building

```bash
# Configure project (first time only)
idf.py set-target esp32s3

# Build project
idf.py build

# Flash and monitor
idf.py flash monitor
```

### Using VS Code with devcontainer

```bash
# Open in VS Code
code .

# In VS Code: "Reopen in Container"
# Container includes ESP-IDF v5.5.1

# Build
idf.py build

# Flash
idf.py flash monitor
```

## Configuration

After first boot, a WiFi hotspot "ESP32-Doorbell" opens.
Access web interface at http://192.168.4.1 for SIP configuration.

### Features

✅ **SIP Registration** - Automatic registration with digest authentication  
✅ **NTP Time Sync** - Real timestamps in logs (supports timezone names like Europe/Berlin)  
✅ **Web Interface** - Modern responsive UI with real-time logs  
✅ **WiFi Management** - Network scanning and configuration  
✅ **Dual-Core Architecture** - SIP on Core 1, WiFi on Core 0  
✅ **DTMF Control** - Door opener and light control via phone  

### Compatibility

- **ESP-IDF**: v5.5.1 or later (required)
- **ESP32 Chips**: ESP32, ESP32-S3
- **SIP Servers**: Asterisk, FreeSWITCH, Kamailio, 3CX, and most RFC 3261 compliant servers
- **Authentication**: MD5 Digest (RFC 2617)


After the first start, a WiFi-Hotspot "ESP32-Doorbell" is created

Initial Password: doorstation123

Webinterface: http://192.168.4.1 for Wifi-Configuration
