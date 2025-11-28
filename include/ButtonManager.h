#pragma once

#include <stdint.h>

constexpr const uint32_t LONG_PRESS_DEFAULT_MS = 500;

// Which button generated the event
enum class ButtonID : uint8_t {
    None = 0,
    A,
    B,
    // Power,   // reserved for future
};

// What kind of press it was
enum class ButtonType : uint8_t {
    None = 0,
    ShortPress,
    LongPress,
};

// Single button event emitted from ButtonManager::poll()
struct ButtonEvent {
    ButtonID   id       = ButtonID::None;
    ButtonType type     = ButtonType::None;
};

class ButtonManager {
public:
    // Configure how long (in ms) a press must be held to count as "long"
    void begin(uint32_t longPressMs = LONG_PRESS_DEFAULT_MS);

    // Call once per loop *after* M5.update().
    // Returns ButtonEvent with type==None when no event.
    ButtonEvent poll();

private:
    uint32_t _longPressMs = LONG_PRESS_DEFAULT_MS;

    // Simple state for A
    bool     _aWasDown    = false;
    uint32_t _aDownAtMs   = 0;

    // Simple state for B
    bool     _bWasDown    = false;
    uint32_t _bDownAtMs   = 0;

    // Internal helpers
    ButtonEvent handleButtonA(bool isDown, uint32_t nowMs);
    ButtonEvent handleButtonB(bool isDown, uint32_t nowMs);
};