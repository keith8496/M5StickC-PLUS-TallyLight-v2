#pragma once

#include <M5Unified.h>
#include <ezTime.h>        // set #define EZTIME_CACHE_NVS in this file
#include <WiFiManager.h>

extern Timezone localTime;
extern WiFiManager wm;

void WiFi_setup();
void WiFi_onLoop();
void WiFi_onEvent(WiFiEvent_t event);
void WiFi_onSaveParams();

// Debounced time initialization API
void requestTimeInit();
void serviceTimeInit();

// Optional: request a one-shot NTP resync via MQTT /cmd
void requestTimeResync();