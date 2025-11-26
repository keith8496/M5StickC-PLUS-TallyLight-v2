#include <millisDelay.h>
#include <M5StickCPlus.h>

#include "NetworkModule.h"
#include "PowerModule.h"
#include "ScreenModule.h"

#include "ConfigState.h"
#include "TallyState.h"

extern ConfigState g_config;
extern TallyState  g_tally;

millisDelay md_screenRefresh;

ScreenId currentScreen = SCREEN_STARTUP;
const int maxScreen = SCREEN_SETUP;

// Logical brightness in the 0–100 range.
// This is what MQTT / prefs / power mgmt all agree on.
int currentBrightness = 50;     // default to 50%

const int tft_width = 240;
const int tft_heigth = 135;

TFT_eSprite startupScreen = TFT_eSprite(&M5.Lcd);
TFT_eSprite tallyScreen = TFT_eSprite(&M5.Lcd);
TFT_eSprite powerScreen = TFT_eSprite(&M5.Lcd);
TFT_eSprite setupScreen = TFT_eSprite(&M5.Lcd);

constexpr size_t LOG_MESSAGE_MAX_LEN     = 64;
struct startupLogData {
    char logMessage[LOG_MESSAGE_MAX_LEN + 1];
    int textSize;
};

startupLogData startupLogEntries[20];
int index_startupLog = -1;

int prevTally = 0;


void refreshTallyScreen() {

    // --- Determine this device's ATEM input ID ---
    // EffectiveConfig merges global + device config
    EffectiveConfig eff = g_config.effective();
    uint8_t myInput = eff.atemInput;

    // If no input is configured yet, treat as idle & just show UI
    bool isProgram = false;
    bool isPreview = false;

    if (myInput != 0) {
        isProgram = g_tally.isProgram(myInput);
        isPreview = g_tally.isPreview(myInput);
    }

    // --- Background color based on tally state ---
    if (isProgram) {
        tallyScreen.fillRect(0, 0, 240, 135, TFT_RED);
        // return mqtt
    } else if (isPreview) {
        tallyScreen.fillRect(0, 0, 240, 135, TFT_GREEN);
        // return mqtt
    } else {
        tallyScreen.fillRect(0, 0, 240, 135, TFT_BLACK);
        // return mqtt
    }
    
    // Battery
    tallyScreen.setTextSize(1);
    tallyScreen.setCursor(10,8);
    tallyScreen.setTextColor(TFT_WHITE, TFT_BLACK);
    tallyScreen.printf("Bat: %.0f%%", pwr.batPercentage);
    
    // Clock
    tallyScreen.setTextSize(2);
    tallyScreen.setCursor((tft_width/2)-20, 8);
    tallyScreen.setTextColor(TFT_WHITE, TFT_BLACK);
    auto now = localTime.dateTime("g:i:s A");
    tallyScreen.print(now ? now : "--:--:--");

    
    // Friendly Name
    tallyScreen.setTextSize(9);
    tallyScreen.setCursor(10,80);
    tallyScreen.setTextColor(TFT_WHITE);
    tallyScreen.print(g_config.device.friendlyName);
    
    tallyScreen.pushSprite(0,0);
    
}


void refreshPowerScreen() {

    powerScreen.fillSprite(TFT_BLACK);
    powerScreen.setTextColor(TFT_WHITE);
    powerScreen.setCursor(0,0);
    powerScreen.setTextSize(2);
    powerScreen.println(F("Power Management"));

    powerScreen.setTextSize(1);
    powerScreen.println(pwr.powerMode);
    powerScreen.printf("Bat: %s\r\n  V: %.3fv     %.1f%%\r\n", pwr.batWarningLevel, pwr.batVoltage, pwr.batPercentage);
    powerScreen.printf("  I: %.3fma  Ic: %.3fma\r\n", pwr.batCurrent, pwr.batChargeCurrent);
    powerScreen.printf("  Imax: %ima  Bmm: (%.f%%/%.f%%) SB: %i\r\n", pwr.maxChargeCurrent, pwr.batPercentageMin, pwr.batPercentageMax, currentBrightness);
    powerScreen.printf("USB:\r\n  V: %.3fv  I: %.3fma\r\n", pwr.vbusVoltage, pwr.vbusCurrent);
    powerScreen.printf("5V-In:\r\n  V: %.3fv  I: %.3fma\r\n", pwr.vinVoltage, pwr.vinCurrent);
    powerScreen.printf("APS:\r\n  V: %.3fv\r\n", pwr.apsVoltage);
    powerScreen.printf("AXP:\r\n  Temp: %.1fc", pwr.tempInAXP192);

    powerScreen.pushSprite(10,10);

}


