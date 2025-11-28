#pragma once

#include <Arduino.h>
#include <map>

// High-level type from your JSON: "camera", "media", etc.
// If you don't care yet, you can leave 'type' as a raw String.
struct AtemInputInfo {
    uint8_t id = 0;
    String shortName;    // e.g. "Cam1"
    String longName;     // e.g. "Cam1 Center - main follow"
    bool   tallyEnabled; // from "TRUE"/"FALSE"
};

struct TallyState {
    // Current program/preview input IDs from:
    //   sanctuary/atem/program
    //   sanctuary/atem/preview
    uint8_t programInput = 0;
    uint8_t previewInput = 0;

    // Map from ATEM input ID → info (from sanctuary/atem/inputs JSON)
    std::map<uint8_t, AtemInputInfo> inputs;

    // Currently selected ATEM input ID for this tally device.
    // 0 means "no selection".
    uint8_t selectedInput = 0;

    // Helpers
    bool isProgram(uint8_t input) const;
    bool isPreview(uint8_t input) const;
    const AtemInputInfo* findInput(uint8_t input) const;

    // Ensure selectedInput points at a tally-enabled input (or 0 if none).
    void normalizeSelected();

    // Cycle to the next tally-enabled input (wraps around, skips disabled).
    void selectNextInput();

    // Convenience accessor for the currently selected input info.
    const AtemInputInfo* currentSelected() const;
};
