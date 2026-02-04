# Project Overview
This is a PlatformIO-based ESP32 (Arduino framework) Pomodoro timer targeting the LilyGO TTGO T-Display (ST7789, 135x240). The UI is drawn with `TFT_eSPI`, and state is managed in `src/main.cpp`.
Wi-Fi is configured to try multiple SSIDs in order and sync time via NTP with Europe/Berlin DST rules.

# Key Hardware/IO
- Display: ST7789 via `TFT_eSPI` with build flags in `platformio.ini`.
- Buttons:
  - Left: GPIO35 (active low, no internal pull-up; uses `INPUT`).
  - Right: GPIO0 (active low, uses `INPUT_PULLUP`).

# App Behavior (High Level)
- Start screen with mode selection (order: `25/10`, `20/10`, `25/5`, `15/5`).
- Timer screen with phase label, countdown, progress bar, and cycle dots.
- Clock:
  - Timer screen shows `HH:MM` top-left aligned to the same Y as the `START` label.
  - Start screen shows the clock only after 60s of inactivity (replaces mode label).
- Wi-Fi indicator: small icon at top-left on the start screen (connected vs. disconnected color).
- Last selected mode is stored in NVS and restored on boot (Preferences `pomodoro` / `mode_idx`).
- Web UI (HTTP) starts after Wi-Fi connects:
  - `GET /` status + controls
  - `GET /start`, `/pause`, `/next`, `/reset`
  - `GET /home` returns to start menu
  - `GET /mode?i=0..N`
- Left button: start on start screen; pause/resume on timer screen.
- Right button: short press advances phase; long press resets current phase.

# Build/Upload
Use PlatformIO with the `lilygo-t-display` environment.

Common commands:
```sh
pio run -e lilygo-t-display
pio run -e lilygo-t-display -t upload
pio device monitor -b 115200
```

# Code Pointers
- Main logic and UI: `src/main.cpp`
- PlatformIO config: `platformio.ini`
