#include "ButtonManager.h"

#include <M5Unified.h>   // for M5.BtnA / BtnB

void ButtonManager::begin(uint32_t longPressMs) {
    _longPressMs = longPressMs;
    _aWasDown    = false;
    _bWasDown    = false;
    _aDownAtMs   = 0;
    _bDownAtMs   = 0;
    _aLongPressFired = false;
    _bLongPressFired = false;
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
        _aWasDown        = true;
        _aDownAtMs       = nowMs;
        _aLongPressFired = false;
    }
    else if (isDown && _aWasDown) {
        // Held down: check for long-press threshold
        const uint32_t held = nowMs - _aDownAtMs;
        if (!_aLongPressFired && held >= _longPressMs) {
            _aLongPressFired = true;
            ev.id   = ButtonID::A;
            ev.type = ButtonType::LongPress;
            return ev;  // fire immediately while still held
        }
    }
    else if (!isDown && _aWasDown) {
        // Just released
        _aWasDown = false;

        // If a long-press already fired, do not emit a short-press
        if (_aLongPressFired) {
            _aLongPressFired = false;
            return ev;  // ev.type remains None
        }

        // Otherwise treat this as a press based on duration
        const uint32_t held = nowMs - _aDownAtMs;
        ev.id = ButtonID::A;
        if (held >= _longPressMs) {
            // Edge case: threshold crossed but we didn't catch it in the held branch
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
        _bWasDown        = true;
        _bDownAtMs       = nowMs;
        _bLongPressFired = false;
    }
    else if (isDown && _bWasDown) {
        // Held down: check for long-press threshold
        const uint32_t held = nowMs - _bDownAtMs;
        if (!_bLongPressFired && held >= _longPressMs) {
            _bLongPressFired = true;
            ev.id   = ButtonID::B;
            ev.type = ButtonType::LongPress;
            return ev;  // fire immediately while still held
        }
    }
    else if (!isDown && _bWasDown) {
        // Just released
        _bWasDown = false;

        // If a long-press already fired, do not emit a short-press
        if (_bLongPressFired) {
            _bLongPressFired = false;
            return ev;  // ev.type remains None
        }

        // Otherwise treat this as a press based on duration
        const uint32_t held = nowMs - _bDownAtMs;
        ev.id = ButtonID::B;
        if (held >= _longPressMs) {
            // Edge case: threshold crossed but we didn't catch it in the held branch
            ev.type = ButtonType::LongPress;
        } else {
            ev.type = ButtonType::ShortPress;
        }
    }

    return ev;
}