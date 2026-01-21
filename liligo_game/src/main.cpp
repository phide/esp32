#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

// TTGO T-Display: Left button = GPIO35 (no internal pull-up), Right button = GPIO0.
const uint8_t BUTTON_LEFT_PIN = 35;
const uint8_t BUTTON_RIGHT_PIN = 0;

const uint32_t DEBOUNCE_MS = 30;
const uint32_t LONG_PRESS_MS = 2000;

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

void renderStartScreen(bool force) {
  if (!force && selectedModeIndex == lastStartModeIndex) {
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

  const char* modeLabel = MODES[selectedModeIndex].label;
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
}

void renderTimerScreen(bool force) {
  uint32_t elapsedMs = currentElapsedMs();
  uint32_t remainingMs = (elapsedMs >= currentDurationMs) ? 0 : (currentDurationMs - elapsedMs);
  uint32_t remainingSeconds = remainingMs / 1000;

  if (!force &&
      remainingSeconds == lastRemainingSeconds &&
      currentPhase == lastPhase &&
      isRunning == lastRunning &&
      completedFocusSessions == lastCompletedFocus) {
    return;
  }

  tft.fillScreen(TFT_BLACK);

  uint16_t phaseColor = colorForPhase(currentPhase);
  const char* phaseLabel = labelForPhase(currentPhase);

  tft.setTextColor(phaseColor, TFT_BLACK);
  tft.setTextSize(2);
  int labelWidth = strlen(phaseLabel) * 6 * 2;
  int labelX = (tft.width() - labelWidth) / 2;
  tft.setCursor(labelX, 6);
  tft.print(phaseLabel);

  drawRoundIndicator(phaseColor);

  char timeStr[6];
  uint32_t minutes = remainingSeconds / 60;
  uint32_t seconds = remainingSeconds % 60;
  snprintf(timeStr, sizeof(timeStr), "%lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);

  const int timeTextSize = 4;
  tft.setTextSize(timeTextSize);
  int timeWidth = strlen(timeStr) * 6 * timeTextSize;
  int timeHeight = 8 * timeTextSize;
  int timeX = (tft.width() - timeWidth) / 2;
  int timeY = isRunning ? (tft.height() - timeHeight) / 2 : 34;
  tft.setCursor(timeX, timeY);
  tft.print(timeStr);

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
  drawProgressBar(phaseColor, phaseColor, elapsedMs, currentDurationMs);

  lastRemainingSeconds = remainingSeconds;
  lastPhase = currentPhase;
  lastRunning = isRunning;
  lastCompletedFocus = completedFocusSessions;
}

void setup() {
  Serial.begin(115200);

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
  renderStartScreen(true);
}

void loop() {
  uint32_t nowMs = millis();
  bool needsRedraw = false;

  if (screenState == SCREEN_START) {
    ButtonEvent leftEvent = updateButton(leftButton, nowMs);
    if (leftEvent == BUTTON_EVENT_SHORT) {
      activeModeIndex = selectedModeIndex;
      completedFocusSessions = 0;
      screenState = SCREEN_TIMER;
      startPhase(PHASE_FOCUS, true);
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
      needsRedraw = true;
    }

    renderStartScreen(needsRedraw);
    return;
  }

  ButtonEvent leftEvent = updateButton(leftButton, nowMs);
  if (leftEvent == BUTTON_EVENT_SHORT) {
    toggleRunning();
    needsRedraw = true;
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
