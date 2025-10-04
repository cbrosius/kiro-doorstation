# Hardware-Dokumentation

## Benötigte Komponenten

### Hauptkomponenten
- ESP32 DevKit C (oder ähnlich)
- I2S Audio Codec (z.B. MAX98357A für Ausgabe)
- I2S Mikrofon (z.B. INMP441 für Eingabe)
- 2x Taster (für Klingeln)
- 2x Relais-Module (5V, optisch gekoppelt)
- Netzteil 5V/2A
- Gehäuse (wetterfest für Außenbereich)

### Audio-Hardware
- Lautsprecher 4-8 Ohm, 3-5W
- Elektret-Mikrofon oder I2S-Mikrofon
- Audio-Verstärker (falls nicht im Codec integriert)

### Schaltplan

```
ESP32 DevKit C Pin-Belegung:

GPIO 21 -----> Klingel 1 (Pullup, Active Low)
GPIO 22 -----> Klingel 2 (Pullup, Active Low)
GPIO 25 -----> Türöffner Relais
GPIO 26 -----> Licht Relais

I2S Audio:
GPIO 14 -----> I2S SCK (Serial Clock)
GPIO 15 -----> I2S WS (Word Select)
GPIO 32 -----> I2S SD OUT (Serial Data Out)
GPIO 33 -----> I2S SD IN (Serial Data In)

Power:
5V      -----> Relais VCC
3.3V    -----> Audio Codec VCC
GND     -----> Common Ground
```

## Relais-Anschluss

### Türöffner-Relais
- NO (Normally Open) an Türöffner-Steuerung
- COM an Türöffner-Masse
- Aktivierungszeit: 3 Sekunden

### Licht-Relais
- NO/NC je nach Licht-Schaltung
- Bistabile Schaltung (Ein/Aus Toggle)

## Audio-Verkabelung

### MAX98357A (I2S DAC)
```
VDD  -> 3.3V
GND  -> GND
DIN  -> GPIO 32 (I2S SD OUT)
BCLK -> GPIO 14 (I2S SCK)
LRC  -> GPIO 15 (I2S WS)
```

### INMP441 (I2S Mikrofon)
```
VDD  -> 3.3V
GND  -> GND
SD   -> GPIO 33 (I2S SD IN)
SCK  -> GPIO 14 (I2S SCK)
WS   -> GPIO 15 (I2S WS)
L/R  -> GND (Left Channel)
```

## Gehäuse und Montage

- IP65-Gehäuse für Außenbereich
- Membrane für Lautsprecher und Mikrofon
- Beleuchtete Klingeltaster
- Montage in 1,5m Höhe
- Kabelführung für Türöffner und Licht

## Stromversorgung

- 5V/2A Netzteil
- Backup-Batterie optional (für kurze Ausfälle)
- Überspannungsschutz empfohlen