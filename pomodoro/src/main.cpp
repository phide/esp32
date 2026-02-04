#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <vector>

TFT_eSPI tft = TFT_eSPI();
Preferences prefs;
WebServer webServer(80);
// TTGO T-Display: Left button = GPIO35 (no internal pull-up), Right button = GPIO0.
const uint8_t BUTTON_LEFT_PIN = 35;
const uint8_t BUTTON_RIGHT_PIN = 0;

const char* NTP_SERVER = "0.de.pool.ntp.org";
const char* TZ_EUROPE_BERLIN = "CET-1CEST,M3.5.0/2,M10.5.0/3";
const uint32_t WIFI_TRY_TIMEOUT_MS = 12000;
const uint32_t WIFI_RETRY_GAP_MS = 2000;

const uint32_t DEBOUNCE_MS = 30;
const uint32_t LONG_PRESS_MS = 2000;
const uint32_t START_IDLE_CLOCK_MS = 60000;
const char* PREFS_NAMESPACE = "pomodoro";
const char* PREFS_MODE_KEY = "mode_idx";
const char* PREFS_WIFI_KEY = "wifi_json";
const char* PREFS_AP_SSID_KEY = "ap_ssid";
const char* PREFS_AP_PASS_KEY = "ap_pass";
const int MAX_WIFI_NETWORKS = 8;
const char* DEFAULT_AP_SSID = "Timer";

struct RgbColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

const RgbColor FOCUS_RGB = {40, 220, 120};
const RgbColor SHORT_RGB = {60, 170, 255};
const RgbColor LONG_RGB = {255, 150, 0};
const RgbColor MUTED_RGB = {160, 160, 160};

struct ModeConfig {
  const char* label;
  uint32_t focusMs;
  uint32_t shortBreakMs;
  uint32_t longBreakMs;
};

const ModeConfig MODES[] = {
  {"25/10", 25UL * 60UL * 1000UL, 10UL * 60UL * 1000UL, 15UL * 60UL * 1000UL},
  {"20/10", 20UL * 60UL * 1000UL, 10UL * 60UL * 1000UL, 15UL * 60UL * 1000UL},
  {"25/5", 25UL * 60UL * 1000UL, 5UL * 60UL * 1000UL, 15UL * 60UL * 1000UL},
  {"15/5", 15UL * 60UL * 1000UL, 5UL * 60UL * 1000UL, 15UL * 60UL * 1000UL}
};
const int MODE_COUNT = sizeof(MODES) / sizeof(MODES[0]);

enum Phase {
  PHASE_FOCUS,
  PHASE_SHORT_BREAK,
  PHASE_LONG_BREAK
};

enum ScreenState {
  SCREEN_START,
  SCREEN_TIMER
};

enum ButtonEvent {
  BUTTON_EVENT_NONE,
  BUTTON_EVENT_SHORT,
  BUTTON_EVENT_LONG
};

struct WifiEntry {
  String ssid;
  String password;
};

const char* labelForPhase(Phase phase);
void startPhase(Phase phase, bool running);
void renderTimerScreen(bool force);
void renderStartScreen(bool force, uint32_t nowMs);
void toggleRunning();
void advancePhase(bool countFocusCompletion, bool keepRunning);
void resetCurrentPhase();
void saveSelectedMode();
void loadWifiConfig();
void saveWifiConfig();

struct ButtonState {
  uint8_t pin;
  bool activeLow;
  bool stablePressed;
  bool lastReading;
  uint32_t lastDebounceMs;
  uint32_t pressedMs;
  bool longPressFired;
};

ButtonState leftButton = {BUTTON_LEFT_PIN, true, false, false, 0, 0, false};
ButtonState rightButton = {BUTTON_RIGHT_PIN, true, false, false, 0, 0, false};

Phase currentPhase = PHASE_FOCUS;
ScreenState screenState = SCREEN_START;
bool isRunning = false;
uint32_t phaseStartMs = 0;
uint32_t pausedElapsedMs = 0;
uint32_t currentDurationMs = 0;
int completedFocusSessions = 0;
int selectedModeIndex = 0;
int activeModeIndex = 0;

uint16_t colorFocus = 0;
uint16_t colorShort = 0;
uint16_t colorLong = 0;
uint16_t colorMuted = 0;

uint32_t lastRemainingSeconds = 0xFFFFFFFFUL;
Phase lastPhase = PHASE_FOCUS;
bool lastRunning = false;
int lastCompletedFocus = -1;
int lastStartModeIndex = -1;
int lastClockHour = -1;
int lastClockMinute = -1;
bool lastClockValid = false;
bool lastStartShowClock = false;
uint32_t lastInputMs = 0;
bool lastWifiConnected = false;
bool webServerStarted = false;

std::vector<WifiEntry> wifiList;
String apSsid = DEFAULT_AP_SSID;
String apPass = "";
bool apActive = false;

int wifiIndex = 0;
bool wifiConnecting = false;
bool timeConfigured = false;
uint32_t wifiAttemptStartMs = 0;
uint32_t wifiNextAttemptMs = 0;

void startSoftAP() {
  if (apActive) {
    return;
  }
  if (apSsid.length() == 0) {
    apSsid = DEFAULT_AP_SSID;
  }
  WiFi.mode(WIFI_AP_STA);
  if (apPass.length() >= 8) {
    WiFi.softAP(apSsid.c_str(), apPass.c_str());
  } else {
    WiFi.softAP(apSsid.c_str());
  }
  apActive = true;
}

void stopSoftAP() {
  if (!apActive) {
    return;
  }
  WiFi.softAPdisconnect(true);
  apActive = false;
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
  }
}

