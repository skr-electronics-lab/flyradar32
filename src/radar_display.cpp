#include "radar_display.h"
#include "storage.h"
#include "wifi_manager.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <math.h>

// ---------------------------------------------------------------------
// IMPORTANT: tft is constructed with the DEFAULT (no-arg) constructor so
// TFT_eSPI uses the native panel resolution baked into the build flags
// (TFT_WIDTH=128, TFT_HEIGHT=160 â€” the panel's true rotation-0 shape).
// tft.setRotation(TFT_ROTATION) then swaps width/height for us so
// tft.width()==SCREEN_W (160) and tft.height()==SCREEN_H (128). Every
// buffer we allocate below (LVGL's draw buf, LVGL's hor_res/ver_res, and
// the radar TFT_eSprite) is sized from SCREEN_W/SCREEN_H â€” the *same*
// rotated numbers the physical driver now reports â€” so nothing can ever
// disagree about the canvas shape again. See config.h for the full
// derivation/explanation of the old landscape bug.
// ---------------------------------------------------------------------
static lv_color_t getLVThemeColor();

// ---------------------------------------------------------------------
// UI design tokens Ã¢â‚¬â€ instrument-panel dark theme (fixed, theme-neutral).
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

// Same colors as RGB565 for the raw-canvas scope drawing
#define UI_SCR_BG_565  0x1088   // 0x0E1116
#define UI_BORDER_565 0x35D4   // 0x333A42

// Menu card geometry Ã¢â‚¬â€ single source of truth for every menu-style screen
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
static lv_obj_t* sbRoot = nullptr;   // radar sidebar card (LVGL)
static lv_obj_t* sbRows[8] = {nullptr};  // its row labels

// LVGL's dark theme ships a different screen grey in every minor version.
// Pin the screen background to our own chrome palette so the UI stays
// layered dark grey no matter which lvgl version the build resolves.
static void applyScreenBg() {
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(UI_SCR_BG), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
}

static void clearLVGL() {
    lv_obj_clean(lv_scr_act());
    applyScreenBg();
    list_obj = nullptr;
    title_label = nullptr;
    detail_cont = nullptr;
    current_menu_title = "";
    current_detail_hex = "";
    sbRoot = nullptr;   // radar sidebar is LVGL too â€” rebuild on next render
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

    // short accent underline under the title â€” the theme's signature detail
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
    if (onGround) return th.dim;
    if (altFt < 10000) return th.err;         // low / red - most alert-worthy
    if (altFt < 30000) return th.planeMd;     // mid / orange
    return th.sweep;                           // cruise / green (matches radar beam)
}

// Set to true if your module's backlight is active-LOW (brightness 0 = full
// bright). Symptom when wrong: screen stuck fully bright or fully dark and
// the Settings -> Brightness slider appears to do nothing.
#define BACKLIGHT_ACTIVE_LOW  false

// Use explicit LEDC API for reliable PWM on GPIO15.
// ledcSetup/ledcAttachPin/ledcWrite are the correct ESP32 Arduino APIs;
// analogWrite() may not initialize LEDC properly when TFT_eSPI has
// already touched GPIO15 during init.
static bool backlightInit = false;
static void setBacklight(uint8_t brightness) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    if (!backlightInit) {
        pinMode(TFT_BLK, OUTPUT);
        ledcAttach(TFT_BLK, 5000, 8);
        backlightInit = true;
    }
    uint8_t val = BACKLIGHT_ACTIVE_LOW ? (255 - brightness) : brightness;
    ledcWrite(TFT_BLK, val);
#else
    if (!backlightInit) {
        pinMode(TFT_BLK, OUTPUT);
        ledcSetup(0, 5000, 8);
        ledcAttachPin(TFT_BLK, 0);
        backlightInit = true;
    }
    uint8_t val = BACKLIGHT_ACTIVE_LOW ? (255 - brightness) : brightness;
    ledcWrite(0, val);
#endif
}

