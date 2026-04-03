#include "M5Dial.h"
#include "Timer.h"
#include <driver/gpio.h>
#include <esp_sleep.h>
M5Canvas img(&M5Dial.Display);

/* todo
✓ make the timer more accurate (now uses millis() for precise elapsed time tracking)
✓ when reset change to 15 minutes and 0 seconds as default
✓ screen dimming on inactivity (dims after 30s, wakes on touch/button)
✓ improved UI with centered timer, progress bar, better layout
- ui update: ✓
  when the timer starts, i want to remove the "bly pomodoro" at top, stop, and reset text.  make the screen black with only the timer and the progress bar showing.
  once the timer stops, reset the screen to the default with the "bly pomodoro" text and the buttons showing again.  this way we can maximize the size of the timer text during the countdown, and minimize distractions.
- low-power sleep:
  after 1 hour of idle setup time, enter light sleep
*/

//***Configuarble options START
int num[3] = { 0, 15, 0 };         // hours, min, sec; default starting timer
int alarmTimerDuration = 15000;  //ms; length of alarm timer sounding, so we don't annoy the neighbors
//***Configuarble options END

// Global state variables
long oldPosition;// = -999;
int mode = 0;  // 0 is set, 1 is run, 3 is ringing
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
const unsigned long INACTIVITY_TIMEOUT = 30000;  // 30 seconds
const unsigned long LIGHT_SLEEP_TIMEOUT = 60000; //test value do not change //3600000;  // 1 hour
const uint8_t NORMAL_BRIGHTNESS = 128;
const uint8_t DIM_BRIGHTNESS = 5;
bool isScreenDimmed = false;

// Display and UI
String numS[3] = { "", "", "" };  ///same as num just String
int mm[3] = { 24, 60, 60 };       // max value for hout, min , sec
int chosen = 2;                   // chosen in array

bool z = 0;
bool deb = 0;
bool deb2 = 0;

bool alarmStart = 0;
Timer alarmTimer;
long stopAlarmEncoderOldPos;  //tracks position of encoder when alarm sounds; allows us to turn encoder to stop
int lastTimer[3] = { 0, 0, 0 };  //hours, minutes, seconds;  holds the last timer value


void setup() {
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, true);
  M5Dial.Rtc.setDateTime({ { 2023, 10, 25 }, { 15, 56, 56 } });
  M5Dial.Display.setBrightness(NORMAL_BRIGHTNESS);
  img.createSprite(240, 240);
  img.setTextDatum(5);
  lastActivityTime = millis();
  delay(200);
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

void draw() {
  if (mode == 3) {

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
    if (mode == 1) {
      drawRunningScreen();
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
  if (mode != 0) {
    if (isScreenDimmed) {
      M5Dial.Display.setBrightness(NORMAL_BRIGHTNESS);
      isScreenDimmed = false;
    }
    return;
  }

  // After a long idle period, enter light sleep so touch/button/encoder can wake it.
  if (inactiveTime >= LIGHT_SLEEP_TIMEOUT) {
    gpio_wakeup_enable(GPIO_NUM_42, GPIO_INTR_LOW_LEVEL);  // Button A
    gpio_wakeup_enable((gpio_num_t)DIAL_ENCODER_PIN_A, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)DIAL_ENCODER_PIN_B, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    M5Dial.Display.setBrightness(0);
    M5Dial.Power.lightSleep(0, true);
    return;
  }

  // Wake screen if it's dimmed and user is active
  if (isScreenDimmed && inactiveTime < INACTIVITY_TIMEOUT) {
    M5Dial.Display.setBrightness(NORMAL_BRIGHTNESS);
    isScreenDimmed = false;
  }
  // Dim screen if inactive for too long while in setup mode
  else if (!isScreenDimmed && inactiveTime > INACTIVITY_TIMEOUT) {
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
  // Single click: restore to the timer value that was run
  mode = 0;
  num[0] = lastTimer[0];
  num[1] = lastTimer[1];
  num[2] = lastTimer[2];
  alarmStart = 0;
}

void resetTo15Minutes() {
  // Double click: reset to 15 minutes and 0 seconds
  mode = 0;
  num[0] = 0;
  num[1] = 15;
  num[2] = 0;
  alarmStart = 0;
}

void reset() {
  // Legacy function - now routes through resetToLastTimer()
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

  // Emergency reset with double-click detection - always available, even if stuck
  if (M5Dial.BtnA.isPressed()) {
    if (!resetButtonPressed && (now - lastButtonPress > BUTTON_DEBOUNCE)) {
      resetButtonPressed = true;
      
      // Check if this is a double click
      if (now - lastResetButtonTime <= DOUBLE_CLICK_TIMEOUT) {
        // Double click detected - reset to 15 minutes
        M5Dial.Speaker.tone(2800, 100);
        resetTo15Minutes();
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
  }

  //alarm active, disable alarm if dial or screen is touched
  if (mode == 3 && alarmStart == 1) {
    //screen touched
    auto t = M5Dial.Touch.getDetail();
    if (t.isPressed())
      stopAlarmAndReset();
    //encoder spun
    long stopAlarmEncoderNewPos = M5Dial.Encoder.read();
    if (stopAlarmEncoderOldPos != stopAlarmEncoderNewPos)
      stopAlarmAndReset();
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
      if (newPosition > oldPosition) num[chosen]++;
      else num[chosen]--;
      if (num[chosen] == mm[chosen]) num[chosen] = 0;
      if (num[chosen] < 0) num[chosen] = mm[chosen] - 1;
      oldPosition = newPosition;
    }
  }

  if (mode == 1) {
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

  for (int i = 0; i < 3; i++)
    if (num[i] < 10) numS[i] = "0" + String(num[i]);
    else numS[i] = String(num[i]);
  
  // Update activity timer and screen brightness
  if (userActive) {
    lastActivityTime = now;
    isScreenDimmed = false;
    if (mode == 0) {
      M5Dial.Display.setBrightness(NORMAL_BRIGHTNESS);
    }
  }
  updateScreenBrightness();

  draw();
}
