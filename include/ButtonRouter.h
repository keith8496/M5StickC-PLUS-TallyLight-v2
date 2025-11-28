#pragma once

#include "ButtonManager.h"
class ConfigState;

class TallyState;

// Routes button events into high-level actions:
// - BtnA short  -> cycle ATEM input (TallyState::selectNextInput)
// - BtnB short  -> cycle brightness
// - BtnB long   -> change screen
class ButtonRouter {
public:
    ButtonRouter(ConfigState& cfg, TallyState& tally);

    // Call when ButtonManager::poll() returns a non-None event.
    void handle(const ButtonEvent& ev);

private:
    ConfigState& _cfg;
    TallyState& _tally;
};