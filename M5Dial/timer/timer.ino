#include "M5Dial.h"
#include "Timer.h"
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <Preferences.h>
M5Canvas img(&M5Dial.Display);

/* todo
✓ make the timer more accurate (now uses millis() for precise elapsed time tracking)
✓ when reset change to the configured default timer
✓ screen dimming on inactivity (dims after 30s, wakes on touch/button)
✓ improved UI with centered timer, progress bar, better layout
- ui update: ✓
  when the timer starts, i want to remove the "bly pomodoro" at top, stop, and reset text.  make the screen black with only the timer and the progress bar showing.
  once the timer stops, reset the screen to the default with the "bly pomodoro" text and the buttons showing again.  this way we can maximize the size of the timer text during the countdown, and minimize distractions.
- low-power sleep: ✓
  after 1 hour of idle setup time, enter light sleep
- configuration menu: ✓
when the timer is not running, long-holding Button A brings me into a configuration menu

the config menu has these options that are settable via the touchscreen:
-checkbox enable/disable screen dim after interval; this controls whether the screen dims or not
-checkbox enable/disable light sleep
-a user definable default reset timer value, similar to how we set the timer in the main screen, use the same interface to allow the user to set the default reset timer value

the 6 o'clock button saves the settings and returns back to main menu

the configuration is not visible when the timer is running.
*/

//***Configuarble options START
int num[3] = { 0, 15, 0 };         // hours, min, sec; current timer value
int alarmTimerDuration = 15000;  //ms; length of alarm timer sounding, so we don't annoy the neighbors
//***Configuarble options END

// Global state variables
long oldPosition;// = -999;
int mode = 0;  // 0 is set, 1 is run, 3 is ringing, 4 is config
int lastS = -999;

// Timer accuracy tracking
unsigned long timerStartMillis = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastButtonPress = 0;  // Safety: prevent button debounce issues
const unsigned long BUTTON_DEBOUNCE = 50;  // ms

// Reset button double-click detection
unsigned long lastResetButtonTime = 0;
bool resetButtonPressed = false;
const unsigned long DOUBLE_CLICK_TIMEOUT = 500;  // ms

// Screen dimming for power savings
unsigned long lastActivityTime = 0;
const uint8_t NORMAL_BRIGHTNESS = 128;
const uint8_t DIM_BRIGHTNESS = 5;
bool isScreenDimmed = false;
bool screenDimmingEnabled = true;
bool lightSleepEnabled = true;
unsigned long dimTimeoutMillis = 30000;
unsigned long lightSleepTimeoutMillis = 3600000;

// Display and UI
String numS[3] = { "", "", "" };  ///same as num just String
int mm[3] = { 24, 60, 60 };       // max value for hout, min , sec
int chosen = 2;                   // chosen in array
int configPage = 0;
int defaultTimer[3] = { 0, 15, 0 };
int dimTimeout[3] = { 0, 0, 30 };
int sleepTimeout[3] = { 1, 0, 0 };

bool z = 0;
bool deb = 0;
bool deb2 = 0;
bool resetHoldConsumed = false;

bool alarmStart = 0;
Timer alarmTimer;
long stopAlarmEncoderOldPos;  //tracks position of encoder when alarm sounds; allows us to turn encoder to stop
int lastTimer[3] = { 0, 0, 0 };  //hours, minutes, seconds;  holds the last timer value
Preferences prefs;


