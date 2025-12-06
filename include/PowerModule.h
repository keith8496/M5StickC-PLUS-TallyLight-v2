#pragma once

#include <M5Unified.h>
#include "ConfigState.h"

constexpr size_t BAT_WARNING_LEVEL_MAX_LEN   = 16;
constexpr size_t POWER_MODE_MAX_LEN   = 16;

struct power {
    char batWarningLevel[BAT_WARNING_LEVEL_MAX_LEN + 1] = "";
    char powerMode[POWER_MODE_MAX_LEN + 1] = "";
    float coulombCount = 0;
    float batVoltage = 0;
    float batPercentage = 0;
    float batPercentageMin = 0;
    float batPercentageMax = 0;
    float batPercentageCoulomb;   // experimental CC-based SoC
    float batPercentageHybrid;    // optional, future hybrid
    float batCurrent = 0;
    float batChargeCurrent = 0;
    float vbusVoltage = 0;
    float vbusCurrent = 0;
    float vinVoltage = 0;
    float vinCurrent = 0;
    float apsVoltage = 0;
    float tempInAXP192 = 0;
    int maxChargeCurrent = 0;
    int maxBrightness = 100;    
    
    // Capacity-learning debug (last observed cycle)
    uint16_t learnedCapOld = 0;
    uint16_t learnedCapNew = 0;
};

extern power pwr;

void power_setup();
void power_onLoop();
