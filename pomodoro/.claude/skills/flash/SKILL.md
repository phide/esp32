---
name: flash
description: Build, upload, and monitor the ESP32 firmware on the connected LilyGO T-Display in one step. Use when the user wants to flash the device, deploy to hardware, or watch serial output after a change.
disable-model-invocation: true
---

Build the firmware, flash it to the connected device, then open the serial monitor.

1. Build and upload:
   ```sh
   pio run -e lilygo-t-display -t upload
   ```
   If the upload fails because no port is found, tell the user to connect the device / check the USB cable, and ask whether to retry. Do not guess a `--upload-port`.

2. On a successful upload, open the serial monitor at 115200 baud:
   ```sh
   pio device monitor -b 115200
   ```
   The monitor runs until the user stops it (Ctrl-C). Run it in the background if you need to keep working, and surface relevant log lines.

Report build/upload errors verbatim — do not assume the flash succeeded.
