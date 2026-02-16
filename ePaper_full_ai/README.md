# ePaper Full AI — Local Status Board

Projektziel: Ein schlankes, robustes ePaper-Statusboard für das **LILYGO T5 4.7" S3**.  
Es zeigt lokale Statusdaten (Uptime, CPU, freier Heap), optional WLAN-Zeit (NTP) und Bilder von der SD-Karte.

**Warum dieses Projekt?**  
- Läuft komplett offline (WLAN optional)  
- Nutzt die Stärken von ePaper (seltene Updates, stromsparend)  
- Erweiterbar (z. B. Sensoren, MQTT, Kalender)  

## Features
- Startbildschirm + Systemstatus
- SD-Karten-Slideshow (BMP, 24-bit, unkomprimiert)
- Optional: NTP-Zeit (wenn WLAN-Zugangsdaten gesetzt sind)
- Optional: Deep-Sleep (energiesparend, Timer-Wakeup)
- Sichere Defaults ohne Klartext-Passwörter im Code

## Schnellstart
1. Installiere PlatformIO (VS Code empfohlen).
2. Öffne dieses Repo in VS Code.
3. Passe `include/pins_user.h` an (Pinout für dein Board).
4. Optional: Lege `include/wifi_secrets.h` an (SSID/Passwort).
5. Build & Upload.

## Dateien
- `src/main.cpp`: App-Logik
- `include/pins_user.h`: **dein** Pinout
- `include/wifi_secrets.h`: optional, wird nicht eingecheckt
- `platformio.ini`: Build-Konfiguration

## Notizen
- Dieses Projekt setzt Bibliotheken voraus, die PlatformIO automatisch lädt (siehe `platformio.ini`).
- BMPs auf der SD-Karte: 24-bit, unkomprimiert, max. 960x540, idealerweise im Root-Verzeichnis.
- Deep-Sleep ist in `src/main.cpp` per `USE_DEEP_SLEEP` steuerbar.

## Nächste Ideen
- RTC nutzen für Offline-Uhr
- MQTT-Status aus dem Netz
- Touch-Buttons als UI