void startWifiAttempt(int index) {
  if (wifiList.empty()) {
    return;
  }
  const WifiEntry& net = wifiList[index];
  WiFi.disconnect(true);
  delay(50);
  WiFi.begin(net.ssid.c_str(), net.password.c_str());
  wifiAttemptStartMs = millis();
  wifiConnecting = true;
}

void beginWifiSetup() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  wifiIndex = 0;
  wifiConnecting = false;
  wifiNextAttemptMs = millis();
  if (wifiList.empty()) {
    startSoftAP();
  }
}

String currentWifiLabel() {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.SSID();
  }
  if (apActive) {
    return String("AP: ") + apSsid;
  }
  return "Offline";
}

String currentIpLabel() {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }
  if (apActive) {
    return WiFi.softAPIP().toString();
  }
  return "-";
}

void resetWifiAttempts() {
  wifiIndex = 0;
  wifiConnecting = false;
  wifiNextAttemptMs = millis();
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(true);
  }
}

void updateWifiAndTime(uint32_t nowMs) {
  if (!webServerStarted) {
    webServerStarted = true;
      webServer.on("/", []() {
        String html;
        html.reserve(4096);
        html += "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Pomodoro</title>"
                "<style>"
                ":root{--bg:#0f141a;--card:#151c25;--text:#e6f0ff;--muted:#92a1b3;"
                "--accent:#5aa7ff;--ok:#5bd693;--warn:#ffb84d;}"
                "*{box-sizing:border-box}body{margin:0;padding:18px;font-family:ui-sans-serif,system-ui,"
                "-apple-system,Segoe UI,Roboto,Helvetica,Arial; background:linear-gradient(160deg,#0b1118,#141d2a);"
                "color:var(--text)}"
                ".wrap{max-width:720px;margin:0 auto;}"
                ".title{font-size:22px;font-weight:700;letter-spacing:.5px;margin:6px 0 14px;}"
                ".card{background:var(--card);border:1px solid #223044;border-radius:14px;padding:14px 16px;"
                "box-shadow:0 10px 30px rgba(0,0,0,.25)}"
                ".row{display:flex;gap:12px;flex-wrap:wrap;align-items:center;}"
                ".pill{padding:6px 10px;border-radius:999px;font-size:12px;background:#223044;color:var(--muted);}"
                ".pill.ok{background:rgba(91,214,147,.15);color:var(--ok)}"
                ".pill.warn{background:rgba(255,184,77,.15);color:var(--warn)}"
                ".pill.link{cursor:pointer;text-decoration:none}"
                ".stat{font-size:14px;color:var(--muted)}"
                ".big{font-size:28px;font-weight:700}"
                ".btns{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px;margin-top:12px;}"
                "a.btn{display:block;text-align:center;padding:10px 12px;border-radius:10px;"
                "border:1px solid #2a3a54;background:#1d2736;color:var(--text);text-decoration:none;font-weight:600}"
                "a.btn.primary{background:var(--accent);border-color:#3f7ed1;color:#07111e}"
                ".modes{margin-top:12px;display:grid;grid-template-columns:repeat(auto-fit,minmax(90px,1fr));gap:8px;}"
                "a.mode{padding:8px 10px;border-radius:10px;border:1px solid #2a3a54;text-decoration:none;color:var(--text);"
                "text-align:center}"
                "a.mode.active{border-color:var(--accent);box-shadow:0 0 0 1px var(--accent) inset}"
                "</style></head><body><div class='wrap'>";
        html += "<div class='title'>Pomodoro</div>";
        html += "<div class='card'>";
        html += "<div class='row'>";
        html += "<a class='pill link ";
        html += (WiFi.status() == WL_CONNECTED ? "ok" : "warn");
        html += "' href='/wifi'>WLAN: ";
        html += currentWifiLabel();
        html += "</a>";
        html += "<div class='pill'>IP: ";
        html += currentIpLabel();
        html += "</div>";
        html += "</div>";
        html += "<div class='row' style='margin-top:10px'>";
        html += "<div class='stat'>Status</div><div class='big'>";
        html += (screenState == SCREEN_TIMER ? "Timer" : "Start");
        html += "</div>";
        html += "</div>";
        html += "<div class='row' style='margin-top:6px'>";
        html += "<div class='stat'>Phase</div><div class='big'>";
        html += labelForPhase(currentPhase);
        html += "</div>";
        html += "</div>";
        html += "<div class='row' style='margin-top:6px'>";
        html += "<div class='stat'>Running</div><div class='big'>";
        html += (isRunning ? "Ja" : "Nein");
        html += "</div>";
        html += "</div>";
        html += "<div class='row' style='margin-top:6px'>";
        html += "<div class='stat'>Modus</div><div class='big'>";
        html += MODES[selectedModeIndex].label;
        html += "</div>";
        html += "</div>";
        html += "<div class='btns'>";
        html += "<a class='btn primary' href='/start'>Start</a>";
        html += "<a class='btn' href='/pause'>Pause/Resume</a>";
        html += "<a class='btn' href='/next'>Next Phase</a>";
        html += "<a class='btn' href='/reset'>Reset Phase</a>";
        html += "<a class='btn' href='/home'>Start Menu</a>";
        html += "</div>";
        html += "<div class='stat' style='margin-top:12px'>Modus setzen</div>";
        html += "<div class='modes'>";
        for (int i = 0; i < MODE_COUNT; i++) {
          html += "<a class='mode";
          if (i == selectedModeIndex) {
            html += " active";
          }
          html += "' href='/mode?i=";
          html += i;
          html += "'>";
          html += MODES[i].label;
          html += "</a>";
        }
        html += "</div></div></div></body></html>";
        webServer.send(200, "text/html", html);
      });

      webServer.on("/start", []() {
        activeModeIndex = selectedModeIndex;
        completedFocusSessions = 0;
        screenState = SCREEN_TIMER;
        startPhase(PHASE_FOCUS, true);
        lastRemainingSeconds = 0xFFFFFFFFUL;
        lastPhase = currentPhase;
        lastRunning = isRunning;
        lastCompletedFocus = -1;
        lastInputMs = millis();
        renderTimerScreen(true);
        webServer.sendHeader("Location", "/");
        webServer.send(303);
      });

      webServer.on("/pause", []() {
        toggleRunning();
        renderTimerScreen(true);
        webServer.sendHeader("Location", "/");
        webServer.send(303);
      });

      webServer.on("/next", []() {
        bool countFocusCompletion = (currentPhase == PHASE_FOCUS);
        advancePhase(countFocusCompletion, isRunning);
        renderTimerScreen(true);
        webServer.sendHeader("Location", "/");
        webServer.send(303);
      });

      webServer.on("/reset", []() {
        resetCurrentPhase();
        renderTimerScreen(true);
        webServer.sendHeader("Location", "/");
        webServer.send(303);
      });

      webServer.on("/home", []() {
        screenState = SCREEN_START;
        startPhase(PHASE_FOCUS, false);
        completedFocusSessions = 0;
        lastRemainingSeconds = 0xFFFFFFFFUL;
        lastPhase = currentPhase;
        lastRunning = isRunning;
        lastCompletedFocus = -1;
        lastInputMs = millis();
        renderStartScreen(true, lastInputMs);
        webServer.sendHeader("Location", "/");
        webServer.send(303);
      });

      webServer.on("/mode", []() {
        if (webServer.hasArg("i")) {
          int idx = webServer.arg("i").toInt();
          if (idx >= 0 && idx < MODE_COUNT) {
            selectedModeIndex = idx;
            saveSelectedMode();
            lastInputMs = millis();
            if (screenState == SCREEN_START) {
              renderStartScreen(true, lastInputMs);
            }
          }
        }
        webServer.sendHeader("Location", "/");
        webServer.send(303);
      });

      webServer.on("/wifi", []() {
        String html;
        html.reserve(6144);
        html += "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>WLAN</title>"
                "<style>"
                ":root{--bg:#0f141a;--card:#151c25;--text:#e6f0ff;--muted:#92a1b3;"
                "--accent:#5aa7ff;--ok:#5bd693;--warn:#ffb84d;}"
                "*{box-sizing:border-box}body{margin:0;padding:18px;font-family:ui-sans-serif,system-ui,"
                "-apple-system,Segoe UI,Roboto,Helvetica,Arial; background:linear-gradient(160deg,#0b1118,#141d2a);"
                "color:var(--text)}"
                ".wrap{max-width:720px;margin:0 auto;}"
                ".title{font-size:22px;font-weight:700;letter-spacing:.5px;margin:6px 0 14px;}"
                ".card{background:var(--card);border:1px solid #223044;border-radius:14px;padding:14px 16px;"
                "box-shadow:0 10px 30px rgba(0,0,0,.25);margin-bottom:14px}"
                ".row{display:flex;gap:12px;flex-wrap:wrap;align-items:center;}"
                ".pill{padding:6px 10px;border-radius:999px;font-size:12px;background:#223044;color:var(--muted);text-decoration:none}"
                "a.btn{display:inline-block;text-align:center;padding:8px 12px;border-radius:10px;"
                "border:1px solid #2a3a54;background:#1d2736;color:var(--text);text-decoration:none;font-weight:600}"
                "button.btn{padding:8px 12px;border-radius:10px;border:1px solid #2a3a54;background:#1d2736;"
                "color:var(--text);font-weight:600}"
                "input,select{background:#0f141a;border:1px solid #2a3a54;color:var(--text);border-radius:8px;"
                "padding:8px 10px}"
                ".list{display:flex;flex-direction:column;gap:8px}"
                ".item{display:flex;align-items:center;gap:10px;padding:10px;border:1px solid #26344a;"
                "border-radius:10px;background:#121a24}"
                ".handle{cursor:grab;color:var(--muted);font-size:18px;user-select:none}"
                ".spacer{flex:1}"
                ".muted{color:var(--muted);font-size:12px}"
                "</style></head><body><div class='wrap'>";
        html += "<div class='title'>WLAN</div>";
        html += "<div class='card'><div class='row'>";
        html += "<a class='pill' href='/'>Zurueck</a>";
        html += "<div class='pill'>Aktiv: ";
        html += currentWifiLabel();
        html += "</div>";
        html += "<div class='pill'>IP: ";
        html += currentIpLabel();
        html += "</div>";
        html += "</div></div>";

        html += "<div class='card'>";
        html += "<div class='row'><div class='muted'>Gespeicherte Netzwerke (Drag = Prioritaet)</div></div>";
        html += "<div id='wifi-list' class='list'>";
        for (int i = 0; i < (int)wifiList.size(); i++) {
          html += "<div class='item wifi-item' draggable='true' data-idx='";
          html += i;
          html += "'>";
          html += "<div class='handle'>|||</div>";
          html += "<div>";
          html += wifiList[i].ssid;
          html += "</div>";
          html += "<div class='spacer'></div>";
          html += "<form method='post' action='/wifi/delete'>";
          html += "<input type='hidden' name='i' value='";
          html += i;
          html += "'>";
          html += "<button class='btn' type='submit'>Loeschen</button>";
          html += "</form>";
          html += "</div>";
        }
        if (wifiList.empty()) {
          html += "<div class='muted'>Noch keine Netzwerke gespeichert.</div>";
        }
        html += "</div>";
        html += "<form id='orderForm' method='post' action='/wifi/order' style='margin-top:10px'>";
        html += "<input type='hidden' id='orderInput' name='order' value=''>";
        html += "<button class='btn' type='submit'>Reihenfolge speichern</button>";
        html += "</form>";
        html += "</div>";

        html += "<div class='card'>";
        html += "<div class='row'><div class='muted'>Netzwerk hinzufuegen</div></div>";
        html += "<form method='post' action='/wifi/add' style='margin-top:8px'>";
        html += "<div class='row'><input id='ssidInput' name='ssid' placeholder='SSID' required>";
        html += "<input name='pass' placeholder='Passwort (optional)'>";
        html += "<button class='btn' type='submit'>Hinzufuegen</button></div>";
        html += "</form>";
        html += "<div class='row' style='margin-top:10px'>";
        html += "<select id='scanSelect' style='min-width:220px'></select>";
        html += "<button class='btn' type='button' onclick='scan()'>Scan</button>";
        html += "<button class='btn' type='button' onclick='useSelected()'>Uebernehmen</button>";
        html += "</div>";
        html += "</div>";

        html += "<div class='card'>";
        html += "<div class='row'><div class='muted'>Access Point (Fallback)</div></div>";
        html += "<form method='post' action='/wifi/ap' style='margin-top:8px'>";
        html += "<div class='row'><input name='ap_ssid' value='";
        html += apSsid;
        html += "' placeholder='AP SSID'>";
        html += "<input name='ap_pass' value='";
        html += apPass;
        html += "' placeholder='AP Passwort (leer = offen)'>";
        html += "<button class='btn' type='submit'>Speichern</button></div>";
        html += "<div class='muted' style='margin-top:6px'>Passwort muss >= 8 Zeichen sein, sonst offenes WLAN.</div>";
        html += "</form></div>";

        html += "<script>"
                "const list=document.getElementById('wifi-list');"
                "let dragEl=null;"
                "function updateOrder(){const ids=[...list.children].filter(x=>x.dataset.idx!==undefined)"
                ".map(el=>el.dataset.idx);document.getElementById('orderInput').value=ids.join(',');}"
                "function enableDrag(){[...document.querySelectorAll('.wifi-item')].forEach(el=>{"
                "el.addEventListener('dragstart',e=>{dragEl=el;e.dataTransfer.effectAllowed='move';});"
                "el.addEventListener('dragover',e=>{e.preventDefault();const t=e.currentTarget;"
                "if(t===dragEl)return;const r=t.getBoundingClientRect();"
                "const next=(e.clientY-r.top)>(r.height/2);"
                "list.insertBefore(dragEl,next?t.nextSibling:t);updateOrder();});"
                "});updateOrder();}"
                "async function scan(){const res=await fetch('/wifi/scan');"
                "const data=await res.json();const sel=document.getElementById('scanSelect');"
                "sel.innerHTML='';(data.networks||[]).forEach(n=>{const o=document.createElement('option');"
                "o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm'+(n.open?', offen':'')+')';"
                "sel.appendChild(o);});}"
                "function useSelected(){const sel=document.getElementById('scanSelect');"
                "if(sel.value){document.getElementById('ssidInput').value=sel.value;}}"
                "enableDrag();"
                "</script>";

        html += "</div></body></html>";
        webServer.send(200, "text/html", html);
      });

      webServer.on("/wifi/scan", []() {
        int n = WiFi.scanNetworks();
        StaticJsonDocument<1024> doc;
        JsonArray arr = doc.createNestedArray("networks");
        for (int i = 0; i < n; i++) {
          JsonObject obj = arr.createNestedObject();
          obj["ssid"] = WiFi.SSID(i);
          obj["rssi"] = WiFi.RSSI(i);
          obj["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        }
        WiFi.scanDelete();
        String out;
        serializeJson(doc, out);
        webServer.send(200, "application/json", out);
      });

      webServer.on("/wifi/add", []() {
        String ssid = webServer.arg("ssid");
        String pass = webServer.arg("pass");
        ssid.trim();
        if (ssid.length() > 0) {
          bool updated = false;
          for (auto& entry : wifiList) {
            if (entry.ssid == ssid) {
              entry.password = pass;
              updated = true;
              break;
            }
          }
          if (!updated && (int)wifiList.size() < MAX_WIFI_NETWORKS) {
            WifiEntry entry;
            entry.ssid = ssid;
            entry.password = pass;
            wifiList.push_back(entry);
          }
          saveWifiConfig();
          resetWifiAttempts();
        }
        webServer.sendHeader("Location", "/wifi");
        webServer.send(303);
      });

      webServer.on("/wifi/delete", []() {
        int idx = webServer.arg("i").toInt();
        if (idx >= 0 && idx < (int)wifiList.size()) {
          wifiList.erase(wifiList.begin() + idx);
          saveWifiConfig();
          resetWifiAttempts();
        }
        webServer.sendHeader("Location", "/wifi");
        webServer.send(303);
      });

      webServer.on("/wifi/order", []() {
        String order = webServer.arg("order");
        std::vector<WifiEntry> newList;
        int start = 0;
        while (start < order.length()) {
          int comma = order.indexOf(',', start);
          if (comma < 0) {
            comma = order.length();
          }
          String token = order.substring(start, comma);
          int idx = token.toInt();
          if (idx >= 0 && idx < (int)wifiList.size()) {
            newList.push_back(wifiList[idx]);
          }
          start = comma + 1;
        }
        if (!newList.empty()) {
          wifiList = newList;
          saveWifiConfig();
          resetWifiAttempts();
        }
        webServer.sendHeader("Location", "/wifi");
        webServer.send(303);
      });

      webServer.on("/wifi/ap", []() {
        String ssid = webServer.arg("ap_ssid");
        String pass = webServer.arg("ap_pass");
        ssid.trim();
        apSsid = ssid.length() ? ssid : DEFAULT_AP_SSID;
        apPass = pass;
        saveWifiConfig();
        if (WiFi.status() != WL_CONNECTED) {
          stopSoftAP();
          startSoftAP();
        }
        webServer.sendHeader("Location", "/wifi");
        webServer.send(303);
      });

      webServer.begin();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!timeConfigured) {
      configTzTime(TZ_EUROPE_BERLIN, NTP_SERVER);
      timeConfigured = true;
    }
    stopSoftAP();
    wifiConnecting = false;
    return;
  }

  if (wifiList.empty()) {
    startSoftAP();
    wifiConnecting = false;
    return;
  }

  startSoftAP();

  if (!wifiConnecting) {
    if (nowMs >= wifiNextAttemptMs) {
      if (wifiIndex < 0 || wifiIndex >= (int)wifiList.size()) {
        wifiIndex = 0;
      }
      startWifiAttempt(wifiIndex);
    }
    return;
  }

  if ((nowMs - wifiAttemptStartMs) >= WIFI_TRY_TIMEOUT_MS) {
    wifiConnecting = false;
    wifiIndex = (wifiIndex + 1) % (int)wifiList.size();
    wifiNextAttemptMs = nowMs + WIFI_RETRY_GAP_MS;
  }
}

bool getLocalTimeInfo(char* out, size_t outSize, int* hourOut, int* minuteOut) {
  if (!timeConfigured) {
    return false;
  }
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 50)) {
    return false;
  }
  if (hourOut) {
    *hourOut = timeinfo.tm_hour;
  }
  if (minuteOut) {
    *minuteOut = timeinfo.tm_min;
  }
  snprintf(out, outSize, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  return true;
}