void setup() {
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, true);
  M5Dial.Rtc.setDateTime({ { 2023, 10, 25 }, { 15, 56, 56 } });
  prefs.begin("pomodoro", false);
  screenDimmingEnabled = prefs.getBool("dim_en", true);
  lightSleepEnabled = prefs.getBool("sleep_en", true);
  dimTimeout[0] = prefs.getUChar("dim_h", dimTimeout[0]);
  dimTimeout[1] = prefs.getUChar("dim_m", dimTimeout[1]);
  dimTimeout[2] = prefs.getUChar("dim_s", dimTimeout[2]);
  sleepTimeout[0] = prefs.getUChar("sleep_h", sleepTimeout[0]);
  sleepTimeout[1] = prefs.getUChar("sleep_m", sleepTimeout[1]);
  sleepTimeout[2] = prefs.getUChar("sleep_s", sleepTimeout[2]);
  defaultTimer[0] = prefs.getUChar("def_h", 0);
  defaultTimer[1] = prefs.getUChar("def_m", 15);
  defaultTimer[2] = prefs.getUChar("def_s", 0);
  dimTimeoutMillis = (unsigned long)dimTimeout[0] * 3600000UL + (unsigned long)dimTimeout[1] * 60000UL + (unsigned long)dimTimeout[2] * 1000UL;
  lightSleepTimeoutMillis = (unsigned long)sleepTimeout[0] * 3600000UL + (unsigned long)sleepTimeout[1] * 60000UL + (unsigned long)sleepTimeout[2] * 1000UL;
  for (int i = 0; i < 3; i++) {
    num[i] = defaultTimer[i];
  }
  M5Dial.Display.setBrightness(NORMAL_BRIGHTNESS);
  img.createSprite(240, 240);
  img.setTextDatum(5);
  lastActivityTime = millis();
  delay(200);
}

void saveConfig() {
  prefs.putBool("dim_en", screenDimmingEnabled);
  prefs.putBool("sleep_en", lightSleepEnabled);
  prefs.putUChar("dim_h", dimTimeout[0]);
  prefs.putUChar("dim_m", dimTimeout[1]);
  prefs.putUChar("dim_s", dimTimeout[2]);
  prefs.putUChar("sleep_h", sleepTimeout[0]);
  prefs.putUChar("sleep_m", sleepTimeout[1]);
  prefs.putUChar("sleep_s", sleepTimeout[2]);
  prefs.putUChar("def_h", defaultTimer[0]);
  prefs.putUChar("def_m", defaultTimer[1]);
  prefs.putUChar("def_s", defaultTimer[2]);
}

unsigned long timeArrayToMillis(const int timeValue[3]) {
  return (unsigned long)timeValue[0] * 3600000UL + (unsigned long)timeValue[1] * 60000UL + (unsigned long)timeValue[2] * 1000UL;
}

void storeCurrentConfigPage() {
  if (configPage == 0) {
    dimTimeout[0] = num[0];
    dimTimeout[1] = num[1];
    dimTimeout[2] = num[2];
    dimTimeoutMillis = timeArrayToMillis(dimTimeout);
  } else if (configPage == 1) {
    sleepTimeout[0] = num[0];
    sleepTimeout[1] = num[1];
    sleepTimeout[2] = num[2];
    lightSleepTimeoutMillis = timeArrayToMillis(sleepTimeout);
  } else {
    defaultTimer[0] = num[0];
    defaultTimer[1] = num[1];
    defaultTimer[2] = num[2];
  }
  saveConfig();
}

void loadConfigPage(int page) {
  if (page == 0) {
    num[0] = dimTimeout[0];
    num[1] = dimTimeout[1];
    num[2] = dimTimeout[2];
  } else if (page == 1) {
    num[0] = sleepTimeout[0];
    num[1] = sleepTimeout[1];
    num[2] = sleepTimeout[2];
  } else {
    num[0] = defaultTimer[0];
    num[1] = defaultTimer[1];
    num[2] = defaultTimer[2];
  }
}

void syncTimerToDefault() {
  for (int i = 0; i < 3; i++) {
    num[i] = defaultTimer[i];
  }
}

void enterConfigMenu() {
  mode = 4;
  configPage = 0;
  loadConfigPage(configPage);
  chosen = 2;
  oldPosition = M5Dial.Encoder.read();
  lastActivityTime = millis();
  isScreenDimmed = false;
  M5Dial.Display.setBrightness(NORMAL_BRIGHTNESS);
}

bool isRunMode() {
  return mode == 1;
}

