#include <M5Dial.h>
#include "Timer.h"
#define LV_TICK_CUSTOM 1
#include "lv_conf.h"
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
static lv_obj_t *uiAction = nullptr;
static lv_obj_t *uiSelected = nullptr;
static lv_obj_t *uiProgress = nullptr;
static lv_obj_t *uiConfigAction = nullptr;
static lv_obj_t *uiAlarmBanner = nullptr;
static lv_obj_t *uiUnitPills[3] = { nullptr, nullptr, nullptr };
static lv_obj_t *uiUnitLabels[3] = { nullptr, nullptr, nullptr };

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
unsigned long alarmTimerDuration = 15000;
bool resetButtonPressed = false;
bool resetHoldConsumed = false;
long stopAlarmEncoderOldPos = 0;
long stopAlarmEncoderNewPos = 0;

// --- UI Helpers & Globals ---

static const lv_color_t STAR_BG = lv_color_hex(0x000000);
static const lv_color_t STAR_PANEL = lv_color_hex(0x16233A);
static const lv_color_t STAR_PANEL_2 = lv_color_hex(0x273247);
static const lv_color_t STAR_AMBER = lv_color_hex(0xFF9A2E);
static const lv_color_t STAR_ORANGE = lv_color_hex(0xD56C28);
static const lv_color_t STAR_CYAN = lv_color_hex(0x58E8FF);
static const lv_color_t STAR_GREEN = lv_color_hex(0x78F1A7);
static const lv_color_t STAR_RED = lv_color_hex(0xFF5A4F);
static const lv_color_t SETUP_PILL_RED = lv_color_hex(0xFF0000);
static const lv_color_t STAR_TEXT = lv_color_hex(0xF1E9DB);
static const lv_color_t STAR_MUTED = lv_color_hex(0x8C98AC);
static const lv_color_t STAR_DARK = lv_color_hex(0x000000);
static const lv_color_t STICK_HEADER = lv_color_hex(0x043250);
static const lv_color_t STICK_HEADER_TAG = lv_color_hex(0x2C2E44);
static const lv_color_t STICK_HEADER_ACCENT = lv_color_hex(0xF8D302);
static const lv_color_t STICK_HEADER_TEXT = lv_color_hex(0xC5C4C4);

static constexpr int TFT_FONT_SMALL = 1;
static constexpr int TFT_FONT_MEDIUM = 2;
static constexpr int TFT_FONT_LARGE = 4;
static constexpr int TFT_FONT_TIMER = 7;
static constexpr int TFT_FONT_TITLE = 2;

static constexpr uint16_t rgb888To565(uint32_t color) {
  return ((color & 0x00F80000) >> 8)
       | ((color & 0x0000FC00) >> 5)
       | ((color & 0x000000F8) >> 3);
}

static constexpr uint16_t STAR_BG_565 = rgb888To565(0x000000);
static constexpr uint16_t STAR_PANEL_565 = rgb888To565(0x16233A);
static constexpr uint16_t STAR_PANEL_2_565 = rgb888To565(0x273247);
static constexpr uint16_t STAR_AMBER_565 = rgb888To565(0xFF9A2E);
static constexpr uint16_t STAR_ORANGE_565 = rgb888To565(0xD56C28);
static constexpr uint16_t STAR_CYAN_565 = rgb888To565(0x58E8FF);
static constexpr uint16_t STAR_GREEN_565 = rgb888To565(0x78F1A7);
static constexpr uint16_t STAR_RED_565 = rgb888To565(0xFF5A4F);
static constexpr uint16_t SETUP_PILL_RED_565 = rgb888To565(0xFF0000);
static constexpr uint16_t STAR_TEXT_565 = rgb888To565(0xF1E9DB);
static constexpr uint16_t STAR_MUTED_565 = rgb888To565(0x8C98AC);
static constexpr uint16_t STAR_DARK_565 = rgb888To565(0x000000);
static constexpr uint16_t STICK_HEADER_565 = rgb888To565(0x043250);
static constexpr uint16_t STICK_HEADER_TAG_565 = rgb888To565(0x2C2E44);
static constexpr uint16_t STICK_HEADER_ACCENT_565 = rgb888To565(0xF8D302);
static constexpr uint16_t STICK_HEADER_TEXT_565 = rgb888To565(0xC5C4C4);

