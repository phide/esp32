#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <time.h>
#include <esp_sleep.h>

#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <GxEPD2_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

#include "pins_user.h"

// Optional WiFi secrets
#if __has_include("wifi_secrets.h")
  #include "wifi_secrets.h"
  #define WIFI_SECRETS_PRESENT 1
#else
  #define WIFI_SECRETS_PRESENT 0
#endif

// ---- Display setup ----
// Note: Choose the correct display class for your panel.
// The T5 4.7" often uses a 960x540 4.7" panel (ED047TC1).
// If your panel differs, adjust the driver below.
GxEPD2_BW<GxEPD2_540, GxEPD2_540::HEIGHT> display(
  GxEPD2_540(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY)
);

// ---- Settings ----
static const uint32_t SLIDESHOW_DELAY_MS = 4000;
static const uint64_t SLEEP_INTERVAL_US = 5ULL * 60ULL * 1000000ULL; // 5 minutes
#define USE_DEEP_SLEEP 1

// ---- Helpers ----
static String formatUptime(uint32_t seconds) {
  uint32_t days = seconds / 86400;
  seconds %= 86400;
  uint32_t hours = seconds / 3600;
  seconds %= 3600;
  uint32_t minutes = seconds / 60;
  seconds %= 60;

  char buf[64];
  if (days > 0) {
    snprintf(buf, sizeof(buf), "%ud %02uh %02um %02us", days, hours, minutes, seconds);
  } else {
    snprintf(buf, sizeof(buf), "%02uh %02um %02us", hours, minutes, seconds);
  }
  return String(buf);
}

static String formatTimeNow() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return String("--:--");
  }
  char buf[32];
  strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
  return String(buf);
}

static void drawStatusPage() {
  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeSans9pt7b);

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(12, 24);
    display.print("LILYGO T5 4.7\" S3");

    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(12, 60);
    display.print("Status Board");

    display.setFont(&FreeSans9pt7b);
    display.setCursor(12, 96);
    display.print("Uptime: ");
    display.print(formatUptime(millis() / 1000));

    display.setCursor(12, 120);
    display.print("Heap free: ");
    display.print(ESP.getFreeHeap());
    display.print(" B");

    display.setCursor(12, 144);
    display.print("CPU MHz: ");
    display.print(getCpuFrequencyMhz());

    display.setCursor(12, 168);
    display.print("Time: ");
    display.print(formatTimeNow());

    display.setCursor(12, 200);
    display.print("SD: ");
    display.print(SD.cardType() == CARD_NONE ? "not found" : "ok");

  } while (display.nextPage());
}

static bool setupSD() {
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  return SD.begin(PIN_SD_CS);
}

static uint16_t read16(File &f) {
  uint16_t v;
  ((uint8_t *)&v)[0] = f.read();
  ((uint8_t *)&v)[1] = f.read();
  return v;
}

static uint32_t read32(File &f) {
  uint32_t v;
  ((uint8_t *)&v)[0] = f.read();
  ((uint8_t *)&v)[1] = f.read();
  ((uint8_t *)&v)[2] = f.read();
  ((uint8_t *)&v)[3] = f.read();
  return v;
}

static bool drawBmpFromSD(const char *filename) {
  File bmpFile = SD.open(filename, FILE_READ);
  if (!bmpFile) return false;

  // BMP header
  if (read16(bmpFile) != 0x4D42) { // 'BM'
    bmpFile.close();
    return false;
  }
  read32(bmpFile); // file size
  read32(bmpFile); // creator bytes
  uint32_t bmpImageOffset = read32(bmpFile);
  uint32_t headerSize = read32(bmpFile);
  int32_t bmpWidth = (int32_t)read32(bmpFile);
  int32_t bmpHeight = (int32_t)read32(bmpFile);
  if (read16(bmpFile) != 1) { // planes
    bmpFile.close();
    return false;
  }
  uint16_t bmpDepth = read16(bmpFile);
  uint32_t bmpCompression = read32(bmpFile);

  if (headerSize < 40 || bmpDepth != 24 || bmpCompression != 0) {
    bmpFile.close();
    return false;
  }

  bool flip = true;
  if (bmpHeight < 0) {
    bmpHeight = -bmpHeight;
    flip = false;
  }

  display.setRotation(1);
  int16_t dispW = display.width();
  int16_t dispH = display.height();

  // Center the image
  int16_t x0 = (dispW - bmpWidth) / 2;
  int16_t y0 = (dispH - bmpHeight) / 2;

  // Row size is padded to 4 bytes
  uint32_t rowSize = (bmpWidth * 3 + 3) & ~3;

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    for (int32_t row = 0; row < bmpHeight; row++) {
      int32_t pos = bmpImageOffset + (flip ? (bmpHeight - 1 - row) : row) * rowSize;
      if (!bmpFile.seek(pos)) continue;

      for (int32_t col = 0; col < bmpWidth; col++) {
        uint8_t b = bmpFile.read();
        uint8_t g = bmpFile.read();
        uint8_t r = bmpFile.read();

        // Simple threshold to map to B/W
        uint8_t lum = (uint8_t)((uint16_t)r * 30 / 100 + (uint16_t)g * 59 / 100 + (uint16_t)b * 11 / 100);
        if (lum < 128) {
          int16_t x = x0 + col;
          int16_t y = y0 + row;
          if (x >= 0 && y >= 0 && x < dispW && y < dispH) {
            display.drawPixel(x, y, GxEPD_BLACK);
          }
        }
      }
    }
  } while (display.nextPage());

  bmpFile.close();
  return true;
}

static void connectWiFi() {
#if WIFI_SECRETS_PRESENT && defined(WIFI_SSID) && defined(WIFI_PASS)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 8000) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  }
#endif
}

static void slideshowIfPresent() {
  File root = SD.open("/");
  if (!root) return;

  File file = root.openNextFile();
  while (file) {
    String name = file.name();
    name.toLowerCase();
    if (!file.isDirectory() && name.endsWith(".bmp")) {
      drawBmpFromSD(file.name());
      delay(SLIDESHOW_DELAY_MS);
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  SPI.begin(PIN_EPD_SCK, PIN_EPD_MISO, PIN_EPD_MOSI, PIN_EPD_CS);

  display.init(115200, true, 2, false);

  setupSD();
  connectWiFi();

  drawStatusPage();
  slideshowIfPresent();

  digitalWrite(PIN_LED, LOW);
}

void loop() {
  // ePaper: update rarely to save power
#if USE_DEEP_SLEEP
  drawStatusPage();
  esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_US);
  esp_deep_sleep_start();
#else
  delay(60000);
  drawStatusPage();
#endif
}
