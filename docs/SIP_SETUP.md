# SIP-Konfiguration

## Unterstützte SIP-Server

Das System wurde getestet mit:
- TekSIP
- FritzBox (AVM)

## Konfiguration

### 1. SIP-Account erstellen
Erstellen Sie einen SIP-Account auf Ihrem SIP-Server:
- Benutzername: `doorbell`
- Passwort: Sicheres Passwort wählen
- Codec: PCMU (G.711 μ-law)

### 2. Apartment-Accounts
Erstellen Sie SIP-Accounts für die Apartments:
- `apartment1@ihredomain.com`
- `apartment2@ihredomain.com`

### 3. Webinterface-Konfiguration
1. Verbinden Sie sich mit dem WiFi-Hotspot "ESP32-Doorbell"
2. Öffnen Sie http://192.168.4.1
3. Konfigurieren Sie WiFi und SIP-Einstellungen

### 4. SIP-Server Einstellungen
```
SIP Server: sip.ihredomain.com
Port: 5060 (Standard)
Benutzername: doorbell
Passwort: IhrPasswort
```

## DTMF-Codes

Die folgenden DTMF-Codes werden unterstützt:

| Code | Funktion |
|------|----------|
| *1   | Türöffner aktivieren (3 Sekunden) |
| *2   | Licht ein/ausschalten |
| #    | Gespräch beenden |

## Audio-Einstellungen

- Sample Rate: 8 kHz
- Codec: PCMU (G.711)
- Kanäle: Mono
- Bitrate: 64 kbps

## Troubleshooting

### Registrierung fehlgeschlagen
- Prüfen Sie Server-Adresse und Port
- Überprüfen Sie Benutzername/Passwort
- Firewall-Einstellungen prüfen

### Kein Audio
- I2S-Verkabelung überprüfen
- Audio-Codec Spannungsversorgung prüfen
- Lautstärke-Einstellungen

### DTMF funktioniert nicht
- Audio-Qualität prüfen
- DTMF-Decoder Schwellwerte anpassen
- Störgeräusche minimieren

## Erweiterte Konfiguration

### Asterisk Dialplan Beispiel
```
[doorbell]
exten => apartment1,1,Dial(SIP/apartment1,30)
exten => apartment1,n,Hangup()

exten => apartment2,1,Dial(SIP/apartment2,30)
exten => apartment2,n,Hangup()
```

### FreePBX Konfiguration
1. Neuen SIP-Trunk für Türstation erstellen
2. Inbound Routes für Apartments konfigurieren
3. DTMF-Verarbeitung in Custom Context