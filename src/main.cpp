#include <Arduino.h>

#include <M5StickCPlus.h>
#include <millisDelay.h>
#include <WiFi.h>

#include "ScreenModule.h"
#include "NetworkModule.h"
#include "PrefsModule.h"
#include "PowerModule.h"
#include "WebSocketsModule.h"

#include "ConfigState.h"
#include "TallyState.h"
#include "MqttClient.h"
#include "MqttRouter.h"

#define TPS false
#if TPS
    #include "RunningAverage.h"
    millisDelay ms_runningAvg;
    millisDelay ms_tps;
    int ticks = 0;
    const int samples = 100;
    RunningAverage ra_TPS(samples);
#endif

millisDelay ms_startup;

// Global state
ConfigState g_config;
TallyState  g_tally;
MqttClient  g_mqtt(g_config, g_tally);

uint32_t g_bootMillis;

MqttCommand g_pendingCommand;  // global or static

// Example status snapshot builder
StatusSnapshot buildStatusSnapshot() {
    StatusSnapshot st;
    st.uptimeSec = (millis() - g_bootMillis) / 1000;
    float batPct = pwr.batPercentage;
    if (batPct < 0.0f) batPct = 0.0f;
    else if (batPct > 100.0f) batPct = 100.0f;
    st.batteryPct = static_cast<uint8_t>(batPct + 0.5f);
    st.batteryMv  = static_cast<uint16_t>(pwr.batVoltage * 1000.0f);
    st.rssi       = static_cast<int8_t>(WiFi.RSSI());
    st.temperatureC = pwr.tempInAXP192;
    st.firmwareVersion = F("2.0.0-mqtt");
    st.hwRevision      = F("M5StickC-Plus");
    return st;
}

void onMqttMessage(const String& topic, const String& payload) {
    // Route into ConfigState + TallyState, and capture any command
    handleMqttMessage(g_config, g_tally, topic, payload, g_pendingCommand);

    // Optional debug
    Serial.printf("[MQTT] %s => %s\n", topic.c_str(), payload.c_str());
}


void setup () {

    Serial.begin(115200);
    M5.begin();
    M5.Lcd.setRotation(3);
    setCpuFrequencyMhz(80); //Save battery by turning down the CPU clock
    btStop();               //Save battery by turning off Bluetooth

    currentBrightness = 50;
    setBrightness(currentBrightness);

    // Set deviceId and deviceName
    uint8_t macAddress[6];
    WiFi.macAddress(macAddress);
    snprintf(deviceId, sizeof(deviceId),
         "%02X%02X%02X", macAddress[3], macAddress[4], macAddress[5]);
    snprintf(deviceName, sizeof(deviceName),
         "M5StickC-Plus-%s", deviceId);
    
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
        if (ws_isConnected && (timeStatus() == timeSet)) {
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

    // Begin New MQTT Stuff
    g_bootMillis = millis();

    // Bridge those values into ConfigState
    // We already called preferences_setup() earlier,
    // so the global buffers are populated. Just bridge now:
    prefs_applyToConfig(g_config);

    // Init MQTT wrapper
    g_mqtt.setMessageHandler(onMqttMessage);
    g_mqtt.begin();
    // End New MQTT Stuff

}

void loop () {

    M5.update();
    events();               // ezTime
    WiFi_onLoop();
    webSockets_onLoop();
    power_onLoop();

    // Begin New MQTT Stuff
    g_mqtt.loop();

    // Periodic status publish
    static uint32_t lastStatusMs = 0;
    const auto eff = g_config.effective();
    uint32_t now = millis();
    if (now - lastStatusMs > (uint32_t)eff.statusIntervalSec * 1000UL) {
        lastStatusMs = now;
        g_mqtt.publishStatus(buildStatusSnapshot());
    }

    // Handle pending command (deep sleep/reboot/etc.)
    if (g_pendingCommand.type != MqttCommandType::None) {
        MqttCommandType cmd = g_pendingCommand.type;
        g_pendingCommand.type = MqttCommandType::None;  // consume it

        switch (cmd) {
            case MqttCommandType::DeepSleep:
                // TODO: publish offline, flush, then enter deep sleep
                // e.g. g_mqtt.publishAvailability("offline"); delay(50); esp_deep_sleep_start();
                Serial.println("Would DeepSleep (ignoring for now)");
                break;

            case MqttCommandType::Reboot:
                // TODO: publish offline, flush, then restart
                // ESP.restart();
                Serial.println("Would Reboot (ignoring for now)");
                break;

            case MqttCommandType::Wakeup:
                // Typically handled by hardware; you may ignore in firmware
                Serial.println("Would Wakeup (ignoring for now)");
                break;

            case MqttCommandType::OtaUpdate:
                // Reserved for future; for now maybe log and ignore
                // g_mqtt.publishLog("OTA command received (not implemented yet)", LogLevel::Warn);
                Serial.println("Would OtaUpdate (ignoring for now)");
                break;

            case MqttCommandType::FactoryReset:
                // Future: clear NVS prefs, reboot, etc.
                Serial.println("Would FactoryReset (ignoring for now)");
                break;

            case MqttCommandType::None:
            default:
                break;
        }
    }
    // End New MQTT Stuff

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