void drawWifiIndicator(bool connected, bool force) {
  const int baseX = 6;
  const int baseY = 6;
  const uint16_t color = connected ? colorShort : colorMuted;

  if (force) {
    tft.fillRect(baseX, baseY, 16, 12, TFT_BLACK);
    // Three vertical rounded bars (small -> medium -> large).
    const int barW = 3;
    const int gap = 2;
    const int x1 = baseX;
    const int x2 = x1 + barW + gap;
    const int x3 = x2 + barW + gap;
    tft.fillRoundRect(x1, baseY + 6, barW, 6, 1, color);
    tft.fillRoundRect(x2, baseY + 3, barW, 9, 1, color);
    tft.fillRoundRect(x3, baseY, barW, 12, 1, color);
  }
}

void drawClockAt(const char* clockStr, int x, int y, uint16_t color, bool force) {
  const int clockTextSize = 1;
  const int clockBoxW = 5 * 6 * clockTextSize;
  const int clockBoxH = 8 * clockTextSize;

  if (force) {
    tft.fillRect(x, y, clockBoxW, clockBoxH, TFT_BLACK);
    tft.setTextSize(clockTextSize);
    tft.setTextColor(color, TFT_BLACK);
    tft.setCursor(x, y);
    tft.print(clockStr);
  }
}

