
#include "ScreenModule.h"
#include "millisDelay.h"
#include <M5StickCPlus.h>
#include "PrefsModule.h"
#include "WebSocketsModule.h"
#include "PowerModule.h"
#include "NetworkModule.h"

millisDelay md_screenRefresh;

const int maxScreen = 3;
int currentScreen = 0;          // 0-Startup, 1-Tally, 2-Power, 3-Setup
int currentBrightness = 11;     // default 11, max 12

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

    bool isProgram = false;
    bool isPreview = false;
    
    /*
    // Bitshift on inputIds. Rightmost bit = input 1
    for (int i = 1; i <= 16; i++) {
        bool bitValue = (inputIds >> i-1) & 0x01;
        if ((bitValue) & (i == atem_pgm1_input_id)) isProgram = true;
        if ((bitValue) & (i == atem_pvw1_input_id)) isPreview = true;
    }
    */

   if (strcmp(friendlyName, atem_pgm1_friendlyName) == 0) isProgram = true;
   if (strcmp(friendlyName, atem_pvw1_friendlyName) == 0) isPreview = true;

    if (isProgram) {
        tallyScreen.fillRect(0,0,240,135, TFT_RED);\
        if (prevTally != 2) {prevTally = 2; webSockets_returnTally(2);}
    } else if (isPreview) {
        tallyScreen.fillRect(0,0,240,135, TFT_GREEN);
        if (prevTally != 1) {prevTally = 1; webSockets_returnTally(1);}
    } else {
        tallyScreen.fillRect(0,0,240,135, TFT_BLACK);
        if (prevTally != 0) {prevTally = 0; webSockets_returnTally(0);}
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
    tallyScreen.print(localTime.dateTime("g:i:s A"));
    
    // Friendly Name
    tallyScreen.setTextSize(9);
    tallyScreen.setCursor(10,80);
    tallyScreen.setTextColor(TFT_WHITE);
    tallyScreen.print(friendlyName);
    
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

    String strTimeStatus;
    strTimeStatus.reserve(14);
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
    setupScreen.println("Node-RED Server: " + String(nodeRED_ServerIP) + ":" + String(nodeRED_ServerPort));
    setupScreen.println("Connected: " + String(ws_isConnected));
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
        if (currentScreen == maxScreen) currentScreen = 0;  // reset
        currentScreen++;
    } else {
        currentScreen = newScreen;
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
        case 0:
            // startupScreen
            startupScreen.createSprite(tft_width-5, tft_heigth-5);
            startupScreen.setRotation(3);
            break;
        case 1:
            // tallyScreen
            tallyScreen.createSprite(tft_width, tft_heigth);
            tallyScreen.setRotation(3);
            webSockets_getTally();
            break;
        case 2:
            // powerScreen
            powerScreen.createSprite(tft_width, tft_heigth);
            powerScreen.setRotation(3);
            break;
        case 3:
            // setupScreen
            if (!wm.getWebPortalActive()) wm.startWebPortal();
            setupScreen.createSprite(tft_width, tft_heigth);
            setupScreen.setRotation(3);
            break;
        default:
            M5.Lcd.println("Invalid Screen!");
            break; 
    }

    md_screenRefresh.start(33.33333); // 30 fps

}


void refreshScreen() {

    // Limit to 30 FPS
    if(!md_screenRefresh.justFinished()) return;
    md_screenRefresh.repeat();
    
    switch (currentScreen) {
        case 0:
            refreshStartupScreen();
            break;
        case 1:
            refreshTallyScreen();
            break;
        case 2:
            refreshPowerScreen();
            break;
        case 3:
            refreshSetupScreen();
            break;
        default:
            break; 
    }

}


void setBrightness(int newBrightness) {
    if (newBrightness > pwr.maxBrightness) newBrightness = 10;
    currentBrightness = newBrightness;
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