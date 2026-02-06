#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_sleep.h>

#include "epd_driver.h"
#include "epd_highlevel.h"
#include "firasans.h"

#include "secrets.h"

static const uint32_t WIFI_TIMEOUT_MS = 20000;
static const uint32_t UPDATE_INTERVAL_MIN = 15;
static const gpio_num_t BAT_ADC_PIN = GPIO_NUM_14; // Board pin overview: BAT ADC

struct TimeInfo {
  long gmt_offset_sec = 0;
  int dst_offset_sec = 0;
  String timezone;
  String abbreviation;
  bool ok = false;
};

struct LocationInfo {
  double lat = 0.0;
  double lon = 0.0;
  String city;
  String region;
  String country;
  bool ok = false;
};

struct WeatherInfo {
  float temp_c = NAN;
  float wind_kmh = NAN;
  int code = -1;
  bool ok = false;
};

static uint8_t *framebuffer = nullptr;

static String http_get(const String &url) {
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(url)) {
    return "";
  }
  int code = http.GET();
  if (code != 200) {
    http.end();
    return "";
  }
  String payload = http.getString();
  http.end();
  return payload;
}

static long parse_utc_offset(const String &utc_offset) {
  if (utc_offset.length() < 6) return 0;
  int sign = (utc_offset[0] == '-') ? -1 : 1;
  int hours = utc_offset.substring(1, 3).toInt();
  int mins = utc_offset.substring(4, 6).toInt();
  return sign * (hours * 3600L + mins * 60L);
}

static TimeInfo fetch_time_info() {
  TimeInfo info;
  String json = http_get("http://worldtimeapi.org/api/ip");
  if (json.isEmpty()) return info;

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return info;

  const char *utc_offset = doc["utc_offset"] | "+00:00";
  info.gmt_offset_sec = parse_utc_offset(utc_offset);
  bool dst = doc["dst"] | false;
  info.dst_offset_sec = dst ? 3600 : 0;
  info.timezone = doc["timezone"] | "";
  info.abbreviation = doc["abbreviation"] | "";
  info.ok = true;
  return info;
}

static LocationInfo fetch_location() {
  LocationInfo info;
  String json = http_get("http://ip-api.com/json/?fields=lat,lon,city,regionName,countryCode");
  if (json.isEmpty()) return info;

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return info;

  info.lat = doc["lat"] | 0.0;
  info.lon = doc["lon"] | 0.0;
  info.city = doc["city"] | "";
  info.region = doc["regionName"] | "";
  info.country = doc["countryCode"] | "";
  info.ok = true;
  return info;
}

static WeatherInfo fetch_weather(const LocationInfo &loc) {
  WeatherInfo info;
  if (!loc.ok) return info;

  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(loc.lat, 4) +
               "&longitude=" + String(loc.lon, 4) +
               "&current=temperature_2m,weather_code,wind_speed_10m" +
               "&wind_speed_unit=kmh";

  String json = http_get(url);
  if (json.isEmpty()) return info;

  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return info;

  JsonObject current = doc["current"];
  if (current.isNull()) return info;

  info.temp_c = current["temperature_2m"] | NAN;
  info.wind_kmh = current["wind_speed_10m"] | NAN;
  info.code = current["weather_code"] | -1;
  info.ok = true;
  return info;
}

static const char *weather_description(int code) {
  if (code == 0) return "Clear";
  if (code >= 1 && code <= 3) return "Partly cloudy";
  if (code >= 45 && code <= 48) return "Fog";
  if (code >= 51 && code <= 57) return "Drizzle";
  if (code >= 61 && code <= 67) return "Rain";
  if (code >= 71 && code <= 77) return "Snow";
  if (code >= 80 && code <= 82) return "Showers";
  if (code >= 95 && code <= 99) return "Thunderstorm";
  return "Unknown";
}

static bool connect_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_TIMEOUT_MS) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

static float read_battery_voltage() {
  analogReadResolution(12);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
  uint32_t mv = analogReadMilliVolts(BAT_ADC_PIN);
  if (mv == 0) return NAN;
  // Board uses a 2:1 divider on battery sense.
  return (mv / 1000.0f) * 2.0f;
}