void loadSavedMode() {
  prefs.begin(PREFS_NAMESPACE, true);
  int saved = prefs.getInt(PREFS_MODE_KEY, 0);
  prefs.end();
  if (saved < 0 || saved >= MODE_COUNT) {
    saved = 0;
  }
  selectedModeIndex = saved;
}

void saveSelectedMode() {
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putInt(PREFS_MODE_KEY, selectedModeIndex);
  prefs.end();
}

void loadWifiConfig() {
  prefs.begin(PREFS_NAMESPACE, true);
  String json = prefs.getString(PREFS_WIFI_KEY, "");
  apSsid = prefs.getString(PREFS_AP_SSID_KEY, DEFAULT_AP_SSID);
  apPass = prefs.getString(PREFS_AP_PASS_KEY, "");
  prefs.end();

  wifiList.clear();
  if (json.length() == 0) {
    return;
  }

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    return;
  }
  JsonArray arr = doc.as<JsonArray>();
  for (JsonVariant v : arr) {
    if (!v.is<JsonObject>()) {
      continue;
    }
    String ssid = v["s"] | "";
    String pass = v["p"] | "";
    if (ssid.length() == 0) {
      continue;
    }
    WifiEntry entry;
    entry.ssid = ssid;
    entry.password = pass;
    wifiList.push_back(entry);
    if ((int)wifiList.size() >= MAX_WIFI_NETWORKS) {
      break;
    }
  }
}

