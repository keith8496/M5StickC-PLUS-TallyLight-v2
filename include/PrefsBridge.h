#pragma once

#include <Arduino.h>
#include "ConfigState.h"

// Your existing PrefsModule forward declaration
class PrefsModule;

// Loads Wi-Fi + MQTT connection fields from PrefsModule
// into ConfigState.global.
//
// Call this *before* starting Wi-Fi or MQTT.
void loadNetworkPrefsIntoConfig(PrefsModule& prefs, ConfigState& cfg);
