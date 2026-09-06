#pragma once
#include <Arduino.h>

enum ButtonId { BTN_ID_UP = 0, BTN_ID_DOWN = 1, BTN_ID_SELECT = 2, BTN_ID_COUNT = 3 };
enum ButtonEvent { BTN_EVENT_NONE = 0, BTN_EVENT_SHORT_PRESS, BTN_EVENT_LONG_PRESS, BTN_EVENT_REPEAT };

// Call Buttons::begin() once, Buttons::poll() every loop iteration.
// poll() returns at most one event per call; call it in a tight loop
// (it's non-blocking) so rapid presses on different buttons aren't lost.
// After holding LONG_PRESS_MS (700ms), BTN_EVENT_REPEAT fires every ~200ms
// for scrolling.
namespace Buttons {
    void begin();
    ButtonEvent poll(ButtonId& outWhich);
}
