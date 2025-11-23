#include <M5StickCPlus.h>
#include "NetworkModule.h"
#include "WebSocketsModule.h"
#include "PrefsModule.h"
#include "PowerModule.h"
#include "ScreenModule.h"
#include "millisDelay.h"
#include "RunningAverage.h"

millisDelay ms_startup;

#define TPS false
#if TPS
    millisDelay ms_runningAvg;
    millisDelay ms_tps;
    int ticks = 0;
    const int samples = 100;
    RunningAverage ra_TPS(samples);
#endif


char deviceId[17];
char deviceName[33];

void setup () {

    M5.begin();
    M5.Lcd.setRotation(3);
    setCpuFrequencyMhz(80); //Save battery by turning down the CPU clock
    btStop();               //Save battery by turning off Bluetooth

    currentBrightness = 50;
    setBrightness(currentBrightness);

    // Set deviceId and deviceName
    uint8_t macAddress[6];
    WiFi.macAddress(macAddress);
    sprintf(deviceId, "%02X%02X%02X", macAddress[3], macAddress[4], macAddress[5]);
    strcpy(deviceName, "M5StickC-Plus-");
    strcat(deviceName, deviceId);
    
    changeScreen(0);
    startupLog("Starting...", 1);
    startupLog("Initializing preferences...", 1);
    preferences_setup();
    startupLog("Initializing power management...", 1);
    power_setup();
    startupLog("Initializing WiFi...", 1);
    WiFi_setup();
    ms_startup.start(30000);
    startupLog("Initializing webSockets...", 1);
    webSockets_setup();

    while (ms_startup.isRunning()) {
        WiFi_onLoop();
        webSockets_onLoop();
        power_onLoop();
        if (ws_isConnected & (timeStatus() == timeSet)) {
            ms_startup.stop();
            startupLog("Startup complete.", 1);
        }
        if (ms_startup.justFinished()) {
            ms_startup.stop();
            startupLog("Startup incomplete.", 1);
        }
    }
      
    startupLog("", 1);
    startupLog("Press \"M5\" button \r\nto continue.", 2);

    #if TPS
        ms_tps.start(1000);
        ms_runningAvg.start(60000);
    #endif

}

void loop () {

    M5.update();
    events();               // ezTime
    WiFi_onLoop();
    webSockets_onLoop();
    power_onLoop();

    // M5 Button
    if (M5.BtnA.wasReleased()) {
        changeScreen(-1);
    }

    // Action Button
    if (M5.BtnB.wasReleased()) {
        int newBrightness = currentBrightness + 10;
        setBrightness(newBrightness);
    }

    refreshScreen();

    #if TPS
        if (ms_tps.justFinished()) {
            ms_tps.repeat();
            ra_TPS.addValue(ticks);
            ticks = 0;
        }

        if (ms_runningAvg.justFinished()) {
            ms_runningAvg.repeat();
            Serial.println(localTime.dateTime(ISO8601));
            Serial.println("tps: " + String(ra_TPS.getFastAverage()) );
            Serial.println();
        }
        ticks++;
    #endif

}
