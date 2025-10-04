# ESP32 SIP Türstation

Ein ESP32-basiertes SIP-Türstation-Projekt mit folgenden Features:

- 2 Klingelknöpfe für verschiedene Wohnungen/Bereiche
- SIP-Telefonie für Gegensprechen
- DTMF-Steuerung für Licht und Türöffner
- Audio-Ein-/Ausgabe über I2S
- Webinterface für Konfiguration

## Hardware-Anforderungen

- ESP32 DevKit
- I2S Audio Codec (z.B. MAX98357A für Ausgabe)
- I2S Mikrofon (z.B. INMP441)
- 2x Taster für Klingeln
- Relais für Türöffner und Licht
- Lautsprecher und Mikrofon

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

## Entwicklung

Das Projekt verwendet ESP-IDF mit VS Code und devcontainer für eine konsistente Entwicklungsumgebung.

```bash
# Container starten
code .
# In VS Code: "Reopen in Container"

# Projekt bauen
idf.py build

# Flashen
idf.py flash monitor
```

## Konfiguration

Nach dem ersten Start öffnet sich ein WiFi-Hotspot "ESP32-Doorbell". 
Webinterface unter http://192.168.4.1 für SIP-Konfiguration.