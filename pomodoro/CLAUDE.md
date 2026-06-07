# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

ESP32 (Arduino framework) Pomodoro timer + apps for the LilyGO TTGO T-Display (ST7789, 135x240), built with PlatformIO. Wi-Fi networks are stored in NVS and tried in priority order; time syncs via configurable NTP + DST; the device falls back to a self-hosted AP when no station connection is available.

## Build / Upload / Monitor

PlatformIO env is `lilygo-t-display`:

```sh
pio run -e lilygo-t-display              # compile
pio run -e lilygo-t-display -t upload    # flash to device
pio device monitor -b 115200             # serial monitor (115200 baud)
```

There is no test suite or CI. To verify a change: build, then flash to real hardware and confirm behavior. A successful compile alone does not prove runtime correctness.

## Architecture

- All logic lives in a single file: `src/main.cpp` (~5000 lines) — UI rendering, WiFi state machine, web server, button handling, NTP, apps, and games. There are no header files; `include/`, `lib/`, and `test/` are empty.
- Display config (ST7789 pins, fonts, SPI freq) is set via `build_flags` in `platformio.ini`, not a `User_Setup.h`.
- Persistent state is in NVS via `Preferences` (namespace `pomodoro`) — there is no SPIFFS/LittleFS filesystem, so no filesystem image to upload. The web UI's HTML/CSS/JS is built inline as C++ `String` concatenation inside each `webServer.on(...)` handler.

## Hardware / IO

- Display: ST7789 via `TFT_eSPI` (pins/flags in `platformio.ini`).
- Left button: GPIO35 — active low, **no internal pull-up** (uses `INPUT`).
- Right button: GPIO0 — active low (uses `INPUT_PULLUP`).
- Holding both buttons ~10s clears the AP password (reverts to open AP).

## Apps & on-device controls

Apps: Pomodoro, Clock, AI (only on a configured Wi-Fi), Weather, Snake, Flappy, WoL (Wake-on-LAN).
- App selection screen: left short-press opens an app, right short-press cycles apps; a fast double-press of the upper (left) button returns to app select from within an app. Last app is restored on boot.
- Pomodoro: left = start (start screen) / pause-resume (timer); right short = next phase, right long = reset phase.
- Clock: in the Clock app, right short-press cycles 3 time sizes (persisted). Time color is configurable. On the start screen the clock replaces the mode label after 60s idle.
- Weather: Open-Meteo, fixed coords (53.5737, 9.9001), refresh ~10 min; lower button toggles overview/details.
- Snake/Flappy: tile/dirty partial rendering; exit gesture = long-press left then short-press left within ~1.5s.

## Web interface

HTTP server runs on both STA and AP. Pages: `/` (home: app tabs + status + Pomodoro controls), `/settings` (hub), `/apps`, `/wifi`, `/ai`, `/wol`, `/time` (Pomodoro mode editor — add/edit/delete/reorder modes, durations, per-phase colors), `/ntp` (Time Sync: NTP server + DST). JSON/action endpoints back these (e.g. `/status`, `/wifi/scan`, `/ai/send`, `/wol/scan`, `/time/save_mode`, `/ntp/save`). A `/pihole` page + handlers exist but Pi-hole is disabled at boot and not linked from the UI.

## NVS keys (namespace `pomodoro`)

`mode_idx`, `app_idx`, `modes_json` (modes + per-mode colors), `clock_color`, `clock_size`, `wifi_json` (saved networks), `ap_ssid`, `ap_pass`, `ntp_server`, `dst_mode`, `ai_host`, `ai_wifi`, `ai_model`, `ai_system`, `wol_json`. Most config is saved together via `saveWifiConfig()`.

## Gotchas

- The `lilygo_screen_47_s3` / `LilyGo-EPD47` directory that may appear in `git status` belongs to the sibling `../ai` project (a different e-paper board) and is unrelated to this build.
- `git` workflow: make commits when asked; the user pushes via their Git GUI.
