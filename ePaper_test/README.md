# LILYGO Screen-4.7-S3 ePaper Dashboard

This project renders a simple autonomous dashboard on the 4.7" ePaper panel:
- Local time + date (auto via IP timezone)
- WiFi status and RSSI
- Battery voltage (if ADC available)
- Current weather via Open-Meteo

## Build & Flash (PlatformIO)
1. Install PlatformIO.
2. Open this folder as a project.
3. Build/Upload.

## Configuration
Update WiFi credentials in `include/secrets.h`.

## Notes
- The device wakes, updates the display, then deep-sleeps for 15 minutes.
- Weather uses IP geolocation; no API key required.
