#include "radar_display.h"
#include "storage.h"
#include "wifi_manager.h"
#include "weather_icons.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <math.h>

// ---------------------------------------------------------------------
// IMPORTANT: tft is constructed with the DEFAULT (no-arg) constructor so
// TFT_eSPI uses the native panel resolution baked into the build flags
// (TFT_WIDTH=128, TFT_HEIGHT=160 — the panel's true rotation-0 shape).
// tft.setRotation(TFT_ROTATION) then swaps width/height for us so
// tft.width()==SCREEN_W (160) and tft.height()==SCREEN_H (128). Every
// buffer we allocate below (LVGL's draw buf, LVGL's hor_res/ver_res, and
// the radar TFT_eSprite) is sized from SCREEN_W/SCREEN_H — the *same*
// rotated numbers the physical driver now reports — so nothing can ever
// disagree about the canvas shape again. See config.h for the full
// derivation/explanation of the old landscape bug.
// ---------------------------------------------------------------------
static lv_color_t getLVThemeColor();

// ---------------------------------------------------------------------
// UI design tokens â€” instrument-panel dark theme (fixed, theme-neutral).
// Three elevation tiers (screen < card < row) so surfaces visibly layer
// instead of collapsing into black; borders are light enough to read on
// every tier; text tiers keep >=4.5:1 (primary) / >=3:1 (secondary)
// contrast per tier.
// ---------------------------------------------------------------------
#define UI_SCR_BG    0x0E1116   // tier 0: deep blue-black screen
#define UI_CARD_BG   0x1C2127   // tier 1: elevated card
#define UI_ROW_BG    0x242A31   // tier 2: rows / inset lists
#define UI_BORDER    0x333A42   // hairline borders/dividers
#define UI_TEXT_MAIN 0xE8EAED   // primary text (~13:1 on card)
#define UI_TEXT_DIM  0x9AA3AD   // secondary text (~5:1 on card)

// Menu card geometry â€” single source of truth for every menu-style screen
// (scroll menu, plane list, plane detail, info screens). Card: 150x116,
// centered on the 160x128 canvas, leaving a clean 5px margin on each side.
#define CARD_W  150
#define CARD_H  116
#define LIST_W  140
#define MENU_LIST_H  86   // NB: plain LIST_H collides with FreeRTOS list.h

static TFT_eSPI tft;
static TFT_eSprite canvas = TFT_eSprite(&tft);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[SCREEN_W * 10];

static void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(disp_drv);
}

// State tracking
static bool is_radar_active = false;
static String current_menu_title = "";
static lv_obj_t* list_obj = nullptr;
static lv_obj_t* title_label = nullptr;
static String current_detail_hex = "";
static lv_obj_t* detail_cont = nullptr;

// LVGL's dark theme ships a different screen grey in every minor version.
// Pin the screen background to our own chrome palette so the UI stays
// layered dark grey no matter which lvgl version the build resolves.
static void applyScreenBg() {
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(UI_SCR_BG), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
}

// Dynamic weather screen widget pointers & two-screen state
static int wx_page = 0; // 0 = Surface & Wind, 1 = Barometer & Sky
static int wx_rendered_page = -1;
static unsigned long wx_last_flip_ms = 0;

static lv_obj_t* wx_cond_icon = nullptr;
static lv_obj_t* wx_cond_label = nullptr;
static lv_obj_t* wx_cat_badge = nullptr;
static lv_obj_t* wx_cat_label = nullptr;

// Page 0 widgets
static lv_obj_t* wx_p0_temp_main = nullptr;
static lv_obj_t* wx_p0_temp_sub = nullptr;
static lv_obj_t* wx_p0_wind_main = nullptr;
static lv_obj_t* wx_p0_wind_sub = nullptr;

// Page 1 widgets
static lv_obj_t* wx_p1_baro_main = nullptr;
static lv_obj_t* wx_p1_baro_sub = nullptr;
static lv_obj_t* wx_p1_sky_main = nullptr;
static lv_obj_t* wx_p1_sky_sub = nullptr;
static lv_obj_t* br_bar_obj = nullptr;
static lv_obj_t* br_val_label = nullptr;

static bool wx_was_valid = false;
static time_t wx_last_fetched_at = 0;

static void clearLVGL() {
    lv_obj_clean(lv_scr_act());
    applyScreenBg();
    list_obj = nullptr;
    title_label = nullptr;
    detail_cont = nullptr;
    br_bar_obj = nullptr;
    br_val_label = nullptr;
    wx_cond_icon = nullptr;
    wx_cond_label = nullptr;
    wx_cat_badge = nullptr;
    wx_cat_label = nullptr;
    wx_p0_temp_main = nullptr;
    wx_p0_temp_sub = nullptr;
    wx_p0_wind_main = nullptr;
    wx_p0_wind_sub = nullptr;
    wx_p1_baro_main = nullptr;
    wx_p1_baro_sub = nullptr;
    wx_p1_sky_main = nullptr;
    wx_p1_sky_sub = nullptr;
    wx_was_valid = false;
    wx_rendered_page = -1;
    wx_last_fetched_at = 0;
    current_menu_title = "";
    current_detail_hex = "";
}

void RadarDisplay::forceLVGLRefresh() {
    clearLVGL();
    lv_theme_t * th = lv_theme_default_init(NULL, getLVThemeColor(), getLVThemeColor(), true, &lv_font_montserrat_12);
    lv_disp_set_theme(NULL, th);
    applyScreenBg();
}

// ---------------------------------------------------------------------
// Shared card frame used by every non-radar screen: rounded elevated
// panel, accent title, accent underline. All screens share one visual
// language (same radius, padding, type scale: 14 = titles, 12 = body).
// ---------------------------------------------------------------------
static lv_obj_t* buildCard(const char* title, lv_color_t titleColor) {
    clearLVGL();

    lv_obj_t * card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_CARD_BG), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(UI_BORDER), 0);
    lv_obj_set_style_pad_all(card, 5, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * header = lv_label_create(card);
    lv_label_set_text(header, title);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(header, titleColor, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);

    // short accent underline under the title — the theme's signature detail
    lv_obj_t * rule = lv_obj_create(card);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, 24, 2);
    lv_obj_set_style_bg_color(rule, titleColor, 0);
    lv_obj_set_style_radius(rule, 1, 0);
    lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, 17);

    lv_obj_t * line = lv_line_create(card);
    static lv_point_t line_pts[] = { {0, 0}, {LIST_W, 0} };
    lv_line_set_points(line, line_pts, 2);
    lv_obj_set_style_line_color(line, lv_color_hex(UI_BORDER), 0);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 24);
    return card;
}

// ---------------------------------------------------------------------
// Theme palettes. Each is a self-contained set of RGB565 colors so the
// whole radar can be re-skinned instantly without touching draw code.
// ---------------------------------------------------------------------
struct ThemePalette {
    uint16_t grid, sweep, planeHi, planeMd, planeLo, text, dim, sel, err, accent;
    uint32_t accent_hex;
};

static const ThemePalette THEMES[THEME_COUNT] = {
    // 0: Matrix Green (pure radar green look matching connected text)
    { 0x05E0, 0x07E0, 0xFFE0, 0xFD20, 0xF800, 0xFFFF, 0x7BEF, 0x07FF, 0xF800, 0x07E0, 0x00FF00 },
    // 1: Ice Cyan (modern HUD look)
    { 0x04FF, 0x07FF, 0xFFE0, 0xFD20, 0xF81F, 0xFFFF, 0x5AEB, 0xFFE0, 0xF800, 0x07FF, 0x00FFFF },
    // 2: Amber Retro (classic amber CRT look)
    { 0x7800, 0xFD00, 0xFFE0, 0xFB00, 0xF800, 0xFFFF, 0x9A80, 0xFFFF, 0xF800, 0xFD00, 0xFFB300 },
};

static inline const ThemePalette& theme() {
    uint8_t t = Storage::settings().theme;
    if (t >= THEME_COUNT) t = 0;
    return THEMES[t];
}

static lv_color_t getLVThemeColor() {
    return lv_color_hex(theme().accent_hex);
}

static uint16_t altitudeColor(int altFt, bool onGround, const ThemePalette& th) {
    if (onGround) return 0x07FF;              // Vibrant Cyan for surface / ground traffic (never dull grey)
    if (altFt < 10000) return th.err;         // low / red - most alert-worthy
    if (altFt < 30000) return th.planeMd;     // mid / orange
    return th.sweep;                           // cruise / green (matches radar beam)
}

#define TFT_LEDC_FREQ     5000
#define TFT_LEDC_RES      8

static void applyBacklightDuty(uint8_t brightnessPercent) {
    if (brightnessPercent < 10) brightnessPercent = 10;
    if (brightnessPercent > 100) brightnessPercent = 100;
    uint32_t duty = (uint32_t)brightnessPercent * 255 / 100;
    ledcWrite(TFT_BLK, duty);
}

// Backlight is Active-HIGH on standard ST7735 breakouts: GPIO HIGH = transistor ON = LED 100% brightness.
static void initBacklight() {
    ledcAttach(TFT_BLK, TFT_LEDC_FREQ, TFT_LEDC_RES);
    gpio_set_drive_capability((gpio_num_t)TFT_BLK, GPIO_DRIVE_CAP_3); // Max drive strength (up to ~40mA)
    applyBacklightDuty(Storage::settings().brightness);
}

void RadarDisplay::setBrightness(uint8_t brightnessPercent) {
    applyBacklightDuty(brightnessPercent);
}