void saveWifiConfig() {
  StaticJsonDocument<1024> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& entry : wifiList) {
    JsonObject obj = arr.createNestedObject();
    obj["s"] = entry.ssid;
    obj["p"] = entry.password;
  }
  String json;
  serializeJson(doc, json);

  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putString(PREFS_WIFI_KEY, json);
  prefs.putString(PREFS_AP_SSID_KEY, apSsid);
  prefs.putString(PREFS_AP_PASS_KEY, apPass);
  prefs.end();
}

uint32_t durationForPhase(Phase phase) {
  const ModeConfig& mode = MODES[activeModeIndex];
  if (phase == PHASE_FOCUS) {
    return mode.focusMs;
  }
  if (phase == PHASE_SHORT_BREAK) {
    return mode.shortBreakMs;
  }
  return mode.longBreakMs;
}

const char* labelForPhase(Phase phase) {
  if (phase == PHASE_FOCUS) {
    return "FOKUS";
  }
  if (phase == PHASE_SHORT_BREAK) {
    return "KURZPAUSE";
  }
  return "LANGPAUSE";
}

uint16_t colorForPhase(Phase phase) {
  if (phase == PHASE_FOCUS) {
    return colorFocus;
  }
  if (phase == PHASE_SHORT_BREAK) {
    return colorShort;
  }
  return colorLong;
}