static void drawBuiltinText(const String &text, int x, int y, int font, uint16_t fg, uint16_t bg) {
  M5Dial.Display.setTextColor(fg, bg);
  M5Dial.Display.drawString(text, x, y, font);
}

static void drawBuiltinText(const char *text, int x, int y, int font, uint16_t fg, uint16_t bg) {
  M5Dial.Display.setTextColor(fg, bg);
  M5Dial.Display.drawString(text, x, y, font);
}

static void drawBuiltinTextTransparent(const char *text, int x, int y, int font, uint16_t fg) {
  M5Dial.Display.setTextColor(fg);
  M5Dial.Display.drawString(text, x, y, font);
}

void renderSetupTextOverlay();
void renderRunningTextOverlay();
void renderConfigTextOverlay();
void renderAlarmTextOverlay();
void renderAlarmFastOverlay();
void renderTextOverlay();

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
lv_obj_t *buildHeaderBanner(lv_obj_t *parent, lv_color_t mainColor, lv_color_t accentColor = STICK_HEADER_ACCENT);
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
  lv_obj_set_style_outline_width(scr, 0, 0);
  lv_obj_set_style_outline_opa(scr, LV_OPA_TRANSP, 0);
}

lv_obj_t *createPanel(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color, int radius) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(obj, color, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_outline_width(obj, 0, 0);
  lv_obj_set_style_outline_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(obj, radius, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  return obj;
}

lv_obj_t *createPillLabel(lv_obj_t *parent, const char *text) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, STAR_TEXT, 0);
  lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, 0);
  lv_obj_center(label);
  return label;
}

lv_obj_t *buildHeaderBanner(lv_obj_t *parent, lv_color_t mainColor, lv_color_t accentColor) {
  lv_obj_t *main = createPanel(parent, 30, 12, 180, 18, mainColor, 3);
  createPanel(parent, 30, 12, 10, 18, accentColor, 2);
  createPanel(parent, 200, 12, 10, 18, accentColor, 2);
  createPanel(parent, 86, 34, 68, 4, accentColor, 2);
  return main;
}

void syncSetupUi() {
  for (int i = 0; i < 3; i++) {
    if (uiUnitPills[i] == nullptr) continue;
    lv_color_t pillColor = (i == chosen) ? SETUP_PILL_RED : STAR_ORANGE;
    lv_obj_set_style_bg_color(uiUnitPills[i], pillColor, 0);
    lv_obj_set_style_border_color(uiUnitPills[i], pillColor, 0);
  }
}

void syncRunningUi() {
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
  for (int i = 0; i < 3; i++) {
    if (uiUnitPills[i] == nullptr) continue;
    lv_color_t pillColor = (i == chosen) ? SETUP_PILL_RED : STAR_ORANGE;
    lv_obj_set_style_bg_color(uiUnitPills[i], pillColor, 0);
    lv_obj_set_style_border_color(uiUnitPills[i], pillColor, 0);
  }
}

void syncAlarmUi() {
  bool flash = ((millis() / 250) % 2) == 0;
  if (flash != uiAlarmFlashCache) {
    uiAlarmFlashCache = flash;
    lv_color_t bg = flash ? STAR_RED : STAR_DARK;
    lv_obj_set_style_bg_color(lv_scr_act(), bg, 0);
    lv_obj_set_style_bg_color(uiAlarmBanner, flash ? STICK_HEADER_ACCENT : STICK_HEADER, 0);
  }
}

void buildSetupUi() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  setCommonScreenStyle(scr);
  buildHeaderBanner(scr, STICK_HEADER);
  createPanel(scr, 14, 52, 82, 12, STAR_ORANGE, 6);
  createPanel(scr, 144, 52, 82, 12, STAR_CYAN, 6);
  static const char *unitLabels[3] = { "HRS", "MIN", "SEC" };
  for (int i = 0; i < 3; i++) {
    uiUnitPills[i] = createPanel(scr, 18 + (i * 76), 148, 60, 18, STAR_ORANGE, 8);
    lv_obj_set_style_border_width(uiUnitPills[i], 2, 0);
    lv_obj_set_style_border_color(uiUnitPills[i], STAR_ORANGE, 0);
    lv_obj_set_style_border_opa(uiUnitPills[i], LV_OPA_COVER, 0);
    uiUnitLabels[i] = createPillLabel(uiUnitPills[i], unitLabels[i]);
  }
  uiSelected = nullptr;
  uiAction = createPanel(scr, 24, 178, 192, 30, STAR_CYAN, 12);
  syncSetupUi();
}