// Public re-assert — call from setup() after all other init is done.
void RadarDisplay::assertBacklight() {
    applyBacklightDuty(Storage::settings().brightness);
}

// ---------------------------------------------------------------------
// Breadcrumb trail â€” keeps the last few fixes for each aircraft so the
// radar can draw a fading tail behind it. Matched by ICAO hex across
// fetch cycles. Memory cost: MAX_PLANES * (TRAIL_LEN*8 + 16) â‰ˆ 1.1 KB.
// ---------------------------------------------------------------------
struct TrailSlot {
    char hex[8] = {0};
    bool used = false;
    uint8_t count = 0;
    uint8_t age = 0;      // bumped on each sample; used for oldest-steal
    uint8_t missedCycles = 0; // grace period before dropping history
    float distKm[TRAIL_LEN];
    float bearingDeg[TRAIL_LEN];
};
static TrailSlot trails[MAX_PLANES];
static uint8_t trailAgeCounter = 0;

static TrailSlot* findOrAllocTrail(const char* hex) {
    for (int i = 0; i < MAX_PLANES; i++) {
        if (trails[i].used && strncmp(trails[i].hex, hex, 7) == 0) return &trails[i];
    }
    // Take the first unused slot; if all are full, steal the oldest slot
    // (lowest age stamp) â€” NOT an arbitrary trails[0], which would corrupt
    // an unrelated aircraft's history.
    TrailSlot* victim = &trails[0];
    for (int i = 0; i < MAX_PLANES; i++) {
        if (!trails[i].used) { victim = &trails[i]; break; }
        if ((uint8_t)(trailAgeCounter - trails[i].age) > (uint8_t)(trailAgeCounter - victim->age)) victim = &trails[i];
    }
    victim->used = true;
    victim->count = 0;
    victim->missedCycles = 0;
    strncpy(victim->hex, hex, 7);
    victim->hex[7] = '\0';
    return victim;
}

static void sampleTrails(const AircraftPoint planes[], int count) {
    bool seen[MAX_PLANES] = {false};
    trailAgeCounter++;
    for (int i = 0; i < count; i++) {
        if (!planes[i].valid || planes[i].icaoHex[0] == '\0') continue;
        TrailSlot* slot = findOrAllocTrail(planes[i].icaoHex);
        slot->age = trailAgeCounter;
        slot->missedCycles = 0;
        // shift older samples back, insert newest at [0]
        for (int k = TRAIL_LEN - 1; k > 0; k--) {
            slot->distKm[k] = slot->distKm[k - 1];
            slot->bearingDeg[k] = slot->bearingDeg[k - 1];
        }
        slot->distKm[0] = planes[i].distanceKm;
        slot->bearingDeg[0] = planes[i].bearingDeg;
        if (slot->count < TRAIL_LEN) slot->count++;
        // Mark the owning index (O(n) lookup only when the slot was just stolen)
        for (int j = 0; j < MAX_PLANES; j++) if (&trails[j] == slot) seen[j] = true;
    }
    // Age out slots for aircraft that vanished (grace of 4 missed cycles ≈ 40-60s)
    for (int i = 0; i < MAX_PLANES; i++) {
        if (trails[i].used && !seen[i]) {
            trails[i].missedCycles++;
            if (trails[i].missedCycles >= 4) {
                trails[i].used = false;
                trails[i].count = 0;
            }
        }
    }
}

// ---------------------------------------------------------------------
// Rotate a local (forward, right) offset by a heading angle (deg,
// clockwise from north/up) into screen-space (dx, dy). Shared by both
// the arrow and aeroplane icon styles.
// ---------------------------------------------------------------------
static inline void rotatePt(float fwd, float right, float rad, int& dx, int& dy) {
    float s = sin(rad), c = cos(rad);
    dx = (int)roundf(fwd * s + right * c);
    dy = (int)roundf(-fwd * c + right * s);
}

static void drawAircraftIcon(int px, int py, float trackDeg, uint16_t color, uint8_t iconType) {
    if (iconType == AIRCRAFT_ICON_DOT) {
        canvas.fillCircle(px, py, 2, color);
        canvas.drawPixel(px, py, CLR_TEXT);
        return;
    }

    float rad = trackDeg * DEG_TO_RAD;
    int nx, ny, wlx, wly, wrx, wry, tx, ty;

    if (iconType == AIRCRAFT_ICON_ARROW) {
        rotatePt(5, 0, rad, nx, ny);
        rotatePt(-3, -3.0f, rad, wlx, wly);
        rotatePt(-3, 3.0f, rad, wrx, wry);
        rotatePt(-1, 0, rad, tx, ty);
        canvas.fillTriangle(px + nx, py + ny, px + wlx, py + wly, px + tx, py + ty, color);
        canvas.fillTriangle(px + nx, py + ny, px + wrx, py + wry, px + tx, py + ty, color);
        canvas.drawPixel(px, py, CLR_TEXT);
        return;
    }

    // AIRCRAFT_ICON_PLANE: Authentic FlightRadar24 commercial jet airliner silhouette
    int fx, fy, tlx, tly, trx, try_, elx, ely, erx, ery;
    rotatePt(7, 0, rad, nx, ny);            // Nose cone
    rotatePt(0, -6.0f, rad, wlx, wly);     // Left wingtip
    rotatePt(0, 6.0f, rad, wrx, wry);      // Right wingtip
    rotatePt(-1.5f, -3.0f, rad, elx, ely);  // Left engine nacelle
    rotatePt(-1.5f, 3.0f, rad, erx, ery);   // Right engine nacelle
    rotatePt(-5, -3.0f, rad, tlx, tly);    // Left tail stabilizer
    rotatePt(-5, 3.0f, rad, trx, try_);    // Right tail stabilizer
    rotatePt(-5.5f, 0, rad, tx, ty);       // Tail tip
    rotatePt(-2, 0, rad, fx, fy);          // Mid-fuselage root

    // Main swept wings
    canvas.fillTriangle(px + nx, py + ny, px + wlx, py + wly, px + fx, py + fy, color);
    canvas.fillTriangle(px + nx, py + ny, px + wrx, py + wry, px + fx, py + fy, color);
    // Swept tail wings
    canvas.fillTriangle(px + fx, py + fy, px + tlx, py + tly, px + tx, py + ty, color);
    canvas.fillTriangle(px + fx, py + fy, px + trx, py + try_, px + tx, py + ty, color);
    // Engine nacelles under wings
    canvas.fillCircle(px + elx, py + ely, 1, color);
    canvas.fillCircle(px + erx, py + ery, 1, color);
    // Fuselage spine
    canvas.drawLine(px + nx, py + ny, px + tx, py + ty, color);
    canvas.drawPixel(px, py, CLR_TEXT);
}

void RadarDisplay::begin() {
    tft.init();
    tft.setRotation(TFT_ROTATION);

    canvas.setColorDepth(16);
    canvas.createSprite(SCREEN_W, SCREEN_H);
    canvas.setTextWrap(false);

    initBacklight();

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, SCREEN_W * 10);
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_W;
    disp_drv.ver_res = SCREEN_H;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Set modern theme dynamically based on saved setting
    lv_theme_t * th = lv_theme_default_init(NULL, getLVThemeColor(), getLVThemeColor(), true, &lv_font_montserrat_12);
    lv_disp_set_theme(NULL, th);
    applyScreenBg();
}

void RadarDisplay::showBootStatus(const char* line1, const char* line2) {
    if (is_radar_active) { is_radar_active = false; clearLVGL(); }
    if (!title_label) {
        clearLVGL();

        lv_obj_t * logo = lv_label_create(lv_scr_act());
        lv_label_set_text(logo, "FlyRadar32");
        lv_obj_set_style_text_font(logo, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(logo, getLVThemeColor(), 0);
        lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 22);

        // accent rule under the logo — same signature as buildCard
        lv_obj_t * rule = lv_obj_create(lv_scr_act());
        lv_obj_remove_style_all(rule);
        lv_obj_set_size(rule, 32, 2);
        lv_obj_set_style_bg_color(rule, getLVThemeColor(), 0);
        lv_obj_set_style_radius(rule, 1, 0);
        lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, 42);

        lv_obj_t * sub = lv_label_create(lv_scr_act());
        lv_label_set_text(sub, "SKR Electronics Lab");
        lv_obj_set_style_text_color(sub, lv_color_hex(UI_TEXT_DIM), 0);
        lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 50);

        title_label = lv_label_create(lv_scr_act());
        lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(title_label, lv_color_hex(UI_TEXT_MAIN), 0);
        lv_obj_align(title_label, LV_ALIGN_BOTTOM_MID, 0, -18);
    }

    String status = String(line1);
    if (line2 && line2[0]) status += "\n" + String(line2);
    lv_label_set_text(title_label, status.c_str());
}

void RadarDisplay::showWifiSetupScreen(const String& apSsid, const String& apIp) {
    if (is_radar_active) { is_radar_active = false; }
    lv_obj_t * card = buildCard("WI-FI SETUP", getLVThemeColor());

    lv_obj_t * desc = lv_label_create(card);
    String pass = String(AP_PASSWORD);
    lv_label_set_recolor(desc, true);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
    if (pass.length() > 0) {
        lv_label_set_text_fmt(desc, "SSID: #%06x %s#\nPass: #%06x %s#\nIP: #%06x %s#\nWEB: #%06x %s.local#",
            theme().accent_hex, apSsid.c_str(), theme().accent_hex, pass.c_str(), theme().accent_hex, apIp.c_str(), theme().accent_hex, MDNS_NAME);
    } else {
        lv_label_set_text_fmt(desc, "SSID: #%06x %s#\nIP: #%06x %s#\nWEB: #%06x %s.local#",
            theme().accent_hex, apSsid.c_str(), theme().accent_hex, apIp.c_str(), theme().accent_hex, MDNS_NAME);
    }
    lv_obj_align(desc, LV_ALIGN_TOP_MID, 0, 30);
}

