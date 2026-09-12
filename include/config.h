#pragma once
#include <Arduino.h>

// ============================================================
//  DISPLAY PINS (ST7735, 128x160 native panel)
//  6-pin: SCL→18, SDA→23, RES→4, DC→2, CS→5, BL→15
// ============================================================
#define TFT_CS    5
#define TFT_RST   4
#define TFT_DC    2
#define TFT_BLK   15
#define TFT_SCLK  18
#define TFT_MOSI  23

// ============================================================
//  ORIENTATION
//  ------------------------------------------------------------
//  The ST7735 panel is *physically* 128 px wide x 160 px tall
//  (that native size is what TFT_WIDTH/TFT_HEIGHT in
//  platformio.ini describe, and must never change — it's what
//  the controller's init sequence was written for).
//
//  Landscape mode is achieved purely by ROTATING that native
//  panel with TFT_ROTATION below. TFT_eSPI swaps width/height
//  for you once you rotate — so from this point on, SCREEN_W /
//  SCREEN_H below are the *logical* (already-rotated) canvas
//  size, and EVERYTHING in the firmware (LVGL resolution, the
//  TFT_eSprite radar canvas, and every layout constant) is
//  derived from these two numbers and nothing else. That is
//  the fix for the old "right side cut off ~40%" bug: previously
//  SCREEN_W/SCREEN_H stayed at the portrait 128x160 values while
//  only the physical rotation changed, so LVGL kept thinking the
//  panel was 128 wide and never drew (160-128)=32 of the 160
//  physical columns, AND the radar sprite was allocated
//  128(w) x 160(h) and pushed onto a panel that was now
//  addressed as 160(w) x 128(h) — a shape mismatch that made
//  the driver's address window wrap/clip, which combined with
//  the old circle being centered on x=64 of what is now the
//  left ~40% of a 160-wide panel, is exactly why it looked like
//  "40% cut off on the right". Fixing SCREEN_W/SCREEN_H to the
//  true rotated size and re-deriving every constant from them
//  removes the mismatch entirely.
//
//  If the image comes up mirrored/upside-down for your specific
//  panel wiring, change TFT_ROTATION to 3 (the only other valid
//  landscape rotation for ST7735) — nothing else needs to change.
// ============================================================
#define TFT_ROTATION 3

#define SCREEN_W  160
#define SCREEN_H  128

// ============================================================
//  BUTTON PINS  (deliberately NOT GPIO0 - that pin is the boot-mode
//  strap; holding it low at power-on/reset forces the download
//  bootloader instead of your app)
// ============================================================
#define BTN_UP      25
#define BTN_DOWN    26
#define BTN_SELECT  27

#define LONG_PRESS_MS   700
#define DEBOUNCE_MS     20

// Backlight PWM runs on GPIO15 (TFT_BLK). If the screen dims INVERTED
// (25% looks brighter than 100%), flip BACKLIGHT_ACTIVE_LOW in
// src/radar_display.cpp. If brightness changes nothing at all, your
// module's BL pin is probably tied to 3.3V / not wired to GPIO15.

// ============================================================
//  RADAR SCREEN LAYOUT (160x128 landscape)
//  ------------------------------------------------------------
//  Math: the radar circle is placed in the left region of the
//  panel, leaving a fixed-width sidebar on the right for status
//  text (so nothing ever overlaps the circle). The sidebar width
//  is sized around the built-in 6px/char GLCD font so a 7-char
//  flight callsign (e.g. "IG06313") always fits with margin:
//    7 chars * 6px = 42px text + 4px left pad + 2px right margin
//    => SIDEBAR_W = 48 is the minimum that never overflows.
//
//  Everything else is derived so it can never overflow the
//  160x128 canvas:
//
//    SIDEBAR_W = 48                          (right info column)
//    RADAR_ZONE_W = SCREEN_W - SIDEBAR_W = 112
//    CX = RADAR_ZONE_W / 2 = 56              (center of the zone)
//    CY = SCREEN_H / 2 = 64                  (vertical center)
//    RADAR_R = CX - MARGIN(4) = 52           (horizontal is the
//              tighter constraint here, so it sets the radius)
//
//  Circle extents: x∈[4,108] (4px clear of the 0 and 112 edges),
//  y∈[12,116] (12px clear of the 0 and 128 edges) — fully on
//  screen with margin on every side, at every rotation.
// ============================================================
#define SIDEBAR_W     48
#define RADAR_ZONE_W  (SCREEN_W - SIDEBAR_W)   // 112
#define CX            (RADAR_ZONE_W / 2)       // 56
#define CY            (SCREEN_H / 2)           // 64
#define RADAR_R       52
#define SIDEBAR_X     (SCREEN_W - SIDEBAR_W)   // 112 (divider line x)
#define SIDEBAR_CHARS_MAX 7                    // longest label safe in the sidebar column

// ============================================================
//  COLORS (RGB565) — base/legacy palette (Theme 0 uses these)
// ============================================================
#define CLR_BG       0x0000
#define CLR_GRID     0x03E0
#define CLR_SWEEP    0x07E0
#define CLR_PLANE    0xF800
#define CLR_PLANE_HI 0xFFE0   // yellow - low altitude
#define CLR_PLANE_MD 0xFD20   // orange - mid altitude
#define CLR_TEXT     0xFFFF
#define CLR_DIM      0x7BEF
#define CLR_SEL      0x07FF   // cyan selection ring
#define CLR_ERR      0xF800

#define THEME_COUNT 3

// ============================================================
//  API PROVIDERS
// ============================================================
#define PROVIDER_OPENSKY        0
#define PROVIDER_ADSB_LOL       1
#define PROVIDER_AIRPLANES_LIVE 2
#define PROVIDER_COUNT          3

// ============================================================
//  AIRCRAFT DATA
// ============================================================
#define MAX_PLANES 25

// Trail (breadcrumb) history — cheap: 15 * 3 * 4 bytes ≈ 180 B
#define TRAIL_LEN 5

struct AircraftPoint {
    bool  valid;
    char  icaoHex[8];
    char  flight[10];
    char  aircraftType[8];
    char  desc[32];
    char  registration[12];
    char  operatorName[32];
    char  squawk[5];
    float distanceKm;
    float bearingDeg;
    int   altitudeFt;
    float speedKt;
    float trackDeg;
    bool  onGround;
};

// Icon styles selectable in settings
#define AIRCRAFT_ICON_DOT      0
#define AIRCRAFT_ICON_ARROW    1
#define AIRCRAFT_ICON_PLANE    2
#define AIRCRAFT_ICON_COUNT    3

// ============================================================
//  NVS / PREFERENCES NAMESPACES & KEYS
// ============================================================
#define NVS_NS_WIFI     "wifi"
#define NVS_NS_LOC      "loc"
#define NVS_NS_API      "api"
#define NVS_NS_DISPLAY  "display"

// ============================================================
//  FIRMWARE INFO
// ============================================================
#define FW_NAME     "FlyRadar32"
#define FW_VERSION  "2.0.0"
#define FW_BRAND    "by SKR Electronics Lab"
#define MDNS_NAME   "flyradar32"

// ============================================================
//  DEFAULTS
// ============================================================
#define DEFAULT_LAT  22.5726
#define DEFAULT_LON  88.3639
#define AP_SSID_PREFIX "FlyRadar32-"
#define AP_PASSWORD    "radar1234"
#define WEB_CONFIG_PORT 80