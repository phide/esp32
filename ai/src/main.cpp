#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>

#include "epd_driver.h"
#include "firasans.h"

// WiFi
static const char *WIFI_SSID = "EMPTYSPACE";
static const char *WIFI_PASS = "Frucht4llee19";

// Ollama (local IP)
static const char *OLLAMA_HOST = "192.168.178.200";
static const uint16_t OLLAMA_PORT = 11434;
static const char *OLLAMA_PATH = "/api/generate";
static const char *OLLAMA_MODEL = "llama3.2:3b";

// Button (LilyGo Screen-4.7-S3 uses IO21 for user button)
static const int BUTTON_PIN = 21;

// Display
static uint8_t *framebuffer = nullptr;
static const size_t FRAMEBUFFER_SIZE = EPD_WIDTH * EPD_HEIGHT / 2;

static const int32_t PADDING = 18;
static const int32_t HEADER_H = 90;
static const int32_t STATUS_H = 70;

static const Rect_t HEADER_AREA = {0, 0, EPD_WIDTH, HEADER_H};
static const Rect_t STATUS_AREA = {0, HEADER_H, EPD_WIDTH, STATUS_H};
static const Rect_t CHAT_AREA = {0, HEADER_H + STATUS_H, EPD_WIDTH, EPD_HEIGHT - (HEADER_H + STATUS_H)};

static uint32_t last_render_ms = 0;
static const uint32_t RENDER_INTERVAL_MS = 700;

static String response_text;
static bool display_on = false;

static void displayOn() {
  if (!display_on) {
    epd_poweron();
    display_on = true;
  }
}

static void displayOff() {
  if (display_on) {
    epd_poweroff_all();
    display_on = false;
  }
}

static int textWidthPx(const String &s) {
  int32_t x = 0, y = 0, x1 = 0, y1 = 0, w = 0, h = 0;
  get_text_bounds((GFXfont *)&FiraSans, s.c_str(), &x, &y, &x1, &y1, &w, &h, NULL);
  return w;
}

static void drawTextInArea(const Rect_t &area, const String &text) {
  const int maxWidth = area.width - 2 * PADDING;
  int maxLines = (area.height - 2 * PADDING) / FiraSans.advance_y;
  if (maxLines < 1) maxLines = 1;

  const int MAX_LINES = 64;
  String lines[MAX_LINES];
  int lineCount = 0;

  String word;
  String line;

  auto pushLine = [&](const String &l) {
    if (lineCount < MAX_LINES) {
      lines[lineCount++] = l;
    }
  };

  for (int i = 0; i <= (int)text.length(); i++) {
    char c = (i < (int)text.length()) ? text[i] : '\n';
    if (c == '\n' || c == ' ' || c == '\r' || c == '\t') {
      if (word.length()) {
        String candidate = line.length() ? (line + " " + word) : word;
        if (!line.length() || textWidthPx(candidate) <= maxWidth) {
          line = candidate;
        } else {
          pushLine(line);
          line = word;
        }
        word = "";
      }
      if (c == '\n') {
        pushLine(line);
        line = "";
      }
    } else {
      word += c;
    }
  }
  if (word.length()) {
    String candidate = line.length() ? (line + " " + word) : word;
    if (!line.length() || textWidthPx(candidate) <= maxWidth) {
      line = candidate;
    } else {
      pushLine(line);
      line = word;
    }
  }
  if (line.length()) pushLine(line);

  int start = 0;
  if (lineCount > maxLines) {
    start = lineCount - maxLines;
  }

  for (int i = 0; i < maxLines && (start + i) < lineCount; i++) {
    int32_t cursor_x = area.x + PADDING;
    int32_t cursor_y = area.y + PADDING + FiraSans.ascender +
                       i * FiraSans.advance_y;
    writeln((GFXfont *)&FiraSans, lines[start + i].c_str(), &cursor_x, &cursor_y, NULL);
  }
}