void RadarDisplay::showConnectedScreen(const String& staIp) {
    if (is_radar_active) { is_radar_active = false; }
    lv_obj_t * card = buildCard("CONNECTED!", getLVThemeColor());

    lv_obj_t * desc = lv_label_create(card);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_12, 0);
    lv_label_set_text_fmt(desc, "IP: %s\nWEB: %s.local", staIp.c_str(), MDNS_NAME);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(desc, LV_ALIGN_TOP_MID, 0, 32);

    lv_obj_t * hint = lv_label_create(card);
    lv_label_set_text(hint, "Starting radar...");
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_TEXT_DIM), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);
}

void RadarDisplay::renderScrollMenu(const char* title, const char* items[], int itemCount, int selectedIndex, int scrollOffset) {
    if (is_radar_active) { is_radar_active = false; }

    static int lastSelScroll = -1;
    if (current_menu_title != String(title) || list_obj == nullptr) {
        lv_obj_t * card = buildCard(title, getLVThemeColor());

        list_obj = lv_list_create(card);
        lv_obj_set_size(list_obj, LIST_W, MENU_LIST_H);
        lv_obj_align(list_obj, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(list_obj, lv_color_hex(UI_CARD_BG), 0);
        lv_obj_set_style_border_width(list_obj, 0, 0);
        lv_obj_set_style_pad_all(list_obj, 0, 0);
        lv_obj_set_scrollbar_mode(list_obj, LV_SCROLLBAR_MODE_OFF);

        for (int i = 0; i < itemCount; i++) {
            char formattedItem[36];
            snprintf(formattedItem, sizeof(formattedItem), "%s%s", (i == selectedIndex ? "> " : "  "), items[i]);
            lv_obj_t * btn = lv_list_add_btn(list_obj, NULL, formattedItem);
            lv_obj_set_style_bg_color(btn, lv_color_hex(UI_ROW_BG), 0);
            lv_obj_set_style_bg_color(btn, getLVThemeColor(), LV_STATE_FOCUSED);
            lv_obj_set_style_text_color(btn, lv_color_hex(UI_TEXT_MAIN), 0);
            lv_obj_set_style_text_color(btn, lv_color_black(), LV_STATE_FOCUSED);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(UI_BORDER), 0);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_pad_top(btn, 5, 0);
            lv_obj_set_style_pad_bottom(btn, 5, 0);
        }
        current_menu_title = String(title);
        lastSelScroll = -1;
    }

    if (list_obj) {
        for (uint32_t i = 0; i < lv_obj_get_child_cnt(list_obj); i++) {
            lv_obj_t * btn = lv_obj_get_child(list_obj, i);
            lv_obj_t * label = lv_obj_get_child(btn, 0);
            if (label) {
                char formattedItem[36];
                snprintf(formattedItem, sizeof(formattedItem), "%s%s", ((int)i == selectedIndex ? "> " : "  "), items[i]);
                lv_label_set_text(label, formattedItem);
            }
            if ((int)i == selectedIndex) {
                lv_obj_add_state(btn, LV_STATE_FOCUSED);
                if (lastSelScroll != selectedIndex) {
                    lv_obj_scroll_to_view(btn, LV_ANIM_ON);
                    lastSelScroll = selectedIndex;
                }
            } else {
                lv_obj_clear_state(btn, LV_STATE_FOCUSED);
            }
        }
    }
}

void RadarDisplay::renderSystemInfo(const String& ip, const String& wifiSsid) {
    if (is_radar_active) { is_radar_active = false; }
    if (current_menu_title != "SYSINFO") {
        lv_obj_t * card = buildCard("SYSTEM INFO", getLVThemeColor());

        // Compact single-label block — all in 12-pt font to fit without overlap.
        // Removed the blank line before brand text that caused the hint to overlap.
        lv_obj_t * info = lv_label_create(card);
        String tmLine = WifiManager::timeSynced() ? String("\nTime: ") + WifiManager::getClockDateTime() : "";
        lv_label_set_text_fmt(info,
            "%s v%s\nWiFi: %s\nIP: %s%s\nSKR Electronics Lab",
            FW_NAME, FW_VERSION, wifiSsid.c_str(), ip.c_str(), tmLine.c_str());
        lv_obj_set_style_text_font(info, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(info, lv_color_hex(UI_TEXT_MAIN), 0);
        lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 28);

        current_menu_title = "SYSINFO";
    }
}

void RadarDisplay::renderFactoryResetConfirm() {
    if (is_radar_active) { is_radar_active = false; }
    if (current_menu_title != "RESET") {
        lv_obj_t * card = buildCard("FACTORY RESET", lv_palette_main(LV_PALETTE_RED));

        lv_obj_t * info = lv_label_create(card);
        lv_label_set_text(info, "Erase ALL settings\nand restart?");
        lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(info, lv_color_hex(UI_TEXT_MAIN), 0);
        lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 34);

        lv_obj_t * sel = lv_label_create(card);
        lv_label_set_text(sel, "SELECT = confirm");
        lv_obj_set_style_text_color(sel, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_align(sel, LV_ALIGN_BOTTOM_MID, 0, -18);

        lv_obj_t * cancel = lv_label_create(card);
        lv_label_set_text(cancel, "UP/DOWN = cancel");
        lv_obj_set_style_text_color(cancel, lv_color_hex(UI_TEXT_DIM), 0);
        lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, 0, -2);

        current_menu_title = "RESET";
    }
}

