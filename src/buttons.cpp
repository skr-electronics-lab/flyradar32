#include "buttons.h"
#include "config.h"

struct BtnState {
    uint8_t pin;
    bool pressed = false;
    bool longHandled = false;
    bool repeatFired = false;
    unsigned long pressStart = 0;
    unsigned long lastRepeatMs = 0;
};

static BtnState btns[BTN_ID_COUNT];

void Buttons::begin() {
    btns[BTN_ID_UP].pin     = BTN_UP;
    btns[BTN_ID_DOWN].pin   = BTN_DOWN;
    btns[BTN_ID_SELECT].pin = BTN_SELECT;
    for (auto& b : btns) pinMode(b.pin, INPUT_PULLUP);
}

ButtonEvent Buttons::poll(ButtonId& outWhich) {
    unsigned long now = millis();
    for (int i = 0; i < BTN_ID_COUNT; i++) {
        BtnState& b = btns[i];
        bool down = (digitalRead(b.pin) == LOW);

        if (down && !b.pressed) {
            b.pressed = true;
            b.pressStart = now;
            b.longHandled = false;
            b.repeatFired = false;
            b.lastRepeatMs = now;
        } else if (down && b.pressed) {
            unsigned long held = now - b.pressStart;
            if (!b.longHandled && held > LONG_PRESS_MS) {
                b.longHandled = true;
                b.repeatFired = true;
                b.lastRepeatMs = now;
                outWhich = (ButtonId)i;
                return BTN_EVENT_LONG_PRESS;
            }
            if (b.longHandled && held > 800 && now - b.lastRepeatMs > 200) {
                b.lastRepeatMs = now;
                outWhich = (ButtonId)i;
                return BTN_EVENT_REPEAT;
            }
        } else if (!down && b.pressed) {
            b.pressed = false;
            unsigned long held = now - b.pressStart;
            if (!b.longHandled && held > DEBOUNCE_MS) {
                outWhich = (ButtonId)i;
                return BTN_EVENT_SHORT_PRESS;
            }
        }
    }
    return BTN_EVENT_NONE;
}