static void renderStaticLayout() {
  if (!framebuffer) return;

  memset(framebuffer, 0xFF, FRAMEBUFFER_SIZE);

  epd_draw_rect(4, 4, EPD_WIDTH - 8, EPD_HEIGHT - 8, 0x00, framebuffer);
  epd_draw_hline(PADDING, HEADER_H - 10, EPD_WIDTH - 2 * PADDING, 0x00, framebuffer);

  int32_t cx = PADDING;
  int32_t cy = PADDING + FiraSans.advance_y;
  writeln((GFXfont *)&FiraSans, "LLAMA CHAT", &cx, &cy, framebuffer);

  epd_poweron();
  epd_clear();
  epd_draw_grayscale_image(epd_full_screen(), framebuffer);
  epd_poweroff_all();
  display_on = false;
}

static void updateStatus(const String &status, bool autoOff = true) {
  displayOn();
  epd_clear_area(STATUS_AREA);
  drawTextInArea(STATUS_AREA, status);
  if (autoOff) displayOff();
}

static void updateChat(const String &chat, bool autoOff = true) {
  displayOn();
  epd_clear_area(CHAT_AREA);
  drawTextInArea(CHAT_AREA, chat);
  if (autoOff) displayOff();
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
  }
}

bool ollamaStream(const String &prompt) {
  WiFiClient client;
  if (!client.connect(OLLAMA_HOST, OLLAMA_PORT)) {
    return false;
  }

  String body = String("{\"model\":\"") + OLLAMA_MODEL +
                "\",\"prompt\":\"" + prompt +
                "\",\"stream\":true}";

  client.print(String("POST ") + OLLAMA_PATH + " HTTP/1.1\r\n");
  client.print(String("Host: ") + OLLAMA_HOST + ":" + OLLAMA_PORT + "\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Connection: close\r\n");
  client.print(String("Content-Length: ") + body.length() + "\r\n\r\n");
  client.print(body);

  // Skip HTTP headers
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r" || line.length() == 0) break;
  }

  response_text = "";
  last_render_ms = 0;
  displayOn();

  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    // Skip chunk size lines if chunked transfer encoding is used
    bool hex_only = true;
    for (size_t i = 0; i < line.length(); i++) {
      char c = line[i];
      if (!isxdigit(c)) { hex_only = false; break; }
    }
    if (hex_only && line.length() <= 8) {
      continue;
    }

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
      continue;
    }

    if (doc.containsKey("response")) {
      const char *chunk = doc["response"];
      response_text += String(chunk);
    }

    uint32_t now = millis();
    if (now - last_render_ms > RENDER_INTERVAL_MS) {
      updateChat(response_text, false);
      last_render_ms = now;
    }

    if (doc.containsKey("done") && doc["done"].as<bool>()) {
      break;
    }
  }

  // Final render
  updateChat(response_text, false);
  displayOff();
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  epd_init();
  framebuffer = (uint8_t *)ps_calloc(1, FRAMEBUFFER_SIZE);

  renderStaticLayout();
  updateStatus("Booting...  Connecting WiFi");
  connectWifi();

  if (WiFi.status() == WL_CONNECTED) {
    updateStatus(String("WiFi OK  IP: ") + WiFi.localIP().toString() + "  Press button");
  } else {
    updateStatus("WiFi failed  Check SSID/PW  Press reset");
  }
}

void loop() {
  static bool last_pressed = false;
  bool pressed = (digitalRead(BUTTON_PIN) == LOW);

  if (pressed && !last_pressed) {
    if (WiFi.status() != WL_CONNECTED) {
      updateStatus("WiFi not connected  Retrying...");
      connectWifi();
      if (WiFi.status() != WL_CONNECTED) {
        updateStatus("WiFi failed  Press button");
        last_pressed = pressed;
        return;
      }
    }

    updateStatus("Sending: Hallo  Streaming...");
    bool ok = ollamaStream("Hallo");
    if (!ok) {
      updateStatus("Ollama connect failed  Check host/IP");
    }
  }

  last_pressed = pressed;
  delay(20);
}
