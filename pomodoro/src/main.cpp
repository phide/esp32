#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
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

const char* DEFAULT_NTP_SERVER = "0.de.pool.ntp.org";
const char* TZ_EUROPE_BERLIN = "CET-1CEST,M3.5.0/2,M10.5.0/3";
const char* TZ_STANDARD = "CET-1";
const char* TZ_SUMMER = "CEST-2";
const uint32_t WIFI_TRY_TIMEOUT_MS = 12000;
const uint32_t WIFI_RETRY_GAP_MS = 2000;

const uint32_t DEBOUNCE_MS = 30;
const uint32_t LONG_PRESS_MS = 2000;
const uint32_t DOUBLE_TAP_MS = 350;
const uint32_t START_IDLE_CLOCK_MS = 60000;
const char* PREFS_NAMESPACE = "pomodoro";
const char* PREFS_MODE_KEY = "mode_idx";
const char* PREFS_APP_KEY = "app_idx";
const char* PREFS_CLOCK_COLOR_KEY = "clock_color";
const char* PREFS_CLOCK_SIZE_KEY = "clock_size";
const char* PREFS_WIFI_KEY = "wifi_json";
const char* PREFS_AP_SSID_KEY = "ap_ssid";
const char* PREFS_AP_PASS_KEY = "ap_pass";
const char* PREFS_NTP_SERVER_KEY = "ntp_server";
const char* PREFS_DST_MODE_KEY = "dst_mode";
const char* PREFS_MODES_KEY = "modes_json";
const char* PREFS_AI_HOST_KEY = "ai_host";
const char* PREFS_AI_WIFI_KEY = "ai_wifi";
const char* PREFS_AI_SYSTEM_KEY = "ai_system";
const char* PREFS_AI_MODEL_KEY = "ai_model";
const float WEATHER_LAT = 53.5737f;
const float WEATHER_LON = 9.9001f;
const uint32_t WEATHER_REFRESH_MS = 10UL * 60UL * 1000UL;
const int MAX_WIFI_NETWORKS = 8;
const int MAX_MODES = 8;
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

struct ModeEntry {
  String label;
  int focusMin;
  int shortMin;
  int longMin;
  String focusColor;
  String shortColor;
  String longColor;
};

enum Phase {
  PHASE_FOCUS,
  PHASE_SHORT_BREAK,
  PHASE_LONG_BREAK
};

