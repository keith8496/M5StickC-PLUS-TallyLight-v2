#pragma once

#include <Arduino.h>
#include <map>

// High-level type from your JSON: "camera", "media", etc.
// If you don't care yet, you can leave 'type' as a raw String.
struct AtemInputInfo {
    uint8_t id = 0;
    String  label;
    String  type;          // "camera", "bars", ...
    bool    tallyEnabled = true;
};

struct TallyState {
    // Current program/preview input IDs from:
    //   sanctuary/atem/program
    //   sanctuary/atem/preview
    uint8_t programInput = 0;
    uint8_t previewInput = 0;

    // Map from ATEM input ID → info (from sanctuary/atem/inputs JSON)
    std::map<uint8_t, AtemInputInfo> inputs;

    // Helpers
    bool isProgram(uint8_t input) const { return input != 0 && input == programInput; }
    bool isPreview(uint8_t input) const { return input != 0 && input == previewInput; }

    const AtemInputInfo* findInput(uint8_t input) const {
        auto it = inputs.find(input);
        return (it != inputs.end()) ? &it->second : nullptr;
    }
};