bool isPressedRaw(const ButtonState& button) {
  int level = digitalRead(button.pin);
  return button.activeLow ? (level == LOW) : (level == HIGH);
}

void initButton(ButtonState& button, bool usePullup) {
  pinMode(button.pin, usePullup ? INPUT_PULLUP : INPUT);
  bool pressed = isPressedRaw(button);
  button.stablePressed = pressed;
  button.lastReading = pressed;
  button.lastDebounceMs = millis();
  button.pressedMs = 0;
  button.longPressFired = false;
}

ButtonEvent updateButton(ButtonState& button, uint32_t nowMs) {
  bool reading = isPressedRaw(button);
  if (reading != button.lastReading) {
    button.lastDebounceMs = nowMs;
    button.lastReading = reading;
  }

  if ((nowMs - button.lastDebounceMs) > DEBOUNCE_MS) {
    if (reading != button.stablePressed) {
      button.stablePressed = reading;
      if (button.stablePressed) {
        button.pressedMs = nowMs;
        button.longPressFired = false;
      } else {
        if (!button.longPressFired) {
          return BUTTON_EVENT_SHORT;
        }
      }
    }
  }

  if (button.stablePressed && !button.longPressFired &&
      (nowMs - button.pressedMs >= LONG_PRESS_MS)) {
    button.longPressFired = true;
    return BUTTON_EVENT_LONG;
  }

  return BUTTON_EVENT_NONE;
}

uint32_t currentElapsedMs() {
  if (isRunning) {
    return millis() - phaseStartMs;
  }
  return pausedElapsedMs;
}

void startPhase(Phase phase, bool running) {
  currentPhase = phase;
  currentDurationMs = durationForPhase(phase);
  pausedElapsedMs = 0;
  if (running) {
    phaseStartMs = millis();
  }
  isRunning = running;
}

void pauseTimer() {
  if (!isRunning) {
    return;
  }
  pausedElapsedMs = millis() - phaseStartMs;
  isRunning = false;
}

void resumeTimer() {
  if (isRunning) {
    return;
  }
  phaseStartMs = millis() - pausedElapsedMs;
  isRunning = true;
}

void resetCurrentPhase() {
  pausedElapsedMs = 0;
  if (isRunning) {
    phaseStartMs = millis();
  }
}

Phase computeNextPhase(bool countFocusCompletion) {
  if (currentPhase == PHASE_FOCUS) {
    if (countFocusCompletion) {
      completedFocusSessions += 1;
    }
    if (completedFocusSessions > 4) {
      completedFocusSessions = 4;
    }
    if (completedFocusSessions >= 4) {
      return PHASE_LONG_BREAK;
    }
    return PHASE_SHORT_BREAK;
  }

  if (currentPhase == PHASE_SHORT_BREAK) {
    return PHASE_FOCUS;
  }

  completedFocusSessions = 0;
  return PHASE_FOCUS;
}

void showPhaseTransition(Phase nextPhase) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(colorForPhase(nextPhase), TFT_BLACK);
  tft.setTextSize(2);

  const char* line1 = "PHASE WECHSEL";
  int line1Width = strlen(line1) * 6 * 2;
  int line1X = (tft.width() - line1Width) / 2;
  tft.setCursor(line1X, 40);
  tft.print(line1);

  const char* line2 = labelForPhase(nextPhase);
  int line2Width = strlen(line2) * 6 * 2;
  int line2X = (tft.width() - line2Width) / 2;
  tft.setCursor(line2X, 68);
  tft.print(line2);

  const uint16_t flashColor = tft.color565(220, 220, 220);
  for (int i = 0; i < 3; i++) {
    tft.fillScreen(flashColor);
    delay(90);
    tft.fillScreen(TFT_BLACK);
    delay(90);
  }
}

void advancePhase(bool countFocusCompletion, bool keepRunning) {
  Phase next = computeNextPhase(countFocusCompletion);
  showPhaseTransition(next);
  startPhase(next, keepRunning);
}

void toggleRunning() {
  if (isRunning) {
    pauseTimer();
  } else {
    resumeTimer();
  }
}

void drawCycleDots(uint16_t color) {
  const int totalDots = 4;
  const int radius = 4;
  const int gap = 10;
  const int dotsWidth = (totalDots * radius * 2) + (gap * (totalDots - 1));
  const int startX = (tft.width() - dotsWidth) / 2;
  const int centerY = tft.height() - 30;

  for (int i = 0; i < totalDots; i++) {
    int centerX = startX + (i * (radius * 2 + gap)) + radius;
    if (i < completedFocusSessions) {
      tft.fillCircle(centerX, centerY, radius, color);
    } else {
      tft.drawCircle(centerX, centerY, radius, color);
    }
  }
}

void drawProgressBar(uint16_t borderColor, uint16_t fillColor, uint32_t elapsedMs, uint32_t durationMs) {
  const int barX = 10;
  const int barY = tft.height() - 14;
  const int barW = tft.width() - 20;
  const int barH = 8;

  tft.drawRect(barX, barY, barW, barH, borderColor);
  if (durationMs == 0) {
    return;
  }

  uint32_t fillW = (uint32_t)((uint64_t)(barW - 2) * elapsedMs / durationMs);
  if (fillW > (uint32_t)(barW - 2)) {
    fillW = barW - 2;
  }
  tft.fillRect(barX + 1, barY + 1, (int)fillW, barH - 2, fillColor);
}

