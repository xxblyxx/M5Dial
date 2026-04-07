#include <M5Dial.h>
#include "Timer.h"
#define LV_TICK_CUSTOM 1
#include <lvgl.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <Preferences.h>

/* LVGL/LCARS-inspired timer sketch.
 * This is the alternate version of the timer UI.
 */

// --- Constants & Configuration ---
static constexpr uint8_t BRIGHTNESS_NORMAL = 128;
static constexpr uint8_t BRIGHTNESS_DIM = 5;
static constexpr unsigned long DEBOUNCE_MS = 50;
static constexpr unsigned long DOUBLE_CLICK_TIMEOUT_MS = 500;
static constexpr unsigned long DIM_TIMEOUT_DEFAULT_MS = 30000;
static constexpr unsigned long SLEEP_TIMEOUT_DEFAULT_MS = 3600000;

// --- UI Modes ---
enum class AppMode {
  SETUP = 0,
  RUNNING = 1,
  ALARM = 3,
  CONFIG = 4
};

// --- Global State ---
int g_timer_values[3] = { 0, 15, 0 }; // Current working timer (H, M, S)
int g_alarm_duration_ms = 15000;

long g_encoder_old_pos = 0;
AppMode g_current_mode = AppMode::SETUP;
int g_last_s_val = -999;

unsigned long g_timer_start_millis = 0;
unsigned long g_last_display_update_millis = 0;
unsigned long g_last_button_press_millis = 0;

unsigned long g_last_reset_btn_time = 0;
bool g_reset_btn_pressed = false;

unsigned long g_last_activity_millis = 0;
bool g_is_screen_dimmed = false;
bool g_screen_dimming_enabled = true;
bool g_light_sleep_enabled = true;
unsigned long g_dim_timeout_ms = DIM_TIMEOUT_DEFAULT_MS;
unsigned long g_light_sleep_timeout_ms = SLEEP_TIMEOUT_DEFAULT_MS;

String g_num_strings[3] = { "", "", "" };
int g_minute_limits[3] = { 24, 60, 60 }; // Limits for H, M, S
int g_chosen_unit = 2; // 0:H, 1:M, 2:S
int g_config_page = 0;

int g_default_timer[3] = { 0, 15, 0 };
int g_dim_timeout_settings[3] = { 0, 0, 30 };
int g_sleep_timeout_settings[3] = { 1, 0, 0 };

bool g_reset_hold_consumed = false;
bool g_alarm_active = false;
Timer g_alarm_timer;
long g_stop_alarm_encoder_pos = 0;
int g_last_saved_timer[3] = { 0, 0, 0 };

Preferences prefs;

// --- UI Component Pointers ---
static lv_obj_t *uiTitle = nullptr;
static lv_obj_t *uiTimer = nullptr;
static lv_obj_t *uiSubtitle = nullptr;
static lv_obj_t *uiAction = nullptr;
static lv_obj_t *uiActionLabel = nullptr;
static lv_obj_t *uiReset = nullptr;
static lv_obj_t *uiSelected = nullptr;
static lv_obj_t *uiProgress = nullptr;
static lv_obj_t *uiConfigTitle = nullptr;
static lv_obj_t *uiConfigHint = nullptr;
static lv_obj_t *uiConfigAction = nullptr;
static lv_obj_t *uiConfigActionLabel = nullptr;
static lv_obj_t *uiConfigToggle = nullptr;
static lv_obj_t *uiAlarmBanner = nullptr;

// --- Internal Flags ---
bool g_touch_debounced = false;
bool g_encoder_changed = false;
int g_deb = 0;

static constexpr int SCREEN_W = 240;
static constexpr int SCREEN_H = 240;
static constexpr int LVGL_BUF_LINES = 20;
static lv_color_t lvglBuf[SCREEN_W * LVGL_BUF_LINES];
static lv_disp_draw_buf_t lvglDrawBuf;
static lv_disp_drv_t lvglDispDrv;
static unsigned long lastLvglTick = 0;
static int uiModeCache = -1;
static int uiConfigPageCache = -1;
static int uiChosenCache = -1;
static bool uiDimCache = false;
static bool uiSleepCache = false;
static bool uiAlarmFlashCache = false;

