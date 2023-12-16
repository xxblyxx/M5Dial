#include "M5Dial.h"
#include "Timer.h"
M5Canvas img(&M5Dial.Display);

/* todo
x timer set to only go off for 30 seconds
- any action stops timer when going off
- any action stops timer from counting down
n/a - think about maybe adding a motion detection to stop alarm from sounding; no hardware available
*/

void setup() {
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, true);
  M5Dial.Rtc.setDateTime({ { 2023, 10, 25 }, { 15, 56, 56 } });
  M5Dial.Display.setBrightness(24);
  img.createSprite(240, 240);
  img.setTextDatum(5);
  delay(200);
}

long oldPosition = -999;
int mode = 0;  // 0 is set, 1 ir run , 3 is ringing
int lastS = -999;

int num[3] = { 0, 1, 0 };         // hours, min, secx
String numS[3] = { "", "", "" };  ///same as num just String
int mm[3] = { 24, 60, 60 };       // max value for hout, min , sec
int chosen = 2;                   // chosen in array

bool z = 0;
bool deb = 0;
bool deb2 = 0;

int lastTimer[3] = { 0, 1, 0 };  //holds the last timer value; default is one minute
bool alarmStart = 0;
int alarmTimerDuration = 15000;  //length of alarm timer, so we don't annoy the neighbors
Timer alarmTimer;
long stopAlarmEncoderOldPos;  //tracks position of encoder when alarm sounds; allows us to turn encoder to stop
bool actionButtonPressed=0;

void draw() {
  if (mode == 3) {

    if (alarmStart == 0)  //start tracking alarm elapsed timer; allows us to stop the alarm after a pre-defined time
    {
      stopAlarmEncoderOldPos = M5Dial.Encoder.read();
      alarmTimer.start();
    }

    if (alarmStart == 1) {
      if (alarmTimer.read() > alarmTimerDuration) {
        alarmTimer.stop();
        reset();
        delay(200);
        return;
      }
    }

    M5Dial.Speaker.tone(4000, 100);
    delay(100);
    z = !z;
    if (z)
      img.fillSprite(WHITE);
    else
      img.fillSprite(BLACK);
    img.drawString(numS[0] + ":" + numS[1] + ":" + numS[2], 120, 120, 7);

    alarmStart = 1;
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
  if (num[0] <= 0 && num[1] <= 0 && num[2] <= 0 && mode == 1)
    mode = 3;

  auto dt = M5Dial.Rtc.getDateTime();
  if (lastS != dt.time.seconds) {
    num[2]--;
    lastS = dt.time.seconds;

    if (num[2] < 0) {
      num[1]--;
      num[2] = mm[2] - 1;
    }
    if (num[1] < 0) {
      num[0]--;
      num[1] = mm[1] - 1;
    }
  }
  //encoder spun, stop timer countdown
  long stopAlarmEncoderNewPos = M5Dial.Encoder.read();
  if (stopAlarmEncoderOldPos != stopAlarmEncoderNewPos)
    stopAlarmAndReset();
}

void reset() {
  mode = 0;
  // num[2]=15;
  // num[1]=0;
  // num[0]=0;
  num[2] = lastTimer[2];
  num[1] = lastTimer[1];
  num[0] = lastTimer[0];
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
  auto x = M5Dial.Touch.getDetail();
  if (x.isReleased())
  {
    actionButtonPressed = 0;
  }

  if (M5Dial.BtnA.isPressed()) {
    stopAlarmAndReset();
  }

  //alarm active, disable alarm if dial or screen is touched
  if (mode == 3 and alarmStart == 1) {
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
          actionButtonPressed = 1;
          stopAlarmEncoderOldPos = M5Dial.Encoder.read();
          lastTimer[chosen] = num[chosen];  //saves the chosen time to lastimer
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
    //TODO not working...the touch is still recognized
    // auto t = M5Dial.Touch.getDetail(); //stop alarm when screen is pressed
    // if (t.isPressed())
    //   stopAlarmAndReset();
    // else

    auto t = M5Dial.Touch.getDetail();
    if (t.isPressed() && actionButtonPressed == 0 && t.y > 160) {
      M5Dial.Speaker.tone(3000, 100);
      stopAlarmAndReset();
    }

    updateTime();
  }

  for (int i = 0; i < 3; i++)
    if (num[i] < 10) numS[i] = "0" + String(num[i]);
    else numS[i] = String(num[i]);

  draw();
}
