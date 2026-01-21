#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

// TTGO T-Display: Left button = GPIO35 (no internal pull-up), Right button = GPIO0.
const uint8_t BUTTON_LEFT_PIN = 35;
const uint8_t BUTTON_RIGHT_PIN = 0;

const uint32_t FOCUS_MS = 25UL * 60UL * 1000UL;
const uint32_t SHORT_BREAK_MS = 5UL * 60UL * 1000UL;
const uint32_t LONG_BREAK_MS = 15UL * 60UL * 1000UL;

const uint32_t DEBOUNCE_MS = 30;
const uint32_t LONG_PRESS_MS = 2000;

enum Phase {
  PHASE_FOCUS,
  PHASE_SHORT_BREAK,
  PHASE_LONG_BREAK
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
bool isRunning = false;
uint32_t phaseStartMs = 0;
uint32_t pausedElapsedMs = 0;
uint32_t currentDurationMs = FOCUS_MS;
int completedFocusSessions = 0;

uint16_t colorFocus = 0;
uint16_t colorShort = 0;
uint16_t colorLong = 0;
uint16_t colorMuted = 0;

uint32_t lastRemainingSeconds = 0xFFFFFFFFUL;
Phase lastPhase = PHASE_FOCUS;
bool lastRunning = false;
int lastCompletedFocus = -1;

uint32_t durationForPhase(Phase phase) {
  if (phase == PHASE_FOCUS) {
    return FOCUS_MS;
  }
  if (phase == PHASE_SHORT_BREAK) {
    return SHORT_BREAK_MS;
  }
  return LONG_BREAK_MS;
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

void drawProgressBar(uint16_t color, uint32_t elapsedMs, uint32_t durationMs) {
  const int barX = 10;
  const int barY = tft.height() - 14;
  const int barW = tft.width() - 20;
  const int barH = 8;

  tft.drawRect(barX, barY, barW, barH, color);
  if (durationMs == 0) {
    return;
  }

  uint32_t fillW = (uint32_t)((uint64_t)(barW - 2) * elapsedMs / durationMs);
  if (fillW > (uint32_t)(barW - 2)) {
    fillW = barW - 2;
  }
  tft.fillRect(barX + 1, barY + 1, (int)fillW, barH - 2, color);
}

void render(bool force) {
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

  char timeStr[6];
  uint32_t minutes = remainingSeconds / 60;
  uint32_t seconds = remainingSeconds % 60;
  snprintf(timeStr, sizeof(timeStr), "%lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);

  tft.setTextSize(4);
  int timeWidth = strlen(timeStr) * 6 * 4;
  int timeX = (tft.width() - timeWidth) / 2;
  tft.setCursor(timeX, 34);
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
  drawProgressBar(phaseColor, elapsedMs, currentDurationMs);

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

  colorFocus = tft.color565(40, 220, 120);
  colorShort = tft.color565(60, 170, 255);
  colorLong = tft.color565(255, 150, 0);
  colorMuted = tft.color565(160, 160, 160);

  initButton(leftButton, false);
  initButton(rightButton, true);

  startPhase(PHASE_FOCUS, false);
  render(true);
}

void loop() {
  uint32_t nowMs = millis();
  bool needsRedraw = false;

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

  render(needsRedraw);
}