bool isAlarmMode() {
  return mode == 3;
}

bool isConfigMode() {
  return mode == 4;
}

bool isSetupMode() {
  return mode == 0 || mode == 4;
}

void setCurrentTimerToDefault() {
  syncTimerToDefault();
  chosen = 2;
}

void exitConfigMenu() {
  storeCurrentConfigPage();
  mode = 0;
  syncTimerToDefault();
  chosen = 2;
  oldPosition = M5Dial.Encoder.read();
  lastActivityTime = millis();
  isScreenDimmed = false;
  M5Dial.Display.setBrightness(NORMAL_BRIGHTNESS);
}

void advanceConfigPage() {
  storeCurrentConfigPage();
  if (configPage >= 2) {
    exitConfigMenu();
    return;
  }
  configPage++;
  loadConfigPage(configPage);
  chosen = 2;
  oldPosition = M5Dial.Encoder.read();
}

void adjustTimerValue(int direction, int target[3], int targetChosen) {
  if (direction > 0) target[targetChosen]++;
  else target[targetChosen]--;
  if (target[targetChosen] == mm[targetChosen]) target[targetChosen] = 0;
  if (target[targetChosen] < 0) target[targetChosen] = mm[targetChosen] - 1;
}

void updateNumStrings() {
  for (int i = 0; i < 3; i++) {
    if (num[i] < 10) numS[i] = "0" + String(num[i]);
    else numS[i] = String(num[i]);
  }
}

void drawProgressBar() {
  // Draw filled circular progress bar around screen edge (only during timer run mode)
  if (mode != 1) return;
  
  unsigned long currentMillis = millis();
  unsigned long elapsedSeconds = (currentMillis - timerStartMillis) / 1000;
  int totalSeconds = lastTimer[0] * 3600 + lastTimer[1] * 60 + lastTimer[2];
  
  if (totalSeconds <= 0) return;
  
  // Calculate progress angle (0-360 degrees)
  float progress = (float)elapsedSeconds / totalSeconds;
  if (progress > 1.0) progress = 1.0;
  int progressAngle = (int)(progress * 360);
  
  // Draw filled progress bar around the edge, starting from 12 o'clock (top)
  uint16_t barColor = 0x07E0;  // Green
  int startAngle = 270;  // Start from 12 o'clock (top)
  int endAngle = startAngle + progressAngle;
  
  // Draw thick bar by drawing concentric circles along the arc
  for (int angle = startAngle; angle < endAngle; angle += 2) {
    float rad = angle * 3.14159 / 180.0;
    // Draw a segment of the bar at different radii to fill it in
    for (int radius = 100; radius <= 115; radius += 2) {
      int x = 120 + (int)(radius * cos(rad));
      int y = 120 + (int)(radius * sin(rad));
      img.drawPixel(x, y, barColor);
    }
  }
}

void drawSetupScreen() {
  img.fillSprite(BLACK);
  img.fillRect(0, 160, 240, 60, 0x0A2D);
  img.fillRect(0, 0, 240, 80, 0x0A2D);

  img.setTextColor(ORANGE, 0x0A2D);
  img.drawString("BLY POMODORO", 120, 60, 4);

  img.setTextColor(WHITE, BLACK);
  img.drawString(numS[0] + ":" + numS[1] + ":" + numS[2], 120, 120, 7);

  if (mode == 0) {
    img.fillRect(14 + (chosen * 76), 150, 59, 4, GREEN);
    img.setTextColor(WHITE, 0x0A2D);
    setActionButtonText("START");
  }

  img.setTextColor(ORANGE, BLACK);
  img.drawString("RESET", 120, 232, 2);
}

void drawRunningScreen() {
  img.fillSprite(BLACK);
  img.setTextColor(WHITE, BLACK);
  img.drawString(numS[0] + ":" + numS[1] + ":" + numS[2], 120, 120, 7);
  drawProgressBar();
}