// ---------------------------------------------------------------------
// Breadcrumb trail Ã¢â‚¬â€ keeps the last few fixes for each aircraft so the
// radar can draw a fading tail behind it. Matched by ICAO hex across
// fetch cycles. Memory cost: MAX_PLANES * (TRAIL_LEN*8 + 16) Ã¢â€°Ë† 1.1 KB.
// ---------------------------------------------------------------------
struct TrailSlot {
    char hex[8] = {0};
    bool used = false;
    uint8_t count = 0;
    uint8_t age = 0;      // bumped on each sample; used for oldest-steal
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
    // (lowest age stamp) Ã¢â‚¬â€ NOT an arbitrary trails[0], which would corrupt
    // an unrelated aircraft's history.
    TrailSlot* victim = &trails[0];
    for (int i = 0; i < MAX_PLANES; i++) {
        if (!trails[i].used) { victim = &trails[i]; break; }
        if ((uint8_t)(trailAgeCounter - trails[i].age) > (uint8_t)(trailAgeCounter - victim->age)) victim = &trails[i];
    }
    victim->used = true;
    victim->count = 0;
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
    // Free slots for aircraft that vanished so they don't linger forever.
    for (int i = 0; i < MAX_PLANES; i++) {
        if (trails[i].used && !seen[i]) trails[i].used = false;
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

    // Scope canvas covers only the scope zone; the sidebar is an LVGL card
    canvas.setColorDepth(16);
    canvas.createSprite(RADAR_ZONE_W, SCREEN_H);

    setBacklight(Storage::settings().brightness);

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

void RadarDisplay::applyBrightness() {
    setBacklight(Storage::settings().brightness);
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

        // accent rule under the logo â€” same signature as buildCard
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

        // Compact single-label block â€” all in 12-pt font to fit without overlap.
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

        lv_obj_t * hint = lv_label_create(card);
        lv_label_set_text(hint, "SELECT to return");
        lv_obj_set_style_text_color(hint, lv_color_hex(UI_TEXT_DIM), 0);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);

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
    // ICAO hex changes â€” otherwise altitude/speed/distance stay frozen at
    // whatever they were when the screen was first drawn.
    static uint32_t builtSig = 0;
    uint32_t sig = ((uint32_t)p.altitudeFt * 31u) ^ ((uint32_t)(p.speedKt * 10.0f) * 7u) ^
                   ((uint32_t)(p.distanceKm * 10.0f) << 8) ^ ((uint32_t)p.trackDeg << 16);
    if (current_detail_hex != String(p.icaoHex) || detail_cont == nullptr || sig != builtSig) {
        builtSig = sig;
        lv_obj_t * card = buildCard(fNameSafe(p).c_str(), getLVThemeColor());
        detail_cont = lv_obj_create(card);
        lv_obj_set_size(detail_cont, LIST_W, MENU_LIST_H);
        lv_obj_align(detail_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(detail_cont, lv_color_hex(UI_ROW_BG), 0);
        lv_obj_set_style_radius(detail_cont, 4, 0);
        lv_obj_set_style_border_width(detail_cont, 0, 0);
        lv_obj_set_style_pad_all(detail_cont, 2, 0);
        lv_obj_set_scrollbar_mode(detail_cont, LV_SCROLLBAR_MODE_OFF);

        uint32_t thx = theme().accent_hex;
        int y = 0;
        
        auto addRow = [&](const char* title, String data) {
            lv_obj_t* lblTitle = lv_label_create(detail_cont);
            lv_label_set_text(lblTitle, title);
            lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(lblTitle, lv_color_hex(UI_TEXT_DIM), 0);
            lv_obj_set_pos(lblTitle, 0, y);

            lv_obj_t* lblData = lv_label_create(detail_cont);
            lv_label_set_text(lblData, data.c_str());
            lv_obj_set_style_text_font(lblData, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(lblData, lv_color_hex(thx), 0);
            lv_obj_set_width(lblData, LIST_W - 35);
            lv_obj_set_pos(lblData, 35, y);
            lv_label_set_long_mode(lblData, LV_LABEL_LONG_SCROLL_CIRCULAR);
            y += 16;
        };

        addRow("ICAO:", String(p.icaoHex));
        addRow("Reg:", String(p.registration));
        addRow("Op:", String(p.operatorName));
        addRow("Type:", String(p.aircraftType));
        addRow("Desc:", String(p.desc));
        addRow("Alt:", String(p.altitudeFt) + " ft");
        addRow("Spd:", String((int)p.speedKt) + " kt");
        addRow("Trk:", String((int)p.trackDeg) + " deg");
        addRow("Sqk:", String(p.squawk));
        addRow("Dst:", String((int)p.distanceKm) + " km");
        addRow("Brg:", String((int)p.bearingDeg) + " deg");
        if (WifiManager::timeSynced()) {
            addRow("Upd:", WifiManager::getClockTime());
        }

        current_detail_hex = String(p.icaoHex);
    }

    // Apply manual vertical scroll
    if (detail_cont) {
        lv_obj_scroll_to_y(detail_cont, scrollY, LV_ANIM_OFF);
    }
}

void RadarDisplay::renderPlaneList(const AircraftPoint planes[], int count, int selectedIndex) {
    if (is_radar_active) { is_radar_active = false; }

    // Rebuild when the aircraft set changes (cheap hex-signature compare) â€”
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

    // -----------------------------------------------------------------
    // Scope background: modern dark tier + hairline frame (matches the
    // card language of every other screen). Scope zone is 0..SIDEBAR_X.
    // -----------------------------------------------------------------
    canvas.fillScreen(UI_SCR_BG_565);
    canvas.drawRect(0, 0, RADAR_ZONE_W, SCREEN_H, UI_BORDER_565);

    // --- Radar graticule ---
    canvas.drawCircle(CX, CY, RADAR_R, th.grid);
    canvas.drawCircle(CX, CY, (int)(RADAR_R * 0.66f), th.grid);
    canvas.drawCircle(CX, CY, (int)(RADAR_R * 0.33f), th.grid);
    canvas.drawLine(CX, CY - RADAR_R, CX, CY + RADAR_R, th.grid);
    canvas.drawLine(CX - RADAR_R, CY, CX + RADAR_R, CY, th.grid);

    // Always draw range labels and compass if enabled, providing immediate scope visibility
    if (s.showRangeLabels) {
        canvas.setTextColor(th.dim, UI_SCR_BG_565);
        canvas.setCursor(CX + 2, CY - (int)(RADAR_R * 0.33f) - 7);
        canvas.print((int)(rangeKm * 0.33f));
        canvas.setCursor(CX + 2, CY - (int)(RADAR_R * 0.66f) - 7);
        canvas.print((int)(rangeKm * 0.66f));
        canvas.setCursor(CX + 2, CY - RADAR_R - 7);
        canvas.print((int)rangeKm);
    }

    if (s.showCompass) {
        canvas.setTextColor(th.dim, UI_SCR_BG_565);
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
                if (slot->distKm[k] > rangeKm || slot->distKm[k-1] > rangeKm) continue;
                
                float r0 = (slot->distKm[k-1] / rangeKm) * RADAR_R;
                float rad0 = (slot->bearingDeg[k-1] - 90) * DEG_TO_RAD;
                int x0 = CX + (int)(cos(rad0) * r0);
                int y0 = CY + (int)(sin(rad0) * r0);
                
                float r1 = (slot->distKm[k] / rangeKm) * RADAR_R;
                float rad1 = (slot->bearingDeg[k] - 90) * DEG_TO_RAD;
                int x1 = CX + (int)(cos(rad1) * r1);
                int y1 = CY + (int)(sin(rad1) * r1);
                
                uint16_t color = (k == 1) ? th.dim : th.grid;
                canvas.drawLine(x0, y0, x1, y1, color);
            }
        }
    }

    // --- Aircraft ---
    for (int i = 0; i < count; i++) {
        if (!planes[i].valid) continue;
        if (planes[i].distanceKm > rangeKm) continue;

        float screenRadius = (planes[i].distanceKm / rangeKm) * RADAR_R;
        float planeRad = (planes[i].bearingDeg - 90) * DEG_TO_RAD;
        int px = CX + (int)(cos(planeRad) * screenRadius);
        int py = CY + (int)(sin(planeRad) * screenRadius);

        uint16_t color = altitudeColor(planes[i].altitudeFt, planes[i].onGround, th);
        drawAircraftIcon(px, py, planes[i].trackDeg, color, s.aircraftIcon);

        if (i == selectedIndex) {
            canvas.drawCircle(px, py, 7, th.sel);
        }

        if (labelsMode == 2 || (labelsMode == 1 && i == selectedIndex)) {
            const char* raw = planes[i].flight[0] ? planes[i].flight : "UNK";
            char lbl[8];
            strncpy(lbl, raw, 7); lbl[7] = '\0';
            for (int c = strlen(lbl) - 1; c >= 0 && lbl[c] == ' '; c--) lbl[c] = '\0';
            int labelW = (int)strlen(lbl) * 6;

            int lx = (px + 9 + labelW <= SIDEBAR_X - 2) ? (px + 9) : (px - 9 - labelW);
            if (lx < 1) lx = 1;
            int ly = py - 4;
            if (ly < 1) ly = 1;
            if (ly > SCREEN_H - 9) ly = SCREEN_H - 9;

            canvas.setTextColor(th.text, UI_SCR_BG_565);
            canvas.setCursor(lx, ly);
            canvas.print(lbl);
        }
    }

    // --- Scanning pulse when no aircraft are visible ---
    if (count == 0) {
        int pulseR = 4 + (millis() % 2400) / 120; // 4..24px
        uint16_t pulseColor = ((millis() % 2400) < 1200) ? th.grid : th.dim;
        canvas.drawCircle(CX, CY, pulseR, pulseColor);
        canvas.setTextColor(th.dim, UI_SCR_BG_565);
        canvas.setTextDatum(MC_DATUM);
        canvas.drawString("SCANNING SKY...", CX, CY + (int)(RADAR_R * 0.42f));
        canvas.setTextDatum(TL_DATUM);
    }

    // --- Sweep animation with phosphor wake (drawn under aircraft so targets stay sharp) ---
    if (showSweepAnim) {
        // 3-tier phosphor wake trailing behind clockwise sweep
        const int wakeAngles[3] = {6, 4, 2};
        const uint16_t wakeColors[3] = {th.grid, th.dim, th.sweep};
        for (int w = 0; w < 3; w++) {
            float wRad = (sweepAngle - wakeAngles[w] - 90) * DEG_TO_RAD;
            int wx = CX + (int)(cos(wRad) * RADAR_R);
            int wy = CY + (int)(sin(wRad) * RADAR_R);
            canvas.drawLine(CX, CY, wx, wy, wakeColors[w]);
        }
        // Bright leading beam
        float rad = (sweepAngle - 90) * DEG_TO_RAD;
        int ex = CX + (int)(cos(rad) * RADAR_R);
        int ey = CY + (int)(sin(rad) * RADAR_R);
        canvas.drawLine(CX, CY, ex, ey, th.accent);
    }

    canvas.pushSprite(0, 0);

    // -----------------------------------------------------------------
    // Sidebar: floating LVGL card (matches settings/detail screens)
    // -----------------------------------------------------------------
    if (!sbRoot) {
        sbRoot = lv_obj_create(lv_scr_act());
        lv_obj_set_pos(sbRoot, SIDEBAR_X + 2, 3);
        lv_obj_set_size(sbRoot, SIDEBAR_W - 5, SCREEN_H - 6);
        lv_obj_set_style_bg_color(sbRoot, lv_color_hex(UI_CARD_BG), 0);
        lv_obj_set_style_border_width(sbRoot, 1, 0);
        lv_obj_set_style_border_color(sbRoot, lv_color_hex(UI_BORDER), 0);
        lv_obj_set_style_pad_all(sbRoot, 4, 0);
        lv_obj_set_style_radius(sbRoot, 8, 0);
        lv_obj_set_style_shadow_width(sbRoot, 0, 0);
        lv_obj_clear_flag(sbRoot, LV_OBJ_FLAG_SCROLLABLE);
        for (int i = 0; i < 8; i++) {
            sbRows[i] = lv_label_create(sbRoot);
            lv_obj_set_style_text_font(sbRows[i], &lv_font_montserrat_12, 0);
            lv_obj_set_pos(sbRows[i], 0, i * 14);
            lv_obj_set_style_text_align(sbRows[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(sbRows[i], SIDEBAR_W - 13);
        }
    }

    const AircraftPoint* sp = (selectedIndex >= 0 && selectedIndex < count && planes[selectedIndex].valid)
                              ? &planes[selectedIndex] : nullptr;

    // Row 0: status pill
    const char* stStr = "STANDBY";
    uint32_t stHex = 0x9AA3AD;
    if (status.fetchInProgress)      { stStr = "SYNC";  stHex = theme().accent_hex; }
    else if (count > 0 || (status.lastSuccessMs > 0 && (millis() - status.lastSuccessMs < 45000))) { stStr = "LIVE"; stHex = theme().accent_hex; }
    else if (status.lastAttemptMs > 0) { stStr = "SEARCH"; stHex = 0x9AA3AD; }
    char stBuf[20];
    snprintf(stBuf, sizeof(stBuf), "%s  %d TGT", stStr, count);
    lv_label_set_text(sbRows[0], stBuf);
    lv_obj_set_style_text_color(sbRows[0], lv_color_hex(stHex), 0);

    // Row 1: range
    char rngBuf[16];
    snprintf(rngBuf, sizeof(rngBuf), "%d km", (int)rangeKm);
    lv_label_set_text(sbRows[1], rngBuf);
    lv_obj_set_style_text_color(sbRows[1], lv_color_hex(UI_TEXT_DIM), 0);

    if (sp) {
        const char* rawSel = sp->flight[0] ? sp->flight : sp->icaoHex;
        char selLbl[8];
        strncpy(selLbl, rawSel, 7); selLbl[7] = '\0';
        for (int c = strlen(selLbl) - 1; c >= 0 && selLbl[c] == ' '; c--) selLbl[c] = '\0';

        char b[8][20];
        snprintf(b[2], 20, "%s", selLbl);
        snprintf(b[3], 20, "%ddkft", (int)(sp->altitudeFt / 1000.0f));
        snprintf(b[4], 20, "%d kt", (int)sp->speedKt);
        snprintf(b[5], 20, "%d km", (int)sp->distanceKm);
        snprintf(b[6], 20, "%d%s", (int)sp->trackDeg, "\xC2\xB0");
        if (sp->aircraftType[0])      snprintf(b[7], 20, "%s", sp->aircraftType);
        else if (sp->squawk[0])       snprintf(b[7], 20, "SQ %s", sp->squawk);
        else                          snprintf(b[7], 20, "%s", sp->icaoHex);
        for (int i = 2; i < 8; i++) {
            lv_label_set_text(sbRows[i], b[i]);
            lv_obj_set_style_text_color(sbRows[i], i == 2 ? lv_color_hex(theme().accent_hex) : lv_color_hex(UI_TEXT_MAIN), 0);
        }
    } else {
        lv_label_set_text(sbRows[2], "TARGET");
        lv_label_set_text(sbRows[3], "SELECT");
        lv_label_set_text(sbRows[4], "AUTO");
        lv_label_set_text(sbRows[5], "");
        lv_label_set_text(sbRows[6], "");
        lv_label_set_text(sbRows[7], "");
        for (int i = 2; i < 8; i++) lv_obj_set_style_text_color(sbRows[i], lv_color_hex(UI_TEXT_DIM), 0);
    }
}
