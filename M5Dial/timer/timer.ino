#include "M5Dial.h"
#include "Timer.h"
M5Canvas img(&M5Dial.Display);

/* todo
✓ make the timer more accurate (now uses millis() for precise elapsed time tracking)
✓ when reset change to 15 minutes and 0 seconds as default
✓ screen dimming on inactivity (dims after 30s, wakes on touch/button)
✓ improved UI with centered timer, progress bar, better layout
*/

//***Configuarble options START
int num[3] = { 0, 15, 0 };         // hours, min, sec; default starting timer
int alarmTimerDuration = 15000;  //ms; length of alarm timer sounding, so we don't annoy the neighbors
//***Configuarble options END


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

long oldPosition;// = -999;
int mode = 0;  // 0 is set, 1 is run, 3 is ringing
int lastS = -999;

// Timer accuracy tracking
unsigned long timerStartMillis = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastButtonPress = 0;  // Safety: prevent button debounce issues
const unsigned long BUTTON_DEBOUNCE = 50;  // ms

// Screen dimming for power savings
unsigned long lastActivityTime = 0;
const unsigned long INACTIVITY_TIMEOUT = 30000;  // 30 seconds
const uint8_t NORMAL_BRIGHTNESS = 128;
const uint8_t DIM_BRIGHTNESS = 20;
bool isScreenDimmed = false;


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


void drawProgressBar() {
  // Draw circular progress bar around screen edge (only during timer run mode)
  if (mode != 1) return;
  
  unsigned long currentMillis = millis();
  unsigned long elapsedSeconds = (currentMillis - timerStartMillis) / 1000;
  int totalSeconds = lastTimer[0] * 3600 + lastTimer[1] * 60 + lastTimer[2];
  
  if (totalSeconds <= 0) return;
  
  // Calculate progress angle (0-360 degrees)
  float progress = (float)elapsedSeconds / totalSeconds;
  if (progress > 1.0) progress = 1.0;
  int progressAngle = (int)(progress * 360);
  
  // Draw thin arc around the edge
  uint16_t arcColor = 0x07E0;  // Green
  for (int angle = 0; angle < progressAngle; angle += 5) {
    float rad = angle * 3.14159 / 180.0;
    int x = 120 + (int)(110 * cos(rad - 1.5708));
    int y = 120 + (int)(110 * sin(rad - 1.5708));
    img.fillCircle(x, y, 2, arcColor);
  }
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
    z = !z;
    if (z)
      img.fillSprite(WHITE);
    else
      img.fillSprite(BLACK);
    img.setTextColor(RED, (z ? WHITE : BLACK));
    img.drawString(numS[0] + ":" + numS[1] + ":" + numS[2], 120, 120, 8);
  } else {
    img.fillSprite(BLACK);
    
    // Top section - Title (compact layout)
    img.fillRect(0, 0, 240, 40, 0x0A2D);
    img.setTextColor(ORANGE, 0x0A2D);
    img.setTextDatum(5);  // Middle center
    img.drawString("BLY", 120, 15, 3);
    img.drawString("POMODORO", 120, 30, 3);
    
    // Middle section - Timer display (large, centered)
    img.setTextColor(WHITE, BLACK);
    img.drawString(numS[0] + ":" + numS[1] + ":" + numS[2], 120, 120, 8);
    
    // Draw progress bar during Timer run
    drawProgressBar();
    
    // Bottom section - Controls
    img.fillRect(0, 160, 240, 80, 0x0A2D);
    img.setTextColor(WHITE, 0x0A2D);
    
    // Unit indicator (smaller, above button)
    if (mode == 0) {
      String unitNames[3] = {"HRS", "MIN", "SEC"};
      img.drawString(unitNames[chosen], 120, 165, 2);
      img.fillRect(50 + (chosen * 60), 175, 30, 3, GREEN);
    }
    
    // Main action button (larger)
    if (mode == 0)
      setActionButtonText("START");
    else if (mode == 1 || mode == 2)
      setActionButtonText("STOP");
    img.drawString("RESET", 120, 232, 2);
  }
  img.pushSprite(0, 0);
}

void updateScreenBrightness() {
  unsigned long now = millis();
  
  // Wake screen if it's dimmed and user is active
  if (isScreenDimmed && (now - lastActivityTime < INACTIVITY_TIMEOUT)) {
    M5Dial.Display.setBrightness(NORMAL_BRIGHTNESS);
    isScreenDimmed = false;
  }
  // Dim screen if inactive for too long (only when not running or alarming)
  else if (!isScreenDimmed && (now - lastActivityTime > INACTIVITY_TIMEOUT) && mode == 0) {
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

void reset() {
  //always resets to 15 minutes, 0 seconds; allows for quick reset to common pomodoro time
  mode = 0;
  num[0] = 0;
  num[1] = 15;
  num[2] = 0;
  alarmStart = 0;
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

  // Emergency reset - always available, even if stuck
  if (M5Dial.BtnA.isPressed()) {
    if (now - lastButtonPress > BUTTON_DEBOUNCE) {
      stopAlarmAndReset();
      lastButtonPress = now;
      userActive = true;
    }
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
    //timer counting, STOP button detection - any touch at bottom stops timer
    auto t = M5Dial.Touch.getDetail(); 
    if (t.isPressed() && t.y > 160) {
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
  }
  updateScreenBrightness();

  draw();
}