void drawCheckboxRow(int x, int y, bool checked, const String& label) {
  img.drawRect(x, y, 18, 18, WHITE);
  if (checked) {
    img.fillRect(x + 4, y + 4, 10, 10, GREEN);
  }
  img.setTextColor(WHITE, BLACK);
  img.drawString(label, x + 34, y + 9, 1);
}

void drawConfigScreen() {
  img.fillSprite(BLACK);
  img.fillRect(0, 200, 240, 40, 0x0A2D);

  img.setTextColor(ORANGE, BLACK);
  img.drawString("CFG", 120, 14, 2);

  if (configPage == 0) {
    drawCheckboxRow(26, 43, screenDimmingEnabled, "DIM");
    img.setTextColor(ORANGE, BLACK);
    img.drawString("DIM TIMEOUT", 120, 83, 1);
  } else if (configPage == 1) {
    drawCheckboxRow(132, 43, lightSleepEnabled, "SLEEP");
    img.setTextColor(ORANGE, BLACK);
    img.drawString("SLEEP TIMEOUT", 120, 83, 1);
  } else {
    img.setTextColor(ORANGE, BLACK);
    img.drawString("DEFAULT TIMER", 120, 83, 1);
  }

  img.setTextColor(WHITE, BLACK);
  img.drawString(numS[0] + ":" + numS[1] + ":" + numS[2], 120, 121, 7);

  img.fillRect(14 + (chosen * 76), 147, 59, 4, GREEN);
  img.setTextColor(WHITE, 0x0A2D);
  img.drawString(configPage < 2 ? "NEXT" : "DONE", 120, 220, 2);
}

void draw() {
  if (isAlarmMode()) {

    if (alarmStart == 0)  //start tracking alarm elapsed timer; allows us to stop the alarm after a pre-defined time
    {
      stopAlarmEncoderOldPos = M5Dial.Encoder.read();
      alarmTimer.start();
      alarmStart = 1;  // Mark alarm as started immediately
    }

    // Auto-stop alarm after duration
    if (alarmTimer.read() > alarmTimerDuration) {
      alarmTimer.stop();
      reset();
      delay(200);
      return;
    }

    // Play beep without blocking display loop (non-blocking tone)
    M5Dial.Speaker.tone(4000, 50);
    delay(100);
    z = !z;
    if (z)
      img.fillSprite(WHITE);
    else
      img.fillSprite(BLACK);
    img.drawString(numS[0] + ":" + numS[1] + ":" + numS[2], 120, 120, 7);

    alarmStart = 1;
  } else {
    if (isRunMode()) {
      drawRunningScreen();
    } else if (isConfigMode()) {
      drawConfigScreen();
    } else {
      drawSetupScreen();
    }
  }
  img.pushSprite(0, 0);
}

void updateScreenBrightness() {
  unsigned long now = millis();
  unsigned long inactiveTime = now - lastActivityTime;
  
  // Running or alarming should always stay bright.
  if (isRunMode() || isAlarmMode()) {
    if (isScreenDimmed) {
      M5Dial.Display.setBrightness(NORMAL_BRIGHTNESS);
      isScreenDimmed = false;
    }
    return;
  }

  // After a long idle period, enter light sleep so touch/button/encoder can wake it.
  if (lightSleepEnabled && inactiveTime >= lightSleepTimeoutMillis) {
    gpio_wakeup_enable(GPIO_NUM_42, GPIO_INTR_LOW_LEVEL);  // Button A
    gpio_wakeup_enable((gpio_num_t)DIAL_ENCODER_PIN_A, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)DIAL_ENCODER_PIN_B, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    M5Dial.Display.setBrightness(0);
    M5Dial.Power.lightSleep(0, true);
    return;
  }

  // Wake screen if it's dimmed and user is active
  if (isScreenDimmed && inactiveTime < dimTimeoutMillis) {
    M5Dial.Display.setBrightness(NORMAL_BRIGHTNESS);
    isScreenDimmed = false;
  }
  // Dim screen if inactive for too long while in setup/config mode.
  else if (screenDimmingEnabled && !isScreenDimmed && inactiveTime > dimTimeoutMillis) {
    M5Dial.Display.setBrightness(DIM_BRIGHTNESS);
    isScreenDimmed = true;
  }
}

