# Project Overview
This is a PlatformIO-based ESP32 (Arduino framework) Pomodoro timer targeting the LilyGO TTGO T-Display (ST7789, 135x240). The UI is drawn with `TFT_eSPI`, and state is managed in `src/main.cpp`.
Wi-Fi is managed via NVS: it tries stored SSIDs in priority order, syncs time via configurable NTP + DST, and falls back to a self-hosted AP when no STA connection is available.

# Key Hardware/IO
- Display: ST7789 via `TFT_eSPI` with build flags in `platformio.ini`.
- Buttons:
  - Left: GPIO35 (active low, no internal pull-up; uses `INPUT`).
  - Right: GPIO0 (active low, uses `INPUT_PULLUP`).

# App Behavior (High Level)
- Start screen with mode selection (default order: `25/10`, `20/10`, `25/5`, `15/5`), editable via Web UI.
- App selection screen before app start. Left short press opens app, right short press cycles apps (Pomodoro + Clock + AI when available + Weather + Snake + Flappy). Double-press upper button (left) returns to app select. Last app is auto-opened on boot.
- Weather app is always available. It shows current conditions and a small next-hours preview.
- Snake app: simple grid snake with food + score. Left short turn left, right short turn right. Double-tap left returns to app select. Wi-Fi icon hidden.
- Flappy app: simple bird + pipe with score. Left or right short flaps; double-tap left returns to app select. Wi-Fi icon hidden.
- App selection is now triggered via a fast double-press of the upper (left) button. This replaces the old long-press app-switch behavior.
- Web UI mirrors app select: when the device is in app selection, the home page shows app selection buttons and Pomodoro controls are disabled.
- Timer screen with phase label, countdown, progress bar, and cycle dots.
- Clock:
  - Timer screen shows `HH:MM` top-left aligned to the same Y as the `START` label.
  - Start screen shows the clock only after 60s of inactivity (replaces mode label).
  - Clock app shows big time and smaller date; time color is configurable in Web UI.
  - In Clock app, right short press cycles time size (3 sizes), persisted in NVS (`clock_size`).
- AI App:
  - Only available on a selected Wi-Fi network (configured in AI settings).
  - Web UI can send prompts; typing is mirrored live on the device.
  - AI responses stream to the device; long-hold upper button scrolls up, long-hold lower button scrolls down.
  - System Message is configurable and sent as the first message on each request.
- Weather App:
  - Uses Open-Meteo with fixed coordinates (53.5737, 9.9001) and refreshes about every 10 minutes when online.
  - Two views toggled by the lower button: overview with temp + next 3 hours, and a details view (feels-like, wind, precip chance).
  - Displays a simple icon based on weather code / rain / snow; Wi-Fi icon is hidden in this app.
- Games:
  - Snake and Flappy are always available, and appear in the web app tabs + app selection list.
- Wi-Fi indicator: small icon at top-left on the start and app-select screens (connected vs. disconnected color).
- Last selected mode is stored in NVS and restored on boot (Preferences `pomodoro` / `mode_idx`).
- Web UI (HTTP) runs on both STA and AP:
  - `GET /` status + controls (Apps pill links to `/apps`, Settings pill links to `/settings`)
  - `GET /settings` links to Apps, Wi-Fi, Time, Time Sync
  - `GET /ai` AI settings + system message + chat UI
  - `POST /ai/save`, `/ai/system`, `/ai/send`, `/ai/typing`
  - `GET /ai/status`
  - `GET /apps` app selection + clock app color settings
  - `POST /apps/select`, `/apps/clock`
  - `GET /wifi` Wi-Fi manager (add/delete/reorder + AP settings, shows IP)
  - `GET /time` Pomodoro mode editor (add/edit/delete/reorder, sliders, colors) and save buttons
  - `GET /ntp` time sync (NTP + DST)
  - `GET /status` JSON status for live UI updates
  - `POST /ntp/save`
  - `POST /time/mode`, `/time/save_mode`, `/time/delete`
  - `GET /start`, `/pause`, `/next`, `/reset` (Pomodoro app only)
  - `GET /home` returns to app select (Pomodoro app only)
  - `GET /mode?i=0..N` (Pomodoro app only)
  - `GET /wifi/scan` JSON scan results
  - `POST /wifi/add`, `/wifi/delete`, `/wifi/order`, `/wifi/ap`
- Wi-Fi settings stored in NVS:
  - Networks list: `pomodoro` / `wifi_json`
  - AP config: `pomodoro` / `ap_ssid`, `ap_pass`
  - NTP config: `pomodoro` / `ntp_server`, `dst_mode`
- Mode list stored in NVS: `pomodoro` / `modes_json` (editable, reorderable, per-mode colors).
- Clock app time color stored in NVS: `pomodoro` / `clock_color`.
- Clock app time size stored in NVS: `pomodoro` / `clock_size`.
- AI settings stored in NVS: `pomodoro` / `ai_host`, `pomodoro` / `ai_wifi`.
- AI system message stored in NVS: `pomodoro` / `ai_system`.
- Holding both buttons for ~10s clears AP password (reverts to open AP).
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