enum ScreenState {
  SCREEN_APP_SELECT,
  SCREEN_START,
  SCREEN_TIMER,
  SCREEN_CLOCK,
  SCREEN_AI,
  SCREEN_WEATHER,
  SCREEN_SNAKE,
  SCREEN_FLAPPY
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

struct ModeEntry;

enum DstMode {
  DST_AUTO = 0,
  DST_STANDARD = 1,
  DST_SUMMER = 2
};

const char* labelForPhase(Phase phase);
const char* labelForApp(int appIndex);
bool isAiAvailable();
void fetchWeather(bool force);
void renderWeatherScreen(bool force);
int snakeCols();
int snakeRows();
void initSnakeGame();
void updateSnakeGame(uint32_t nowMs);
void renderSnakeScreen(bool force);
void initFlappyGame();
void updateFlappyGame(uint32_t nowMs);
void renderFlappyScreen(bool force);
void startPhase(Phase phase, bool running);
void renderTimerScreen(bool force);
void renderStartScreen(bool force, uint32_t nowMs);
void renderAppSelectScreen(bool force);
void renderClockScreen(bool force);
void renderAiScreen(bool force);
void toggleRunning();
void advancePhase(bool countFocusCompletion, bool keepRunning);
void resetCurrentPhase();
void saveSelectedMode();
void loadWifiConfig();
void saveWifiConfig();
uint32_t currentElapsedMs();
void loadModesConfig();
void saveModesConfig();
String rgbToHex(const RgbColor& rgb);
void switchToApp(int appIndex);
bool getLocalTimeInfo(char* out, size_t outSize, int* hourOut, int* minuteOut);

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
int selectedAppIndex = 0;
const int APP_POMODORO = 0;
const int APP_CLOCK = 1;
const int APP_AI = 2;
const int APP_WEATHER = 3;
const int APP_SNAKE = 4;
const int APP_FLAPPY = 5;

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
int lastAppIndex = -1;

std::vector<WifiEntry> wifiList;
std::vector<ModeEntry> modes;
String apSsid = DEFAULT_AP_SSID;
String apPass = "";
String ntpServer = DEFAULT_NTP_SERVER;
int dstMode = DST_AUTO;
String clockColor = "";
int clockSizeIndex = 1;
String aiHost = "";
String aiWifiSsid = "";
String aiModel = "llama3.2:3b";
String aiSystemMessage = "";
String aiTypingText = "";
String aiResponseText = "";
int aiScrollOffset = 0;
bool aiGenerating = false;

struct WeatherData {
  bool valid;
  uint32_t updatedMs;
  float temp;
  float feels;
  float wind;
  float precipProb;
  float rain;
  float snow;
  int code;
  int nextHour[3];
  float nextTemp[3];
  int nextProb[3];
  int nextCode[3];
};

WeatherData weather = {false, 0, 0, 0, 0, 0, 0, 0, 0, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
int weatherViewIndex = 0;

// Snake game state
bool snakeInit = false;
int snakeDir = 0; // 0=up,1=right,2=down,3=left
int snakeLen = 3;
int snakeX[128];
int snakeY[128];
int snakeFoodX = 0;
int snakeFoodY = 0;
uint32_t snakeLastStepMs = 0;
bool snakeGameOver = false;
bool snakeDirty = true;
int snakePrevHeadX = 0;
int snakePrevHeadY = 0;
int snakePrevTailX = 0;
int snakePrevTailY = 0;
bool snakePrevTailValid = false;
bool snakePrevHeadValid = false;
bool snakeGrewLast = false;
bool snakeFoodChanged = false;

// Flappy game state
bool flappyInit = false;
float flappyY = 40.0f;
float flappyVel = 0.0f;
int flappyGapY = 40;
int flappyGapX = 120;
int flappyScore = 0;
bool flappyGameOver = false;
uint32_t flappyLastMs = 0;
bool flappyDirty = true;
int flappyPrevGapX = 0;
int flappyPrevGapY = 0;
float flappyPrevY = 0.0f;
bool flappyPrevValid = false;

bool leftPending = false;
uint32_t leftPendingMs = 0;
int leftPendingAction = 0;
bool apActive = false;

int wifiIndex = 0;
bool wifiConnecting = false;
bool timeConfigured = false;
uint32_t wifiAttemptStartMs = 0;
uint32_t wifiNextAttemptMs = 0;
uint32_t bothPressStartMs = 0;
bool bothPressHandled = false;

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


const char* tzForMode() {
  if (dstMode == DST_STANDARD) {
    return TZ_STANDARD;
  }
  if (dstMode == DST_SUMMER) {
    return TZ_SUMMER;
  }
  return TZ_EUROPE_BERLIN;
}

void applyTimeConfig() {
  if (ntpServer.length() == 0) {
    ntpServer = DEFAULT_NTP_SERVER;
  }
  configTzTime(tzForMode(), ntpServer.c_str());
  timeConfigured = true;
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
                ".grid{display:grid;grid-template-columns:1fr 280px;gap:14px;align-items:start;}"
                ".grid.one{grid-template-columns:1fr;}"
                ".panel{border:1px solid #223044;border-radius:12px;background:#111822;padding:12px}"
                ".panel.time{min-height:140px;display:flex;flex-direction:column;justify-content:center;}"
                ".timerBox{font-size:58px;font-weight:700;letter-spacing:1px;text-align:center}"
                ".timerSub{font-size:12px;color:var(--muted);text-align:center;margin-top:4px}"
                ".pill{padding:6px 10px;border-radius:999px;font-size:12px;background:#223044;color:var(--muted);}"
                ".pill.ok{background:rgba(91,214,147,.15);color:var(--ok)}"
                ".pill.warn{background:rgba(255,184,77,.15);color:var(--warn)}"
                ".pill.link{cursor:pointer;text-decoration:none}"
                ".stat{font-size:14px;color:var(--muted)}"
                ".big{font-size:28px;font-weight:700}"
                ".btns{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px;margin-top:12px;}"
                "a.btn,button.btn{display:block;text-align:center;padding:10px 12px;border-radius:10px;"
                "border:1px solid #2a3a54;background:#1d2736;color:var(--text);text-decoration:none;font-weight:600}"
                "a.btn.disabled,button.btn.disabled{opacity:.45;pointer-events:none}"
                "a.btn.primary,button.btn.primary{background:var(--accent);border-color:#3f7ed1;color:#07111e}"
                "a.btn.save,button.btn.save{background:#59d98e;border-color:#3aa66b;color:#07111e}"
                ".modes{margin-top:12px;display:grid;grid-template-columns:repeat(auto-fit,minmax(90px,1fr));gap:8px;}"
                "a.mode{padding:8px 10px;border-radius:10px;border:1px solid #2a3a54;text-decoration:none;color:var(--text);"
                "text-align:center}"
                "a.mode.active{border-color:var(--accent);box-shadow:0 0 0 1px var(--accent) inset}"
                "textarea{background:#0f141a;border:1px solid #2a3a54;color:var(--text);border-radius:8px;"
                "padding:8px 10px;width:100%;min-height:80px;resize:vertical}"
                ".response{white-space:pre-wrap;background:#101722;border:1px solid #26344a;border-radius:10px;"
                "padding:10px;margin-top:10px;min-height:80px}"
                ".tabs{display:flex;gap:10px;margin-bottom:12px;align-items:center;}"
                ".tab{padding:8px 12px;border-radius:999px;border:1px solid #2a3a54;"
                "background:#1d2736;color:var(--text);text-decoration:none;font-weight:700;font-size:13px}"
                ".tab.active{background:var(--accent);border-color:#3f7ed1;color:#07111e}"
                "</style></head><body><div class='wrap'>";
        html += "<div class='tabs'>";
        html += "<a class='tab";
        if (selectedAppIndex == APP_POMODORO) html += " active";
        html += "' href='/apps?app=0'>Pomodoro</a>";
        html += "<a class='tab";
        if (selectedAppIndex == APP_CLOCK) html += " active";
        html += "' href='/apps?app=1'>Clock</a>";
        if (isAiAvailable()) {
          html += "<a class='tab";
          if (selectedAppIndex == APP_AI) html += " active";
          html += "' href='/apps?app=2'>AI</a>";
        }
        html += "<a class='tab";
        if (selectedAppIndex == APP_WEATHER) html += " active";
        html += "' href='/apps?app=3'>Weather</a>";
        html += "<a class='tab";
        if (selectedAppIndex == APP_SNAKE) html += " active";
        html += "' href='/apps?app=4'>Snake</a>";
        html += "<a class='tab";
        if (selectedAppIndex == APP_FLAPPY) html += " active";
        html += "' href='/apps?app=5'>Flappy</a>";
        html += "</div>";
        html += "<div class='card'><div class='grid";
        if (selectedAppIndex == APP_CLOCK) {
          html += " one";
        }
        html += "'>";
        html += "<div>";
        html += "<div class='row'>";
        html += "<a class='pill link ";
        html += (WiFi.status() == WL_CONNECTED ? "ok" : "warn");
        html += "' href='/wifi'>Wi-Fi: ";
        html += currentWifiLabel();
        html += "</a>";
        html += "<a class='pill' href='/settings'>Settings</a>";
        html += "</div>";
        html += "<div class='row' style='margin-top:10px'>";
        html += "<div class='stat'>App</div><div class='big' id='appText'>";
        html += labelForApp(selectedAppIndex);
        html += "</div>";
        html += "</div>";
        bool appSelectActive = (screenState == SCREEN_APP_SELECT);
        bool pomodoroActive = (!appSelectActive && selectedAppIndex == APP_POMODORO);
        bool aiActive = (!appSelectActive && selectedAppIndex == APP_AI);
        if (appSelectActive) {
          html += "<div class='row' style='margin-top:6px'>";
          html += "<div class='stat'>Status</div><div class='big' id='statusText'>App Select</div>";
          html += "</div>";
          html += "<div class='stat' style='margin-top:12px'>Select App</div>";
          html += "<div class='modes'>";
          html += "<a class='mode";
          if (selectedAppIndex == APP_POMODORO) html += " active";
          html += "' href='/apps?app=0'>Pomodoro</a>";
          html += "<a class='mode";
          if (selectedAppIndex == APP_CLOCK) html += " active";
          html += "' href='/apps?app=1'>Clock</a>";
          if (isAiAvailable()) {
            html += "<a class='mode";
            if (selectedAppIndex == APP_AI) html += " active";
            html += "' href='/apps?app=2'>AI</a>";
          }
          html += "<a class='mode";
          if (selectedAppIndex == APP_WEATHER) html += " active";
          html += "' href='/apps?app=3'>Weather</a>";
          html += "<a class='mode";
          if (selectedAppIndex == APP_SNAKE) html += " active";
          html += "' href='/apps?app=4'>Snake</a>";
          html += "<a class='mode";
          if (selectedAppIndex == APP_FLAPPY) html += " active";
          html += "' href='/apps?app=5'>Flappy</a>";
          html += "</div>";
          if (!isAiAvailable() && aiWifiSsid.length() > 0) {
            html += "<div class='stat' style='margin-top:8px;color:var(--muted)'>AI available only on ";
            html += aiWifiSsid;
            html += ".</div>";
          }
        } else if (pomodoroActive) {
          html += "<div class='row' style='margin-top:6px'>";
          html += "<div class='stat'>Status</div><div class='big' id='statusText'>";
          if (screenState == SCREEN_CLOCK) {
            html += "Clock";
          } else if (screenState == SCREEN_TIMER) {
            html += "Timer";
          } else {
            html += "Start";
          }
          html += "</div>";
          html += "</div>";
          html += "<div class='row' style='margin-top:6px'>";
          html += "<div class='stat'>Phase</div><div class='big' id='phaseText'>";
          html += labelForPhase(currentPhase);
          html += "</div>";
          html += "</div>";
          html += "<div class='row' style='margin-top:6px'>";
          html += "<div class='stat'>Running</div><div class='big' id='runningText'>";
          html += (isRunning ? "Yes" : "No");
          html += "</div>";
          html += "</div>";
          html += "<div class='row' style='margin-top:6px'>";
          html += "<div class='stat'>Mode</div><div class='big' id='modeText'>";
          if (!modes.empty() && selectedModeIndex >= 0 && selectedModeIndex < (int)modes.size()) {
            html += modes[selectedModeIndex].label;
          }
          html += "</div>";
          html += "</div>";
        } else if (selectedAppIndex == APP_CLOCK) {
          html += "<div class='row' style='margin-top:6px'>";
          html += "<div class='stat'>Status</div><div class='big' id='statusText'>Clock</div>";
          html += "</div>";
        } else if (selectedAppIndex == APP_AI) {
          html += "<div class='row' style='margin-top:6px'>";
          html += "<div class='stat'>Status</div><div class='big' id='statusText'>Ready</div>";
          html += "</div>";
        } else if (selectedAppIndex == APP_WEATHER) {
          html += "<div class='row' style='margin-top:6px'>";
          html += "<div class='stat'>Status</div><div class='big' id='statusText'>Weather</div>";
          html += "</div>";
        } else if (selectedAppIndex == APP_SNAKE) {
          html += "<div class='row' style='margin-top:6px'>";
          html += "<div class='stat'>Status</div><div class='big' id='statusText'>Snake</div>";
          html += "</div>";
        } else if (selectedAppIndex == APP_FLAPPY) {
          html += "<div class='row' style='margin-top:6px'>";
          html += "<div class='stat'>Status</div><div class='big' id='statusText'>Flappy</div>";
          html += "</div>";
        }
        bool timerActive = (screenState == SCREEN_TIMER);
        bool canStart = pomodoroActive && (screenState == SCREEN_START);
        bool canControl = pomodoroActive && timerActive;
        if (pomodoroActive) {
          html += "<div class='btns'>";
          html += "<a class='btn primary";
          html += (canStart ? "" : " disabled");
          html += "' href='";
          html += (canStart ? "/start" : "#");
          html += "'>Start</a>";
          html += "<a class='btn";
          html += (canControl ? "" : " disabled");
          html += "' href='";
          html += (canControl ? "/pause" : "#");
          html += "'>Pause/Resume</a>";
          html += "<a class='btn";
          html += (canControl ? "" : " disabled");
          html += "' href='";
          html += (canControl ? "/next" : "#");
          html += "'>Next Phase</a>";
          html += "<a class='btn";
          html += (canControl ? "" : " disabled");
          html += "' href='";
          html += (canControl ? "/reset" : "#");
          html += "'>Reset Phase</a>";
          html += "<a class='btn";
          html += (canControl ? "" : " disabled");
          html += "' href='";
          html += (canControl ? "/home" : "#");
          html += "'>Start Menu</a>";
          html += "</div>";
          html += "<div class='stat' style='margin-top:12px'>Set Mode</div>";
          html += "<div class='modes'>";
          for (int i = 0; i < (int)modes.size(); i++) {
            html += "<a class='mode";
            if (i == selectedModeIndex) {
              html += " active";
            }
            html += "' href='/mode?i=";
            html += i;
            html += "'>";
            html += modes[i].label;
            html += "</a>";
          }
          html += "</div>";
        }
        html += "</div></div>";
        if (selectedAppIndex == APP_POMODORO) {
          html += "<div class='panel'>";
        } else if (selectedAppIndex == APP_AI) {
          html += "<div class='panel'>";
        } else {
          html += "<div class='panel time'>";
        }
        if (selectedAppIndex == APP_WEATHER) {
          String tempStr = weather.valid ? String(weather.temp, 1) + "C" : "--";
          String feelsStr = weather.valid ? String(weather.feels, 1) + "C" : "--";
          String windStr = weather.valid ? String(weather.wind, 1) + " km/h" : "--";
          String popStr = weather.valid ? String((int)weather.precipProb) + "%" : "--";
          String icon = "SUN";
          if (weather.valid) {
            if (weather.snow > 0.0f) {
              icon = "SNOW";
            } else if (weather.rain > 0.0f) {
              icon = "RAIN";
            } else {
              int code = weather.code;
              if (code >= 1 && code <= 3) icon = "CLOUD";
              if (code >= 45 && code <= 48) icon = "FOG";
              if (code >= 51 && code <= 67) icon = "RAIN";
              if (code >= 71 && code <= 77) icon = "SNOW";
              if (code >= 80 && code <= 99) icon = "STORM";
            }
          }
          html += "<div class='timerBox' id='weatherTemp'>";
          html += tempStr;
          html += "</div>";
          html += "<div class='timerSub' id='weatherMeta'>Feels ";
          html += feelsStr;
          html += " · Wind ";
          html += windStr;
          html += " · Precip ";
          html += popStr;
          html += " · ";
          html += icon;
          html += "</div>";
        } else if (selectedAppIndex == APP_SNAKE || selectedAppIndex == APP_FLAPPY) {
          html += "<div class='timerBox' id='gameLabel'>";
          html += labelForApp(selectedAppIndex);
          html += "</div>";
          html += "<div class='timerSub' id='gameScore'>Score 0</div>";
        } else if (!aiActive) {
          char timeStr[6];
          const char* timerSub = "Remaining";
          if (selectedAppIndex == APP_CLOCK) {
            timerSub = "Time";
            int hour = -1;
            int minute = -1;
            if (!getLocalTimeInfo(timeStr, sizeof(timeStr), &hour, &minute)) {
              snprintf(timeStr, sizeof(timeStr), "--:--");
            }
          } else {
            uint32_t elapsedMs = currentElapsedMs();
            uint32_t remainingMs = (elapsedMs >= currentDurationMs) ? 0 : (currentDurationMs - elapsedMs);
            uint32_t remainingSeconds = remainingMs / 1000;
            uint32_t minutes = remainingSeconds / 60;
            uint32_t seconds = remainingSeconds % 60;
            snprintf(timeStr, sizeof(timeStr), "%lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
          }
          html += "<div class='timerBox' id='timerText'>";
          html += timeStr;
          html += "</div>";
          html += "<div class='timerSub' id='timerSub'>";
          html += timerSub;
          html += "</div>";
        }
        if (aiActive) {
          html += "<form id='aiHomeForm' method='post' action='/ai/send' style='margin-top:10px'>";
          html += "<textarea id='aiPromptHome' name='prompt' placeholder='Type a message...'></textarea>";
          html += "<div class='row' style='margin-top:8px'>";
          html += "<button class='btn save' type='submit'>Send</button>";
          html += "</div>";
          html += "</form>";
          html += "<div id='aiResponse' class='response'></div>";
        }
        html += "</div></div></div>";
        html += "<script>"
                "let aiStreaming=false;"
                "async function refreshStatus(){"
                "try{const res=await fetch('/status?ts='+Date.now());"
                "const s=await res.json();"
                "const timerText=document.getElementById('timerText');"
                "if(timerText){timerText.textContent=s.remaining;}"
                "const timerSub=document.getElementById('timerSub');"
                "if(timerSub){timerSub.textContent=s.timerSub || 'Remaining';}"
                "const appText=document.getElementById('appText');"
                "if(appText){appText.textContent=s.app;}"
                "const statusText=document.getElementById('statusText');"
                "if(statusText){statusText.textContent=(s.screen==='AI' ? (s.ai_state||'Ready') : s.screen);}"
                "const phaseText=document.getElementById('phaseText');"
                "if(phaseText){phaseText.textContent=s.phase;}"
                "const runningText=document.getElementById('runningText');"
                "if(runningText){runningText.textContent=s.running ? 'Yes':'No';}"
                "const modeText=document.getElementById('modeText');"
                "if(modeText){modeText.textContent=s.mode;}"
                "const gameLabel=document.getElementById('gameLabel');"
                "if(gameLabel){gameLabel.textContent=s.app;}"
                "const gameScore=document.getElementById('gameScore');"
                "if(gameScore && s.game_score!==undefined){gameScore.textContent='Score '+s.game_score;}"
                "const aiResp=document.getElementById('aiResponse');"
                "if(aiResp && !aiStreaming){aiResp.innerHTML=(s.ai_response||'').replace(/\\*\\*(.+?)\\*\\*/g,'<strong>$1</strong>');}"
                "const wTemp=document.getElementById('weatherTemp');"
                "if(wTemp && s.w_temp!==undefined){wTemp.textContent=(+s.w_temp).toFixed(1)+'C';}"
                "const wMeta=document.getElementById('weatherMeta');"
                "if(wMeta && s.w_temp!==undefined){"
                "let icon='SUN';"
                "const rain=+s.w_rain||0;const snow=+s.w_snow||0;const code=+s.w_code||0;"
                "if(snow>0){icon='SNOW';}"
                "else if(rain>0){icon='RAIN';}"
                "else if(code>=1 && code<=3){icon='CLOUD';}"
                "else if(code>=45 && code<=48){icon='FOG';}"
                "else if(code>=51 && code<=67){icon='RAIN';}"
                "else if(code>=71 && code<=77){icon='SNOW';}"
                "else if(code>=80 && code<=99){icon='STORM';}"
                "wMeta.textContent='Feels '+(+s.w_feels).toFixed(1)+'C · Wind '+(+s.w_wind).toFixed(1)+' km/h · Precip '+Math.round(+s.w_pop)+'% · '+icon;}"
                "}catch(e){}}"
                "setInterval(refreshStatus,1000);"
                "const promptHome=document.getElementById('aiPromptHome');"
                "if(promptHome){"
                "let lastText='';let typingTimer=null;"
                "function sendTyping(txt){"
                "fetch('/ai/typing',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
                "body:'text='+encodeURIComponent(txt)});}"
                "promptHome.addEventListener('input',()=>{"
                "const val=promptHome.value;"
                "if(val===lastText)return;lastText=val;"
                "if(typingTimer)clearTimeout(typingTimer);"
                "typingTimer=setTimeout(()=>sendTyping(val),250);"
                "});"
                "const form=document.getElementById('aiHomeForm');"
                "form.addEventListener('submit',async(e)=>{e.preventDefault();"
                "const prompt=promptHome.value.trim();if(!prompt)return;"
                "sendTyping(prompt);"
                "const body='prompt='+encodeURIComponent(prompt);"
                "const res=await fetch('/ai/send',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-Requested-With':'fetch'},body});"
                "if(!res.body){return;}"
                "if(!res.ok){const t=await res.text();const aiResp=document.getElementById('aiResponse');"
                "if(aiResp){aiResp.textContent=t;}return;}"
                "promptHome.value='';sendTyping('');"
                "});"
                "}"
                "</script>";
        html += "</body></html>";
        webServer.send(200, "text/html", html);
      });

      webServer.on("/start", []() {
        if (selectedAppIndex != APP_POMODORO) {
          webServer.sendHeader("Location", "/");
          webServer.send(303);
          return;
        }
        if (screenState != SCREEN_START) {
          webServer.sendHeader("Location", "/");
          webServer.send(303);
          return;
        }
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
        if (selectedAppIndex != APP_POMODORO) {
          webServer.sendHeader("Location", "/");
          webServer.send(303);
          return;
        }
        if (screenState != SCREEN_TIMER) {
          webServer.sendHeader("Location", "/");
          webServer.send(303);
          return;
        }
        toggleRunning();
        renderTimerScreen(true);
        webServer.sendHeader("Location", "/");
        webServer.send(303);
      });

      webServer.on("/next", []() {
        if (selectedAppIndex != APP_POMODORO) {
          webServer.sendHeader("Location", "/");
          webServer.send(303);
          return;
        }
        if (screenState != SCREEN_TIMER) {
          webServer.sendHeader("Location", "/");
          webServer.send(303);
          return;
        }
        bool countFocusCompletion = (currentPhase == PHASE_FOCUS);
        advancePhase(countFocusCompletion, isRunning);
        renderTimerScreen(true);
        webServer.sendHeader("Location", "/");
        webServer.send(303);
      });

      webServer.on("/reset", []() {
        if (selectedAppIndex != APP_POMODORO) {
          webServer.sendHeader("Location", "/");
          webServer.send(303);
          return;
        }
        if (screenState != SCREEN_TIMER) {
          webServer.sendHeader("Location", "/");
          webServer.send(303);
          return;
        }
        resetCurrentPhase();
        renderTimerScreen(true);
        webServer.sendHeader("Location", "/");
        webServer.send(303);
      });

      webServer.on("/home", []() {
        if (selectedAppIndex != APP_POMODORO) {
          webServer.sendHeader("Location", "/");
          webServer.send(303);
          return;
        }
        if (screenState != SCREEN_TIMER) {
          webServer.sendHeader("Location", "/");
          webServer.send(303);
          return;
        }
        screenState = SCREEN_APP_SELECT;
        startPhase(PHASE_FOCUS, false);
        completedFocusSessions = 0;
        lastRemainingSeconds = 0xFFFFFFFFUL;
        lastPhase = currentPhase;
        lastRunning = isRunning;
        lastCompletedFocus = -1;
        lastInputMs = millis();
        renderAppSelectScreen(true);
        webServer.sendHeader("Location", "/");
        webServer.send(303);
      });

      webServer.on("/mode", []() {
        if (selectedAppIndex != APP_POMODORO) {
          webServer.sendHeader("Location", "/");
          webServer.send(303);
          return;
        }
        if (screenState != SCREEN_START) {
          webServer.sendHeader("Location", "/");
          webServer.send(303);
          return;
        }
        if (webServer.hasArg("i")) {
          int idx = webServer.arg("i").toInt();
          if (idx >= 0 && idx < (int)modes.size()) {
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

      webServer.on("/status", []() {
        StaticJsonDocument<1024> doc;
        char timeStr[6];
        if (selectedAppIndex == APP_CLOCK) {
          int hour = -1;
          int minute = -1;
          if (!getLocalTimeInfo(timeStr, sizeof(timeStr), &hour, &minute)) {
            snprintf(timeStr, sizeof(timeStr), "--:--");
          }
          doc["timerSub"] = "Time";
        } else if (selectedAppIndex == APP_AI) {
          snprintf(timeStr, sizeof(timeStr), "--:--");
          doc["timerSub"] = "AI";
        } else if (selectedAppIndex == APP_SNAKE || selectedAppIndex == APP_FLAPPY) {
          snprintf(timeStr, sizeof(timeStr), "--:--");
          doc["timerSub"] = "Score";
        } else {
          uint32_t elapsedMs = currentElapsedMs();
          uint32_t remainingMs = (elapsedMs >= currentDurationMs) ? 0 : (currentDurationMs - elapsedMs);
          uint32_t remainingSeconds = remainingMs / 1000;
          uint32_t minutes = remainingSeconds / 60;
          uint32_t seconds = remainingSeconds % 60;
          snprintf(timeStr, sizeof(timeStr), "%lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
          doc["timerSub"] = "Remaining";
        }
        doc["remaining"] = timeStr;
        doc["app"] = labelForApp(selectedAppIndex);
        if (screenState == SCREEN_APP_SELECT) {
          doc["screen"] = "App Select";
        } else if (screenState == SCREEN_AI) {
          doc["screen"] = "AI";
        } else if (screenState == SCREEN_WEATHER) {
          doc["screen"] = "Weather";
        } else if (screenState == SCREEN_SNAKE) {
          doc["screen"] = "Snake";
        } else if (screenState == SCREEN_FLAPPY) {
          doc["screen"] = "Flappy";
        } else if (screenState == SCREEN_CLOCK) {
          doc["screen"] = "Clock";
        } else if (screenState == SCREEN_TIMER) {
          doc["screen"] = "Timer";
        } else {
          doc["screen"] = "Start";
        }
        if (selectedAppIndex != APP_POMODORO || screenState == SCREEN_APP_SELECT) {
          doc["phase"] = "--";
          doc["running"] = false;
          doc["mode"] = "--";
        } else {
          doc["phase"] = labelForPhase(currentPhase);
          doc["running"] = isRunning;
          if (!modes.empty() && selectedModeIndex >= 0 && selectedModeIndex < (int)modes.size()) {
            doc["mode"] = modes[selectedModeIndex].label;
          } else {
            doc["mode"] = "--";
          }
        }
        String aiState = "Ready";
        if (aiGenerating) {
          aiState = "Generating";
        } else if (aiTypingText.length() > 0) {
          aiState = "Writing";
        }
        doc["ai_state"] = aiState;
        if (selectedAppIndex == APP_SNAKE) {
          int score = snakeLen - 3;
          if (score < 0) score = 0;
          doc["game_score"] = score;
        } else if (selectedAppIndex == APP_FLAPPY) {
          doc["game_score"] = flappyScore;
        }
        String aiPreview = aiResponseText;
        if (aiPreview.length() > 800) {
          aiPreview = aiPreview.substring(aiPreview.length() - 800);
        }
        doc["ai_response"] = aiPreview;
        String typingPreview = aiTypingText;
        if (typingPreview.length() > 200) {
          typingPreview = typingPreview.substring(typingPreview.length() - 200);
        }
        doc["ai_typing"] = typingPreview;
        if (weather.valid) {
          doc["w_temp"] = weather.temp;
          doc["w_feels"] = weather.feels;
          doc["w_wind"] = weather.wind;
          doc["w_pop"] = weather.precipProb;
          doc["w_code"] = weather.code;
          doc["w_rain"] = weather.rain;
          doc["w_snow"] = weather.snow;
        }
        String out;
        serializeJson(doc, out);
        webServer.send(200, "application/json", out);
      });

      webServer.on("/wifi", []() {
        String html;
        html.reserve(6144);
        html += "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Wi-Fi</title>"
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
        html += "<div class='title'>Wi-Fi</div>";
        html += "<div class='card'><div class='row'>";
        html += "<a class='pill' href='/'>Back</a>";
        html += "<div class='pill'>Active: ";
        html += currentWifiLabel();
        html += "</div>";
        html += "<div class='pill'>IP: ";
        html += currentIpLabel();
        html += "</div>";
        html += "</div></div>";

        html += "<div class='card'>";
        html += "<div class='row'><div class='muted'>Saved Networks (Drag = Priority)</div></div>";
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
          html += "<button class='btn' type='submit'>Delete</button>";
          html += "</form>";
          html += "</div>";
        }
        if (wifiList.empty()) {
          html += "<div class='muted'>No networks saved yet.</div>";
        }
        html += "</div>";
        html += "<form id='orderForm' method='post' action='/wifi/order' style='margin-top:10px'>";
        html += "<input type='hidden' id='orderInput' name='order' value=''>";
        html += "<button class='btn' type='submit'>Save Order</button>";
        html += "</form>";
        html += "</div>";

        html += "<div class='card'>";
        html += "<div class='row'><div class='muted'>Add Network</div></div>";
        html += "<form method='post' action='/wifi/add' style='margin-top:8px'>";
        html += "<div class='row'><input id='ssidInput' name='ssid' placeholder='SSID' required>";
        html += "<input name='pass' placeholder='Password (optional)'>";
        html += "<button class='btn' type='submit'>Add</button></div>";
        html += "</form>";
        html += "<div class='row' style='margin-top:10px'>";
        html += "<select id='scanSelect' style='min-width:220px'></select>";
        html += "<button class='btn' type='button' onclick='scan()'>Scan</button>";
        html += "<button class='btn' type='button' onclick='useSelected()'>Use</button>";
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
        html += "' placeholder='AP Password (empty = open)'>";
        html += "<button class='btn save' type='submit'>Save</button></div>";
        html += "<div class='muted' style='margin-top:6px'>AP password must be >= 8 chars, otherwise open.</div>";
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
                "o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm'+(n.open?', open':'')+')';"
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

      webServer.on("/settings", []() {
        String html;
        html.reserve(2048);
        html += "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Settings</title>"
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
                "a.btn{display:inline-block;text-align:center;padding:10px 12px;border-radius:10px;"
                "border:1px solid #2a3a54;background:#1d2736;color:var(--text);text-decoration:none;font-weight:600}"
                "</style></head><body><div class='wrap'>";
        html += "<div class='title'>Settings</div>";
        html += "<div class='card'><div class='row'>";
        html += "<a class='btn' href='/'>Back</a>";
        html += "<a class='btn' href='/apps'>Apps</a>";
        html += "<a class='btn' href='/wifi'>Wi-Fi</a>";
        html += "<a class='btn' href='/ai'>AI</a>";
        html += "<a class='btn' href='/time'>Time</a>";
        html += "<a class='btn' href='/ntp'>Time Sync</a>";
        html += "</div></div>";
        html += "</div></body></html>";
        webServer.send(200, "text/html", html);
      });

      webServer.on("/apps", []() {
        if (webServer.hasArg("app")) {
          int appIdx = webServer.arg("app").toInt();
          switchToApp(appIdx);
          webServer.sendHeader("Location", "/");
          webServer.send(303);
          return;
        }
        String html;
        html.reserve(4096);
        html += "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Apps</title>"
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
                "button.btn{padding:8px 12px;border-radius:10px;border:1px solid #2a3a54;background:#1d2736;"
                "color:var(--text);font-weight:600}"
                "button.btn.save{background:#59d98e;border-color:#3aa66b;color:#07111e}"
                "input,select{background:#0f141a;border:1px solid #2a3a54;color:var(--text);border-radius:8px;"
                "padding:8px 10px;min-width:220px}"
                "input[type=color]{appearance:none;width:36px;height:28px;padding:0;border:1px solid #2a3a54;"
                "border-radius:6px;background:#0f141a;min-width:auto}"
                ".swatch{width:18px;height:18px;border-radius:4px;border:1px solid #2a3a54}"
                ".muted{color:var(--muted);font-size:12px}"
                "</style></head><body><div class='wrap'>";
        html += "<div class='title'>Apps</div>";
        html += "<div class='card'><div class='row'>";
        html += "<a class='pill' href='/'>Back</a>";
        html += "<div class='pill'>Active: ";
        html += currentWifiLabel();
        html += "</div>";
        html += "</div></div>";

        html += "<div class='card'>";
        html += "<div class='row'><div class='muted'>App Selection</div></div>";
        html += "<form method='post' action='/apps/select' style='margin-top:8px'>";
        html += "<div class='row'><select name='app'>";
        html += "<option value='0'";
        if (selectedAppIndex == APP_POMODORO) html += " selected";
        html += ">Pomodoro</option>";
        html += "<option value='1'";
        if (selectedAppIndex == APP_CLOCK) html += " selected";
        html += ">Clock</option>";
        bool aiAvail = isAiAvailable();
        html += "<option value='2'";
        if (selectedAppIndex == APP_AI) html += " selected";
        if (!aiAvail) html += " disabled";
        html += ">AI</option>";
        html += "<option value='3'";
        if (selectedAppIndex == APP_WEATHER) html += " selected";
        html += ">Weather</option>";
        html += "<option value='4'";
        if (selectedAppIndex == APP_SNAKE) html += " selected";
        html += ">Snake</option>";
        html += "<option value='5'";
        if (selectedAppIndex == APP_FLAPPY) html += " selected";
        html += ">Flappy</option>";
        html += "</select>";
        html += "<button class='btn save' type='submit'>Open</button></div>";
        if (!aiAvail && aiWifiSsid.length() > 0) {
          html += "<div class='muted' style='margin-top:6px'>AI app is only available on Wi-Fi: ";
          html += aiWifiSsid;
          html += ".</div>";
        } else {
          html += "<div class='muted' style='margin-top:6px'>Switching apps resets the Pomodoro timer.</div>";
        }
        html += "</form></div>";

        String defaultClock = rgbToHex(FOCUS_RGB);
        String clockHex = clockColor.length() ? clockColor : defaultClock;
        html += "<div class='card'>";
        html += "<div class='row'><div class='muted'>Clock App</div></div>";
        html += "<form method='post' action='/apps/clock' style='margin-top:8px'>";
        html += "<div class='row'><div class='muted'>Time Color</div>";
        html += "<input type='color' id='clockColor' name='clock_color' value='";
        html += clockHex;
        html += "' oninput='syncClock()'>";
        html += "<div id='clockSwatch' class='swatch' style='background:";
        html += clockHex;
        html += "'></div></div>";
        html += "<div class='row' style='margin-top:10px'><button class='btn save' type='submit'>Save</button>";
        html += "<button class='btn' type='button' onclick='resetClock()'>Reset</button></div>";
        html += "</form></div>";

        html += "<script>"
                "const defClock='" + defaultClock + "';"
                "function syncClock(){const v=document.getElementById('clockColor').value;"
                "document.getElementById('clockSwatch').style.background=v;}"
                "function resetClock(){document.getElementById('clockColor').value=defClock;syncClock();}"
                "</script>";

        html += "</div></body></html>";
        webServer.send(200, "text/html", html);
      });

      webServer.on("/apps/select", []() {
        int appIdx = webServer.arg("app").toInt();
        switchToApp(appIdx);
        webServer.sendHeader("Location", "/apps");
        webServer.send(303);
      });

      webServer.on("/apps/clock", []() {
        String color = webServer.arg("clock_color");
        color.trim();
        clockColor = color;
        saveWifiConfig();
        if (selectedAppIndex == APP_CLOCK) {
          renderClockScreen(true);
        }
        webServer.sendHeader("Location", "/apps");
        webServer.send(303);
      });

      webServer.on("/ai", []() {
        String html;
        html.reserve(6144);
        html += "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>AI</title>"
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
                "button.btn{padding:8px 12px;border-radius:10px;border:1px solid #2a3a54;background:#1d2736;"
                "color:var(--text);font-weight:600}"
                "button.btn.save{background:#59d98e;border-color:#3aa66b;color:#07111e}"
                "input,select,textarea{background:#0f141a;border:1px solid #2a3a54;color:var(--text);border-radius:8px;"
                "padding:8px 10px;min-width:220px}"
                "textarea{min-width:100%;min-height:90px;resize:vertical}"
                ".muted{color:var(--muted);font-size:12px}"
                ".response{white-space:pre-wrap;background:#101722;border:1px solid #26344a;border-radius:10px;"
                "padding:10px;margin-top:10px;min-height:80px}"
                "</style></head><body><div class='wrap'>";
        html += "<div class='title'>AI</div>";
        html += "<div class='card'><div class='row'>";
        html += "<a class='pill' href='/'>Back</a>";
        html += "<div class='pill'>Active: ";
        html += currentWifiLabel();
        html += "</div>";
        html += "</div></div>";

        html += "<div class='card'>";
        html += "<div class='row'><div class='muted'>AI Settings</div></div>";
        html += "<form method='post' action='/ai/save' style='margin-top:8px'>";
        html += "<div class='row'><select name='ai_wifi'>";
        html += "<option value=''>Select Wi-Fi</option>";
        for (int i = 0; i < (int)wifiList.size(); i++) {
          html += "<option value='";
          html += wifiList[i].ssid;
          html += "'";
          if (wifiList[i].ssid == aiWifiSsid) html += " selected";
          html += ">";
          html += wifiList[i].ssid;
          html += "</option>";
        }
        html += "</select>";
        html += "<input name='ai_host' placeholder='AI Host (e.g. http://192.168.178.200:11434)' value='";
        html += aiHost;
        html += "'>";
        html += "<select id='mdlSel' name='ai_model'><option value='";
        html += aiModel;
        html += "'>";
        html += aiModel;
        html += "</option></select>";
        html += "<button class='btn save' type='submit'>Save</button></div>";
        html += "<script>"
                "fetch('/ai/models').then(r=>r.json()).then(list=>{"
                "var sel=document.getElementById('mdlSel');"
                "var cur=sel.value;"
                "sel.innerHTML='';"
                "list.forEach(function(m){"
                "var o=document.createElement('option');"
                "o.value=m;o.textContent=m;"
                "if(m===cur)o.selected=true;"
                "sel.appendChild(o);});"
                "if(!list.includes(cur)&&list.length>0)sel.options[0].selected=true;"
                "}).catch(function(){});"
                "</script>";
        html += "<div class='muted' style='margin-top:6px'>AI app shows only on the selected Wi-Fi.</div>";
        html += "</form>";
        html += "<form method='post' action='/ai/system' style='margin-top:10px'>";
        html += "<div class='row'><div class='muted'>System Message</div></div>";
        html += "<textarea name='system' placeholder='System message...'>";
        html += aiSystemMessage;
        html += "</textarea>";
        html += "<div class='row' style='margin-top:10px'><button class='btn save' type='submit'>Save</button></div>";
        html += "</form>";
        html += "</div>";

        html += "<div class='card'>";
        html += "<div class='row'><div class='muted'>Chat</div></div>";
        html += "<form id='aiForm' method='post' action='/ai/send' style='margin-top:8px'>";
        html += "<textarea id='aiPrompt' name='prompt' placeholder='Type a message...'></textarea>";
        html += "<div class='row' style='margin-top:10px'>";
        html += "<button class='btn save' type='submit'>Send</button>";
        html += "</div>";
        html += "</form>";
        html += "<div id='aiResponse' class='response'></div>";
        html += "</div>";

        html += "<script>"
                "let lastText='';"
                "let typingTimer=null;"
                "const promptEl=document.getElementById('aiPrompt');"
                "function sendTyping(txt){"
                "fetch('/ai/typing',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
                "body:'text='+encodeURIComponent(txt)});}"
                "promptEl.addEventListener('input',()=>{"
                "const val=promptEl.value;"
                "if(val===lastText)return;"
                "lastText=val;"
                "if(typingTimer)clearTimeout(typingTimer);"
                "typingTimer=setTimeout(()=>sendTyping(val),250);"
                "});"
                "let aiStreaming=false;"
                "const form=document.getElementById('aiForm');"
                "form.addEventListener('submit',async(e)=>{e.preventDefault();"
                "const prompt=promptEl.value.trim();if(!prompt)return;"
                "sendTyping(prompt);"
                "const body='prompt='+encodeURIComponent(prompt);"
                "const res=await fetch('/ai/send',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-Requested-With':'fetch'},body});"
                "if(!res.ok){const t=await res.text();document.getElementById('aiResponse').textContent=t;return;}"
                "promptEl.value='';sendTyping('');"
                "});"
                "async function refresh(){"
                "if(aiStreaming)return;"
                "try{const res=await fetch('/ai/status?ts='+Date.now());"
                "const s=await res.json();"
                "document.getElementById('aiResponse').innerHTML=(s.response||'').replace(/\\*\\*(.+?)\\*\\*/g,'<strong>$1</strong>');"
                "}catch(e){}}"
                "setInterval(refresh,1000);"
                "</script>";

        html += "</div></body></html>";
        webServer.send(200, "text/html", html);
      });

      webServer.on("/ai/save", []() {
        String host = webServer.arg("ai_host");
        String wifi = webServer.arg("ai_wifi");
        String model = webServer.arg("ai_model");
        host.trim();
        wifi.trim();
        model.trim();
        aiHost = host;
        aiWifiSsid = wifi;
        if (model.length() > 0) aiModel = model;
        saveWifiConfig();
        if (!isAiAvailable() && selectedAppIndex == APP_AI) {
          switchToApp(APP_POMODORO);
        }
        webServer.sendHeader("Location", "/ai");
        webServer.send(303);
      });

      webServer.on("/ai/models", []() {
        String json = "[";
        if (aiHost.length() > 0) {
          String baseUrl = aiHost;
          if (!baseUrl.startsWith("http://") && !baseUrl.startsWith("https://")) baseUrl = "http://" + baseUrl;
          if (!baseUrl.endsWith("/")) baseUrl += "/";
          HTTPClient http;
          http.begin(baseUrl + "api/tags");
          http.setTimeout(5000);
          int code = http.GET();
          if (code == 200) {
            String payload = http.getString();
            DynamicJsonDocument doc(4096);
            if (!deserializeJson(doc, payload)) {
              JsonArray arr = doc["models"].as<JsonArray>();
              bool first = true;
              for (JsonObject m : arr) {
                const char* n = m["name"] | "";
                if (strlen(n) > 0) {
                  if (!first) json += ",";
                  json += "\"";
                  json += n;
                  json += "\"";
                  first = false;
                }
              }
            }
          }
          http.end();
        }
        json += "]";
        webServer.send(200, "application/json", json);
      });

      webServer.on("/ai/system", []() {
        String sys = webServer.arg("system");
        aiSystemMessage = sys;
        saveWifiConfig();
        webServer.sendHeader("Location", "/ai");
        webServer.send(303);
      });

      webServer.on("/ai/typing", []() {
        String text = webServer.arg("text");
        aiTypingText = text;
        aiScrollOffset = 0;
        if (selectedAppIndex == APP_AI && screenState == SCREEN_AI) {
          renderAiScreen(false);
        }
        webServer.send(200, "application/json", "{\"ok\":true}");
      });

      webServer.on("/ai/status", []() {
        StaticJsonDocument<512> doc;
        String typingPreview = aiTypingText;
        if (typingPreview.length() > 200) {
          typingPreview = typingPreview.substring(typingPreview.length() - 200);
        }
        String aiPreview = aiResponseText;
        if (aiPreview.length() > 1200) {
          aiPreview = aiPreview.substring(aiPreview.length() - 1200);
        }
        doc["typing"] = typingPreview;
        doc["response"] = aiPreview;
        doc["generating"] = aiGenerating;
        String out;
        serializeJson(doc, out);
        webServer.send(200, "application/json", out);
      });

      webServer.on("/ai/send", []() {
        String prompt = webServer.arg("prompt");
        prompt.trim();
        if (prompt.length() == 0) {
          webServer.sendHeader("Location", "/ai");
          webServer.send(303);
          return;
        }
        if (aiHost.length() == 0) {
          aiResponseText = "AI host not configured.";
          aiTypingText = "";
          aiGenerating = false;
          if (selectedAppIndex == APP_AI && screenState == SCREEN_AI) {
            renderAiScreen(true);
          }
          if (webServer.header("X-Requested-With") == "fetch") {
            webServer.send(200, "application/json", "{\"ok\":false}");
          } else {
            webServer.sendHeader("Location", "/");
            webServer.send(303);
          }
          return;
        }
        aiTypingText = prompt;
        aiResponseText = "";
        aiGenerating = true;
        aiScrollOffset = 0;
        if (selectedAppIndex == APP_AI && screenState == SCREEN_AI) {
          renderAiScreen(false);
        }

        String baseUrl = aiHost;
        if (!baseUrl.startsWith("http://") && !baseUrl.startsWith("https://")) {
          baseUrl = "http://" + baseUrl;
        }
        if (!baseUrl.endsWith("/")) baseUrl += "/";
        String url = baseUrl + "api/chat";

        HTTPClient http;
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(60000);
        StaticJsonDocument<1024> req;
        req["model"] = aiModel.c_str();
        req["stream"] = false;
        JsonArray messages = req.createNestedArray("messages");
        if (aiSystemMessage.length() > 0) {
          JsonObject sys = messages.createNestedObject();
          sys["role"] = "system";
          sys["content"] = aiSystemMessage;
        }
        JsonObject user = messages.createNestedObject();
        user["role"] = "user";
        user["content"] = prompt;
        String body;
        serializeJson(req, body);

        int code = http.POST(body);
        if (code > 0) {
          String payload = http.getString();
          StaticJsonDocument<2048> resp;
          DeserializationError err = deserializeJson(resp, payload);
          if (!err) {
            const char* content = resp["message"]["content"] | "";
            aiResponseText = String(content);
          } else {
            aiResponseText = "AI parse error.";
          }
        } else {
          aiResponseText = "AI request failed (" + String(code) + ").";
        }
        http.end();
        aiTypingText = "";
        aiGenerating = false;
        aiScrollOffset = 0;
        if (selectedAppIndex == APP_AI && screenState == SCREEN_AI) {
          renderAiScreen(false);
        }
        if (webServer.header("X-Requested-With") == "fetch") {
          webServer.send(200, "application/json", "{\"ok\":true}");
        } else {
          webServer.sendHeader("Location", "/");
          webServer.send(303);
        }
      });


      webServer.on("/ai/open", []() {
        webServer.sendHeader("Location", "/ai");
        webServer.send(303);
      });


      webServer.on("/time", []() {
        String html;
        html.reserve(4096);
        html += "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Time Settings</title>"
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
                "button.btn{padding:8px 12px;border-radius:10px;border:1px solid #2a3a54;background:#1d2736;"
                "color:var(--text);font-weight:600}"
                "button.btn.save{background:#59d98e;border-color:#3aa66b;color:#07111e}"
                "input,select{background:#0f141a;border:1px solid #2a3a54;color:var(--text);border-radius:8px;"
                "padding:8px 10px;min-width:220px}"
                "input[type=range]{min-width:260px;accent-color:#59d98e}"
                "input[type=color]{appearance:none;width:36px;height:28px;padding:0;border:1px solid #2a3a54;"
                "border-radius:6px;background:#0f141a;min-width:auto}"
                ".swatch{width:18px;height:18px;border-radius:4px;border:1px solid #2a3a54}"
                ".muted{color:var(--muted);font-size:12px}"
                ".list{display:flex;flex-direction:column;gap:8px}"
                ".item{display:flex;align-items:center;gap:10px;padding:10px;border:1px solid #26344a;"
                "border-radius:10px;background:#121a24}"
                ".handle{cursor:grab;color:var(--muted);font-size:18px;user-select:none}"
                ".spacer{flex:1}"
                "</style></head><body><div class='wrap'>";
        html += "<div class='title'>Time</div>";
        html += "<div class='card'><div class='row'>";
        html += "<a class='pill' href='/'>Back</a>";
        html += "<div class='pill'>Active: ";
        html += currentWifiLabel();
        html += "</div>";
        html += "</div></div>";

        html += "<div class='card'>";
        html += "<div class='row'><div class='muted'>Modes (Drag = Priority)</div></div>";
        html += "<div id='mode-list' class='list' style='margin-top:8px'>";
        for (int i = 0; i < (int)modes.size(); i++) {
          html += "<div class='item mode-item' draggable='true' data-idx='";
          html += i;
          html += "' data-label='";
          html += modes[i].label;
          html += "' data-focus='";
          html += modes[i].focusMin;
          html += "' data-short='";
          html += modes[i].shortMin;
          html += "' data-long='";
          html += modes[i].longMin;
          html += "' data-fc='";
          html += (modes[i].focusColor.length() ? modes[i].focusColor : rgbToHex(FOCUS_RGB));
          html += "' data-sc='";
          html += (modes[i].shortColor.length() ? modes[i].shortColor : rgbToHex(SHORT_RGB));
          html += "' data-lc='";
          html += (modes[i].longColor.length() ? modes[i].longColor : rgbToHex(LONG_RGB));
          html += "'>";
          html += "<div class='handle'>|||</div>";
          html += "<div>";
          html += modes[i].label;
          html += "</div>";
          String swatch = modes[i].focusColor.length() ? modes[i].focusColor : rgbToHex(FOCUS_RGB);
          html += "<div style='width:16px;height:16px;border-radius:4px;background:";
          html += swatch;
          html += ";border:1px solid #2a3a54'></div>";
          html += "<div class='spacer'></div>";
          html += "<button class='btn' type='button' onclick='editMode(";
          html += i;
          html += ")'>Edit</button>";
          html += "<form method='post' action='/time/delete'>";
          html += "<input type='hidden' name='i' value='";
          html += i;
          html += "'>";
          html += "<button class='btn' type='submit'>Delete</button>";
          html += "</form>";
          html += "</div>";
        }
        html += "</div>";
        html += "<form id='orderForm' method='post' action='/time/mode' style='margin-top:10px'>";
        html += "<input type='hidden' id='modeOrder' name='order' value=''>";
        html += "<div class='row'>";
        html += "<button class='btn save' type='submit'>Save Order</button>";
        html += "</div></form></div>";

        html += "<div class='card'>";
        html += "<div class='row'><div class='muted'>Add / Edit Mode</div></div>";
        html += "<form method='post' action='/time/save_mode' style='margin-top:8px'>";
        html += "<input type='hidden' name='i' id='modeIndex' value='-1'>";
        html += "<div class='row'><input name='label' id='modeLabel' placeholder='Label' required></div>";
        html += "<div class='row' style='margin-top:10px'>";
        html += "<div class='muted'>Focus: <span id='focusVal'>25</span> min</div>";
        html += "<input type='range' min='5' max='90' name='focus' id='modeFocus' value='25' oninput='syncRange(\"modeFocus\",\"focusVal\")'>";
        html += "</div>";
        html += "<div class='row'><div class='muted'>Focus Color</div>";
        html += "<input type='color' id='focusColor' name='focus_color' value='";
        html += rgbToHex(FOCUS_RGB);
        html += "' oninput='syncColor(\"focusColor\",\"focusSwatch\")'>";
        html += "<div id='focusSwatch' class='swatch' style='background:";
        html += rgbToHex(FOCUS_RGB);
        html += "'></div></div>";
        html += "<div class='row' style='margin-top:10px'>";
        html += "<div class='muted'>Short Break: <span id='shortVal'>5</span> min</div>";
        html += "<input type='range' min='1' max='30' name='short' id='modeShort' value='5' oninput='syncRange(\"modeShort\",\"shortVal\")'>";
        html += "</div>";
        html += "<div class='row'><div class='muted'>Short Color</div>";
        html += "<input type='color' id='shortColor' name='short_color' value='";
        html += rgbToHex(SHORT_RGB);
        html += "' oninput='syncColor(\"shortColor\",\"shortSwatch\")'>";
        html += "<div id='shortSwatch' class='swatch' style='background:";
        html += rgbToHex(SHORT_RGB);
        html += "'></div></div>";
        html += "<div class='row' style='margin-top:10px'>";
        html += "<div class='muted'>Long Break: <span id='longVal'>15</span> min</div>";
        html += "<input type='range' min='5' max='40' name='long' id='modeLong' value='15' oninput='syncRange(\"modeLong\",\"longVal\")'>";
        html += "</div>";
        html += "<div class='row'><div class='muted'>Long Color</div>";
        html += "<input type='color' id='longColor' name='long_color' value='";
        html += rgbToHex(LONG_RGB);
        html += "' oninput='syncColor(\"longColor\",\"longSwatch\")'>";
        html += "<div id='longSwatch' class='swatch' style='background:";
        html += rgbToHex(LONG_RGB);
        html += "'></div></div>";
        html += "<div class='row' style='margin-top:10px'><button class='btn save' type='submit'>Save Mode</button>";
        html += "<button class='btn' type='button' onclick='resetColors()'>Reset Colors</button>";
        html += "<button class='btn' type='button' onclick='clearMode()'>Clear</button></div>";
        html += "</form></div>";

        html += "<script>"
                "const modeList=document.getElementById('mode-list');"
                "let dragM=null;"
                "function updateModeOrder(){const ids=[...modeList.children].filter(x=>x.dataset.idx!==undefined)"
                ".map(el=>el.dataset.idx);document.getElementById('modeOrder').value=ids.join(',');}"
                "function enableModeDrag(){[...document.querySelectorAll('.mode-item')].forEach(el=>{"
                "el.addEventListener('dragstart',e=>{dragM=el;e.dataTransfer.effectAllowed='move';});"
                "el.addEventListener('dragover',e=>{e.preventDefault();const t=e.currentTarget;"
                "if(t===dragM)return;const r=t.getBoundingClientRect();"
                "const next=(e.clientY-r.top)>(r.height/2);"
                "modeList.insertBefore(dragM,next?t.nextSibling:t);updateModeOrder();});"
                "});updateModeOrder();}"
                "const defFocus='" + rgbToHex(FOCUS_RGB) + "';"
                "const defShort='" + rgbToHex(SHORT_RGB) + "';"
                "const defLong='" + rgbToHex(LONG_RGB) + "';"
                "function syncRange(inputId, valueId){"
                "const v=document.getElementById(inputId).value;"
                "document.getElementById(valueId).textContent=v;}"
                "function syncColor(inputId, swatchId){"
                "const v=document.getElementById(inputId).value;"
                "document.getElementById(swatchId).style.background=v;}"
                "function editMode(i){const el=[...document.querySelectorAll('.mode-item')].find(x=>x.dataset.idx==i);"
                "if(!el)return;document.getElementById('modeIndex').value=i;"
                "document.getElementById('modeLabel').value=el.dataset.label;"
                "document.getElementById('modeFocus').value=el.dataset.focus;syncRange('modeFocus','focusVal');"
                "document.getElementById('modeShort').value=el.dataset.short;syncRange('modeShort','shortVal');"
                "document.getElementById('modeLong').value=el.dataset.long;syncRange('modeLong','longVal');"
                "document.getElementById('focusColor').value=el.dataset.fc||defFocus;syncColor('focusColor','focusSwatch');"
                "document.getElementById('shortColor').value=el.dataset.sc||defShort;syncColor('shortColor','shortSwatch');"
                "document.getElementById('longColor').value=el.dataset.lc||defLong;syncColor('longColor','longSwatch');}"
                "function resetColors(){document.getElementById('focusColor').value=defFocus;syncColor('focusColor','focusSwatch');"
                "document.getElementById('shortColor').value=defShort;syncColor('shortColor','shortSwatch');"
                "document.getElementById('longColor').value=defLong;syncColor('longColor','longSwatch');}"
                "function clearMode(){document.getElementById('modeIndex').value='-1';"
                "document.getElementById('modeLabel').value='';"
                "document.getElementById('modeFocus').value='25';syncRange('modeFocus','focusVal');"
                "document.getElementById('modeShort').value='5';syncRange('modeShort','shortVal');"
                "document.getElementById('modeLong').value='15';syncRange('modeLong','longVal');"
                "resetColors();}"
                "enableModeDrag();"
                "</script>";

        html += "</div></body></html>";
        webServer.send(200, "text/html", html);
      });

      webServer.on("/ntp", []() {
        String html;
        html.reserve(4096);
        html += "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Time Sync</title>"
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
                "button.btn{padding:8px 12px;border-radius:10px;border:1px solid #2a3a54;background:#1d2736;"
                "color:var(--text);font-weight:600}"
                "button.btn.save{background:#59d98e;border-color:#3aa66b;color:#07111e}"
                "input,select{background:#0f141a;border:1px solid #2a3a54;color:var(--text);border-radius:8px;"
                "padding:8px 10px;min-width:220px}"
                ".muted{color:var(--muted);font-size:12px}"
                "</style></head><body><div class='wrap'>";
        html += "<div class='title'>Time Sync</div>";
        html += "<div class='card'><div class='row'>";
        html += "<a class='pill' href='/'>Back</a>";
        html += "<div class='pill'>Active: ";
        html += currentWifiLabel();
        html += "</div>";
        html += "<div class='pill'>IP: ";
        html += currentIpLabel();
        html += "</div>";
        html += "</div></div>";
        html += "<div class='card'>";
        html += "<form method='post' action='/ntp/save'>";
        html += "<div class='row'><input name='ntp_server' value='";
        html += ntpServer;
        html += "' placeholder='NTP Server'></div>";
        html += "<div class='row' style='margin-top:10px'><select name='dst_mode'>";
        html += "<option value='0'";
        if (dstMode == DST_AUTO) html += " selected";
        html += ">DST Auto</option>";
        html += "<option value='1'";
        if (dstMode == DST_STANDARD) html += " selected";
        html += ">Force Standard Time</option>";
        html += "<option value='2'";
        if (dstMode == DST_SUMMER) html += " selected";
        html += ">Force Summer Time</option>";
        html += "</select></div>";
        html += "<div class='row' style='margin-top:12px'>";
        html += "<button class='btn save' type='submit'>Save</button>";
        html += "</div>";
        html += "<div class='muted' style='margin-top:6px'>If you leave without saving, nothing changes.</div>";
        html += "</form></div>";
        html += "</div></body></html>";
        webServer.send(200, "text/html", html);
      });

      webServer.on("/ntp/save", []() {
        String ntp = webServer.arg("ntp_server");
        String dst = webServer.arg("dst_mode");
        ntp.trim();
        if (ntp.length() > 0) {
          ntpServer = ntp;
        } else {
          ntpServer = DEFAULT_NTP_SERVER;
        }
        int mode = dst.toInt();
        if (mode < DST_AUTO || mode > DST_SUMMER) {
          mode = DST_AUTO;
        }
        dstMode = mode;
        saveWifiConfig();
        timeConfigured = false;
        if (WiFi.status() == WL_CONNECTED) {
          applyTimeConfig();
        }
        webServer.sendHeader("Location", "/ntp");
        webServer.send(303);
      });

      webServer.on("/time/mode", []() {
        String order = webServer.arg("order");
        String selectedLabel = "";
        if (!modes.empty() && selectedModeIndex >= 0 && selectedModeIndex < (int)modes.size()) {
          selectedLabel = modes[selectedModeIndex].label;
        }
        std::vector<ModeEntry> newList;
        int start = 0;
        while (start < order.length()) {
          int comma = order.indexOf(',', start);
          if (comma < 0) {
            comma = order.length();
          }
          String token = order.substring(start, comma);
          int idx = token.toInt();
          if (idx >= 0 && idx < (int)modes.size()) {
            newList.push_back(modes[idx]);
          }
          start = comma + 1;
        }
        if (!newList.empty()) {
          modes = newList;
          int newIndex = 0;
          if (selectedLabel.length() > 0) {
            for (int i = 0; i < (int)modes.size(); i++) {
              if (modes[i].label == selectedLabel) {
                newIndex = i;
                break;
              }
            }
          }
          selectedModeIndex = newIndex;
          saveModesConfig();
          saveSelectedMode();
        }
        webServer.sendHeader("Location", "/time");
        webServer.send(303);
      });

      webServer.on("/time/save_mode", []() {
        int idx = webServer.arg("i").toInt();
        String label = webServer.arg("label");
        int focus = webServer.arg("focus").toInt();
        int sh = webServer.arg("short").toInt();
        int lng = webServer.arg("long").toInt();
        String fc = webServer.arg("focus_color");
        String sc = webServer.arg("short_color");
        String lc = webServer.arg("long_color");
        label.trim();
        if (label.length() == 0) {
          webServer.sendHeader("Location", "/time");
          webServer.send(303);
          return;
        }
        if (focus < 5) focus = 5;
        if (focus > 90) focus = 90;
        if (sh < 1) sh = 1;
        if (sh > 30) sh = 30;
        if (lng < 5) lng = 5;
        if (lng > 40) lng = 40;
        if (idx >= 0 && idx < (int)modes.size()) {
          modes[idx].label = label;
          modes[idx].focusMin = focus;
          modes[idx].shortMin = sh;
          modes[idx].longMin = lng;
          modes[idx].focusColor = fc;
          modes[idx].shortColor = sc;
          modes[idx].longColor = lc;
        } else if ((int)modes.size() < MAX_MODES) {
          ModeEntry m = {label, focus, sh, lng, fc, sc, lc};
          modes.push_back(m);
        }
        saveModesConfig();
        webServer.sendHeader("Location", "/time");
        webServer.send(303);
      });

      webServer.on("/time/delete", []() {
        int idx = webServer.arg("i").toInt();
        if (idx >= 0 && idx < (int)modes.size()) {
          modes.erase(modes.begin() + idx);
        if (modes.empty()) {
          ModeEntry m1 = {"25/10", 25, 10, 15, "", "", ""};
          modes.push_back(m1);
        }
          if (selectedModeIndex >= (int)modes.size()) {
            selectedModeIndex = 0;
          }
          saveModesConfig();
          saveSelectedMode();
        }
        webServer.sendHeader("Location", "/time");
        webServer.send(303);
      });

      webServer.begin();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!timeConfigured) {
      applyTimeConfig();
    }
    fetchWeather(false);
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

void fetchWeather(bool force) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  uint32_t now = millis();
  if (!force && weather.valid && (now - weather.updatedMs) < WEATHER_REFRESH_MS) {
    return;
  }

  String url = "http://api.open-meteo.com/v1/forecast?latitude=";
  url += String(WEATHER_LAT, 4);
  url += "&longitude=";
  url += String(WEATHER_LON, 4);
  url += "&current=temperature_2m,apparent_temperature,relative_humidity_2m,precipitation_probability,precipitation,rain,snowfall,wind_speed_10m,weather_code";
  url += "&hourly=temperature_2m,precipitation_probability,weather_code";
  url += "&timezone=Europe%2FBerlin";

  HTTPClient http;
  http.begin(url);
  http.setTimeout(8000);
  int code = http.GET();
  if (code <= 0) {
    http.end();
    return;
  }
  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    return;
  }

  JsonObject cur = doc["current"];
  if (!cur.isNull()) {
    weather.temp = cur["temperature_2m"] | 0.0f;
    weather.feels = cur["apparent_temperature"] | 0.0f;
    weather.wind = cur["wind_speed_10m"] | 0.0f;
    weather.precipProb = cur["precipitation_probability"] | 0.0f;
    weather.rain = cur["rain"] | 0.0f;
    weather.snow = cur["snowfall"] | 0.0f;
    weather.code = cur["weather_code"] | 0;
  }

  // next hours from hourly arrays
  JsonArray times = doc["hourly"]["time"].as<JsonArray>();
  JsonArray temps = doc["hourly"]["temperature_2m"].as<JsonArray>();
  JsonArray probs = doc["hourly"]["precipitation_probability"].as<JsonArray>();
  JsonArray codes = doc["hourly"]["weather_code"].as<JsonArray>();

  int index = -1;
  struct tm timeinfo;
  bool hasTime = getLocalTime(&timeinfo, 50);
  if (hasTime) {
    char tbuf[20];
    snprintf(tbuf, sizeof(tbuf), "%04d-%02d-%02dT%02d:00",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour);
    for (int i = 0; i < (int)times.size(); i++) {
      const char* t = times[i] | "";
      if (strcmp(t, tbuf) == 0) {
        index = i;
        break;
      }
    }
  }
  if (index < 0) {
    index = 0;
  }

  for (int i = 0; i < 3; i++) {
    int idx = index + 1 + i;
    if (idx < (int)times.size()) {
      weather.nextTemp[i] = temps[idx] | 0.0f;
      weather.nextProb[i] = probs[idx] | 0;
      weather.nextCode[i] = codes[idx] | 0;
      weather.nextHour[i] = hasTime ? (timeinfo.tm_hour + 1 + i) % 24 : (i + 1);
    } else {
      weather.nextTemp[i] = 0.0f;
      weather.nextProb[i] = 0;
      weather.nextCode[i] = 0;
      weather.nextHour[i] = 0;
    }
  }

  weather.updatedMs = now;
  weather.valid = true;
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

String rgbToHex(const RgbColor& rgb) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02X%02X%02X", rgb.r, rgb.g, rgb.b);
  return String(buf);
}

RgbColor parseHexColor(const String& hex, const RgbColor& fallback) {
  String s = hex;
  s.trim();
  if (s.startsWith("#")) {
    s = s.substring(1);
  }
  if (s.length() != 6) {
    return fallback;
  }
  char buf[7];
  s.toCharArray(buf, sizeof(buf));
  unsigned long val = strtoul(buf, nullptr, 16);
  RgbColor out;
  out.r = (val >> 16) & 0xFF;
  out.g = (val >> 8) & 0xFF;
  out.b = val & 0xFF;
  return out;
}

uint16_t modeColorForPhase(Phase phase) {
  if (modes.empty()) {
    if (phase == PHASE_FOCUS) return colorFocus;
    if (phase == PHASE_SHORT_BREAK) return colorShort;
    return colorLong;
  }
  int idx = activeModeIndex;
  if (idx < 0 || idx >= (int)modes.size()) {
    idx = 0;
  }
  const ModeEntry& m = modes[idx];
  if (phase == PHASE_FOCUS) {
    RgbColor c = parseHexColor(m.focusColor, FOCUS_RGB);
    return tft.color565(c.r, c.g, c.b);
  }
  if (phase == PHASE_SHORT_BREAK) {
    RgbColor c = parseHexColor(m.shortColor, SHORT_RGB);
    return tft.color565(c.r, c.g, c.b);
  }
  RgbColor c = parseHexColor(m.longColor, LONG_RGB);
  return tft.color565(c.r, c.g, c.b);
}

void loadSavedMode() {
  prefs.begin(PREFS_NAMESPACE, true);
  int saved = prefs.getInt(PREFS_MODE_KEY, 0);
  prefs.end();
  if (saved < 0 || saved >= (int)modes.size()) {
    saved = 0;
  }
  selectedModeIndex = saved;
}

void saveSelectedMode() {
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putInt(PREFS_MODE_KEY, selectedModeIndex);
  prefs.end();
}

void loadSavedApp() {
  prefs.begin(PREFS_NAMESPACE, true);
  int saved = prefs.getInt(PREFS_APP_KEY, 0);
  prefs.end();
  if (saved < 0 || saved > APP_FLAPPY) {
    saved = 0;
  }
  selectedAppIndex = saved;
}

void saveSelectedApp() {
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putInt(PREFS_APP_KEY, selectedAppIndex);
  prefs.end();
}

void switchToApp(int appIndex) {
  if (appIndex == APP_AI && !isAiAvailable()) {
    appIndex = APP_POMODORO;
  }
  if (appIndex < 0 || appIndex > APP_FLAPPY) {
    appIndex = APP_POMODORO;
  }
  selectedAppIndex = appIndex;
  saveSelectedApp();

  if (selectedAppIndex == APP_CLOCK) {
    screenState = SCREEN_CLOCK;
    renderClockScreen(true);
    return;
  }
  if (selectedAppIndex == APP_AI) {
    screenState = SCREEN_AI;
    renderAiScreen(true);
    return;
  }
  if (selectedAppIndex == APP_WEATHER) {
    screenState = SCREEN_WEATHER;
    fetchWeather(true);
    renderWeatherScreen(true);
    return;
  }
  if (selectedAppIndex == APP_SNAKE) {
    screenState = SCREEN_SNAKE;
    initSnakeGame();
    renderSnakeScreen(true);
    return;
  }
  if (selectedAppIndex == APP_FLAPPY) {
    screenState = SCREEN_FLAPPY;
    initFlappyGame();
    renderFlappyScreen(true);
    return;
  }

  screenState = SCREEN_START;
  startPhase(PHASE_FOCUS, false);
  completedFocusSessions = 0;
  lastRemainingSeconds = 0xFFFFFFFFUL;
  lastPhase = currentPhase;
  lastRunning = isRunning;
  lastCompletedFocus = -1;
  lastInputMs = millis();
  renderStartScreen(true, lastInputMs);
}

void loadWifiConfig() {
  prefs.begin(PREFS_NAMESPACE, true);
  String json = prefs.getString(PREFS_WIFI_KEY, "");
  apSsid = prefs.getString(PREFS_AP_SSID_KEY, DEFAULT_AP_SSID);
  apPass = prefs.getString(PREFS_AP_PASS_KEY, "");
  ntpServer = prefs.getString(PREFS_NTP_SERVER_KEY, DEFAULT_NTP_SERVER);
  dstMode = prefs.getInt(PREFS_DST_MODE_KEY, DST_AUTO);
  clockColor = prefs.getString(PREFS_CLOCK_COLOR_KEY, "");
  clockSizeIndex = prefs.getInt(PREFS_CLOCK_SIZE_KEY, 1);
  aiHost = prefs.getString(PREFS_AI_HOST_KEY, "");
  aiWifiSsid = prefs.getString(PREFS_AI_WIFI_KEY, "");
  aiModel = prefs.getString(PREFS_AI_MODEL_KEY, "llama3.2:3b");
  aiSystemMessage = prefs.getString(PREFS_AI_SYSTEM_KEY, "");
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
  prefs.putString(PREFS_NTP_SERVER_KEY, ntpServer);
  prefs.putInt(PREFS_DST_MODE_KEY, dstMode);
  prefs.putString(PREFS_CLOCK_COLOR_KEY, clockColor);
  prefs.putInt(PREFS_CLOCK_SIZE_KEY, clockSizeIndex);
  prefs.putString(PREFS_AI_HOST_KEY, aiHost);
  prefs.putString(PREFS_AI_WIFI_KEY, aiWifiSsid);
  prefs.putString(PREFS_AI_MODEL_KEY, aiModel);
  prefs.putString(PREFS_AI_SYSTEM_KEY, aiSystemMessage);
  prefs.end();
}

void loadModesConfig() {
  prefs.begin(PREFS_NAMESPACE, true);
  String json = prefs.getString(PREFS_MODES_KEY, "");
  prefs.end();

  modes.clear();
  if (json.length() > 0) {
    StaticJsonDocument<1536> doc;
    DeserializationError err = deserializeJson(doc, json);
    if (!err) {
  JsonArray arr = doc.as<JsonArray>();
  for (JsonVariant v : arr) {
    if (!v.is<JsonObject>()) {
      continue;
    }
    String label = v["l"] | "";
    int focus = v["f"] | 25;
    int sh = v["s"] | 5;
    int lng = v["g"] | 15;
    String fc = v["fc"] | "";
    String sc = v["sc"] | "";
    String lc = v["lc"] | "";
    if (label.length() == 0) {
      continue;
    }
    ModeEntry m;
    m.label = label;
    m.focusMin = focus;
    m.shortMin = sh;
    m.longMin = lng;
    m.focusColor = fc;
    m.shortColor = sc;
    m.longColor = lc;
    modes.push_back(m);
    if ((int)modes.size() >= MAX_MODES) {
      break;
    }
  }
    }
  }

  if (modes.empty()) {
    ModeEntry m1 = {"25/10", 25, 10, 15, "", "", ""};
    ModeEntry m2 = {"20/10", 20, 10, 15, "", "", ""};
    ModeEntry m3 = {"25/5", 25, 5, 15, "", "", ""};
    ModeEntry m4 = {"15/5", 15, 5, 15, "", "", ""};
    modes.push_back(m1);
    modes.push_back(m2);
    modes.push_back(m3);
    modes.push_back(m4);
  }
}

void saveModesConfig() {
  StaticJsonDocument<1536> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& m : modes) {
    JsonObject obj = arr.createNestedObject();
    obj["l"] = m.label;
    obj["f"] = m.focusMin;
    obj["s"] = m.shortMin;
    obj["g"] = m.longMin;
    if (m.focusColor.length() > 0) obj["fc"] = m.focusColor;
    if (m.shortColor.length() > 0) obj["sc"] = m.shortColor;
    if (m.longColor.length() > 0) obj["lc"] = m.longColor;
  }
  String json;
  serializeJson(doc, json);
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putString(PREFS_MODES_KEY, json);
  prefs.end();
}

uint32_t durationForPhase(Phase phase) {
  if (modes.empty()) {
    return 0;
  }
  int idx = activeModeIndex;
  if (idx < 0 || idx >= (int)modes.size()) {
    idx = 0;
  }
  const ModeEntry& mode = modes[idx];
  if (phase == PHASE_FOCUS) {
    return (uint32_t)mode.focusMin * 60UL * 1000UL;
  }
  if (phase == PHASE_SHORT_BREAK) {
    return (uint32_t)mode.shortMin * 60UL * 1000UL;
  }
  return (uint32_t)mode.longMin * 60UL * 1000UL;
}

const char* labelForPhase(Phase phase) {
  if (phase == PHASE_FOCUS) {
    return "FOCUS";
  }
  if (phase == PHASE_SHORT_BREAK) {
    return "SHORT BREAK";
  }
  return "LONG BREAK";
}

const char* labelForApp(int appIndex) {
  if (appIndex == APP_AI) {
    return "AI";
  }
  if (appIndex == APP_WEATHER) {
    return "Weather";
  }
  if (appIndex == APP_SNAKE) {
    return "Snake";
  }
  if (appIndex == APP_FLAPPY) {
    return "Flappy";
  }
  return (appIndex == APP_CLOCK) ? "Clock" : "Pomodoro";
}

bool isAiAvailable() {
  if (aiHost.length() == 0 || aiWifiSsid.length() == 0) {
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  return WiFi.SSID() == aiWifiSsid;
}

uint16_t colorForPhase(Phase phase) {
  return modeColorForPhase(phase);
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

  const char* line1 = "PHASE CHANGE";
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
  snprintf(roundStr, sizeof(roundStr), "ROUND %d/4", round);

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

  const char* modeLabel = showClockInstead ? clockStr : (modes.empty() ? "--" : modes[selectedModeIndex].label.c_str());
  const int modeTextSize = 6;
  tft.setTextSize(modeTextSize);
  uint16_t modeColor = colorFocus;
  if (!modes.empty() && selectedModeIndex >= 0 && selectedModeIndex < (int)modes.size()) {
    RgbColor c = parseHexColor(modes[selectedModeIndex].focusColor, FOCUS_RGB);
    modeColor = tft.color565(c.r, c.g, c.b);
  }
  tft.setTextColor(modeColor, TFT_BLACK);
  int modeWidth = strlen(modeLabel) * 6 * modeTextSize;
  int modeHeight = 8 * modeTextSize;
  int modeX = (tft.width() - modeWidth) / 2;
  int modeY = (tft.height() - modeHeight) / 2;
  tft.setCursor(modeX, modeY);
  tft.print(modeLabel);

  const char* modeHint = "CHANGE MODE";
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

void renderAppSelectScreen(bool force) {
  if (!isAiAvailable() && selectedAppIndex == APP_AI) {
    selectedAppIndex = APP_POMODORO;
  }
  if (!force && selectedAppIndex == lastAppIndex) {
    return;
  }

  tft.fillScreen(TFT_BLACK);

  tft.setTextSize(2);
  tft.setTextColor(colorFocus, TFT_BLACK);
  const char* title = "APP";
  int titleWidth = strlen(title) * 6 * 2;
  int titleX = (tft.width() - titleWidth) / 2;
  tft.setCursor(titleX, 6);
  tft.print(title);

  tft.setTextSize(1);
  tft.setTextColor(colorShort, TFT_BLACK);
  const char* startLabel = "OPEN";
  int startWidth = strlen(startLabel) * 6;
  int startX = tft.width() - startWidth - 6;
  tft.setCursor(startX, 6);
  tft.print(startLabel);

  const char* appLabel = "POMODORO";
  if (selectedAppIndex == APP_CLOCK) {
    appLabel = "CLOCK";
  } else if (selectedAppIndex == APP_AI) {
    appLabel = "AI";
  } else if (selectedAppIndex == APP_WEATHER) {
    appLabel = "WEATHER";
  } else if (selectedAppIndex == APP_SNAKE) {
    appLabel = "SNAKE";
  } else if (selectedAppIndex == APP_FLAPPY) {
    appLabel = "FLAPPY";
  }
  const int appTextSize = 4;
  tft.setTextSize(appTextSize);
  tft.setTextColor(colorFocus, TFT_BLACK);
  int appWidth = strlen(appLabel) * 6 * appTextSize;
  int appHeight = 8 * appTextSize;
  int appX = (tft.width() - appWidth) / 2;
  int appY = (tft.height() - appHeight) / 2;
  tft.setCursor(appX, appY);
  tft.print(appLabel);

  const char* hint = "APP SELECT";
  tft.setTextSize(1);
  tft.setTextColor(colorMuted, TFT_BLACK);
  int hintWidth = strlen(hint) * 6;
  int hintX = tft.width() - hintWidth - 6;
  tft.setCursor(hintX, tft.height() - 18);
  tft.print(hint);

  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  drawWifiIndicator(wifiConnected, true);

  lastAppIndex = selectedAppIndex;
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
      const char* pausedLabel = "PAUSED";
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

void renderClockScreen(bool force) {
  static int lastHour = -1;
  static int lastMinute = -1;
  static int lastDay = -1;
  static int lastMonth = -1;
  static int lastYear = -1;
  static int lastSizeIndex = -1;

  struct tm timeinfo;
  bool hasTime = getLocalTime(&timeinfo, 50);
  if (!hasTime) {
    if (!force) {
      return;
    }
  }

  bool timeChanged = hasTime &&
                     (timeinfo.tm_hour != lastHour || timeinfo.tm_min != lastMinute ||
                      timeinfo.tm_mday != lastDay || timeinfo.tm_mon != lastMonth ||
                      timeinfo.tm_year != lastYear);

  bool sizeChanged = (clockSizeIndex != lastSizeIndex);
  if (!force && !timeChanged && !sizeChanged) {
    return;
  }

  tft.fillScreen(TFT_BLACK);
  RgbColor c = parseHexColor(clockColor, FOCUS_RGB);
  uint16_t clockCol = tft.color565(c.r, c.g, c.b);

  char timeStr[6] = "--:--";
  char dateStr[20] = "--.--.----";
  if (hasTime) {
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    snprintf(dateStr, sizeof(dateStr), "%02d.%02d.%04d", timeinfo.tm_mday,
             timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
  }

  int sizeOptions[] = {4, 5, 7};
  int sizeCount = sizeof(sizeOptions) / sizeof(sizeOptions[0]);
  int sizeIdx = clockSizeIndex;
  if (sizeIdx < 0) sizeIdx = 0;
  if (sizeIdx >= sizeCount) sizeIdx = sizeCount - 1;
  int timeSize = sizeOptions[sizeIdx];
  int dateSize = (timeSize >= 7) ? 3 : 2;
  int timeHeight = 8 * timeSize;
  int dateHeight = 8 * dateSize;
  int gap = 6;
  int blockHeight = timeHeight + gap + dateHeight;
  int timeY = (tft.height() - blockHeight) / 2;
  if (timeY < 8) timeY = 8;
  int dateY = timeY + timeHeight + gap;

  tft.setTextSize(timeSize);
  tft.setTextColor(clockCol, TFT_BLACK);
  int timeWidth = strlen(timeStr) * 6 * timeSize;
  int timeX = (tft.width() - timeWidth) / 2;
  tft.setCursor(timeX, timeY);
  tft.print(timeStr);

  tft.setTextSize(dateSize);
  tft.setTextColor(colorMuted, TFT_BLACK);
  int dateWidth = strlen(dateStr) * 6 * dateSize;
  int dateX = (tft.width() - dateWidth) / 2;
  tft.setCursor(dateX, dateY);
  tft.print(dateStr);

  lastHour = hasTime ? timeinfo.tm_hour : -1;
  lastMinute = hasTime ? timeinfo.tm_min : -1;
  lastDay = hasTime ? timeinfo.tm_mday : -1;
  lastMonth = hasTime ? timeinfo.tm_mon : -1;
  lastYear = hasTime ? timeinfo.tm_year : -1;
  lastSizeIndex = clockSizeIndex;
}

std::vector<String> wrapText(const String& text, int maxChars) {
  std::vector<String> lines;
  String current = "";
  int start = 0;
  while (start < text.length()) {
    int nl = text.indexOf('\n', start);
    String segment;
    if (nl >= 0) {
      segment = text.substring(start, nl);
      start = nl + 1;
    } else {
      segment = text.substring(start);
      start = text.length();
    }
    int segStart = 0;
    while (segStart < segment.length()) {
      int space = segment.indexOf(' ', segStart);
      if (space < 0) space = segment.length();
      String word = segment.substring(segStart, space);
      if (word.length() == 0) {
        segStart = space + 1;
        continue;
      }
      if (current.length() == 0) {
        current = word;
      } else if ((int)(current.length() + 1 + word.length()) <= maxChars) {
        current += " ";
        current += word;
      } else {
        lines.push_back(current);
        current = word;
      }
      segStart = space + 1;
    }
    if (current.length() > 0) {
      lines.push_back(current);
      current = "";
    }
    if (nl >= 0 && segment.length() == 0) {
      lines.push_back("");
    }
  }
  if (lines.empty()) {
    lines.push_back("");
  }
  return lines;
}

void renderAiScreen(bool force) {
  static String lastContent = "";
  static int lastScroll = -1;

  String content = "";
  if (aiTypingText.length() > 0) {
    content = "You:\n" + aiTypingText + "\n\nAI:\n...";
  } else if (aiResponseText.length() > 0) {
    content = "AI:\n" + aiResponseText;
  } else {
    content = "AI ready.\nOpen the web UI to send a message.";
  }

  String displayContent = content;
  displayContent.replace("ä", "ae");
  displayContent.replace("ö", "oe");
  displayContent.replace("ü", "ue");
  displayContent.replace("Ä", "Ae");
  displayContent.replace("Ö", "Oe");
  displayContent.replace("Ü", "Ue");
  displayContent.replace("ß", "ss");

  if (!force && displayContent == lastContent && aiScrollOffset == lastScroll) {
    return;
  }

  if (force) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(colorFocus, TFT_BLACK);
    tft.setCursor(6, 6);
    tft.print("AI");
    drawWifiIndicator(WiFi.status() == WL_CONNECTED, true);
  }

  const int textSize = 2;
  const int lineHeight = 8 * textSize + 2;
  const int topY = 28;
  const int maxWidth = tft.width() - 12;
  int maxChars = maxWidth / (6 * textSize);
  if (maxChars < 10) maxChars = 10;
  std::vector<String> lines = wrapText(displayContent, maxChars);

  int maxVisible = (tft.height() - topY - 6) / lineHeight;
  if (maxVisible < 1) maxVisible = 1;
  int maxScroll = (int)lines.size() - maxVisible;
  if (maxScroll < 0) maxScroll = 0;
  if (aiScrollOffset < 0) aiScrollOffset = 0;
  if (aiScrollOffset > maxScroll) aiScrollOffset = maxScroll;

  tft.fillRect(0, topY, tft.width(), tft.height() - topY, TFT_BLACK);
  tft.setTextSize(textSize);
  tft.setTextColor(colorMuted, TFT_BLACK);
  for (int i = 0; i < maxVisible; i++) {
    int idx = aiScrollOffset + i;
    if (idx >= (int)lines.size()) break;
    String line = lines[idx];
    bool boldLine = line.indexOf("**") >= 0;
    line.replace("**", "");
    tft.setCursor(6, topY + i * lineHeight);
    tft.print(line);
    if (boldLine) {
      tft.setCursor(7, topY + i * lineHeight);
      tft.print(line);
    }
  }

  lastContent = displayContent;
  lastScroll = aiScrollOffset;
}

void drawWeatherIcon(int code, float rain, float snow, int x, int y) {
  const int w = 32;
  const int h = 32;
  uint16_t sunC = tft.color565(255, 200, 40);
  uint16_t cloudC = tft.color565(90, 140, 200);
  uint16_t rainC = tft.color565(70, 170, 255);
  uint16_t snowC = tft.color565(200, 220, 255);
  uint16_t stormC = tft.color565(255, 180, 40);

  auto drawSun = [&]() {
    int cx = x + w / 2;
    int cy = y + h / 2;
    tft.fillCircle(cx, cy, 7, sunC);
    tft.drawLine(cx, y + 2, cx, y + 8, sunC);
    tft.drawLine(cx, y + h - 8, cx, y + h - 2, sunC);
    tft.drawLine(x + 2, cy, x + 8, cy, sunC);
    tft.drawLine(x + w - 8, cy, x + w - 2, cy, sunC);
    tft.drawLine(cx - 6, cy - 6, cx - 2, cy - 2, sunC);
    tft.drawLine(cx + 2, cy + 2, cx + 6, cy + 6, sunC);
    tft.drawLine(cx + 2, cy - 2, cx + 6, cy - 6, sunC);
    tft.drawLine(cx - 6, cy + 6, cx - 2, cy + 2, sunC);
  };

  auto drawCloud = [&](uint16_t c) {
    tft.fillCircle(x + 10, y + 16, 7, c);
    tft.fillCircle(x + 18, y + 13, 9, c);
    tft.fillCircle(x + 25, y + 16, 7, c);
    tft.fillRoundRect(x + 6, y + 16, 22, 10, 5, c);
  };

  auto drawRainDrops = [&](uint16_t c) {
    tft.drawLine(x + 10, y + 26, x + 8, y + 30, c);
    tft.drawLine(x + 16, y + 26, x + 14, y + 30, c);
    tft.drawLine(x + 22, y + 26, x + 20, y + 30, c);
  };

  auto drawSnowFlake = [&](int cx, int cy, uint16_t c) {
    tft.drawLine(cx - 2, cy, cx + 2, cy, c);
    tft.drawLine(cx, cy - 2, cx, cy + 2, c);
    tft.drawLine(cx - 2, cy - 2, cx + 2, cy + 2, c);
    tft.drawLine(cx - 2, cy + 2, cx + 2, cy - 2, c);
  };

  auto drawLightning = [&](uint16_t c) {
    tft.fillTriangle(x + 16, y + 18, x + 20, y + 18, x + 12, y + 30, c);
    tft.fillTriangle(x + 12, y + 26, x + 18, y + 26, x + 10, y + 32, c);
  };

  if (snow > 0.0f) {
    drawCloud(cloudC);
    drawSnowFlake(x + 10, y + 20, snowC);
    drawSnowFlake(x + 16, y + 20, snowC);
    return;
  }
  if (rain > 0.0f) {
    drawCloud(cloudC);
    drawRainDrops(rainC);
    return;
  }
  if (code >= 80 && code <= 99) {
    drawCloud(cloudC);
    drawLightning(stormC);
    return;
  }
  if (code >= 45 && code <= 48) {
    drawCloud(cloudC);
    tft.drawFastHLine(x + 4, y + 20, 16, cloudC);
    return;
  }
  if (code >= 1 && code <= 3) {
    drawCloud(cloudC);
    return;
  }
  drawSun();
}

void renderWeatherScreen(bool force) {
  static uint32_t lastDrawMs = 0;
  static int lastView = -1;
  static bool lastValid = false;
  static float lastTemp = 999;
  static float lastFeels = 999;
  static float lastWind = 999;
  static float lastPop = 999;
  static int lastCode = -1;
  static float lastRain = -1;
  static float lastSnow = -1;
  static int lastHour0 = -1;
  static int lastHour1 = -1;
  static int lastHour2 = -1;
  static float lastNextTemp0 = 999;
  static float lastNextTemp1 = 999;
  static float lastNextTemp2 = 999;
  static int lastNextProb0 = -1;
  static int lastNextProb1 = -1;
  static int lastNextProb2 = -1;

  if (!force && (millis() - lastDrawMs) < 800) {
    return;
  }

  bool changed = force || lastView != weatherViewIndex || lastValid != weather.valid ||
                 lastTemp != weather.temp || lastFeels != weather.feels || lastWind != weather.wind ||
                 lastPop != weather.precipProb || lastCode != weather.code ||
                 lastRain != weather.rain || lastSnow != weather.snow ||
                 lastHour0 != weather.nextHour[0] || lastHour1 != weather.nextHour[1] || lastHour2 != weather.nextHour[2] ||
                 lastNextTemp0 != weather.nextTemp[0] || lastNextTemp1 != weather.nextTemp[1] || lastNextTemp2 != weather.nextTemp[2] ||
                 lastNextProb0 != weather.nextProb[0] || lastNextProb1 != weather.nextProb[1] || lastNextProb2 != weather.nextProb[2];

  if (!changed) {
    return;
  }

  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(colorFocus, TFT_BLACK);
  tft.setCursor(6, 6);
  tft.print("WEATHER");
  int iconX = 196;
  int iconY = 6;

  if (weatherViewIndex == 0) {
    int tempX = 10;
    int tempY = 28;
    tft.setTextSize(3);
    tft.setTextColor(colorShort, TFT_BLACK);
    if (weather.valid) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%.1fC", weather.temp);
      tft.setCursor(tempX, tempY);
      tft.print(buf);
    } else {
      tft.setCursor(tempX, tempY);
      tft.print("--.-C");
    }
    drawWeatherIcon(weather.code, weather.rain, weather.snow, iconX, iconY);

    tft.setTextSize(1);
    tft.setTextColor(colorMuted, TFT_BLACK);
    tft.setCursor(10, 58);
    tft.print("Next:");
    for (int i = 0; i < 3; i++) {
      int x = 10 + i * 70;
      int y = 72;
      if (weather.valid) {
        char hbuf[8];
        snprintf(hbuf, sizeof(hbuf), "%02d:00", weather.nextHour[i]);
        tft.setCursor(x, y);
        tft.print(hbuf);
        tft.setCursor(x, y + 12);
        char tbuf[8];
        snprintf(tbuf, sizeof(tbuf), "%.0fC", weather.nextTemp[i]);
        tft.print(tbuf);
        tft.setCursor(x, y + 24);
        tft.print(String(weather.nextProb[i]) + "%");
      } else {
        tft.setCursor(x, y);
        tft.print("--");
      }
    }
  } else {
    drawWeatherIcon(weather.code, weather.rain, weather.snow, iconX, iconY);
    tft.setTextSize(2);
    tft.setTextColor(colorShort, TFT_BLACK);
    if (weather.valid) {
      char buf[20];
      snprintf(buf, sizeof(buf), "Temp %.1fC", weather.temp);
      tft.setCursor(10, 32);
      tft.print(buf);
      snprintf(buf, sizeof(buf), "Feels %.1fC", weather.feels);
      tft.setCursor(10, 52);
      tft.print(buf);
      snprintf(buf, sizeof(buf), "Wind %.1f km/h", weather.wind);
      tft.setCursor(10, 72);
      tft.print(buf);
      snprintf(buf, sizeof(buf), "Precip %d%%", (int)weather.precipProb);
      tft.setCursor(10, 92);
      tft.print(buf);
    } else {
      tft.setCursor(10, 32);
      tft.print("No data");
    }
  }
  lastDrawMs = millis();
  lastView = weatherViewIndex;
  lastValid = weather.valid;
  lastTemp = weather.temp;
  lastFeels = weather.feels;
  lastWind = weather.wind;
  lastPop = weather.precipProb;
  lastCode = weather.code;
  lastRain = weather.rain;
  lastSnow = weather.snow;
  lastHour0 = weather.nextHour[0];
  lastHour1 = weather.nextHour[1];
  lastHour2 = weather.nextHour[2];
  lastNextTemp0 = weather.nextTemp[0];
  lastNextTemp1 = weather.nextTemp[1];
  lastNextTemp2 = weather.nextTemp[2];
  lastNextProb0 = weather.nextProb[0];
  lastNextProb1 = weather.nextProb[1];
  lastNextProb2 = weather.nextProb[2];
}

int snakeCols() {
  return tft.width() / 6;
}

int snakeRows() {
  int playHeight = tft.height() - 18;
  return playHeight / 6;
}

void placeSnakeFood() {
  int cols = snakeCols();
  int rows = snakeRows();
  if (cols <= 1 || rows <= 1) {
    snakeFoodX = 0;
    snakeFoodY = 0;
    return;
  }
  bool placed = false;
  for (int tries = 0; tries < 200 && !placed; tries++) {
    int fx = random(0, cols);
    int fy = random(0, rows);
    bool onSnake = false;
    for (int i = 0; i < snakeLen; i++) {
      if (snakeX[i] == fx && snakeY[i] == fy) {
        onSnake = true;
        break;
      }
    }
    if (!onSnake) {
      snakeFoodX = fx;
      snakeFoodY = fy;
      placed = true;
    }
  }
  if (!placed) {
    snakeFoodX = 0;
    snakeFoodY = 0;
  }
  snakeFoodChanged = true;
}

void initSnakeGame() {
  int cols = snakeCols();
  int rows = snakeRows();
  if (cols < 6) cols = 6;
  if (rows < 6) rows = 6;
  snakeLen = 3;
  snakeDir = 1;
  snakeGameOver = false;
  int startX = cols / 2;
  int startY = rows / 2;
  for (int i = 0; i < snakeLen; i++) {
    snakeX[i] = startX - i;
    snakeY[i] = startY;
  }
  placeSnakeFood();
  snakeLastStepMs = millis();
  snakeInit = true;
  snakeDirty = true;
  snakePrevTailValid = false;
  snakePrevHeadValid = false;
  snakeGrewLast = false;
  snakeFoodChanged = true;
}

void updateSnakeGame(uint32_t nowMs) {
  if (snakeGameOver) {
    return;
  }
  if (!snakeInit) {
    initSnakeGame();
  }
  if (nowMs - snakeLastStepMs < 180) {
    return;
  }
  snakeLastStepMs = nowMs;
  int headX = snakeX[0];
  int headY = snakeY[0];
  if (snakeDir == 0) headY -= 1;
  if (snakeDir == 1) headX += 1;
  if (snakeDir == 2) headY += 1;
  if (snakeDir == 3) headX -= 1;

  int cols = snakeCols();
  int rows = snakeRows();
  if (headX < 0 || headY < 0 || headX >= cols || headY >= rows) {
    snakeGameOver = true;
    snakeDirty = true;
    return;
  }
  for (int i = 0; i < snakeLen; i++) {
    if (snakeX[i] == headX && snakeY[i] == headY) {
      snakeGameOver = true;
      snakeDirty = true;
      return;
    }
  }

  bool grew = (headX == snakeFoodX && headY == snakeFoodY);
  int maxLen = (int)(sizeof(snakeX) / sizeof(snakeX[0]));
  int nextLen = snakeLen + (grew ? 1 : 0);
  if (nextLen > maxLen) {
    nextLen = maxLen;
  }

  snakePrevHeadX = snakeX[0];
  snakePrevHeadY = snakeY[0];
  snakePrevHeadValid = true;
  if (!grew && snakeLen > 0) {
    snakePrevTailX = snakeX[snakeLen - 1];
    snakePrevTailY = snakeY[snakeLen - 1];
    snakePrevTailValid = true;
  } else {
    snakePrevTailValid = false;
  }

  for (int i = nextLen - 1; i > 0; i--) {
    int from = i - 1;
    if (from >= snakeLen) {
      from = snakeLen - 1;
    }
    snakeX[i] = snakeX[from];
    snakeY[i] = snakeY[from];
  }
  snakeX[0] = headX;
  snakeY[0] = headY;
  if (grew) {
    snakeLen = nextLen;
    placeSnakeFood();
  }
  snakeGrewLast = grew;
  snakeDirty = true;
}

void renderSnakeScreen(bool force) {
  if (!force && !snakeDirty) {
    return;
  }
  int cols = snakeCols();
  int rows = snakeRows();
  const int cell = 6;
  int top = 18;

  if (force) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(colorFocus, TFT_BLACK);
    tft.setCursor(6, 6);
    tft.print("SNAKE");
    for (int y = 0; y < rows; y++) {
      for (int x = 0; x < cols; x++) {
        tft.fillRect(x * cell, top + y * cell, cell, cell, TFT_BLACK);
      }
    }
    for (int i = 0; i < snakeLen; i++) {
      uint16_t c = (i == 0) ? colorFocus : colorMuted;
      tft.fillRect(snakeX[i] * cell, top + snakeY[i] * cell, cell, cell, c);
    }
    tft.fillRect(snakeFoodX * cell, top + snakeFoodY * cell, cell, cell, colorShort);
  } else {
    if (snakePrevTailValid && !snakeGrewLast) {
      tft.fillRect(snakePrevTailX * cell, top + snakePrevTailY * cell, cell, cell, TFT_BLACK);
    }
    if (snakePrevHeadValid) {
      tft.fillRect(snakePrevHeadX * cell, top + snakePrevHeadY * cell, cell, cell, colorMuted);
    }
    tft.fillRect(snakeX[0] * cell, top + snakeY[0] * cell, cell, cell, colorFocus);
    if (snakeFoodChanged) {
      tft.fillRect(snakeFoodX * cell, top + snakeFoodY * cell, cell, cell, colorShort);
      snakeFoodChanged = false;
    }
  }

  tft.setTextSize(1);
  tft.setTextColor(colorMuted, TFT_BLACK);
  tft.fillRect(tft.width() - 64, 4, 60, 10, TFT_BLACK);
  tft.setCursor(tft.width() - 60, 6);
  tft.print("Score ");
  int score = snakeLen - 3;
  if (score < 0) score = 0;
  tft.print(score);

  if (snakeGameOver) {
    tft.setTextSize(2);
    tft.setTextColor(colorShort, TFT_BLACK);
    tft.setCursor(20, tft.height() - 30);
    tft.print("GAME OVER");
  }
  snakeDirty = false;
}

void initFlappyGame() {
  flappyY = tft.height() / 2.0f;
  flappyVel = 0.0f;
  flappyGapX = tft.width();
  flappyGapY = tft.height() / 2 - 20;
  flappyScore = 0;
  flappyGameOver = false;
  flappyLastMs = millis();
  flappyInit = true;
  flappyDirty = true;
  flappyPrevValid = false;
}

void updateFlappyGame(uint32_t nowMs) {
  if (flappyGameOver) {
    return;
  }
  if (!flappyInit) {
    initFlappyGame();
  }
  if (nowMs - flappyLastMs < 40) {
    return;
  }
  flappyLastMs = nowMs;
  flappyPrevGapX = flappyGapX;
  flappyPrevGapY = flappyGapY;
  flappyPrevY = flappyY;
  flappyPrevValid = true;

  flappyVel += 0.35f;
  flappyY += flappyVel;

  flappyGapX -= 2;
  int pipeW = 18;
  int gapH = 46;
  if (flappyGapX < -pipeW) {
    flappyGapX = tft.width() + 10;
    int minGap = 18;
    int maxGap = tft.height() - gapH - 18;
    if (maxGap < minGap) maxGap = minGap;
    flappyGapY = random(minGap, maxGap);
    flappyScore += 1;
  }

  int birdX = 40;
  int birdY = (int)flappyY;
  if (birdY < 2 || birdY > tft.height() - 4) {
    flappyGameOver = true;
  }
  if (!flappyGameOver && birdX + 4 >= flappyGapX && birdX - 4 <= flappyGapX + pipeW) {
    if (birdY < flappyGapY || birdY > flappyGapY + gapH) {
      flappyGameOver = true;
    }
  }
  flappyDirty = true;
}

void renderFlappyScreen(bool force) {
  if (!force && !flappyDirty) {
    return;
  }
  int pipeW = 18;
  int gapH = 46;
  int top = 18;

  if (force) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(colorFocus, TFT_BLACK);
    tft.setCursor(6, 6);
    tft.print("FLAPPY");
  } else if (flappyPrevValid) {
    tft.fillRect(flappyPrevGapX - 1, top, pipeW + 2, tft.height() - top, TFT_BLACK);
    int prevBirdY = (int)flappyPrevY;
    tft.fillRect(36, prevBirdY - 6, 10, 12, TFT_BLACK);
  }

  tft.fillRect(flappyGapX, top, pipeW, flappyGapY - top, colorMuted);
  tft.fillRect(flappyGapX, flappyGapY + gapH, pipeW,
               tft.height() - (flappyGapY + gapH), colorMuted);

  int birdX = 40;
  int birdY = (int)flappyY;
  tft.fillCircle(birdX, birdY, 4, colorShort);

  tft.setTextSize(1);
  tft.setTextColor(colorMuted, TFT_BLACK);
  tft.fillRect(tft.width() - 64, 4, 60, 10, TFT_BLACK);
  tft.setCursor(tft.width() - 60, 6);
  tft.print("Score ");
  tft.print(flappyScore);

  if (flappyGameOver) {
    tft.setTextSize(2);
    tft.setTextColor(colorShort, TFT_BLACK);
    tft.setCursor(20, tft.height() - 30);
    tft.print("GAME OVER");
  }
  flappyDirty = false;
}

int nextAppIndex(int current) {
  std::vector<int> apps;
  apps.push_back(APP_POMODORO);
  apps.push_back(APP_CLOCK);
  if (isAiAvailable()) {
    apps.push_back(APP_AI);
  }
  apps.push_back(APP_WEATHER);
  apps.push_back(APP_SNAKE);
  apps.push_back(APP_FLAPPY);
  int pos = 0;
  for (int i = 0; i < (int)apps.size(); i++) {
    if (apps[i] == current) {
      pos = i;
      break;
    }
  }
  pos = (pos + 1) % (int)apps.size();
  return apps[pos];
}

void setup() {
  Serial.begin(115200);
  randomSeed(micros());
  loadWifiConfig();
  loadModesConfig();
  loadSavedApp();
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
  if (selectedAppIndex == APP_AI && !isAiAvailable()) {
    selectedAppIndex = APP_POMODORO;
  }
  if (selectedAppIndex == APP_AI && isAiAvailable()) {
    screenState = SCREEN_AI;
    renderAiScreen(true);
  } else if (selectedAppIndex == APP_CLOCK) {
    screenState = SCREEN_CLOCK;
    renderClockScreen(true);
  } else if (selectedAppIndex == APP_WEATHER) {
    screenState = SCREEN_WEATHER;
    renderWeatherScreen(true);
  } else if (selectedAppIndex == APP_SNAKE) {
    screenState = SCREEN_SNAKE;
    initSnakeGame();
    renderSnakeScreen(true);
  } else if (selectedAppIndex == APP_FLAPPY) {
    screenState = SCREEN_FLAPPY;
    initFlappyGame();
    renderFlappyScreen(true);
  } else {
    screenState = SCREEN_START;
    renderStartScreen(true, lastInputMs);
  }
}

void loop() {
  uint32_t nowMs = millis();
  bool needsRedraw = false;
  updateWifiAndTime(nowMs);
  if (webServerStarted) {
    webServer.handleClient();
  }

  if (screenState == SCREEN_AI && !isAiAvailable()) {
    screenState = SCREEN_APP_SELECT;
    renderAppSelectScreen(true);
    return;
  }

  bool bothPressed = isPressedRaw(leftButton) && isPressedRaw(rightButton);
  if (bothPressed) {
    if (bothPressStartMs == 0) {
      bothPressStartMs = nowMs;
      bothPressHandled = false;
    } else if (!bothPressHandled && (nowMs - bothPressStartMs >= 10000)) {
      apPass = "";
      saveWifiConfig();
      timeConfigured = false;
      if (WiFi.status() != WL_CONNECTED) {
        stopSoftAP();
        startSoftAP();
      }
      bothPressHandled = true;
    }
  } else {
    bothPressStartMs = 0;
    bothPressHandled = false;
  }

  if (screenState == SCREEN_APP_SELECT) {
    leftPending = false;
    ButtonEvent leftEvent = updateButton(leftButton, nowMs);
    if (leftEvent == BUTTON_EVENT_SHORT) {
      if (selectedAppIndex == APP_CLOCK) {
        screenState = SCREEN_CLOCK;
        renderClockScreen(true);
      } else if (selectedAppIndex == APP_AI) {
        screenState = SCREEN_AI;
        renderAiScreen(true);
      } else if (selectedAppIndex == APP_WEATHER) {
        screenState = SCREEN_WEATHER;
        renderWeatherScreen(true);
      } else if (selectedAppIndex == APP_SNAKE) {
        screenState = SCREEN_SNAKE;
        initSnakeGame();
        renderSnakeScreen(true);
      } else if (selectedAppIndex == APP_FLAPPY) {
        screenState = SCREEN_FLAPPY;
        initFlappyGame();
        renderFlappyScreen(true);
      } else {
        screenState = SCREEN_START;
        lastInputMs = nowMs;
        renderStartScreen(true, lastInputMs);
      }
      return;
    }

    ButtonEvent rightEvent = updateButton(rightButton, nowMs);
    if (rightEvent == BUTTON_EVENT_SHORT) {
      selectedAppIndex = nextAppIndex(selectedAppIndex);
      saveSelectedApp();
      renderAppSelectScreen(true);
      return;
    }

    renderAppSelectScreen(false);
    return;
  }

  if (screenState == SCREEN_CLOCK) {
    ButtonEvent leftEvent = updateButton(leftButton, nowMs);
    if (leftEvent == BUTTON_EVENT_SHORT) {
      if (leftPending && (nowMs - leftPendingMs) <= DOUBLE_TAP_MS) {
        leftPending = false;
        screenState = SCREEN_APP_SELECT;
        renderAppSelectScreen(true);
        return;
      }
      leftPending = true;
      leftPendingMs = nowMs;
      leftPendingAction = 0;
    }
    ButtonEvent rightEvent = updateButton(rightButton, nowMs);
    if (rightEvent == BUTTON_EVENT_SHORT) {
      int sizeOptions[] = {4, 5, 7};
      int sizeCount = sizeof(sizeOptions) / sizeof(sizeOptions[0]);
      clockSizeIndex = (clockSizeIndex + 1) % sizeCount;
      saveWifiConfig();
      renderClockScreen(true);
      return;
    }
    if (leftPending && (nowMs - leftPendingMs) > DOUBLE_TAP_MS) {
      leftPending = false;
    }
    renderClockScreen(false);
    return;
  }

  if (screenState == SCREEN_WEATHER) {
    ButtonEvent leftEvent = updateButton(leftButton, nowMs);
    if (leftEvent == BUTTON_EVENT_SHORT) {
      if (leftPending && (nowMs - leftPendingMs) <= DOUBLE_TAP_MS) {
        leftPending = false;
        screenState = SCREEN_APP_SELECT;
        renderAppSelectScreen(true);
        return;
      }
      leftPending = true;
      leftPendingMs = nowMs;
      leftPendingAction = 4;
    }
    ButtonEvent rightEvent = updateButton(rightButton, nowMs);
    if (rightEvent == BUTTON_EVENT_SHORT) {
      weatherViewIndex = (weatherViewIndex + 1) % 2;
      renderWeatherScreen(true);
      return;
    }
    if (leftPending && (nowMs - leftPendingMs) > DOUBLE_TAP_MS) {
      leftPending = false;
    }
    renderWeatherScreen(false);
    return;
  }

  if (screenState == SCREEN_SNAKE) {
    static bool exitArmed = false;
    static uint32_t exitArmedMs = 0;
    leftPending = false;
    ButtonEvent leftEvent = updateButton(leftButton, nowMs);
    if (leftEvent == BUTTON_EVENT_LONG) {
      exitArmed = true;
      exitArmedMs = nowMs;
    }
    if (leftEvent == BUTTON_EVENT_SHORT) {
      if (exitArmed && (nowMs - exitArmedMs) < 1500) {
        exitArmed = false;
        screenState = SCREEN_APP_SELECT;
        renderAppSelectScreen(true);
        return;
      }
      if (snakeGameOver) {
        initSnakeGame();
      } else {
        snakeDir = (snakeDir + 3) % 4;
      }
      snakeDirty = true;
      renderSnakeScreen(true);
    }
    if (exitArmed && (nowMs - exitArmedMs) >= 1500) {
      exitArmed = false;
    }
    ButtonEvent rightEvent = updateButton(rightButton, nowMs);
    if (rightEvent == BUTTON_EVENT_SHORT) {
      if (snakeGameOver) {
        initSnakeGame();
      } else {
        snakeDir = (snakeDir + 1) % 4;
      }
      snakeDirty = true;
      renderSnakeScreen(true);
    }
    updateSnakeGame(nowMs);
    renderSnakeScreen(false);
    return;
  }

  if (screenState == SCREEN_FLAPPY) {
    static bool exitArmed = false;
    static uint32_t exitArmedMs = 0;
    leftPending = false;
    ButtonEvent leftEvent = updateButton(leftButton, nowMs);
    if (leftEvent == BUTTON_EVENT_LONG) {
      exitArmed = true;
      exitArmedMs = nowMs;
    }
    if (leftEvent == BUTTON_EVENT_SHORT) {
      if (exitArmed && (nowMs - exitArmedMs) < 1500) {
        exitArmed = false;
        screenState = SCREEN_APP_SELECT;
        renderAppSelectScreen(true);
        return;
      }
      if (flappyGameOver) {
        initFlappyGame();
      } else {
        flappyVel = -3.8f;
      }
      flappyDirty = true;
      renderFlappyScreen(true);
    }
    if (exitArmed && (nowMs - exitArmedMs) >= 1500) {
      exitArmed = false;
    }
    ButtonEvent rightEvent = updateButton(rightButton, nowMs);
    if (rightEvent == BUTTON_EVENT_SHORT) {
      if (flappyGameOver) {
        initFlappyGame();
      } else {
        flappyVel = -3.8f;
      }
      flappyDirty = true;
      renderFlappyScreen(true);
    }
    updateFlappyGame(nowMs);
    renderFlappyScreen(false);
    return;
  }

  if (screenState == SCREEN_AI) {
    static uint32_t lastScrollUpMs = 0;
    static uint32_t lastScrollDownMs = 0;

    ButtonEvent leftEvent = updateButton(leftButton, nowMs);
    if (leftEvent == BUTTON_EVENT_SHORT) {
      if (leftPending && (nowMs - leftPendingMs) <= DOUBLE_TAP_MS) {
        leftPending = false;
        screenState = SCREEN_APP_SELECT;
        renderAppSelectScreen(true);
        return;
      }
      leftPending = true;
      leftPendingMs = nowMs;
      leftPendingAction = 3;
    }

    ButtonEvent rightEvent = updateButton(rightButton, nowMs);
    if (rightEvent == BUTTON_EVENT_SHORT) {
      aiScrollOffset += 1;
      renderAiScreen(true);
    }

    if (isPressedRaw(leftButton) && (nowMs - leftButton.pressedMs) >= LONG_PRESS_MS) {
      if (nowMs - lastScrollUpMs > 180) {
        aiScrollOffset -= 1;
        lastScrollUpMs = nowMs;
        renderAiScreen(true);
      }
    }
    if (isPressedRaw(rightButton) && (nowMs - rightButton.pressedMs) >= LONG_PRESS_MS) {
      if (nowMs - lastScrollDownMs > 180) {
        aiScrollOffset += 1;
        lastScrollDownMs = nowMs;
        renderAiScreen(true);
      }
    }

    if (leftPending && (nowMs - leftPendingMs) > DOUBLE_TAP_MS) {
      if (leftPendingAction == 3) {
        leftPending = false;
        aiScrollOffset -= 1;
        renderAiScreen(true);
      } else {
        leftPending = false;
      }
    }

    renderAiScreen(false);
    return;
  }

  if (screenState == SCREEN_START) {
    ButtonEvent leftEvent = updateButton(leftButton, nowMs);
    if (leftEvent == BUTTON_EVENT_SHORT) {
      if (leftPending && (nowMs - leftPendingMs) <= DOUBLE_TAP_MS) {
        leftPending = false;
        screenState = SCREEN_APP_SELECT;
        renderAppSelectScreen(true);
        return;
      }
      leftPending = true;
      leftPendingMs = nowMs;
      leftPendingAction = 1;
    }

    ButtonEvent rightEvent = updateButton(rightButton, nowMs);
    if (rightEvent == BUTTON_EVENT_SHORT) {
      if (!modes.empty()) {
        selectedModeIndex = (selectedModeIndex + 1) % (int)modes.size();
      }
      saveSelectedMode();
      lastInputMs = nowMs;
      needsRedraw = true;
    }
    if (leftPending && (nowMs - leftPendingMs) > DOUBLE_TAP_MS) {
      if (leftPendingAction == 1) {
        leftPending = false;
        activeModeIndex = selectedModeIndex;
        completedFocusSessions = 0;
        screenState = SCREEN_TIMER;
        startPhase(PHASE_FOCUS, true);
        currentDurationMs = durationForPhase(PHASE_FOCUS);
        lastInputMs = nowMs;
        lastRemainingSeconds = 0xFFFFFFFFUL;
        lastPhase = currentPhase;
        lastRunning = isRunning;
        lastCompletedFocus = -1;
        renderTimerScreen(true);
        return;
      }
      leftPending = false;
    }

    renderStartScreen(needsRedraw, nowMs);
    return;
  }

  ButtonEvent leftEvent = updateButton(leftButton, nowMs);
  if (leftEvent == BUTTON_EVENT_SHORT) {
    if (leftPending && (nowMs - leftPendingMs) <= DOUBLE_TAP_MS) {
      leftPending = false;
      screenState = SCREEN_APP_SELECT;
      startPhase(PHASE_FOCUS, false);
      completedFocusSessions = 0;
      lastRemainingSeconds = 0xFFFFFFFFUL;
      lastPhase = currentPhase;
      lastRunning = isRunning;
      lastCompletedFocus = -1;
      lastInputMs = nowMs;
      renderAppSelectScreen(true);
      return;
    }
    leftPending = true;
    leftPendingMs = nowMs;
    leftPendingAction = 2;
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

  if (leftPending && (nowMs - leftPendingMs) > DOUBLE_TAP_MS) {
    if (leftPendingAction == 2) {
      leftPending = false;
      toggleRunning();
      needsRedraw = true;
    } else {
      leftPending = false;
    }
  }

  if (isRunning && currentElapsedMs() >= currentDurationMs) {
    bool countFocusCompletion = (currentPhase == PHASE_FOCUS);
    advancePhase(countFocusCompletion, true);
    needsRedraw = true;
  }

  renderTimerScreen(needsRedraw);
}