void updateTime() {
  // Use elapsed milliseconds for accurate timing
  unsigned long currentMillis = millis();
  unsigned long elapsedSeconds = (currentMillis - timerStartMillis) / 1000;
  
  // Update display every second (1000ms) for efficiency
  if (currentMillis - lastDisplayUpdate >= 1000) {
    int totalSeconds = lastTimer[0] * 3600 + lastTimer[1] * 60 + lastTimer[2];
    int remainingSeconds = totalSeconds - elapsedSeconds;
    
    if (remainingSeconds <= 0) {
      num[0] = 0;
      num[1] = 0;
      num[2] = 0;
      if (mode == 1) mode = 3;  // Transition to alarm after expiration
    } else {
      num[0] = remainingSeconds / 3600;
      num[1] = (remainingSeconds % 3600) / 60;
      num[2] = remainingSeconds % 60;
    }
    
    lastDisplayUpdate = currentMillis;
  }
  
  //encoder spun, stop timer countdown
  long stopAlarmEncoderNewPos = M5Dial.Encoder.read();
  if (stopAlarmEncoderOldPos != stopAlarmEncoderNewPos)
    stopAlarmAndReset();
}

void resetToLastTimer() {
  // Single press: restore the last timer value that was run
  mode = 0;
  num[0] = lastTimer[0];
  num[1] = lastTimer[1];
  num[2] = lastTimer[2];
  alarmStart = 0;
}

void resetToDefaultTimer() {
  // Double click: reset to the configured default timer
  mode = 0;
  setCurrentTimerToDefault();
  alarmStart = 0;
}

void reset() {
  // Reset returns to the last timer value.
  resetToLastTimer();
}

void stopAlarmAndReset() {
  M5Dial.Speaker.tone(2800, 100);
  reset();
  delay(200);
}

void setActionButtonText(String buttonText) {
  img.drawString(buttonText, 120, 190, 4);
}