void refreshSetupScreen() {

    String g_mqttServer = g_config.global.mqttServer;
    uint16_t g_mqttPort = g_config.global.mqttPort;
    bool g_mqtt_IsConnected = g_config.device.mqtt_isConnected;
   
    String strTimeStatus;
    strTimeStatus.reserve(16);
    switch (timeStatus()) {
        case (timeNotSet):
            strTimeStatus= "timeNotSet";
            break;
        case (timeNeedsSync):
            strTimeStatus = "timeNeedsSync";
            break;
        case (timeSet):
            strTimeStatus = "timeSet";
            break;
        default:
            break;
    }
    
    setupScreen.fillSprite(TFT_BLACK);
    setupScreen.setTextColor(TFT_WHITE);
    setupScreen.setCursor(0,0);
    setupScreen.setTextSize(2);
    setupScreen.println(F("Setup Screen"));
    setupScreen.println();
    setupScreen.setTextSize(1);
    setupScreen.println("SSID: " + String(wm.getWiFiSSID()) + " " + String(WiFi.RSSI()));
    setupScreen.println("Webportal Active: " + String(wm.getWebPortalActive()));
    setupScreen.println("Hostname: " + wm.getWiFiHostname());
    setupScreen.println("IP: " + WiFi.localIP().toString());
    setupScreen.println("NTP: " + strTimeStatus);
    setupScreen.println();
    setupScreen.println("MQTT Server: " + String(g_mqttServer) + ":" + String(g_mqttPort));
    setupScreen.println("Connected: " + String(g_mqtt_IsConnected ? "Yes" : "No"));
    setupScreen.pushSprite(10,10);

}


void refreshStartupScreen() {
    startupScreen.fillSprite(TFT_BLACK);
    startupScreen.setTextColor(TFT_WHITE);
    startupScreen.setCursor(0,0);
    for (int i = 0; i <= index_startupLog; i++) {
        startupScreen.setTextSize(startupLogEntries[i].textSize);
        startupScreen.println(startupLogEntries[i].logMessage);
    }
    startupScreen.pushSprite(5,5);
}


void changeScreen(int newScreen) {

    Serial.println(F("changeScreen()"));
    if (newScreen < -1 || newScreen > maxScreen) {
        Serial.println(F("changeScreen() error: \"invalid screen number rejected\""));
        return;
    } else if (newScreen == -1) {
        if (currentScreen == maxScreen) currentScreen = SCREEN_STARTUP;  // reset
        currentScreen = static_cast<ScreenId>((static_cast<int>(currentScreen) + 1) % (maxScreen + 1));
    } else {
        currentScreen = static_cast<ScreenId>(newScreen);
    }

    if (wm.getWebPortalActive()) wm.stopWebPortal();
    
    startupScreen.deleteSprite();
    tallyScreen.deleteSprite();
    powerScreen.deleteSprite();
    setupScreen.deleteSprite();
    
    // clearScreen
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(0,0);

    switch (currentScreen) {
        case SCREEN_STARTUP:
            // startupScreen
            startupScreen.createSprite(tft_width-5, tft_heigth-5);
            startupScreen.setRotation(3);
            break;
        case SCREEN_TALLY:
            // tallyScreen
            tallyScreen.createSprite(tft_width, tft_heigth);
            tallyScreen.setRotation(3);
            break;
        case SCREEN_POWER:
            // powerScreen
            powerScreen.createSprite(tft_width, tft_heigth);
            powerScreen.setRotation(3);
            break;
        case SCREEN_SETUP:
            // setupScreen
            if (!wm.getWebPortalActive()) wm.startWebPortal();
            setupScreen.createSprite(tft_width, tft_heigth);
            setupScreen.setRotation(3);
            break;
        default:
            M5.Lcd.println("Invalid Screen!");
            break; 
    }

    md_screenRefresh.start(1000 / 30); // 30 fps

}


void refreshScreen() {

    // Limit to 30 FPS
    if(!md_screenRefresh.justFinished()) return;
    md_screenRefresh.repeat();
    
    switch (currentScreen) {
        case SCREEN_STARTUP:
            refreshStartupScreen();
            break;
        case SCREEN_TALLY:
            refreshTallyScreen();
            break;
        case SCREEN_POWER:
            refreshPowerScreen();
            break;
        case SCREEN_SETUP:
            refreshSetupScreen();
            break;
        default:
            break; 
    }

}


static const int minBrightness = 10;
void setBrightness(int newBrightness) {
    // Clamp to 0–100 and respect the current power-mode cap.
    if (newBrightness < minBrightness) {
        newBrightness = minBrightness;
    }
    if (newBrightness > pwr.maxBrightness) {
        newBrightness = minBrightness; // wrap to min if exceeding max
    }

    currentBrightness = newBrightness;

    // On M5StickC-Plus, ScreenBreath expects 0–100.
    M5.Axp.ScreenBreath(currentBrightness);
}


void startupLog(const char* in_logMessage, int in_textSize) {
    index_startupLog++;
    if (index_startupLog > 19) {
        Serial.println(F("Too many log entries."));
        return;
    }
    strncpy(startupLogEntries[index_startupLog].logMessage, in_logMessage, 64);
    startupLogEntries[index_startupLog].logMessage[64] = '\0';
    startupLogEntries[index_startupLog].textSize = in_textSize;
    refreshStartupScreen();
}