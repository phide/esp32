---
name: build
description: Compile-check the ESP32 firmware without flashing. Use for fast feedback after editing src/main.cpp to catch compile errors before deploying to hardware.
---

Compile the firmware to check for errors. This does NOT flash the device.

```sh
pio run -e lilygo-t-display
```

Report any compile errors verbatim with the offending `file:line`. A clean build only proves it compiles — it does not verify runtime behavior, which requires flashing to hardware (see the `flash` skill).
