#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/*Memory manager settings*/
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (16U * 1024U)          /*[bytes] 16KB pool for LVGL objects, animations, and scrolling text*/
#define LV_MEM_ADR 0                       /*0: allocate automatically*/
#define LV_MEM_AUTO_DEFRAG  1

/*HAL settings*/
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/*Fonts*/
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_DEFAULT &lv_font_montserrat_12

/*Themes*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

/*Disable unneeded features*/
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0

/*Widgets*/
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_CANVAS 1
#define LV_USE_LABEL 1
#define LV_USE_LIST 1
#define LV_USE_LINE 1
#define LV_USE_BTN 1
#define LV_USE_IMG 1
#define LV_USE_METER 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1

#endif /*LV_CONF_H*/