#pragma once

#include <M5Unified.h>
#include <ezTime.h>        // set #define EZTIME_CACHE_NVS in this file
#include <WiFiManager.h>

extern Timezone localTime;
extern WiFiManager wm;

void WiFi_setup();
void WiFi_onLoop();