// --- State Variables for Logic ---
int mode = 0;
int configPage = 0;
int chosen = 2;
long oldPosition = 0;
unsigned long lastActivityTime = 0;
bool isScreenDimmed = false;
bool screenDimmingEnabled = true;
bool lightSleepEnabled = true;

int dimTimeout[3] = {0, 15, 0};
int sleepTimeout[3] = {1, 0, 0};
int defaultTimer[3] = {0, 15, 0};
int num[3] = {0, 15, 0};
String numS[3] = {"", "", ""};
int mm[3] = {0, 60, 60};

unsigned long dimTimeoutMillis = 0;
unsigned long lightSleepTimeoutMillis = 0;
unsigned long timerStartMillis = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastResetButtonTime = 0;
unsigned long lastButtonPress = 0;
int lastTimer[3] = {0, 15, 0};
int alarmStart = 0;
unsigned long alarmTimerDuration = 0;
bool resetButtonPressed = false;
bool resetHoldConsumed = false;
long stopAlarmEncoderOldPos = 0;
long stopAlarmEncoderNewPos = 0;

// --- UI Helpers & Globals ---

static const lv_color_t STAR_BG = lv_color_hex(0x04070F);
static const lv_color_t STAR_PANEL = lv_color_hex(0x16233A);
static const lv_color_t STAR_PANEL_2 = lv_color_hex(0x273247);
static const lv_color_t STAR_AMBER = lv_color_hex(0xFF9A2E);
static const lv_color_t STAR_ORANGE = lv_color_hex(0xD56C28);
static const lv_color_t STAR_CYAN = lv_color_hex(0x58E8FF);
static const lv_color_t STAR_GREEN = lv_color_hex(0x78F1A7);
static const lv_color_t STAR_RED = lv_color_hex(0xFF5A4F);
static const lv_color_t STAR_TEXT = lv_color_hex(0xF1E9DB);
static const lv_color_t STAR_MUTED = lv_color_hex(0x8C98AC);
static const lv_color_t STAR_DARK = lv_color_hex(0x0B1019);

static void setLabelZoom(lv_obj_t *label, lv_coord_t zoom) {
  if (label != nullptr) {
    // Transform zoom can clip label glyphs on this LVGL/M5Dial combo.
    // Keep the call sites, but render the text at native size so it stays visible.
    (void)zoom;
  }
}

// --- Function Prototypes ---
void saveConfig();
unsigned long timeArrayToMillis(const int timeValue[3]);
void storeCurrentConfigPage();
void loadConfigPage(int page);
void syncTimerToDefault();
bool isRunMode();
bool isAlarmMode();
bool isConfigMode();
bool isSetupMode();
void setCurrentTimerToDefault();
void enterConfigMenu();
void exitConfigMenu();
void advanceConfigPage();
void adjustTimerValue(int direction, int target[3], int targetChosen);
void updateNumStrings();
void formatTimeText(char *out, size_t outLen, const int value[3]);
void setCommonScreenStyle(lv_obj_t *scr);
lv_obj_t *createPanel(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color, int radius = 14);
lv_obj_t *createTextLabel(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color);
void syncSetupUi();
void syncRunningUi();
void syncConfigUi();
void syncAlarmUi();
void buildSetupUi();
void buildRunningUi();
void buildConfigUi();
void buildAlarmUi();
void buildUiForMode();
void syncUiState();
void processLvgl();
void updateScreenBrightness();
void updateTime();
void resetToLastTimer();
void resetToDefaultTimer();
void reset();
void stopAlarmAndReset();

// --- Implementation ---

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
  for (int i = 0; i < 3; i++) num[i] = defaultTimer[i];
}