static void draw_text(int x, int y, const GFXfont *font, const String &text) {
  int cursor_x = x;
  int cursor_y = y;
  writeln(font, text.c_str(), &cursor_x, &cursor_y, framebuffer);
}

static void render_screen(const TimeInfo &tinfo, const LocationInfo &loc, const WeatherInfo &winf) {
  epd_init();
  epd_poweron();
  epd_clear();
  epd_set_rotation(EPD_ROT_LANDSCAPE);

  size_t fb_size = EPD_WIDTH * EPD_HEIGHT / 2;
  framebuffer = (uint8_t *)ps_calloc(1, fb_size);
  if (!framebuffer) {
    epd_poweroff_all();
    return;
  }
  memset(framebuffer, 0xFF, fb_size);

  struct tm timeinfo;
  char time_buf[16] = "--:--";
  char date_buf[32] = "--";

  if (getLocalTime(&timeinfo, 2000)) {
    strftime(time_buf, sizeof(time_buf), "%H:%M", &timeinfo);
    strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y", &timeinfo);
  }

  String tz_line = tinfo.timezone;
  if (tinfo.abbreviation.length() > 0) {
    tz_line += " (" + tinfo.abbreviation + ")";
  }

  float vbat = read_battery_voltage();
  String vbat_line = "Battery: --";
  if (!isnan(vbat)) {
    vbat_line = "Battery: " + String(vbat, 2) + " V";
  }

  int left = 20;
  int top = 30;

  draw_text(left, top + 40, &FiraSans, String(time_buf));
  draw_text(left, top + 80, &FiraSans, String(date_buf));
  if (tinfo.ok) {
    draw_text(left, top + 115, &FiraSans, tz_line);
  }

  String wifi_line = WiFi.isConnected() ? "WiFi: connected" : "WiFi: offline";
  if (WiFi.isConnected()) {
    wifi_line += " (" + String(WiFi.RSSI()) + " dBm)";
  }
  draw_text(left, top + 160, &FiraSans, wifi_line);
  draw_text(left, top + 195, &FiraSans, vbat_line);

  int right = EPD_WIDTH / 2 + 20;
  draw_text(right, top + 40, &FiraSans, "Weather");
  if (winf.ok) {
    String temp_line = "Temp: " + String(winf.temp_c, 1) + " C";
    String wind_line = "Wind: " + String(winf.wind_kmh, 0) + " km/h";
    String code_line = String("Now: ") + weather_description(winf.code);
    draw_text(right, top + 80, &FiraSans, temp_line);
    draw_text(right, top + 115, &FiraSans, wind_line);
    draw_text(right, top + 150, &FiraSans, code_line);
  } else {
    draw_text(right, top + 80, &FiraSans, "Weather unavailable");
  }

  if (loc.ok) {
    String loc_line = loc.city;
    if (loc.region.length() > 0) loc_line += ", " + loc.region;
    if (loc.country.length() > 0) loc_line += " " + loc.country;
    draw_text(right, top + 195, &FiraSans, loc_line);
  }

  Rect_t area = {0, 0, EPD_WIDTH, EPD_HEIGHT};
  epd_draw_grayscale_image(area, framebuffer);
  epd_poweroff_all();
  free(framebuffer);
  framebuffer = nullptr;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  bool wifi_ok = connect_wifi();
  TimeInfo tinfo;
  LocationInfo loc;
  WeatherInfo winfo;

  if (wifi_ok) {
    tinfo = fetch_time_info();
    if (tinfo.ok) {
      configTime(tinfo.gmt_offset_sec, tinfo.dst_offset_sec, "pool.ntp.org", "time.nist.gov");
      struct tm timeinfo;
      getLocalTime(&timeinfo, 5000);
    }
    loc = fetch_location();
    winfo = fetch_weather(loc);
  }

  render_screen(tinfo, loc, winfo);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  uint64_t sleep_us = UPDATE_INTERVAL_MIN * 60ULL * 1000000ULL;
  esp_sleep_enable_timer_wakeup(sleep_us);
  esp_deep_sleep_start();
}

void loop() {}
