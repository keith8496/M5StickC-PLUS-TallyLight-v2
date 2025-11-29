#include <M5Unified.h>
#include "DisplayModule.h"
#include "ScreenModule.h"  // for currentBrightness and setBrightness

void cycleBrightness() {
    // Discrete brightness levels we cycle through
    static const int levels[] = {20, 30, 40, 50, 60, 70, 80, 90, 100};
    static const size_t numLevels = sizeof(levels) / sizeof(levels[0]);

    // Find the nearest level at or above the current setting
    size_t idx = 0;
    for (; idx < numLevels; ++idx) {
        if (currentBrightness <= levels[idx]) {
            break;
        }
    }

    // Advance to the next level (wrap at the end)
    idx = (idx + 1) % numLevels;

    currentBrightness = levels[idx];
    setBrightness(currentBrightness);
}