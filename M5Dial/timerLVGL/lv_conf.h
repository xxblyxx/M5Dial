/*
 * Local LVGL configuration override for the timer sketch.
 *
 * LVGL v8 enables built-in fonts with LV_FONT_* macros, not LV_USE_* macros.
 * Keep this file aligned with the Arduino-installed lv_conf.h so the sketch
 * still renders text if this copy is the one that gets picked up first.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
//#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif
