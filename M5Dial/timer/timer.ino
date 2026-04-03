#include "M5Dial.h"
#include "Timer.h"
M5Canvas img(&M5Dial.Display);

/* todo
✓ make the timer more accurate (now uses millis() for precise elapsed time tracking)
✓ when reset change to 15 minutes and 0 seconds as default
*/

//***Configuarble options START
int num[3] = { 0, 15, 0 };         // hours, min, sec; default starting timer
int alarmTimerDuration = 15000;  //ms; length of alarm timer sounding, so we don't annoy the neighbors
//***Configuarble options END


void setup() {
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, true);
  M5Dial.Rtc.setDateTime({ { 2023, 10, 25 }, { 15, 56, 56 } });
  M5Dial.Display.setBrightness(24);
  img.createSprite(240, 240);
  img.setTextDatum(5);
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
    img.drawString(numS[0] + ":" + numS[1] + ":" + numS[2], 120, 120, 7);
  } else {
    img.fillSprite(BLACK);
    img.fillRect(0, 160, 240, 60, 0x0A2D);
    img.fillRect(0, 0, 240, 80, 0x0A2D);
    img.setTextColor(WHITE, 0x0A2D);
    //img.drawString("START", 120, 190, 4);
    if (mode == 0)
      setActionButtonText("START");
    else if (mode == 1 || mode == 2)
    {
      setActionButtonText("STOP");
    }
    img.setTextColor(ORANGE, BLACK);
    img.drawString("RESET", 120, 232, 2);
    img.setTextColor(ORANGE, 0x0A2D);
    img.drawString("BLY POMODORO", 120, 60, 4);
    img.setTextColor(WHITE, BLACK);
    if (mode == 0)
      img.fillRect(14 + (chosen * 76), 150, 59, 4, GREEN);
    img.drawString(numS[0] + ":" + numS[1] + ":" + numS[2], 120, 120, 7);
  }
  img.pushSprite(0, 0);
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

  // Emergency reset - always available, even if stuck
  if (M5Dial.BtnA.isPressed()) {
    if (now - lastButtonPress > BUTTON_DEBOUNCE) {
      stopAlarmAndReset();
      lastButtonPress = now;
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

  draw();
}