int currentRoundForDisplay() {
  int round = completedFocusSessions;
  if (currentPhase == PHASE_FOCUS) {
    round += 1;
  }
  if (round < 1) {
    round = 1;
  }
  if (round > 4) {
    round = 4;
  }
  return round;
}

void drawRoundIndicator(uint16_t color) {
  char roundStr[12];
  int round = currentRoundForDisplay();
  snprintf(roundStr, sizeof(roundStr), "RUNDE %d/4", round);

  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  int roundWidth = strlen(roundStr) * 6;
  int roundX = (tft.width() - roundWidth) / 2;
  tft.setCursor(roundX, 24);
  tft.print(roundStr);
}

void renderStartScreen(bool force, uint32_t nowMs) {
  char clockStr[8];
  int hour = -1;
  int minute = -1;
  bool clockOk = getLocalTimeInfo(clockStr, sizeof(clockStr), &hour, &minute);
  if (!clockOk) {
    strncpy(clockStr, "--:--", sizeof(clockStr));
    clockStr[sizeof(clockStr) - 1] = '\0';
  }
  bool clockChanged = (clockOk != lastClockValid) ||
                      (clockOk && (hour != lastClockHour || minute != lastClockMinute));
  bool showClockInstead = (nowMs - lastInputMs) >= START_IDLE_CLOCK_MS;
  bool showModeChanged = (showClockInstead != lastStartShowClock);
  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  bool wifiChanged = (wifiConnected != lastWifiConnected);

  if (!force && selectedModeIndex == lastStartModeIndex && !clockChanged && !showModeChanged && !wifiChanged) {
    return;
  }

  tft.fillScreen(TFT_BLACK);

  tft.setTextSize(2);
  tft.setTextColor(colorFocus, TFT_BLACK);
  const char* title = "POMODORO";
  int titleWidth = strlen(title) * 6 * 2;
  int titleX = (tft.width() - titleWidth) / 2;
  tft.setCursor(titleX, 6);
  tft.print(title);

  tft.setTextSize(1);
  tft.setTextColor(colorShort, TFT_BLACK);
  const char* startLabel = "START";
  int startWidth = strlen(startLabel) * 6;
  int startX = tft.width() - startWidth - 6;
  tft.setCursor(startX, 6);
  tft.print(startLabel);

  drawWifiIndicator(wifiConnected, true);

  const char* modeLabel = showClockInstead ? clockStr : MODES[selectedModeIndex].label;
  const int modeTextSize = 6;
  tft.setTextSize(modeTextSize);
  tft.setTextColor(colorFocus, TFT_BLACK);
  int modeWidth = strlen(modeLabel) * 6 * modeTextSize;
  int modeHeight = 8 * modeTextSize;
  int modeX = (tft.width() - modeWidth) / 2;
  int modeY = (tft.height() - modeHeight) / 2;
  tft.setCursor(modeX, modeY);
  tft.print(modeLabel);

  const char* modeHint = "MODUSWECHSEL";
  tft.setTextSize(1);
  tft.setTextColor(colorMuted, TFT_BLACK);
  int hintWidth = strlen(modeHint) * 6;
  int hintX = tft.width() - hintWidth - 6;
  tft.setCursor(hintX, tft.height() - 18);
  tft.print(modeHint);

  lastStartModeIndex = selectedModeIndex;
  lastStartShowClock = showClockInstead;
  lastClockValid = clockOk;
  lastWifiConnected = wifiConnected;
  if (clockOk) {
    lastClockHour = hour;
    lastClockMinute = minute;
  }
}