void buildRunningUi() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  setCommonScreenStyle(scr);
  uiSelected = nullptr;
  uiAction = nullptr;
  for (int i = 0; i < 3; i++) uiUnitPills[i] = nullptr;
  for (int i = 0; i < 3; i++) uiUnitLabels[i] = nullptr;
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
  lv_obj_set_style_arc_color(uiProgress, STAR_RED, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(uiProgress, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(uiProgress, 0, 0);
  lv_obj_set_style_border_opa(uiProgress, LV_OPA_TRANSP, 0);
  lv_obj_set_style_outline_width(uiProgress, 0, 0);
  lv_obj_set_style_outline_opa(uiProgress, LV_OPA_TRANSP, 0);
  syncRunningUi();
}

void buildConfigUi() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  setCommonScreenStyle(scr);
  buildHeaderBanner(scr, STICK_HEADER);
  uiConfigAction = createPanel(scr, 26, 176, 188, 28, STAR_CYAN, 12);
  static const char *unitLabels[3] = { "HRS", "MIN", "SEC" };
  for (int i = 0; i < 3; i++) {
    uiUnitPills[i] = createPanel(scr, 18 + (i * 76), 148, 60, 18, STAR_ORANGE, 8);
    lv_obj_set_style_border_width(uiUnitPills[i], 2, 0);
    lv_obj_set_style_border_color(uiUnitPills[i], STAR_ORANGE, 0);
    lv_obj_set_style_border_opa(uiUnitPills[i], LV_OPA_COVER, 0);
    uiUnitLabels[i] = createPillLabel(uiUnitPills[i], unitLabels[i]);
  }
  uiSelected = nullptr;
  syncConfigUi();
}

void buildAlarmUi() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  setCommonScreenStyle(scr);
  uiSelected = nullptr;
  for (int i = 0; i < 3; i++) uiUnitLabels[i] = nullptr;
  uiAlarmBanner = buildHeaderBanner(scr, STICK_HEADER);
  uiAction = createPanel(scr, 40, 176, 160, 28, STAR_PANEL, 12);
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
  lv_obj_set_style_border_opa(uiProgress, LV_OPA_TRANSP, 0);
  lv_obj_set_style_outline_width(uiProgress, 0, 0);
  lv_obj_set_style_outline_opa(uiProgress, LV_OPA_TRANSP, 0);
  uiAlarmFlashCache = false;
  for (int i = 0; i < 3; i++) uiUnitPills[i] = nullptr;
  syncAlarmUi();
}

void renderSetupTextOverlay() {
  char timeText[16];
  formatTimeText(timeText, sizeof(timeText), num);
  drawBuiltinText("BLY POMODORO", 120, 22, TFT_FONT_SMALL, STICK_HEADER_TEXT_565, STICK_HEADER_565);
  drawBuiltinText(timeText, 120, 112, TFT_FONT_TIMER, STAR_TEXT_565, STAR_BG_565);
  drawBuiltinText("START", 120, 193, TFT_FONT_LARGE, STAR_DARK_565, STAR_CYAN_565);
}

void renderRunningTextOverlay() {
  char timeText[16];
  formatTimeText(timeText, sizeof(timeText), num);
  drawBuiltinText(timeText, 120, 116, TFT_FONT_TIMER, STAR_TEXT_565, STAR_BG_565);
  drawBuiltinText("MAIN POWER CONDUIT", 120, 148, TFT_FONT_SMALL, STAR_MUTED_565, STAR_BG_565);
}

