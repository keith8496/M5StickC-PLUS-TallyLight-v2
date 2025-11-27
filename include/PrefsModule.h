#pragma once

#include <Arduino.h>
#include "ConfigState.h"

void preferences_setup();
void preferences_save();
void WiFi_onSaveParams();

void prefs_applyToConfig(ConfigState& cfg);