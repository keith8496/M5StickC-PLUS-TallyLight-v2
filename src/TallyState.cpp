#include "TallyState.h"

// Helpers
bool TallyState::isProgram(uint8_t input) const {
    return input != 0 && input == programInput;
}

bool TallyState::isPreview(uint8_t input) const {
    return input != 0 && input == previewInput;
}

const AtemInputInfo* TallyState::findInput(uint8_t input) const {
    auto it = inputs.find(input);
    return (it != inputs.end()) ? &it->second : nullptr;
}

// Ensure selectedInput points at a tally-enabled input (or 0 if none).
void TallyState::normalizeSelected() {
    // If we already have a valid, tally-enabled selection, keep it.
    if (selectedInput != 0) {
        auto it = inputs.find(selectedInput);
        if (it != inputs.end() && it->second.tallyEnabled) {
            return;
        }
    }

    // Otherwise pick the first enabled input, if any.
    for (const auto& kv : inputs) {
        if (kv.second.tallyEnabled) {
            selectedInput = kv.first;
            return;
        }
    }

    // No enabled inputs.
    selectedInput = 0;
}

// Cycle to the next tally-enabled input (wraps around, skips disabled).
void TallyState::selectNextInput() {
    if (inputs.empty()) {
        selectedInput = 0;
        return;
    }

    // Start search just after current selection.
    auto it = (selectedInput == 0) ? inputs.begin()
                                   : inputs.upper_bound(selectedInput);

    auto start = it;
    bool wrapped = false;

    while (true) {
        if (it == inputs.end()) {
            if (wrapped) {
                // No enabled inputs found.
                selectedInput = 0;
                return;
            }
            it = inputs.begin();
            wrapped = true;
        }

        // Avoid infinite loop if there are no tally-enabled entries.
        if (wrapped && it == start) {
            selectedInput = 0;
            return;
        }

        if (it->second.tallyEnabled) {
            selectedInput = it->first;
            return;
        }

        ++it;
    }
}

// Convenience accessor for the currently selected input info.
const AtemInputInfo* TallyState::currentSelected() const {
    return (selectedInput != 0) ? findInput(selectedInput) : nullptr;
}