bool isRunMode() { return mode == 1; }
bool isAlarmMode() { return mode == 3; }
bool isConfigMode() { return mode == 4; }
bool isSetupMode() { return mode == 0 || mode == 4; }

void setCurrentTimerToDefault() {
  syncTimerToDefault();
  chosen = 2;
}

void enterConfigMenu() {
  mode = 4;
  configPage = 0;
  loadConfigPage(configPage);
  chosen = 2;
  oldPosition = M5Dial.Encoder.read();
  lastActivityTime = millis();
  isScreenDimmed = false;
  M5Dial.Display.setBrightness(BRIGHTNESS_NORMAL);
}

void exitConfigMenu() {
  storeCurrentConfigPage();
  mode = 0;
  syncTimerToDefault();
  chosen = 2;
  oldPosition = M5Dial.Encoder.read();
  lastActivityTime = millis();
  isScreenDimmed = false;
  M5Dial.Display.setBrightness(BRIGHTNESS_NORMAL);
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

void formatTimeText(char *out, size_t outLen, const int value[3]) {
  snprintf(out, outLen, "%02d:%02d:%02d", value[0], value[1], value[2]);
}

void setCommonScreenStyle(lv_obj_t *scr) {
  lv_obj_set_style_bg_color(scr, STAR_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
}

lv_obj_t *createPanel(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color, int radius) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(obj, color, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, radius, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  return obj;
}

lv_obj_t *createTextLabel(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  return label;
}

void syncSetupUi() {
  char timeText[16];
  formatTimeText(timeText, sizeof(timeText), num);
  lv_label_set_text(uiTimer, timeText);
  lv_label_set_text(uiTitle, "BLY POMODORO");
  lv_label_set_text(uiSubtitle, "STARFLEET TIMELINE");
  lv_label_set_text(uiActionLabel, "START");
  lv_label_set_text(uiReset, "RESET");
  if (uiSelected != nullptr) lv_obj_set_x(uiSelected, 18 + (chosen * 76));
}

void syncRunningUi() {
  char timeText[16];
  formatTimeText(timeText, sizeof(timeText), num);
  lv_label_set_text(uiTimer, timeText);
  lv_label_set_text(uiTitle, "COUNTDOWN");
  lv_label_set_text(uiSubtitle, "MAIN POWER CONDUIT");
  lv_label_set_text(uiActionLabel, "RUNNING");
  lv_label_set_text(uiReset, "TOUCH TO ABORT");
  int totalSeconds = lastTimer[0] * 3600 + lastTimer[1] * 60 + lastTimer[2];
  int remainingSeconds = num[0] * 3600 + num[1] * 60 + num[2];
  int elapsedSeconds = totalSeconds - remainingSeconds;
  int progress = 0;
  if (totalSeconds > 0) {
    progress = (elapsedSeconds * 100) / totalSeconds;
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;
  }
  lv_arc_set_value(uiProgress, progress);
}

void syncConfigUi() {
  char timeText[16];
  formatTimeText(timeText, sizeof(timeText), num);
  lv_label_set_text(uiTimer, timeText);
  lv_label_set_text(uiTitle, "CFG");
  lv_label_set_text(uiSubtitle, configPage == 0 ? "DIMER TOGGLE" : (configPage == 1 ? "SLEEP TOGGLE" : "DEFAULT TIMER"));
  lv_label_set_text(uiConfigActionLabel, configPage < 2 ? "NEXT" : "DONE");
  lv_label_set_text(uiReset, configPage == 0 ? "SCREEN DIMS" : (configPage == 1 ? "LIGHT SLEEP" : "PREFERRED RESET"));
  if (uiSelected != nullptr) lv_obj_set_x(uiSelected, 18 + (chosen * 76));
  if (uiConfigToggle != nullptr) {
    if (configPage == 0) {
      if (screenDimmingEnabled) lv_obj_add_state(uiConfigToggle, LV_STATE_CHECKED);
      else lv_obj_clear_state(uiConfigToggle, LV_STATE_CHECKED);
      lv_checkbox_set_text(uiConfigToggle, "DIM");
    } else if (configPage == 1) {
      if (lightSleepEnabled) lv_obj_add_state(uiConfigToggle, LV_STATE_CHECKED);
      else lv_obj_clear_state(uiConfigToggle, LV_STATE_CHECKED);
      lv_checkbox_set_text(uiConfigToggle, "SLEEP");
    }
  }
}

void syncAlarmUi() {
  char timeText[16];
  formatTimeText(timeText, sizeof(timeText), num);
  lv_label_set_text(uiTitle, "ALARM");
  lv_label_set_text(uiTimer, timeText);
  lv_label_set_text(uiSubtitle, "TIME QUANTUM EXPIRED");
  lv_label_set_text(uiActionLabel, "SILENCE");
  lv_label_set_text(uiReset, "TOUCH OR ROTATE TO CLEAR");
  bool flash = ((millis() / 250) % 2) == 0;
  if (flash != uiAlarmFlashCache) {
    uiAlarmFlashCache = flash;
    lv_color_t bg = flash ? STAR_RED : STAR_DARK;
    lv_obj_set_style_bg_color(lv_scr_act(), bg, 0);
    lv_obj_set_style_bg_color(uiAlarmBanner, flash ? STAR_AMBER : STAR_PANEL, 0);
    lv_obj_set_style_text_color(uiTitle, flash ? STAR_DARK : STAR_AMBER, 0);
    lv_obj_set_style_text_color(uiTimer, flash ? STAR_DARK : STAR_TEXT, 0);
    lv_obj_set_style_text_color(uiSubtitle, flash ? STAR_DARK : STAR_MUTED, 0);
    lv_obj_set_style_text_color(uiActionLabel, flash ? STAR_DARK : STAR_TEXT, 0);
    lv_obj_set_style_text_color(uiReset, flash ? STAR_DARK : STAR_MUTED, 0);
  }
}

void buildSetupUi() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  setCommonScreenStyle(scr);
  lv_obj_t *topBand = createPanel(scr, 10, 10, 220, 34, STAR_PANEL, 16);
  uiTitle = createTextLabel(topBand, "BLY POMODORO", &lv_font_montserrat_16, STAR_AMBER);
  setLabelZoom(uiTitle, 320);
  lv_obj_center(uiTitle);
  createPanel(scr, 14, 52, 82, 12, STAR_ORANGE, 6);
  createPanel(scr, 144, 52, 82, 12, STAR_CYAN, 6);
  uiTimer = createTextLabel(scr, "00:15:00", &lv_font_montserrat_16, STAR_TEXT);
  setLabelZoom(uiTimer, 560);
  lv_obj_align(uiTimer, LV_ALIGN_CENTER, 0, -8);
  uiSubtitle = createTextLabel(scr, "STARFLEET TIMELINE", &lv_font_montserrat_14, STAR_MUTED);
  setLabelZoom(uiSubtitle, 240);
  lv_obj_align(uiSubtitle, LV_ALIGN_CENTER, 0, 26);
  const char *unitLabels[3] = { "HRS", "MIN", "SEC" };
  for (int i = 0; i < 3; i++) {
    lv_obj_t *unit = createPanel(scr, 18 + (i * 76), 148, 60, 18, i == chosen ? STAR_GREEN : STAR_PANEL_2, 8);
    lv_obj_t *label = createTextLabel(unit, unitLabels[i], &lv_font_montserrat_14, STAR_TEXT);
    setLabelZoom(label, 220);
    lv_obj_center(label);
  }
  uiSelected = createPanel(scr, 18 + (chosen * 76), 168, 60, 4, STAR_GREEN, 2);
  uiAction = createPanel(scr, 24, 178, 192, 30, STAR_CYAN, 12);
  uiActionLabel = createTextLabel(uiAction, "START", &lv_font_montserrat_16, STAR_DARK);
  setLabelZoom(uiActionLabel, 300);
  lv_obj_center(uiActionLabel);
  uiReset = createTextLabel(scr, "RESET", &lv_font_montserrat_14, STAR_AMBER);
  setLabelZoom(uiReset, 220);
  lv_obj_align(uiReset, LV_ALIGN_BOTTOM_MID, 0, -10);
  syncSetupUi();
}

void buildRunningUi() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  setCommonScreenStyle(scr);
  uiProgress = lv_arc_create(scr);
  lv_obj_set_size(uiProgress, 222, 222);
  lv_obj_center(uiProgress);
  lv_arc_set_rotation(uiProgress, 270);
  lv_arc_set_bg_angles(uiProgress, 0, 360);
  lv_arc_set_range(uiProgress, 0, 100);
  lv_arc_set_value(uiProgress, 0);
  lv_obj_clear_flag(uiProgress, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(uiProgress, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(uiProgress, 12, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(uiProgress, STAR_PANEL_2, LV_PART_MAIN);
  lv_obj_set_style_arc_color(uiProgress, STAR_GREEN, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(uiProgress, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(uiProgress, 0, 0);
  uiTitle = createTextLabel(scr, "COUNTDOWN", &lv_font_montserrat_16, STAR_CYAN);
  setLabelZoom(uiTitle, 300);
  lv_obj_align(uiTitle, LV_ALIGN_TOP_MID, 0, 14);
  uiTimer = createTextLabel(scr, "00:00:00", &lv_font_montserrat_16, STAR_TEXT);
  setLabelZoom(uiTimer, 560);
  lv_obj_align(uiTimer, LV_ALIGN_CENTER, 0, -4);
  uiSubtitle = createTextLabel(scr, "MAIN POWER CONDUIT", &lv_font_montserrat_14, STAR_MUTED);
  setLabelZoom(uiSubtitle, 220);
  lv_obj_align(uiSubtitle, LV_ALIGN_CENTER, 0, 28);
  uiAction = createPanel(scr, 28, 176, 184, 28, STAR_PANEL, 12);
  uiActionLabel = createTextLabel(uiAction, "RUNNING", &lv_font_montserrat_16, STAR_AMBER);
  setLabelZoom(uiActionLabel, 280);
  lv_obj_center(uiActionLabel);
  uiReset = createTextLabel(scr, "TOUCH TO ABORT", &lv_font_montserrat_14, STAR_MUTED);
  setLabelZoom(uiReset, 200);
  lv_obj_align(uiReset, LV_ALIGN_BOTTOM_MID, 0, -10);
  syncRunningUi();
}

void buildConfigUi() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  setCommonScreenStyle(scr);
  lv_obj_t *topBand = createPanel(scr, 10, 10, 220, 30, STAR_PANEL, 14);
  uiConfigTitle = createTextLabel(topBand, "CFG", &lv_font_montserrat_16, STAR_AMBER);
  setLabelZoom(uiConfigTitle, 300);
  lv_obj_center(uiConfigTitle);
  uiConfigHint = createTextLabel(scr, configPage == 0 ? "DIM TIMEOUT" : (configPage == 1 ? "SLEEP TIMEOUT" : "DEFAULT TIMER"), &lv_font_montserrat_14, STAR_MUTED);
  setLabelZoom(uiConfigHint, 220);
  lv_obj_align(uiConfigHint, LV_ALIGN_TOP_MID, 0, 52);
  uiTimer = createTextLabel(scr, "00:15:00", &lv_font_montserrat_16, STAR_TEXT);
  setLabelZoom(uiTimer, 560);
  lv_obj_align(uiTimer, LV_ALIGN_CENTER, 0, -4);
  const char *action = configPage < 2 ? "NEXT" : "DONE";
  uiConfigAction = createPanel(scr, 26, 176, 188, 28, STAR_CYAN, 12);
  uiConfigActionLabel = createTextLabel(uiConfigAction, action, &lv_font_montserrat_16, STAR_DARK);
  setLabelZoom(uiConfigActionLabel, 300);
  lv_obj_center(uiConfigActionLabel);
  uiReset = createTextLabel(scr, configPage == 0 ? "SCREEN DIMMING" : (configPage == 1 ? "LIGHT SLEEP" : "PREFERRED TIMER"), &lv_font_montserrat_14, STAR_AMBER);
  setLabelZoom(uiReset, 200);
  lv_obj_align(uiReset, LV_ALIGN_BOTTOM_MID, 0, -10);
  if (configPage == 0 || configPage == 1) {
    uiConfigToggle = lv_checkbox_create(scr);
    lv_checkbox_set_text(uiConfigToggle, configPage == 0 ? "DIM" : "SLEEP");
    lv_obj_align(uiConfigToggle, LV_ALIGN_TOP_LEFT, 22, 34);
    lv_obj_set_style_text_color(uiConfigToggle, STAR_TEXT, 0);
    lv_obj_set_style_bg_opa(uiConfigToggle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(uiConfigToggle, 0, 0);
    lv_obj_set_style_pad_all(uiConfigToggle, 0, 0);
  } else {
    uiConfigToggle = nullptr;
  }
  uiSelected = createPanel(scr, 18 + (chosen * 76), 168, 60, 4, STAR_GREEN, 2);
  syncConfigUi();
}

void buildAlarmUi() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  setCommonScreenStyle(scr);
  uiAlarmBanner = createPanel(scr, 10, 10, 220, 34, STAR_AMBER, 16);
  uiTitle = createTextLabel(uiAlarmBanner, "ALARM", &lv_font_montserrat_16, STAR_DARK);
  setLabelZoom(uiTitle, 300);
  lv_obj_center(uiTitle);
  uiTimer = createTextLabel(scr, "00:00:00", &lv_font_montserrat_16, STAR_TEXT);
  setLabelZoom(uiTimer, 560);
  lv_obj_align(uiTimer, LV_ALIGN_CENTER, 0, -4);
  uiSubtitle = createTextLabel(scr, "TIME QUANTUM EXPIRED", &lv_font_montserrat_14, STAR_MUTED);
  setLabelZoom(uiSubtitle, 220);
  lv_obj_align(uiSubtitle, LV_ALIGN_CENTER, 0, 28);
  uiAction = createPanel(scr, 40, 176, 160, 28, STAR_PANEL, 12);
  uiActionLabel = createTextLabel(uiAction, "SILENCE", &lv_font_montserrat_16, STAR_TEXT);
  setLabelZoom(uiActionLabel, 280);
  lv_obj_center(uiActionLabel);
  uiReset = createTextLabel(scr, "TOUCH OR ROTATE TO CLEAR", &lv_font_montserrat_14, STAR_MUTED);
  setLabelZoom(uiReset, 180);
  lv_obj_align(uiReset, LV_ALIGN_BOTTOM_MID, 0, -10);
  uiProgress = lv_arc_create(scr);
  lv_obj_set_size(uiProgress, 222, 222);
  lv_obj_center(uiProgress);
  lv_arc_set_rotation(uiProgress, 270);
  lv_arc_set_bg_angles(uiProgress, 0, 360);
  lv_arc_set_range(uiProgress, 0, 100);
  lv_arc_set_value(uiProgress, 100);
  lv_obj_clear_flag(uiProgress, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(uiProgress, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(uiProgress, 12, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(uiProgress, STAR_PANEL_2, LV_PART_MAIN);
  lv_obj_set_style_arc_color(uiProgress, STAR_RED, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(uiProgress, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(uiProgress, 0, 0);
  uiAlarmFlashCache = false;
  syncAlarmUi();
}

void buildUiForMode() {
  uiModeCache = mode;
  uiConfigPageCache = configPage;
  uiChosenCache = chosen;
  uiDimCache = screenDimmingEnabled;
  uiSleepCache = lightSleepEnabled;
  if (mode == 1) buildRunningUi();
  else if (mode == 4) buildConfigUi();
  else if (mode == 3) buildAlarmUi();
  else buildSetupUi();
}

void syncUiState() {
  if (mode != uiModeCache || (mode == 4 && configPage != uiConfigPageCache)) {
    buildUiForMode();
    return;
  }
  if (mode == 1) syncRunningUi();
  else if (mode == 4) syncConfigUi();
  else if (mode == 3) syncAlarmUi();
  else syncSetupUi();
}

void processLvgl() {
  lv_timer_handler();
}

void updateScreenBrightness() {
  unsigned long now = millis();
  unsigned long inactiveTime = now - lastActivityTime;
  if (isRunMode() || isAlarmMode()) {
    if (isScreenDimmed) {
      M5Dial.Display.setBrightness(BRIGHTNESS_NORMAL);
      isScreenDimmed = false;
    }
    return;
  }
  if (lightSleepEnabled && inactiveTime >= lightSleepTimeoutMillis) {
    gpio_wakeup_enable(GPIO_NUM_42, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)DIAL_ENCODER_PIN_A, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)DIAL_ENCODER_PIN_B, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    M5Dial.Display.setBrightness(0);
    M5Dial.Power.lightSleep(0, true);
    return;
  }
  if (isScreenDimmed && inactiveTime < dimTimeoutMillis) {
    M5Dial.Display.setBrightness(BRIGHTNESS_NORMAL);
    isScreenDimmed = false;
  } else if (screenDimmingEnabled && !isScreenDimmed && inactiveTime > dimTimeoutMillis) {
    M5Dial.Display.setBrightness(BRIGHTNESS_DIM);
    isScreenDimmed = true;
  }
}

void updateTime() {
  unsigned long currentMillis = millis();
  unsigned long elapsedSeconds = (currentMillis - timerStartMillis) / 1000;
  if (currentMillis - lastDisplayUpdate >= 1000) {
    int totalSeconds = lastTimer[0] * 3600 + lastTimer[1] * 60 + lastTimer[2];
    int remainingSeconds = totalSeconds - elapsedSeconds;
    if (remainingSeconds <= 0) {
      num[0] = 0; num[1] = 0; num[2] = 0;
      if (mode == 1) mode = 3;
    } else {
      num[0] = remainingSeconds / 3600;
      num[1] = (remainingSeconds % 3600) / 60;
      num[2] = remainingSeconds % 60;
    }
    lastDisplayUpdate = currentMillis;
  }
  long stopAlarmEncoderNewPos = M5Dial.Encoder.read();
  if (stopAlarmEncoderOldPos != stopAlarmEncoderNewPos) stopAlarmAndReset();
}

void resetToLastTimer() {
  mode = 0;
  num[0] = lastTimer[0];
  num[1] = lastTimer[1];
  num[2] = lastTimer[2];
  alarmStart = 0;
}

void resetToDefaultTimer() {
  mode = 0;
  setCurrentTimerToDefault();
  alarmStart = 0;
}

void reset() { resetToLastTimer(); }

void stopAlarmAndReset() {
  M5Dial.Speaker.tone(2800, 100);
  reset();
  delay(200);
}

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
  for (int i = 0; i < 3; i++) num[i] = defaultTimer[i];
  M5Dial.Display.setBrightness(BRIGHTNESS_NORMAL);
  lv_init();
  lv_disp_draw_buf_init(&lvglDrawBuf, lvglBuf, NULL, SCREEN_W * LVGL_BUF_LINES);
  lv_disp_drv_init(&lvglDispDrv);
  lvglDispDrv.hor_res = SCREEN_W;
  lvglDispDrv.ver_res = SCREEN_H;
  lvglDispDrv.flush_cb = [](lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    M5Dial.Display.pushImage(area->x1, area->y1, w, h, (const uint16_t *)color_p);
    lv_disp_flush_ready(disp);
  };
  lvglDispDrv.draw_buf = &lvglDrawBuf;
  lv_disp_drv_register(&lvglDispDrv);
  lastActivityTime = millis();
  lastLvglTick = millis();
  delay(200);
}

void loop() {
  M5Dial.update();
  unsigned long now = millis();
  bool userActive = false;

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
      if (g_deb == 0) {
        g_deb = 1;
        M5Dial.Speaker.tone(3000, 80);
        if (configPage == 0 && t.y > 34 && t.y < 66 && t.x > 16 && t.x < 112) {
          screenDimmingEnabled = !screenDimmingEnabled;
          storeCurrentConfigPage();
          if (!screenDimmingEnabled) {
            M5Dial.Display.setBrightness(BRIGHTNESS_NORMAL);
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
      g_deb = 0;
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
    if (M5Dial.BtnA.isPressed() && !resetHoldConsumed) {
      if (!resetButtonPressed && (now - lastButtonPress > DEBOUNCE_MS)) {
        resetButtonPressed = true;
        if (now - lastResetButtonTime <= DOUBLE_CLICK_TIMEOUT_MS) {
          M5Dial.Speaker.tone(2800, 100);
          resetToDefaultTimer();
          delay(200);
          lastResetButtonTime = 0;
        } else {
          M5Dial.Speaker.tone(2800, 100);
          resetToLastTimer();
          delay(200);
          lastResetButtonTime = now;
        }
        lastButtonPress = now;
        userActive = true;
      }
    } else {
      resetButtonPressed = false;
      if (!M5Dial.BtnA.isPressed()) resetHoldConsumed = false;
    }
  }

  if (mode == 0) {
    auto t = M5Dial.Touch.getDetail();
    if (t.isPressed()) {
      userActive = true;
      if (g_deb == 0) {
        g_deb = 1;
        M5Dial.Speaker.tone(3000, 100);
        if (t.y > 160) {
          mode = 1;
          stopAlarmEncoderOldPos = M5Dial.Encoder.read();
          lastTimer[0] = num[0];
          lastTimer[1] = num[1];
          lastTimer[2] = num[2];
          timerStartMillis = millis();
          lastDisplayUpdate = millis();
          delay(200);
        }
        if (t.y > 86 && t.y < 150) {
          if (t.x > 10 && t.x < 90) chosen = 0;
          if (t.x > 90 && t.x < 166) chosen = 1;
          if (t.x > 166 && t.x < 224) chosen = 2;
        }
      }
    } else g_deb = 0;
    long newPosition = M5Dial.Encoder.read();
    if (newPosition != oldPosition) {
      userActive = true;
      M5Dial.Speaker.tone(3600, 30);
      adjustTimerValue(newPosition > oldPosition ? 1 : -1, num, chosen);
      oldPosition = newPosition;
    }
  }

  if (isRunMode()) {
    auto t = M5Dial.Touch.getDetail();
    if (t.isPressed()) {
      if (g_deb == 0) {
        M5Dial.Speaker.tone(3000, 100);
        stopAlarmAndReset();
        g_deb = 1;
      }
    } else {
      g_deb = 0;
    }
    updateTime();
  }

  updateNumStrings();
  if (userActive) {
    lastActivityTime = now;
    isScreenDimmed = false;
    if (isSetupMode()) M5Dial.Display.setBrightness(BRIGHTNESS_NORMAL);
  }
  updateScreenBrightness();
  if (isAlarmMode()) {
    if (alarmStart == 0) {
      stopAlarmEncoderOldPos = M5Dial.Encoder.read();
      g_alarm_timer.start();
      alarmStart = 1;
    }
    if (g_alarm_timer.read() > alarmTimerDuration) {
      g_alarm_timer.stop();
      reset();
      delay(200);
    } else {
      M5Dial.Speaker.tone(4000, 50);
    }
  } else {
    alarmStart = 0;
  }
  syncUiState();
  processLvgl();
}