void renderTimerScreen(bool force) {
  uint32_t elapsedMs = currentElapsedMs();
  uint32_t remainingMs = (elapsedMs >= currentDurationMs) ? 0 : (currentDurationMs - elapsedMs);
  uint32_t remainingSeconds = remainingMs / 1000;

  bool phaseChanged = currentPhase != lastPhase;
  bool runningChanged = isRunning != lastRunning;
  bool roundChanged = completedFocusSessions != lastCompletedFocus;
  bool timeChanged = remainingSeconds != lastRemainingSeconds;

  char clockStr[8];
  int hour = -1;
  int minute = -1;
  bool clockOk = getLocalTimeInfo(clockStr, sizeof(clockStr), &hour, &minute);
  if (!clockOk) {
    strncpy(clockStr, "--:--", sizeof(clockStr));
    clockStr[sizeof(clockStr) - 1] = '\0';
  }
  bool clockChanged = (clockOk != lastClockValid) ||
                      (clockOk && (hour != lastClockHour || minute != lastClockMinute));
  if (!force && !phaseChanged && !runningChanged && !roundChanged && !timeChanged && !clockChanged) {
    return;
  }

  bool fullRedraw = force || phaseChanged || runningChanged || roundChanged;
  if (fullRedraw) {
    tft.fillScreen(TFT_BLACK);
  }

  uint16_t phaseColor = colorForPhase(currentPhase);
  const char* phaseLabel = labelForPhase(currentPhase);

  if (fullRedraw) {
    tft.setTextColor(phaseColor, TFT_BLACK);
    tft.setTextSize(2);
    int labelWidth = strlen(phaseLabel) * 6 * 2;
    int labelX = (tft.width() - labelWidth) / 2;
    tft.setCursor(labelX, 6);
    tft.print(phaseLabel);

    drawRoundIndicator(phaseColor);

    if (clockOk) {
      drawClockAt(clockStr, 6, 6, colorMuted, true);
    }
  }

  char timeStr[6];
  uint32_t minutes = remainingSeconds / 60;
  uint32_t seconds = remainingSeconds % 60;
  snprintf(timeStr, sizeof(timeStr), "%lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);

  const int timeTextSize = 4;
  const int timeBoxW = 5 * 6 * timeTextSize;
  const int timeBoxH = 8 * timeTextSize;
  tft.setTextSize(timeTextSize);
  int timeWidth = strlen(timeStr) * 6 * timeTextSize;
  int timeBoxX = (tft.width() - timeBoxW) / 2;
  int timeBoxY = isRunning ? (tft.height() - timeBoxH) / 2 : 34;

  if (fullRedraw || timeChanged) {
    tft.fillRect(timeBoxX, timeBoxY, timeBoxW, timeBoxH, TFT_BLACK);
    int timeX = timeBoxX + (timeBoxW - timeWidth) / 2;
    tft.setTextColor(phaseColor, TFT_BLACK);
    tft.setCursor(timeX, timeBoxY);
    tft.print(timeStr);
  }

  if (fullRedraw) {
    if (!isRunning) {
      const char* pausedLabel = "PAUSIERT";
      tft.setTextSize(2);
      tft.setTextColor(colorMuted, TFT_BLACK);
      int pausedWidth = strlen(pausedLabel) * 6 * 2;
      int pausedX = (tft.width() - pausedWidth) / 2;
      tft.setCursor(pausedX, 78);
      tft.print(pausedLabel);
    }

    drawCycleDots(phaseColor);
  }

  if (fullRedraw || timeChanged) {
    drawProgressBar(phaseColor, phaseColor, elapsedMs, currentDurationMs);
  }

  if (!fullRedraw && clockChanged) {
    if (clockOk) {
      drawClockAt(clockStr, 6, 6, colorMuted, true);
    }
  }

  lastRemainingSeconds = remainingSeconds;
  lastPhase = currentPhase;
  lastRunning = isRunning;
  lastCompletedFocus = completedFocusSessions;
  lastClockValid = clockOk;
  if (clockOk) {
    lastClockHour = hour;
    lastClockMinute = minute;
  }
}

void setup() {
  Serial.begin(115200);
  loadWifiConfig();
  beginWifiSetup();
  loadSavedMode();

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  colorFocus = tft.color565(FOCUS_RGB.r, FOCUS_RGB.g, FOCUS_RGB.b);
  colorShort = tft.color565(SHORT_RGB.r, SHORT_RGB.g, SHORT_RGB.b);
  colorLong = tft.color565(LONG_RGB.r, LONG_RGB.g, LONG_RGB.b);
  colorMuted = tft.color565(MUTED_RGB.r, MUTED_RGB.g, MUTED_RGB.b);

  initButton(leftButton, false);
  initButton(rightButton, true);

  startPhase(PHASE_FOCUS, false);
  lastInputMs = millis();
  renderStartScreen(true, lastInputMs);
}

void loop() {
  uint32_t nowMs = millis();
  bool needsRedraw = false;
  updateWifiAndTime(nowMs);
  if (webServerStarted) {
    webServer.handleClient();
  }

  if (screenState == SCREEN_START) {
    ButtonEvent leftEvent = updateButton(leftButton, nowMs);
    if (leftEvent == BUTTON_EVENT_SHORT) {
      activeModeIndex = selectedModeIndex;
      completedFocusSessions = 0;
      screenState = SCREEN_TIMER;
      startPhase(PHASE_FOCUS, true);
      lastInputMs = nowMs;
      lastRemainingSeconds = 0xFFFFFFFFUL;
      lastPhase = currentPhase;
      lastRunning = isRunning;
      lastCompletedFocus = -1;
      renderTimerScreen(true);
      return;
    }

    ButtonEvent rightEvent = updateButton(rightButton, nowMs);
    if (rightEvent == BUTTON_EVENT_SHORT) {
      selectedModeIndex = (selectedModeIndex + 1) % MODE_COUNT;
      saveSelectedMode();
      lastInputMs = nowMs;
      needsRedraw = true;
    }

    renderStartScreen(needsRedraw, nowMs);
    return;
  }

  ButtonEvent leftEvent = updateButton(leftButton, nowMs);
  if (leftEvent == BUTTON_EVENT_SHORT) {
    toggleRunning();
    needsRedraw = true;
  } else if (leftEvent == BUTTON_EVENT_LONG) {
    screenState = SCREEN_START;
    startPhase(PHASE_FOCUS, false);
    completedFocusSessions = 0;
    lastRemainingSeconds = 0xFFFFFFFFUL;
    lastPhase = currentPhase;
    lastRunning = isRunning;
    lastCompletedFocus = -1;
    lastInputMs = nowMs;
    renderStartScreen(true, lastInputMs);
    return;
  }

  ButtonEvent rightEvent = updateButton(rightButton, nowMs);
  if (rightEvent == BUTTON_EVENT_LONG) {
    resetCurrentPhase();
    needsRedraw = true;
  } else if (rightEvent == BUTTON_EVENT_SHORT) {
    bool countFocusCompletion = (currentPhase == PHASE_FOCUS);
    advancePhase(countFocusCompletion, isRunning);
    needsRedraw = true;
  }

  if (isRunning && currentElapsedMs() >= currentDurationMs) {
    bool countFocusCompletion = (currentPhase == PHASE_FOCUS);
    advancePhase(countFocusCompletion, true);
    needsRedraw = true;
  }

  renderTimerScreen(needsRedraw);
}