void loop() {

  M5Dial.update();
  unsigned long now = millis();
  
  // Track user activity for screen dimming
  bool userActive = false;

  // Enter config menu from a long hold while on the setup screen.
  if (!isConfigMode() && M5Dial.BtnA.wasHold() && mode == 0) {
    resetHoldConsumed = true;
    resetButtonPressed = false;
    lastResetButtonTime = 0;
    lastButtonPress = now;
    userActive = true;
    M5Dial.Speaker.tone(3200, 80);
    enterConfigMenu();
  }

  if (isConfigMode()) {
    if (M5Dial.BtnA.wasClicked()) {
      userActive = true;
      M5Dial.Speaker.tone(2800, 80);
      advanceConfigPage();
      delay(120);
    }

    auto t = M5Dial.Touch.getDetail();
    if (t.isPressed()) {
      userActive = true;
      if (deb == 0) {
        deb = 1;
        M5Dial.Speaker.tone(3000, 80);
        if (configPage == 0 && t.y > 34 && t.y < 66 && t.x > 16 && t.x < 112) {
          screenDimmingEnabled = !screenDimmingEnabled;
          storeCurrentConfigPage();
          if (!screenDimmingEnabled) {
            M5Dial.Display.setBrightness(NORMAL_BRIGHTNESS);
            isScreenDimmed = false;
          }
        } else if (configPage == 1 && t.y > 34 && t.y < 66 && t.x > 132 && t.x < 224) {
          lightSleepEnabled = !lightSleepEnabled;
          storeCurrentConfigPage();
        } else if (t.y > 86 && t.y < 150) {
          if (t.x > 10 && t.x < 90) chosen = 0;
          if (t.x > 90 && t.x < 166) chosen = 1;
          if (t.x > 166 && t.x < 224) chosen = 2;
        }
      }
    } else {
      deb = 0;
    }

    long newPosition = M5Dial.Encoder.read();
    if (newPosition != oldPosition) {
      userActive = true;
      M5Dial.Speaker.tone(3600, 30);
      adjustTimerValue(newPosition > oldPosition ? 1 : -1, num, chosen);
      oldPosition = newPosition;
      storeCurrentConfigPage();
    }
  } else {
    // Emergency reset with double-click detection - always available, even if stuck
    if (M5Dial.BtnA.isPressed() && !resetHoldConsumed) {
      if (!resetButtonPressed && (now - lastButtonPress > BUTTON_DEBOUNCE)) {
        resetButtonPressed = true;
        
        // Check if this is a double click
        if (now - lastResetButtonTime <= DOUBLE_CLICK_TIMEOUT) {
          // Double click detected - reset to configured default timer
          M5Dial.Speaker.tone(2800, 100);
          resetToDefaultTimer();
          delay(200);
          lastResetButtonTime = 0;  // Reset for next sequence
        } else {
          // First click of new sequence - restore to last timer value
          M5Dial.Speaker.tone(2800, 100);
          resetToLastTimer();
          delay(200);
          lastResetButtonTime = now;
        }

        lastButtonPress = now;
        userActive = true;
      }
    } else {
      resetButtonPressed = false;  // Button released
      if (!M5Dial.BtnA.isPressed()) {
        resetHoldConsumed = false;
      }
    }
  }

  if (mode == 0) {
    auto t = M5Dial.Touch.getDetail();
    if (t.isPressed()) {
      userActive = true;  // Track touchscreen activity
      if (deb == 0) {
        deb = 1;
        M5Dial.Speaker.tone(3000, 100);
        if (t.y > 160) {
          mode = 1;  //start pressed
          stopAlarmEncoderOldPos = M5Dial.Encoder.read();
          lastTimer[0] = num[0];  //saves the chosen time
          lastTimer[1] = num[1];
          lastTimer[2] = num[2];
          timerStartMillis = millis();  //start tracking elapsed time
          lastDisplayUpdate = millis();
          sleep(0.200);
        };
        if (t.y > 86 && t.y < 150) {
          if (t.x > 10 && t.x < 90) chosen = 0;
          if (t.x > 90 && t.x < 166) chosen = 1;
          if (t.x > 166 && t.x < 224) chosen = 2;
        }
      }
    } else deb = 0;

    long newPosition = M5Dial.Encoder.read();
    if (newPosition != oldPosition) {
      userActive = true;  // Track encoder activity
      M5Dial.Speaker.tone(3600, 30);
      adjustTimerValue(newPosition > oldPosition ? 1 : -1, num, chosen);
      oldPosition = newPosition;
    }
  }

  //alarm active, disable alarm if dial or screen is touched
  if (isAlarmMode() && alarmStart == 1) {
    //screen touched
    auto t = M5Dial.Touch.getDetail();
    if (t.isPressed())
      stopAlarmAndReset();
    //encoder spun
    long stopAlarmEncoderNewPos = M5Dial.Encoder.read();
    if (stopAlarmEncoderOldPos != stopAlarmEncoderNewPos)
      stopAlarmAndReset();
  }

  if (isRunMode()) {
    // Timer counting: any touch on the screen stops the timer
    auto t = M5Dial.Touch.getDetail(); 
    if (t.isPressed()) {
      if (deb == 0) {  // Debounce
        M5Dial.Speaker.tone(3000, 100);
        stopAlarmAndReset();
        deb = 1;
      }
    } else {
      deb = 0;  // Allow next press
    }

    updateTime();
  }

  updateNumStrings();
  
  // Update activity timer and screen brightness
  if (userActive) {
    lastActivityTime = now;
    isScreenDimmed = false;
    if (isSetupMode()) {
      M5Dial.Display.setBrightness(NORMAL_BRIGHTNESS);
    }
  }
  updateScreenBrightness();

  draw();
}
