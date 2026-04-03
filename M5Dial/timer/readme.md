# M5Dial Pomodoro Timer

A Pomodoro timer application for the **M5Dial** smart rotary device. Features precise timing, intuitive touch interface, and reliable alarm functionality.

## Features

- **Precise Timing**: Uses `millis()` for accurate elapsed time tracking (eliminates RTC-based inaccuracy)
- **Configurable Default Timer**: Perfect for Pomodoro sessions, starts at 15:00 and can be changed in the CFG menu
- **Intuitive Interface**: 
  - Rotate encoder to adjust hours, minutes, seconds
  - Touch bottom area to start timer
  - Touch top/middle area to switch between time units
- **Alarm Notification**: Visual (flashing display) and audio (4kHz beep) alerts when timer expires
- **Multiple Stop Methods**: Stop alarm by touching screen, rotating encoder, or pressing Button A
- **Auto Reset**: Alarm auto-stops after 15 seconds and returns to setup screen
- **Responsive Controls**: Non-blocking audio and improved debouncing prevents input lockups
- **Power Saving**: Dims after 30 seconds of setup inactivity, enters light sleep after 1 hour; touch, Button A, or the encoder wake it back up
- **Configuration Menu**: Long-hold Button A from the setup screen to edit power saving and the default reset timer

## Hardware Requirements

- **M5Dial** smart rotary device
- USB-C for power and programming

## M5 Dial screen specs
- Screen Shape: Circular/Round
- Resolution: 240 x 240 pixels
- Size: 1.28 inch

## Libraries Required

- `M5Dial` - Core M5Dial library
- `Timer` - Custom timer library (included in `Timer/` folder)

## Usage

### Modes

| Mode | Description |
|------|-------------|
| **0 (Setup)** | Adjust timer value using encoder before starting |
| **1 (Running)** | Timer counting down |
| **3 (Alarm)** | Timer expired, alarm sounding |

### Controls

- **Encoder Rotation**: Adjust selected time unit (hours/minutes/seconds)
- **Touch Top/Middle**: Switch between time units (green indicator shows current)
- **Touch Bottom (Setup)**: Start timer
- **Button A Hold (Setup)**: Open configuration menu
- **Touch Anywhere (Running)**: Stop and return to setup
- **Touch/Rotate (Alarm)**: Stop alarm and return to setup
- **Button A**: Emergency reset (always available)
- **Button A Single Press**: Restores the last timer value that was run
- **Button A Double Press**: Resets to the configured default timer

## Configuration

Edit these values in `timer.ino`:

```cpp
int num[3] = { 0, 15, 0 };         // Default timer: 0h 15m 0s
int alarmTimerDuration = 15000;    // Alarm sounds for 15 seconds
const unsigned long BUTTON_DEBOUNCE = 50;  // Button debounce window (ms)
```

## Display Layout

### Setup Screen

```
┌─────────────────────────┐
│   BLY POMODORO  (Title) │
├─────────────────────────┤
│      00:15:00           │
│  (Timer Display)        │
├─────────────────────────┤
│  (Green indicator shows │
│   current selected unit)│
├─────────────────────────┤
│         START           │
│          RESET          │
└─────────────────────────┘
```

### Running Screen

```
┌─────────────────────────┐
│                         │
│      00:14:59           │
│    (Black background)   │
│                         │
│  (Circular progress bar │
│   around screen edge)   │
└─────────────────────────┘
```

### Configuration Menu

```
┌─────────────────────────┐
│          CFG            │
├─────────────────────────┤
│ [ ] DIM    [ ] SLEEP    │
│                         │
│        DEFAULT          │
│      00:15:00           │
├─────────────────────────┤
│          SAVE           │
└─────────────────────────┘
```

## Recent Updates (April 2026)

- ✅ Fixed critical timer expiration race condition that caused stuck state
- ✅ Replaced RTC-based timing with `millis()` for accurate elapsed time tracking
- ✅ Removed problematic `isScreenPressed` flag blocking STOP button
- ✅ Improved button debouncing with timestamp tracking
- ✅ Reduced audio delay in alarm mode (100ms → 50ms) for better responsiveness
- ✅ Set alarm start flag immediately for better state management
- ✅ Added safety timeout for alarm auto-stop after 15 seconds
- ✅ Always reset timer to 15:00 regardless of previous settings
- ✅ Simplified the running UI to show only the timer and progress bar
- ✅ Restores the full setup UI automatically when the timer stops
- ✅ Added inactivity-based low-power light sleep
- ✅ Added configuration menu for power settings and default timer

## Troubleshooting

**Timer gets stuck:**
- Press Button A for emergency reset
- Check Timer library is properly installed

**Screen sleeps after long inactivity:**
- After 1 hour in setup mode, the device enters light sleep
- Touch, Button A, or the encoder wakes it back up

**Alarm won't stop:**
- Touch screen, rotate encoder, or press Button A
- Alarm will auto-stop after 15 seconds maximum

**Timer inaccurate:**
- Uses `millis()` function which is precise to within ±1ms over time
- RTC is no longer relied upon for countdown accuracy

## Files

- `timer.ino` - Main sketch file
- `Timer/Timer.h` - Timer library header
- `Timer/Timer.cpp` - Timer library implementation
- `M5Dial/` - M5Dial hardware library and examples

## License

See LICENSE file in repository root.
