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

// UP+DOWN held together >= LONG_PRESS_MS -> BTN_EVENT_DUAL_LONG_PRESS
// (used for the weather screen). While the combo is held, individual
// UP/DOWN long/repeat events are suppressed so it can't misfire.
static bool dualArmed = false;

ButtonEvent Buttons::poll(ButtonId& outWhich) {
    unsigned long now = millis();
    bool down[BTN_ID_COUNT];
    for (int i = 0; i < BTN_ID_COUNT; i++) down[i] = (digitalRead(btns[i].pin) == LOW);

    if (down[BTN_ID_UP] && down[BTN_ID_DOWN]) {
        if (!dualArmed) {
            dualArmed = true;
            // treat combo start as the shared press moment
            if (!btns[BTN_ID_UP].pressed)   { btns[BTN_ID_UP].pressed = true;   btns[BTN_ID_UP].pressStart = now; }
            if (!btns[BTN_ID_DOWN].pressed) { btns[BTN_ID_DOWN].pressed = true; btns[BTN_ID_DOWN].pressStart = now; }
            btns[BTN_ID_UP].longHandled = btns[BTN_ID_DOWN].longHandled = false;
            btns[BTN_ID_UP].repeatFired = btns[BTN_ID_DOWN].repeatFired = true;
        }
        unsigned long held = now - btns[BTN_ID_UP].pressStart;
        if (!btns[BTN_ID_UP].longHandled && held > LONG_PRESS_MS) {
            btns[BTN_ID_UP].longHandled = btns[BTN_ID_DOWN].longHandled = true;
            outWhich = BTN_ID_UP;
            return BTN_EVENT_DUAL_LONG_PRESS;
        }
        return BTN_EVENT_NONE;
    }
    if (dualArmed) {
        // Wait until BOTH buttons are physically released before disarming,
        // preventing the trailing button from triggering a phantom short-press.
        if (!down[BTN_ID_UP] && !down[BTN_ID_DOWN]) {
            dualArmed = false;
            btns[BTN_ID_UP].pressed = btns[BTN_ID_DOWN].pressed = false;
            btns[BTN_ID_UP].longHandled = btns[BTN_ID_DOWN].longHandled = true;
        } else {
            btns[BTN_ID_UP].longHandled = btns[BTN_ID_DOWN].longHandled = true;
        }
        return BTN_EVENT_NONE;
    }

    for (int i = 0; i < BTN_ID_COUNT; i++) {
        BtnState& b = btns[i];
        bool isDown = down[i];

        if (isDown && !b.pressed) {
            b.pressed = true;
            b.pressStart = now;
            b.longHandled = false;
            b.repeatFired = false;
            b.lastRepeatMs = now;
        } else if (isDown && b.pressed) {
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
        } else if (!isDown && b.pressed) {
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