void RadarDisplay::renderBrightnessMenu(uint8_t brightnessPercent) {
    if (is_radar_active) { is_radar_active = false; }
    static uint8_t lastRenderedVal = 255;

    if (current_menu_title != "BRIGHTNESS" || br_bar_obj == nullptr) {
        lv_obj_t * card = buildCard("BACKLIGHT", getLVThemeColor());

        br_bar_obj = lv_bar_create(card);
        lv_obj_set_size(br_bar_obj, CARD_W - 28, 14);
        lv_obj_align(br_bar_obj, LV_ALIGN_TOP_MID, 0, 36);
        lv_bar_set_range(br_bar_obj, 10, 100);
        lv_obj_set_style_bg_color(br_bar_obj, lv_color_hex(UI_ROW_BG), 0);
        lv_obj_set_style_bg_color(br_bar_obj, getLVThemeColor(), LV_PART_INDICATOR);
        lv_obj_set_style_radius(br_bar_obj, 3, 0);
        lv_obj_set_style_radius(br_bar_obj, 3, LV_PART_INDICATOR);

        br_val_label = lv_label_create(card);
        lv_obj_set_style_text_font(br_val_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(br_val_label, lv_color_hex(UI_TEXT_MAIN), 0);
        lv_obj_align(br_val_label, LV_ALIGN_TOP_MID, 0, 56);

        lv_obj_t * sel_hint = lv_label_create(card);
        lv_label_set_text(sel_hint, "UP/DOWN = adjust");
        lv_obj_set_style_text_font(sel_hint, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(sel_hint, getLVThemeColor(), 0);
        lv_obj_align(sel_hint, LV_ALIGN_BOTTOM_MID, 0, -16);

        lv_obj_t * back_hint = lv_label_create(card);
        lv_label_set_text(back_hint, "SELECT = save & exit");
        lv_obj_set_style_text_font(back_hint, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(back_hint, lv_color_hex(UI_TEXT_DIM), 0);
        lv_obj_align(back_hint, LV_ALIGN_BOTTOM_MID, 0, -2);

        current_menu_title = "BRIGHTNESS";
        lastRenderedVal = 255;
    }

    if (lastRenderedVal != brightnessPercent && br_bar_obj != nullptr && br_val_label != nullptr) {
        lv_bar_set_value(br_bar_obj, brightnessPercent, LV_ANIM_OFF);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d %%", brightnessPercent);
        lv_label_set_text(br_val_label, buf);
        lastRenderedVal = brightnessPercent;
    }
}

// WMO weather code -> short human text (only the common ranges)
static const char* wmoText(int code) {
    if (code == 0) return "Clear sky";
    if (code <= 3) return "Partly cloudy";
    if (code <= 48) return "Fog";
    if (code <= 57) return "Drizzle";
    if (code <= 67) return "Rain";
    if (code <= 77) return "Snow";
    if (code <= 82) return "Rain showers";
    if (code <= 86) return "Snow showers";
    if (code == 95) return "Thunderstorm";
    if (code <= 99) return "Thunderstorm + hail";
    return "Unknown";
}

// 16-point compass text for wind direction
static const char* compass16(int deg) {
    static const char* pts[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE",
                                "S","SSW","SW","WSW","W","WNW","NW","NNW"};
    return pts[(deg % 360) / 23];
}

static const lv_img_dsc_t* getWmoIcon(int code) {
    if (code == 0 || code == 1) return &img_wx_sun;
    if (code == 2 || code == 3 || code == 45 || code == 48) return &img_wx_cloud;
    if (code >= 95) return &img_wx_storm;
    return &img_wx_rain;
}

static const char* getFlightCategory(int wmoCode, int cloudPct, float precipMm) {
    if (wmoCode >= 95 || precipMm > 5.0f) return "LIFR";
    if (cloudPct >= 80 || precipMm > 0.5f) return "IFR";
    if (cloudPct >= 50 || precipMm > 0.0f) return "MVFR";
    return "VFR";
}

static lv_color_t getFlightCatColor(const char* cat) {
    if (strcmp(cat, "LIFR") == 0) return lv_color_hex(0xFF1744); // neon red
    if (strcmp(cat, "IFR") == 0)  return lv_color_hex(0xFFB300); // aviation amber
    if (strcmp(cat, "MVFR") == 0) return lv_color_hex(0x00E5FF); // cyan
    return lv_color_hex(0x00E676); // neon green for VFR
}

static const char* getCloudCoverageCode(int pct) {
    if (pct < 15) return "SKC 0/8";
    if (pct < 35) return "FEW 2/8";
    if (pct < 65) return "SCT 4/8";
    if (pct < 85) return "BKN 6/8";
    return "OVC 8/8";
}

void RadarDisplay::toggleWeatherPage() {
    wx_page = (wx_page + 1) % 2;
    wx_last_flip_ms = millis();
    wx_rendered_page = -1;
}

void RadarDisplay::renderWeatherScreen() {
    if (is_radar_active) { is_radar_active = false; clearLVGL(); }

    // Auto-transition between Page 0 and Page 1 every 5.5 seconds
    if (millis() - wx_last_flip_ms >= 5500) {
        wx_page = (wx_page + 1) % 2;
        wx_last_flip_ms = millis();
        wx_rendered_page = -1;
    }

    ApiProviders::Weather w = ApiProviders::getWeather();

    // Rebuild frame if screen was entered, page changed, or valid data arrived
    if (current_menu_title != "WEATHER" || wx_rendered_page != wx_page || wx_was_valid != w.valid) {
        clearLVGL();
        current_menu_title = "WEATHER";
        wx_rendered_page = wx_page;
        wx_was_valid = w.valid;

        // Dedicated Aerodrome Weather Frame (154 x 122 px, centered on 160x128 display)
        lv_obj_t* card = lv_obj_create(lv_scr_act());
        lv_obj_set_size(card, 154, 122);
        lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(UI_CARD_BG), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(UI_BORDER), 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_set_style_radius(card, 6, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        // --- 1. Header Bar ---
        const char* cat = w.valid ? getFlightCategory(w.wmoCode, w.cloudPct, w.precipMm) : "VFR";
        lv_color_t catCol = getFlightCatColor(cat);

        if (wx_page == 0) {
            // Page 0 Header: [Icon] Condition Text (Circular Scroll!) & [Flight Cat Badge]
            wx_cond_icon = lv_img_create(card);
            lv_img_set_src(wx_cond_icon, getWmoIcon(w.valid ? w.wmoCode : 0));
            lv_obj_set_style_img_recolor(wx_cond_icon, getLVThemeColor(), 0);
            lv_obj_set_style_img_recolor_opa(wx_cond_icon, LV_OPA_COVER, 0);
            lv_obj_set_pos(wx_cond_icon, 5, 3);

            wx_cond_label = lv_label_create(card);
            lv_label_set_text(wx_cond_label, w.valid ? wmoText(w.wmoCode) : "ACQUIRING...");
            lv_obj_set_style_text_font(wx_cond_label, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(wx_cond_label, lv_color_hex(UI_TEXT_MAIN), 0);
            lv_obj_set_pos(wx_cond_label, 24, 3);
            lv_obj_set_width(wx_cond_label, 84);
            lv_label_set_long_mode(wx_cond_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

            wx_cat_badge = lv_obj_create(card);
            lv_obj_set_size(wx_cat_badge, 36, 16);
            lv_obj_set_pos(wx_cat_badge, 112, 2);
            lv_obj_set_style_bg_color(wx_cat_badge, lv_color_hex(UI_ROW_BG), 0);
            lv_obj_set_style_border_width(wx_cat_badge, 1, 0);
            lv_obj_set_style_border_color(wx_cat_badge, catCol, 0);
            lv_obj_set_style_radius(wx_cat_badge, 4, 0);
            lv_obj_set_style_pad_all(wx_cat_badge, 0, 0);
            lv_obj_clear_flag(wx_cat_badge, LV_OBJ_FLAG_SCROLLABLE);

            wx_cat_label = lv_label_create(wx_cat_badge);
            lv_label_set_text(wx_cat_label, cat);
            lv_obj_set_style_text_font(wx_cat_label, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(wx_cat_label, catCol, 0);
            lv_obj_align(wx_cat_label, LV_ALIGN_CENTER, 0, 0);
        } else {
            // Page 1 Header: [Baro Icon] ATMOSPHERE & [QNH Badge]
            wx_cond_icon = lv_img_create(card);
            lv_img_set_src(wx_cond_icon, &img_wx_baro);
            lv_obj_set_style_img_recolor(wx_cond_icon, getLVThemeColor(), 0);
            lv_obj_set_style_img_recolor_opa(wx_cond_icon, LV_OPA_COVER, 0);
            lv_obj_set_pos(wx_cond_icon, 5, 3);

            wx_cond_label = lv_label_create(card);
            lv_label_set_text(wx_cond_label, "ATMOSPHERE");
            lv_obj_set_style_text_font(wx_cond_label, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(wx_cond_label, lv_color_hex(UI_TEXT_MAIN), 0);
            lv_obj_set_pos(wx_cond_label, 24, 3);
            lv_obj_set_width(wx_cond_label, 84);
            lv_label_set_long_mode(wx_cond_label, LV_LABEL_LONG_CLIP);

            wx_cat_badge = lv_obj_create(card);
            lv_obj_set_size(wx_cat_badge, 36, 16);
            lv_obj_set_pos(wx_cat_badge, 112, 2);
            lv_obj_set_style_bg_color(wx_cat_badge, lv_color_hex(UI_ROW_BG), 0);
            lv_obj_set_style_border_width(wx_cat_badge, 1, 0);
            lv_obj_set_style_border_color(wx_cat_badge, getLVThemeColor(), 0);
            lv_obj_set_style_radius(wx_cat_badge, 4, 0);
            lv_obj_set_style_pad_all(wx_cat_badge, 0, 0);
            lv_obj_clear_flag(wx_cat_badge, LV_OBJ_FLAG_SCROLLABLE);

            wx_cat_label = lv_label_create(wx_cat_badge);
            lv_label_set_text(wx_cat_label, "QNH");
            lv_obj_set_style_text_font(wx_cat_label, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(wx_cat_label, getLVThemeColor(), 0);
            lv_obj_align(wx_cat_label, LV_ALIGN_CENTER, 0, 0);
        }

        // Hairline separator
        lv_obj_t* line = lv_line_create(card);
        static lv_point_t line_pts[] = { {4, 20}, {150, 20} };
        lv_line_set_points(line, line_pts, 2);
        lv_obj_set_style_line_color(line, lv_color_hex(UI_BORDER), 0);
        lv_obj_set_style_line_width(line, 1, 0);

        if (wx_page == 0) {
            // =========================================================
            // === PAGE 0: SURFACE METAR (Full-width cards: 146 x 38 px)
            // =========================================================

            // --- Tile 1: Surface Temperature & Comfort ---
            lv_obj_t* t1 = lv_obj_create(card);
            lv_obj_set_size(t1, 146, 38);
            lv_obj_set_pos(t1, 4, 23);
            lv_obj_set_style_bg_color(t1, lv_color_hex(UI_ROW_BG), 0);
            lv_obj_set_style_border_width(t1, 1, 0);
            lv_obj_set_style_border_color(t1, lv_color_hex(UI_BORDER), 0);
            lv_obj_set_style_radius(t1, 4, 0);
            lv_obj_set_style_pad_all(t1, 0, 0);
            lv_obj_clear_flag(t1, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* i1 = lv_img_create(t1);
            lv_img_set_src(i1, &img_wx_temp);
            lv_obj_set_style_img_recolor(i1, lv_color_hex(0xFFA726), 0);
            lv_obj_set_style_img_recolor_opa(i1, LV_OPA_COVER, 0);
            lv_obj_set_pos(i1, 4, 11);

            wx_p0_temp_main = lv_label_create(t1);
            char tBuf[32];
            if (w.valid) snprintf(tBuf, sizeof(tBuf), "%.1f C  (fls %.0f C)", w.tempC, w.feelsC);
            else snprintf(tBuf, sizeof(tBuf), "-- C  (fls --)");
            lv_label_set_text(wx_p0_temp_main, tBuf);
            lv_obj_set_style_text_font(wx_p0_temp_main, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(wx_p0_temp_main, lv_color_hex(UI_TEXT_MAIN), 0);
            lv_obj_set_pos(wx_p0_temp_main, 24, 3);
            lv_obj_set_width(wx_p0_temp_main, 118);
            lv_label_set_long_mode(wx_p0_temp_main, LV_LABEL_LONG_CLIP);

            wx_p0_temp_sub = lv_label_create(t1);
            char dBuf[32];
            if (w.valid) {
                int dew = (int)(w.tempC - ((100 - w.humidityPct) / 5.0f) + 0.5f);
                snprintf(dBuf, sizeof(dBuf), "Dew: %d C  |  %d%% RH", dew, w.humidityPct);
            } else {
                snprintf(dBuf, sizeof(dBuf), "Dew: --  |  --%% RH");
            }
            lv_label_set_text(wx_p0_temp_sub, dBuf);
            lv_obj_set_style_text_font(wx_p0_temp_sub, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(wx_p0_temp_sub, lv_color_hex(UI_TEXT_DIM), 0);
            lv_obj_set_pos(wx_p0_temp_sub, 24, 21);
            lv_obj_set_width(wx_p0_temp_sub, 118);
            lv_label_set_long_mode(wx_p0_temp_sub, LV_LABEL_LONG_CLIP);

            // --- Tile 2: Surface Wind Dynamics ---
            lv_obj_t* t2 = lv_obj_create(card);
            lv_obj_set_size(t2, 146, 38);
            lv_obj_set_pos(t2, 4, 64);
            lv_obj_set_style_bg_color(t2, lv_color_hex(UI_ROW_BG), 0);
            lv_obj_set_style_border_width(t2, 1, 0);
            lv_obj_set_style_border_color(t2, lv_color_hex(UI_BORDER), 0);
            lv_obj_set_style_radius(t2, 4, 0);
            lv_obj_set_style_pad_all(t2, 0, 0);
            lv_obj_clear_flag(t2, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* i2 = lv_img_create(t2);
            lv_img_set_src(i2, &img_wx_wind);
            lv_obj_set_style_img_recolor(i2, lv_color_hex(0x00E5FF), 0);
            lv_obj_set_style_img_recolor_opa(i2, LV_OPA_COVER, 0);
            lv_obj_set_pos(i2, 4, 11);

            wx_p0_wind_main = lv_label_create(t2);
            char wBuf[32];
            if (w.valid) {
                float kmh = w.windKt * 1.852f;
                snprintf(wBuf, sizeof(wBuf), "%s %d kt (%.0f km/h)", compass16(w.windDirDeg), (int)w.windKt, kmh);
            } else {
                snprintf(wBuf, sizeof(wBuf), "-- kt");
            }
            lv_label_set_text(wx_p0_wind_main, wBuf);
            lv_obj_set_style_text_font(wx_p0_wind_main, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(wx_p0_wind_main, getLVThemeColor(), 0);
            lv_obj_set_pos(wx_p0_wind_main, 24, 3);
            lv_obj_set_width(wx_p0_wind_main, 118);
            lv_label_set_long_mode(wx_p0_wind_main, LV_LABEL_LONG_CLIP);

            wx_p0_wind_sub = lv_label_create(t2);
            char gBuf[32];
            if (w.valid) snprintf(gBuf, sizeof(gBuf), "Gusts: %d kt | Hdg: %d*", (int)w.gustKt, w.windDirDeg);
            else snprintf(gBuf, sizeof(gBuf), "Gusts: -- | Hdg: --*");
            lv_label_set_text(wx_p0_wind_sub, gBuf);
            lv_obj_set_style_text_font(wx_p0_wind_sub, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(wx_p0_wind_sub, lv_color_hex(UI_TEXT_DIM), 0);
            lv_obj_set_pos(wx_p0_wind_sub, 24, 21);
            lv_obj_set_width(wx_p0_wind_sub, 118);
            lv_label_set_long_mode(wx_p0_wind_sub, LV_LABEL_LONG_CLIP);

            // --- Footer: Page Indicator + freshness ---
            lv_obj_t* footer = lv_label_create(card);
            char footerBuf[40];
            if (w.valid && w.fetchedAt > 0) {
                time_t now = time(nullptr);
                int ageMin = (int)((now - w.fetchedAt) / 60);
                if (ageMin < 1) snprintf(footerBuf, sizeof(footerBuf), "1/2  SURFACE  just now");
                else           snprintf(footerBuf, sizeof(footerBuf), "1/2  SURFACE  %dm ago", ageMin);
            } else {
                snprintf(footerBuf, sizeof(footerBuf), "PAGE 1/2   SURFACE & WIND");
            }
            lv_label_set_text(footer, footerBuf);
            lv_obj_set_style_text_font(footer, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(footer, lv_color_hex(UI_TEXT_DIM), 0);
            lv_obj_set_pos(footer, 8, 106);

        } else {
            // =========================================================
            // === PAGE 1: ATMOSPHERE & SKY (Full-width cards: 146 x 38 px)
            // =========================================================

            // --- Tile 1: Barometric Pressure & Altimeter Setting ---
            lv_obj_t* t1 = lv_obj_create(card);
            lv_obj_set_size(t1, 146, 38);
            lv_obj_set_pos(t1, 4, 23);
            lv_obj_set_style_bg_color(t1, lv_color_hex(UI_ROW_BG), 0);
            lv_obj_set_style_border_width(t1, 1, 0);
            lv_obj_set_style_border_color(t1, lv_color_hex(UI_BORDER), 0);
            lv_obj_set_style_radius(t1, 4, 0);
            lv_obj_set_style_pad_all(t1, 0, 0);
            lv_obj_clear_flag(t1, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* i1 = lv_img_create(t1);
            lv_img_set_src(i1, &img_wx_baro);
            lv_obj_set_style_img_recolor(i1, getLVThemeColor(), 0);
            lv_obj_set_style_img_recolor_opa(i1, LV_OPA_COVER, 0);
            lv_obj_set_pos(i1, 4, 11);

            wx_p1_baro_main = lv_label_create(t1);
            char qBuf[32];
            if (w.valid) snprintf(qBuf, sizeof(qBuf), "%d hPa  (QNH)", w.pressureHpa);
            else snprintf(qBuf, sizeof(qBuf), "-- hPa  (QNH)");
            lv_label_set_text(wx_p1_baro_main, qBuf);
            lv_obj_set_style_text_font(wx_p1_baro_main, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(wx_p1_baro_main, lv_color_hex(UI_TEXT_MAIN), 0);
            lv_obj_set_pos(wx_p1_baro_main, 24, 3);
            lv_obj_set_width(wx_p1_baro_main, 118);
            lv_label_set_long_mode(wx_p1_baro_main, LV_LABEL_LONG_CLIP);

            wx_p1_baro_sub = lv_label_create(t1);
            char inhgBuf[32];
            if (w.valid) {
                float inhg = w.pressureHpa * 0.02953f;
                snprintf(inhgBuf, sizeof(inhgBuf), "%.2f inHg | MSL Altimeter", inhg);
            } else {
                snprintf(inhgBuf, sizeof(inhgBuf), "-- inHg | Altimeter");
            }
            lv_label_set_text(wx_p1_baro_sub, inhgBuf);
            lv_obj_set_style_text_font(wx_p1_baro_sub, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(wx_p1_baro_sub, lv_color_hex(UI_TEXT_DIM), 0);
            lv_obj_set_pos(wx_p1_baro_sub, 24, 21);
            lv_obj_set_width(wx_p1_baro_sub, 118);
            lv_label_set_long_mode(wx_p1_baro_sub, LV_LABEL_LONG_CLIP);

            // --- Tile 2: Sky Cover & Precipitation ---
            lv_obj_t* t2 = lv_obj_create(card);
            lv_obj_set_size(t2, 146, 38);
            lv_obj_set_pos(t2, 4, 64);
            lv_obj_set_style_bg_color(t2, lv_color_hex(UI_ROW_BG), 0);
            lv_obj_set_style_border_width(t2, 1, 0);
            lv_obj_set_style_border_color(t2, lv_color_hex(UI_BORDER), 0);
            lv_obj_set_style_radius(t2, 4, 0);
            lv_obj_set_style_pad_all(t2, 0, 0);
            lv_obj_clear_flag(t2, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* i2 = lv_img_create(t2);
            lv_img_set_src(i2, (w.valid && w.precipMm > 0.05f) ? &img_wx_rain : &img_wx_cloud);
            lv_obj_set_style_img_recolor(i2, (w.valid && w.precipMm > 0.05f) ? lv_color_hex(0x42A5F5) : lv_color_hex(0xB0BEC5), 0);
            lv_obj_set_style_img_recolor_opa(i2, LV_OPA_COVER, 0);
            lv_obj_set_pos(i2, 4, 11);

            wx_p1_sky_main = lv_label_create(t2);
            char rBuf[32];
            if (w.valid) snprintf(rBuf, sizeof(rBuf), "Precip: %.1f mm/h", w.precipMm);
            else snprintf(rBuf, sizeof(rBuf), "Precip: 0.0 mm/h");
            lv_label_set_text(wx_p1_sky_main, rBuf);
            lv_obj_set_style_text_font(wx_p1_sky_main, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(wx_p1_sky_main, lv_color_hex(UI_TEXT_MAIN), 0);
            lv_obj_set_pos(wx_p1_sky_main, 24, 3);
            lv_obj_set_width(wx_p1_sky_main, 118);
            lv_label_set_long_mode(wx_p1_sky_main, LV_LABEL_LONG_CLIP);

            wx_p1_sky_sub = lv_label_create(t2);
            char cBuf[40];
            if (w.valid) snprintf(cBuf, sizeof(cBuf), "Sky: %d%% cld  (%s)", w.cloudPct, getCloudCoverageCode(w.cloudPct));
            else snprintf(cBuf, sizeof(cBuf), "Sky: --%% cld  (SKC 0/8)");
            lv_label_set_text(wx_p1_sky_sub, cBuf);
            lv_obj_set_style_text_font(wx_p1_sky_sub, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(wx_p1_sky_sub, lv_color_hex(UI_TEXT_DIM), 0);
            lv_obj_set_pos(wx_p1_sky_sub, 24, 21);
            lv_obj_set_width(wx_p1_sky_sub, 118);
            lv_label_set_long_mode(wx_p1_sky_sub, LV_LABEL_LONG_CLIP);

            // --- Footer: Page Indicator + freshness ---
            lv_obj_t* footer = lv_label_create(card);
            char footerBuf[40];
            if (w.valid && w.fetchedAt > 0) {
                time_t now = time(nullptr);
                int ageMin = (int)((now - w.fetchedAt) / 60);
                if (ageMin < 1) snprintf(footerBuf, sizeof(footerBuf), "2/2  ATMOS   just now");
                else           snprintf(footerBuf, sizeof(footerBuf), "2/2  ATMOS   %dm ago", ageMin);
            } else {
                snprintf(footerBuf, sizeof(footerBuf), "PAGE 2/2   ATMOSPHERE & SKY");
            }
            lv_label_set_text(footer, footerBuf);
            lv_obj_set_style_text_font(footer, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(footer, lv_color_hex(UI_TEXT_DIM), 0);
            lv_obj_set_pos(footer, 8, 106);
        }

    } else if (w.valid && w.fetchedAt != wx_last_fetched_at) {
        wx_last_fetched_at = w.fetchedAt;
        // Dynamic in-place updates ONLY when fresh weather data arrives
        if (wx_page == 0) {
            if (wx_cond_icon) lv_img_set_src(wx_cond_icon, getWmoIcon(w.wmoCode));
            if (wx_cond_label) lv_label_set_text(wx_cond_label, wmoText(w.wmoCode));

            const char* cat = getFlightCategory(w.wmoCode, w.cloudPct, w.precipMm);
            lv_color_t catCol = getFlightCatColor(cat);
            if (wx_cat_badge) lv_obj_set_style_border_color(wx_cat_badge, catCol, 0);
            if (wx_cat_label) {
                lv_label_set_text(wx_cat_label, cat);
                lv_obj_set_style_text_color(wx_cat_label, catCol, 0);
            }

            if (wx_p0_temp_main) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f C  (fls %.0f C)", w.tempC, w.feelsC);
                lv_label_set_text(wx_p0_temp_main, buf);
            }
            if (wx_p0_temp_sub) {
                char buf[32];
                int dew = (int)(w.tempC - ((100 - w.humidityPct) / 5.0f) + 0.5f);
                snprintf(buf, sizeof(buf), "Dew: %d C  |  %d%% RH", dew, w.humidityPct);
                lv_label_set_text(wx_p0_temp_sub, buf);
            }
            if (wx_p0_wind_main) {
                char buf[32];
                float kmh = w.windKt * 1.852f;
                snprintf(buf, sizeof(buf), "%s %d kt (%.0f km/h)", compass16(w.windDirDeg), (int)w.windKt, kmh);
                lv_label_set_text(wx_p0_wind_main, buf);
            }
            if (wx_p0_wind_sub) {
                char buf[32];
                snprintf(buf, sizeof(buf), "Gusts: %d kt | Hdg: %d*", (int)w.gustKt, w.windDirDeg);
                lv_label_set_text(wx_p0_wind_sub, buf);
            }
        } else {
            if (wx_p1_baro_main) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d hPa  (QNH)", w.pressureHpa);
                lv_label_set_text(wx_p1_baro_main, buf);
            }
            if (wx_p1_baro_sub) {
                char buf[32];
                float inhg = w.pressureHpa * 0.02953f;
                snprintf(buf, sizeof(buf), "%.2f inHg | MSL Altimeter", inhg);
                lv_label_set_text(wx_p1_baro_sub, buf);
            }
            if (wx_p1_sky_main) {
                char buf[32];
                snprintf(buf, sizeof(buf), "Precip: %.1f mm/h", w.precipMm);
                lv_label_set_text(wx_p1_sky_main, buf);
            }
            if (wx_p1_sky_sub) {
                char buf[40];
                snprintf(buf, sizeof(buf), "Sky: %d%% cld  (%s)", w.cloudPct, getCloudCoverageCode(w.cloudPct));
                lv_label_set_text(wx_p1_sky_sub, buf);
            }
        }
    }
}

// Display callsign, falling back to the ICAO hex when the flight field
// is empty/blank (some providers send spaces).
static String fNameSafe(const AircraftPoint& p) {
    String f = String(p.flight);
    f.trim();
    if (f.length() < 2) f = String(p.icaoHex);
    return f;
}

void RadarDisplay::renderPlaneDetail(const AircraftPoint& p, int scrollY) {
    if (is_radar_active) { is_radar_active = false; clearLVGL(); }

    // Rebuild whenever the aircraft's *data* changes too, not just when the
    // ICAO hex changes — otherwise altitude/speed/distance stay frozen at
    // whatever they were when the screen was first drawn.
    static uint32_t builtSig = 0;
    uint32_t sig = ((uint32_t)p.altitudeFt * 31u) ^ ((uint32_t)(p.speedKt * 10.0f) * 7u) ^
                   ((uint32_t)(p.distanceKm * 10.0f) << 8) ^ ((uint32_t)p.trackDeg << 16);

    String dataVals[12] = {
        String(p.icaoHex),
        String(p.registration),
        String(p.operatorName),
        String(p.aircraftType),
        String(p.desc),
        String(p.altitudeFt) + " ft",
        String((int)p.speedKt) + " kt",
        String((int)p.trackDeg) + " deg",
        String(p.squawk),
        String((int)p.distanceKm) + " km",
        String((int)p.bearingDeg) + " deg",
        WifiManager::timeSynced() ? WifiManager::getClockTime() : "--:--"
    };

    if (current_detail_hex != String(p.icaoHex) || detail_cont == nullptr) {
        builtSig = sig;
        lv_obj_t * card = buildCard(fNameSafe(p).c_str(), getLVThemeColor());
        detail_cont = lv_obj_create(card);
        lv_obj_set_size(detail_cont, LIST_W, MENU_LIST_H - 12);
        lv_obj_align(detail_cont, LV_ALIGN_TOP_MID, 0, 26);
        lv_obj_set_style_bg_color(detail_cont, lv_color_hex(UI_ROW_BG), 0);
        lv_obj_set_style_radius(detail_cont, 4, 0);
        lv_obj_set_style_border_width(detail_cont, 0, 0);
        lv_obj_set_style_pad_all(detail_cont, 2, 0);
        lv_obj_set_scrollbar_mode(detail_cont, LV_SCROLLBAR_MODE_OFF);

        static const char* titles[12] = {
            "ICAO:", "Reg:", "Op:", "Type:", "Desc:", "Alt:", "Spd:", "Trk:", "Sqk:", "Dst:", "Brg:", "Upd:"
        };
        uint32_t thx = theme().accent_hex;
        int y = 0;
        for (int i = 0; i < 12; i++) {
            lv_obj_t* lblTitle = lv_label_create(detail_cont);
            lv_label_set_text(lblTitle, titles[i]);
            lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(lblTitle, lv_color_hex(UI_TEXT_DIM), 0);
            lv_obj_set_pos(lblTitle, 0, y);

            lv_obj_t* lblData = lv_label_create(detail_cont);
            lv_label_set_text(lblData, dataVals[i].c_str());
            lv_obj_set_style_text_font(lblData, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(lblData, lv_color_hex(thx), 0);
            lv_obj_set_width(lblData, LIST_W - 35);
            lv_obj_set_pos(lblData, 35, y);
            lv_label_set_long_mode(lblData, LV_LABEL_LONG_SCROLL_CIRCULAR);
            y += 16;
        }
        current_detail_hex = String(p.icaoHex);
    } else if (sig != builtSig) {
        builtSig = sig;
        for (int i = 0; i < 12; i++) {
            lv_obj_t* lblData = lv_obj_get_child(detail_cont, 2 * i + 1);
            if (lblData) {
                lv_label_set_text(lblData, dataVals[i].c_str());
            }
        }
    }

    // Apply manual vertical scroll
    if (detail_cont) {
        lv_obj_scroll_to_y(detail_cont, scrollY, LV_ANIM_OFF);
    }
}

void RadarDisplay::renderPlaneList(const AircraftPoint planes[], int count, int selectedIndex) {
    if (is_radar_active) { is_radar_active = false; }

    // Rebuild when the aircraft set changes (cheap hex-signature compare) —
    // previously the list was built once and never refreshed, so rows went
    // stale while the selection index tracked the *new* data.
    static char listSig[MAX_PLANES * 7 + 1] = "";
    char sig[MAX_PLANES * 7 + 1]; sig[0] = '\0';
    for (int i = 0; i < count; i++) strncat(sig, planes[i].icaoHex, sizeof(sig) - strlen(sig) - 1);

    static int lastSelScroll = -1;
    if (current_menu_title != "PLANES" || list_obj == nullptr || strcmp(sig, listSig) != 0) {
        strncpy(listSig, sig, sizeof(listSig) - 1); listSig[sizeof(listSig) - 1] = '\0';
        lv_obj_t * card = buildCard("AIRCRAFT IN RANGE", lv_palette_main(LV_PALETTE_GREEN));

        list_obj = lv_list_create(card);
        lv_obj_set_size(list_obj, LIST_W, MENU_LIST_H);
        lv_obj_align(list_obj, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(list_obj, lv_color_hex(UI_CARD_BG), 0);
        lv_obj_set_style_border_width(list_obj, 0, 0);
        lv_obj_set_style_pad_all(list_obj, 0, 0);
        lv_obj_set_scrollbar_mode(list_obj, LV_SCROLLBAR_MODE_OFF);

        if (count == 0) {
            lv_obj_t * empty = lv_label_create(card);
            lv_label_set_text(empty, "No aircraft in range yet");
            lv_obj_set_style_text_color(empty, lv_color_hex(UI_TEXT_DIM), 0);
            lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
        }

        for (int i = 0; i < count; i++) {
            String name = planes[i].flight[0] ? String(planes[i].flight) : String(planes[i].icaoHex);
            String label = name + "  " + String(planes[i].altitudeFt) + "ft";
            lv_obj_t * btn = lv_list_add_btn(list_obj, NULL, label.c_str());
            lv_obj_set_style_bg_color(btn, lv_color_hex(UI_ROW_BG), 0);
            lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREEN), LV_STATE_FOCUSED);
            lv_obj_set_style_text_color(btn, lv_color_hex(UI_TEXT_MAIN), 0);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_pad_top(btn, 5, 0);
            lv_obj_set_style_pad_bottom(btn, 5, 0);
        }
        current_menu_title = "PLANES";
        lastSelScroll = -1;
    }

    if (list_obj && count > 0) {
        lv_obj_t * btn = lv_obj_get_child(list_obj, selectedIndex);
        if (btn) {
            for (uint32_t i = 0; i < lv_obj_get_child_cnt(list_obj); i++) lv_obj_clear_state(lv_obj_get_child(list_obj, i), LV_STATE_FOCUSED);
            lv_obj_add_state(btn, LV_STATE_FOCUSED);
            if (lastSelScroll != selectedIndex) {
                lv_obj_scroll_to_view(btn, LV_ANIM_ON);
                lastSelScroll = selectedIndex;
            }
        }
    }
}

// Public hook: main loop calls this once per new fetch (keyed on the fetch
// success timestamp) so trails advance even while a menu/detail screen is up.
void RadarDisplay::sampleTrailHistory(const AircraftPoint planes[], int count) {
    static unsigned long lastSampledSuccessMs = 0;
    ApiProviders::Status st = ApiProviders::getStatus();
    if (st.lastSuccessMs != 0 && st.lastSuccessMs != lastSampledSuccessMs) {
        sampleTrails(planes, count);
        lastSampledSuccessMs = st.lastSuccessMs;
    }
}

// ---------------------------------------------------------------------
// RADAR DRAWING (Hyper-optimized Native drawing embedded over LVGL)
// Landscape layout: circular scope on the left (x: 0..SIDEBAR_X), a
// dedicated status sidebar on the right (x: SIDEBAR_X..SCREEN_W). The
// sidebar means status text NEVER overlaps the scope or aircraft, no
// matter how many aircraft are on screen or how long their callsigns are.
// -------------------------------------------------------------------------------------
void RadarDisplay::renderRadar(const AircraftPoint planes[], int count, int sweepAngle,
                                int selectedIndex, float rangeKm, uint8_t labelsMode,
                                bool showSweepAnim, const ApiProviders::Status& status) {
    if (!is_radar_active) {
        clearLVGL(); // Nuke the UI
        is_radar_active = true;
    }

    const ThemePalette& th = theme();
    const AppSettings& s = Storage::settings();

    // NOTE: trail *recording* now happens in the main loop via
    // sampleTrailHistory() so history keeps advancing while menus are open.
    // This function only *draws* what's already recorded.

    canvas.fillScreen(CLR_BG);
    canvas.setTextWrap(false);

    // --- Radar graticule ---
    canvas.drawCircle(CX, CY, RADAR_R, th.grid);
    canvas.drawCircle(CX, CY, (int)(RADAR_R * 0.66f), th.grid);
    canvas.drawCircle(CX, CY, (int)(RADAR_R * 0.33f), th.grid);
    canvas.drawLine(CX, CY - RADAR_R, CX, CY + RADAR_R, th.grid);
    canvas.drawLine(CX - RADAR_R, CY, CX + RADAR_R, CY, th.grid);

    // Always draw range labels and compass if enabled, providing immediate scope visibility
    if (s.showRangeLabels) {
        canvas.setTextColor(th.dim, CLR_BG);
        canvas.setCursor(CX + 2, CY - (int)(RADAR_R * 0.33f) - 7);
        canvas.print((int)(rangeKm * 0.33f));
        canvas.setCursor(CX + 2, CY - (int)(RADAR_R * 0.66f) - 7);
        canvas.print((int)(rangeKm * 0.66f));
        canvas.setCursor(CX + 2, CY - RADAR_R - 7);
        canvas.print((int)rangeKm);
    }

    if (s.showCompass) {
        canvas.setTextColor(th.dim, CLR_BG);
        canvas.setCursor(CX - 3, CY - RADAR_R + 2);
        canvas.print("N");
        canvas.setCursor(CX + RADAR_R - 9, CY - 4);
        canvas.print("E");
        canvas.setCursor(CX - 3, CY + RADAR_R - 10);
        canvas.print("S");
        canvas.setCursor(CX - RADAR_R + 2, CY - 4);
        canvas.print("W");
    }

    // --- Breadcrumb trails (drawn first so aircraft icons sit on top) ---
    if (s.showTrail) {
        for (int i = 0; i < count; i++) {
            if (!planes[i].valid) continue;
            TrailSlot* slot = nullptr;
            for (int j = 0; j < MAX_PLANES; j++) {
                if (trails[j].used && strncmp(trails[j].hex, planes[i].icaoHex, 7) == 0) { slot = &trails[j]; break; }
            }
            if (!slot) continue;
            for (uint8_t k = 1; k < slot->count; k++) {
                if (slot->distKm[k] > rangeKm) continue;
                float r0 = (slot->distKm[k] / rangeKm) * RADAR_R;
                float rad0 = (slot->bearingDeg[k] - 90) * DEG_TO_RAD;
                int x0 = CX + (int)(cos(rad0) * r0);
                int y0 = CY + (int)(sin(rad0) * r0);
                canvas.drawPixel(x0, y0, th.sweep);
            }
        }
    }

    // --- Aircraft Rendering & Smart Label Decluttering ---
    int planeX[MAX_PLANES];
    int planeY[MAX_PLANES];
    bool planeOnScreen[MAX_PLANES] = {false};

    // 1. Draw all aircraft icons and breadcrumb/selection indicators
    for (int i = 0; i < count; i++) {
        if (!planes[i].valid || planes[i].distanceKm > rangeKm) continue;

        float screenRadius = (planes[i].distanceKm / rangeKm) * RADAR_R;
        float rad = (planes[i].bearingDeg - 90) * DEG_TO_RAD;
        int x = CX + (int)(cos(rad) * screenRadius);
        int y = CY + (int)(sin(rad) * screenRadius);

        planeX[i] = x;
        planeY[i] = y;
        planeOnScreen[i] = true;

        uint16_t color = altitudeColor(planes[i].altitudeFt, planes[i].onGround, th);
        drawAircraftIcon(x, y, planes[i].trackDeg, color, s.aircraftIcon);

        // Selection ring
        if (i == selectedIndex) {
            canvas.drawCircle(x, y, 7, th.sel);
        }
    }

    // 2. Smart Label Decluttering (avoids overlapping clutter and black background slicing)
    if (labelsMode > 0) {
        struct LabelRect { int16_t x1, y1, x2, y2; };
        LabelRect placed[MAX_PLANES];
        int placedCount = 0;

        auto collides = [&](int16_t x1, int16_t y1, int16_t x2, int16_t y2) -> bool {
            for (int k = 0; k < placedCount; k++) {
                if (!(x2 < placed[k].x1 || x1 > placed[k].x2 || y2 < placed[k].y1 || y1 > placed[k].y2)) {
                    return true;
                }
            }
            return false;
        };

        // Priority 1: Always draw the SELECTED aircraft's label with a tactical pill & leader
        if (selectedIndex >= 0 && selectedIndex < count && planeOnScreen[selectedIndex]) {
            int sx0 = planeX[selectedIndex];
            int sy0 = planeY[selectedIndex];
            const char* selLbl = planes[selectedIndex].flight[0] ? planes[selectedIndex].flight : planes[selectedIndex].icaoHex;
            int lw = (int)strlen(selLbl) * 6;

            int lx = (sx0 + 8 + lw < SIDEBAR_X - 1) ? (sx0 + 8) : (sx0 - lw - 8);
            int ly = constrain(sy0 - 4, 3, SCREEN_H - 11);
            if (lx < 2) lx = 2;

            canvas.fillRoundRect(lx - 2, ly - 1, lw + 4, 10, 2, 0x0821);
            canvas.drawRoundRect(lx - 2, ly - 1, lw + 4, 10, 2, th.sel);
            canvas.setTextColor(th.sel);
            canvas.setCursor(lx, ly);
            canvas.print(selLbl);

            int lineEdgeX = (lx > sx0) ? (lx - 2) : (lx + lw + 2);
            canvas.drawLine(sx0, sy0, lineEdgeX, ly + 4, th.sel);

            placed[placedCount++] = { (int16_t)(lx - 3), (int16_t)(ly - 2), (int16_t)(lx + lw + 3), (int16_t)(ly + 10) };
        }

        // Priority 2: In 'ALL' mode, draw other labels only where they don't collide
        if (labelsMode == 2) {
            for (int i = 0; i < count; i++) {
                if (i == selectedIndex || !planeOnScreen[i]) continue;

                const char* lbl = planes[i].flight[0] ? planes[i].flight : planes[i].icaoHex;
                int lw = (int)strlen(lbl) * 6;
                int px = planeX[i];
                int py = planeY[i];

                const int candX[4] = { px + 6, px + 6, px - lw - 6, px - lw - 6 };
                const int candY[4] = { py - 7, py + 1, py - 7, py + 1 };

                for (int c = 0; c < 4; c++) {
                    int cx = candX[c];
                    int cy = candY[c];

                    if (cx < 2 || cx + lw >= SIDEBAR_X - 1 || cy < 2 || cy + 8 >= SCREEN_H - 1) continue;

                    if (!collides(cx - 1, cy - 1, cx + lw + 1, cy + 8)) {
                        canvas.setTextColor(th.dim);
                        canvas.setCursor(cx, cy);
                        canvas.print(lbl);

                        placed[placedCount++] = { (int16_t)(cx - 1), (int16_t)(cy - 1), (int16_t)(cx + lw + 1), (int16_t)(cy + 8) };
                        break;
                    }
                }
            }
        }
    }

    // --- Tactical Avionics Scanning / Target Acquisition Display (when 0 aircraft) ---
    if (count == 0) {
        // Dual expanding sonar pulse waves across the range rings
        uint32_t ms = millis();
        int pulse1 = (ms % 2200) * RADAR_R / 2200;
        int pulse2 = ((ms + 1100) % 2200) * RADAR_R / 2200;
        if (pulse1 > 2) canvas.drawCircle(CX, CY, pulse1, (pulse1 < RADAR_R / 2) ? th.sweep : th.grid);
        if (pulse2 > 2) canvas.drawCircle(CX, CY, pulse2, (pulse2 < RADAR_R / 2) ? th.sweep : th.dim);

        // Center crosshairs and target acquisition corner brackets
        canvas.drawFastHLine(CX - 14, CY - 14, 5, th.accent);
        canvas.drawFastVLine(CX - 14, CY - 14, 5, th.accent);
        canvas.drawFastHLine(CX + 10, CY - 14, 5, th.accent);
        canvas.drawFastVLine(CX + 14, CY - 14, 5, th.accent);
        canvas.drawFastHLine(CX - 14, CY + 14, 5, th.accent);
        canvas.drawFastVLine(CX - 14, CY + 10, 5, th.accent);
        canvas.drawFastHLine(CX + 10, CY + 14, 5, th.accent);
        canvas.drawFastVLine(CX + 14, CY + 10, 5, th.accent);

        // Center reticle
        canvas.drawFastHLine(CX - 4, CY, 9, th.sweep);
        canvas.drawFastVLine(CX, CY - 4, 9, th.sweep);

        // Modern ATC scanning banner with animated tracking dots
        static const char* dots[4] = {"SEARCHING   ", "SEARCHING.  ", "SEARCHING.. ", "SEARCHING..."};
        canvas.setTextDatum(MC_DATUM);
        canvas.setTextColor(th.sweep, CLR_BG);
        canvas.drawString("AIRSPACE SWEEP", CX, CY - (int)(RADAR_R * 0.46f));
        canvas.setTextColor(th.dim, CLR_BG);
        canvas.drawString(dots[(ms / 350) % 4], CX, CY + (int)(RADAR_R * 0.46f));
        canvas.setTextDatum(TL_DATUM);
    }

    // --- Clean Radar Sweep Beam (crisp tactical leading beam without faded wake) ---
    if (showSweepAnim) {
        float rad = (sweepAngle - 90) * DEG_TO_RAD;
        int ex = CX + (int)(cos(rad) * RADAR_R);
        int ey = CY + (int)(sin(rad) * RADAR_R);
        canvas.drawLine(CX, CY, ex, ey, th.accent);
    }

    // -----------------------------------------------------------------
    // Sidebar: Dedicated instrument column (X: 112..160, W: 48)
    // -----------------------------------------------------------------
    // Elevated dark panel background + dividing graticule line
    canvas.fillRect(SIDEBAR_X, 0, SIDEBAR_W, SCREEN_H, 0x0821);
    canvas.drawLine(SIDEBAR_X, 0, SIDEBAR_X, SCREEN_H - 1, th.grid);

    int sx = SIDEBAR_X + 3;
    int sw = SIDEBAR_W - 6;

    // 1. Top Status Pill (Y: 2..14)
    const char* stStr = "STANDBY";
    uint16_t stColor = th.dim;
    if (status.fetchInProgress) {
        stStr = "SYNC";
        stColor = th.planeHi;
    } else if (count > 0 || (status.lastSuccessMs > 0 && (millis() - status.lastSuccessMs < 45000))) {
        stStr = "LIVE";
        stColor = th.sweep;
    } else if (status.lastAttemptMs > 0) {
        stStr = "SEARCH";
        stColor = th.dim;
    }
    canvas.drawRoundRect(sx, 2, sw, 12, 3, stColor);
    canvas.fillCircle(sx + 5, 8, 2, stColor);
    canvas.setTextColor(stColor, 0x0821);
    canvas.setCursor(sx + 10, 4);
    canvas.print(stStr);

    // 2. Targets & Range Cards (Y: 16..41)
    canvas.setTextColor(th.dim, 0x0821);
    canvas.setCursor(sx, 17);
    canvas.print("TGT:");
    canvas.setTextColor(th.accent, 0x0821);
    canvas.print(count);

    canvas.setTextColor(th.dim, 0x0821);
    canvas.setCursor(sx, 29);
    canvas.print("R:");
    canvas.setTextColor(th.text, 0x0821);
    canvas.print((int)rangeKm);
    canvas.setTextColor(th.dim, 0x0821);
    canvas.print("km");

    // Divider line
    canvas.drawLine(sx, 42, sx + sw, 42, th.grid);

    // 3. Target Telemetry Box (Y: 44..126)
    if (selectedIndex >= 0 && selectedIndex < count && planes[selectedIndex].valid) {
        const AircraftPoint& sp = planes[selectedIndex];
        const char* rawSel = sp.flight[0] ? sp.flight : sp.icaoHex;
        char selLbl[8];
        strncpy(selLbl, rawSel, 7); selLbl[7] = '\0';
        for (int c = strlen(selLbl) - 1; c >= 0 && selLbl[c] == ' '; c--) selLbl[c] = '\0';

        // Callsign header
        canvas.fillRoundRect(sx, 44, sw, 12, 2, th.grid);
        canvas.setTextColor(th.sel, th.grid);
        canvas.setCursor(sx + 2, 46);
        canvas.print(selLbl);

        canvas.setTextColor(th.text, 0x0821);
        canvas.setCursor(sx, 58);
        canvas.print((int)(sp.altitudeFt / 1000.0f));
        canvas.print(".");
        canvas.print(abs(sp.altitudeFt % 1000) / 100);
        canvas.print("kft");

        canvas.setCursor(sx, 70);
        canvas.print((int)sp.speedKt);
        canvas.print("kt");

        canvas.setCursor(sx, 82);
        canvas.print((int)sp.distanceKm);
        canvas.print("km");

        canvas.setCursor(sx, 94);
        canvas.print((int)sp.trackDeg);
        canvas.drawCircle(canvas.getCursorX() + 2, 95, 1, th.text);

        canvas.setTextColor(th.dim, 0x0821);
        canvas.setCursor(sx, 106);
        if (sp.aircraftType[0]) {
            char tBuf[7]; strncpy(tBuf, sp.aircraftType, 6); tBuf[6] = '\0';
            canvas.print(tBuf);
        } else if (sp.squawk[0]) {
            canvas.print("SQ:");
            canvas.print(sp.squawk);
        } else {
            canvas.print(sp.icaoHex);
        }
    } else if (count == 0) {
        // Check if all providers are disabled
        bool anyEnabled = false;
        const AppSettings& cfg = Storage::settings();
        for (int i = 0; i < PROVIDER_COUNT; i++) { if (cfg.providerEnabled[i]) { anyEnabled = true; break; } }

        if (!anyEnabled) {
            canvas.setTextColor(th.err, 0x0821);
            canvas.setCursor(sx, 48);
            canvas.print("NO");
            canvas.setCursor(sx, 60);
            canvas.print("PROV");
            canvas.setTextColor(th.dim, 0x0821);
            canvas.setCursor(sx, 74);
            canvas.print("ENABLE");
            canvas.setCursor(sx, 86);
            canvas.print("ONE IN");
            canvas.setCursor(sx, 98);
            canvas.print("MENU");
        } else {
            canvas.setTextColor(th.dim, 0x0821);
            canvas.setCursor(sx, 48);
            canvas.print("STATUS");
            canvas.setTextColor(th.sweep, 0x0821);
            canvas.setCursor(sx, 60);
            canvas.print("ACTIVE");
            canvas.setTextColor(th.dim, 0x0821);
            canvas.setCursor(sx, 74);
            canvas.print("BAND:");
            canvas.setTextColor(th.text, 0x0821);
            canvas.setCursor(sx, 86);
            canvas.print("1090M");
            canvas.setTextColor(th.dim, 0x0821);
            canvas.setCursor(sx, 100);
            canvas.print("FEED:");
            canvas.setTextColor(th.accent, 0x0821);
            canvas.setCursor(sx, 112);
            canvas.print(status.lastProviderUsed.length() ? status.lastProviderUsed.substring(0, 6).c_str() : "ADS-B");
        }
    } else {
        canvas.setTextColor(th.dim, 0x0821);
        canvas.setCursor(sx, 48);
        canvas.print("TARGET");
        canvas.setCursor(sx, 60);
        canvas.print("SELECT");
        canvas.setCursor(sx, 72);
        canvas.print("AUTO");
    }

    canvas.pushSprite(0, 0);
}
