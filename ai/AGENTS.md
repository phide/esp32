# AGENTS

This project is a minimal ESP32‑S3 (LilyGo Screen‑4.7‑S3) test app that sends a prompt to a self‑hosted Ollama API with `stream: true` and renders the streaming response on the e‑paper display.

## Hardware
- Board: LilyGo Screen‑4.7‑S3 (ESP32‑S3, 16MB Flash, 8MB PSRAM)
- Display: 4.7" e‑paper (LilyGo EPD47 library)
- Button: GPIO 21 (user button)

## Wi‑Fi
- SSID: `EMPTYSPACE`
- Password: `Frucht4llee19`

## API (Ollama)
- Model: `llama3.2:3b`
- Base URL (local): `http://192.168.178.200:11434`
- Alternative (Tailscale): `http://100.100.50.100:11434`
- Alternative (mDNS): `http://lamacraft:11434`

### Endpoint
- `POST /api/generate`
- Content-Type: `application/json`

### Request (streaming)
```json
{
  "model": "llama3.2:3b",
  "prompt": "Hallo",
  "stream": true
}
```

### Response (streaming)
The response is newline‑delimited JSON. Each line can contain:
- `response`: a text chunk
- `done`: `true` when streaming finished

## Project Structure
- `platformio.ini`: PlatformIO config
- `src/main.cpp`: main firmware

## Board/PlatformIO
- PlatformIO environment: `lilygo_screen_47_s3`
- Board definition used: `esp32-s3-devkitc-1` (works for LilyGo Screen‑4.7‑S3)
- Framework: Arduino
- Upload speed: 921600
- Serial monitor speed: 115200

## Build/Flash
```zsh
pio run -t upload
```

## Display Layout
Defined in `src/main.cpp`:
- `PADDING = 18`
- `HEADER_H = 90`
- `STATUS_H = 70`
- `HEADER_AREA = {0, 0, EPD_WIDTH, HEADER_H}`
- `STATUS_AREA = {0, HEADER_H, EPD_WIDTH, STATUS_H}`
- `CHAT_AREA = {0, HEADER_H + STATUS_H, EPD_WIDTH, EPD_HEIGHT - (HEADER_H + STATUS_H)}`

Layout behavior:
- Header is drawn once on boot to a framebuffer and pushed as a full refresh.
- Status and chat areas are partial refreshes using `epd_clear_area()` + direct `writeln(..., framebuffer = NULL)`.

## Streaming Render Strategy
- While streaming, the display stays powered on.
- Chat area is updated every ~700 ms.
- Full refresh is only done at boot.

## Ollama Client Behavior
- Uses plain `WiFiClient` (HTTP/1.1) with manual headers.
- Reads response line‑by‑line and parses JSON; appends `response` chunks into a single string.
- Stops on `done: true`.

## Known Warnings
- ArduinoJson v7 deprecation warnings (`DynamicJsonDocument`, `containsKey`) do not block build.
- LilyGo EPD47 library emits warnings about `round()` (inside library). Safe to ignore.

## Troubleshooting
- If PlatformIO complains about permissions in `~/.platformio/.cache`, fix ownership:
  `sudo chown -R "$USER" ~/.platformio`
- If upload fails, check the port (e.g. `/dev/cu.usbmodem*`).
- If rendering appears in wrong areas, adjust `HEADER_H`, `STATUS_H`, and `PADDING`.

## Next Ideas
- Use smaller font or multi‑font layout for more text per screen.
- Add partial refresh for single lines instead of full chat area.
- Add prompt input via serial, touch, or buttons.