void renderConfigTextOverlay() {
  char timeText[16];
  formatTimeText(timeText, sizeof(timeText), num);
  const char *hint = configPage == 0 ? "DIM TIMEOUT" : (configPage == 1 ? "SLEEP TIMEOUT" : "DEFAULT TIMER");
  const char *action = configPage < 2 ? "NEXT" : "DONE";
  const char *footer = configPage == 0 ? "SCREEN DIMMING" : (configPage == 1 ? "LIGHT SLEEP" : "PREFERRED TIMER");
  drawBuiltinText("CONFIG", 120, 22, TFT_FONT_SMALL, STICK_HEADER_TEXT_565, STICK_HEADER_565);
  drawBuiltinText(hint, 120, 56, TFT_FONT_SMALL, STAR_MUTED_565, STAR_BG_565);
  drawBuiltinText(timeText, 120, 116, TFT_FONT_TIMER, STAR_TEXT_565, STAR_BG_565);
  drawBuiltinText(action, 120, 190, TFT_FONT_LARGE, STAR_DARK_565, STAR_CYAN_565);
  drawBuiltinText(footer, 120, 226, TFT_FONT_SMALL, STAR_AMBER_565, STAR_BG_565);
}

void renderAlarmTextOverlay() {
  char timeText[16];
  formatTimeText(timeText, sizeof(timeText), num);
  bool flash = ((millis() / 250) % 2) == 0;
  uint16_t screenBg = flash ? STAR_RED_565 : STAR_DARK_565;
  uint16_t bannerBg = flash ? STICK_HEADER_ACCENT_565 : STICK_HEADER_565;
  uint16_t titleFg = flash ? STAR_DARK_565 : STICK_HEADER_TEXT_565;
  uint16_t timerFg = flash ? STAR_DARK_565 : STAR_TEXT_565;
  uint16_t subtitleFg = flash ? STAR_DARK_565 : STAR_MUTED_565;
  uint16_t actionFg = flash ? STAR_DARK_565 : STAR_TEXT_565;
  drawBuiltinText("ALARM", 120, 22, TFT_FONT_SMALL, titleFg, bannerBg);
  drawBuiltinText(timeText, 120, 116, TFT_FONT_TIMER, timerFg, screenBg);
  drawBuiltinText("TIME QUANTUM EXPIRED", 120, 148, TFT_FONT_SMALL, subtitleFg, screenBg);
  drawBuiltinText("SILENCE", 120, 190, TFT_FONT_LARGE, actionFg, STAR_PANEL_565);
  drawBuiltinText("TOUCH OR ROTATE TO CLEAR", 120, 226, TFT_FONT_SMALL, subtitleFg, screenBg);
}

void renderAlarmFastOverlay() {
  char timeText[16];
  formatTimeText(timeText, sizeof(timeText), num);
  bool flash = ((millis() / 100) % 2) == 0;
  uint16_t bg = flash ? STAR_TEXT_565 : STAR_BG_565;
  uint16_t fg = flash ? STAR_BG_565 : STAR_TEXT_565;

  M5Dial.Display.fillScreen(bg);
  drawBuiltinText(timeText, 120, 112, TFT_FONT_TIMER, fg, bg);
}

void renderTextOverlay() {
  if (mode == 1) renderRunningTextOverlay();
  else if (mode == 4) renderConfigTextOverlay();
  else if (mode == 3) renderAlarmTextOverlay();
  else renderSetupTextOverlay();
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
  M5Dial.Display.setTextDatum(5);
  // LVGL's 16-bit draw buffer needs byte swapping when pushed to the panel.
  M5Dial.Display.setSwapBytes(true);
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
        if (t.y > 86 && t.y < 150) {
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

  if (isAlarmMode() && alarmStart == 1) {
    auto t = M5Dial.Touch.getDetail();
    if (t.isPressed()) {
      stopAlarmAndReset();
    }
    long stopAlarmEncoderNewPos = M5Dial.Encoder.read();
    if (stopAlarmEncoderOldPos != stopAlarmEncoderNewPos) {
      stopAlarmAndReset();
    }
  }

  updateNumStrings();
  if (isRunMode() || isAlarmMode()) {
    lastActivityTime = now;
    if (isScreenDimmed) {
      M5Dial.Display.setBrightness(BRIGHTNESS_NORMAL);
      isScreenDimmed = false;
    }
  }
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
      delay(100);
    }
  } else {
    alarmStart = 0;
  }
  if (isAlarmMode()) {
    renderAlarmFastOverlay();
    return;
  }
  syncUiState();
  processLvgl();
  renderTextOverlay();
}
