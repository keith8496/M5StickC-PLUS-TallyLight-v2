#include "ButtonManager.h"

#include <M5Unified.h>   // for M5.BtnA / BtnB

void ButtonManager::begin(uint32_t longPressMs) {
    _longPressMs = longPressMs;
    _aWasDown    = false;
    _bWasDown    = false;
    _aDownAtMs   = 0;
    _bDownAtMs   = 0;
}

ButtonEvent ButtonManager::poll() {
    ButtonEvent ev;  // defaults to "no event"

    const uint32_t now = millis();

    // Read current button states
    const bool aDown = M5.BtnA.isPressed();
    const bool bDown = M5.BtnB.isPressed();

    // Handle A then B; if A produced an event, we can return immediately.
    ev = handleButtonA(aDown, now);
    if (ev.type != ButtonType::None) {
        return ev;
    }

    ev = handleButtonB(bDown, now);
    return ev;
}

ButtonEvent ButtonManager::handleButtonA(bool isDown, uint32_t nowMs) {
    ButtonEvent ev;

    if (isDown && !_aWasDown) {
        // Just pressed
        _aWasDown  = true;
        _aDownAtMs = nowMs;
    } else if (!isDown && _aWasDown) {
        // Just released -> determine short vs long
        _aWasDown = false;
        const uint32_t held = nowMs - _aDownAtMs;

        ev.id = ButtonID::A;
        if (held >= _longPressMs) {
            ev.type = ButtonType::LongPress;
        } else {
            ev.type = ButtonType::ShortPress;
        }
    }

    return ev;
}

ButtonEvent ButtonManager::handleButtonB(bool isDown, uint32_t nowMs) {
    ButtonEvent ev;

    if (isDown && !_bWasDown) {
        // Just pressed
        _bWasDown  = true;
        _bDownAtMs = nowMs;
    } else if (!isDown && _bWasDown) {
        // Just released -> determine short vs long
        _bWasDown = false;
        const uint32_t held = nowMs - _bDownAtMs;

        ev.id = ButtonID::B;
        if (held >= _longPressMs) {
            ev.type = ButtonType::LongPress;
        } else {
            ev.type = ButtonType::ShortPress;
        }
    }

    return ev;
}