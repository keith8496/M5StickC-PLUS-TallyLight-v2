#include <M5Unified.h>
#include <esp_task_wdt.h>
#include <millisDelay.h>
#include <WiFi.h>

#include "ScreenModule.h"
#include "NetworkModule.h"
#include "PrefsModule.h"
#include "PowerModule.h"

#include "ButtonManager.h"
#include "ButtonRouter.h"
#include "DisplayModule.h"

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

ButtonManager g_buttons;
ButtonRouter  g_buttonRouter(g_config, g_tally);

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

    // Initialize M5Unified for M5StickC-Plus
    auto cfg = M5.config();
    // Default config is usually fine for StickC-Plus; tweak here if needed later.
    M5.begin(cfg);

    // Use M5Unified display API
    M5.Display.setRotation(1);
    setCpuFrequencyMhz(80); //Save battery by turning down the CPU clock
    btStop();               //Save battery by turning off Bluetooth

    currentBrightness = 50;
    setBrightness(currentBrightness);
    g_buttons.begin(500);  // 500 ms long-press threshold

    // Set deviceId and friendly deviceName
    uint8_t macAddress[6];
    WiFi.macAddress(macAddress);

    // Build deviceId from last 3 bytes of MAC
    g_config.device.deviceId = String(macAddress[3], HEX) +
                               String(macAddress[4], HEX) +
                               String(macAddress[5], HEX);

    g_config.device.deviceId.toUpperCase();

    // Build deviceName
    g_config.device.deviceName = "M5StickC-Plus-" + g_config.device.deviceId;
    
    changeScreen(SCREEN_STARTUP);
    startupLog("Starting...", 1);
    
    // Preferences
    startupLog("Initializing preferences...", 1);
    preferences_setup();
    prefs_applyToConfig(g_config);
    
    // Power Management
    startupLog("Initializing power management...", 1);
    power_setup();
    power_onLoop();  // initial read

    ms_startup.start(30000);
    bool wifi_isInited = false;
    bool mqtt_isInited = false;
    while (ms_startup.isRunning()) {
        
        if (!wifi_isInited) {
            wifi_isInited = true;
            startupLog("Initializing WiFi...", 1);
            WiFi_setup();
        }

        WiFi_onLoop();

        if (!mqtt_isInited && WiFi.status() == WL_CONNECTED) {
            mqtt_isInited = true;
            startupLog("Initializing MQTT...", 1);
            g_bootMillis = millis();
            g_mqtt.setMessageHandler(onMqttMessage);
            g_mqtt.begin();
        }

        g_mqtt.loop();

        if (!g_config.device.ntp_isSynchronized) {
            // Will loop until NTP sync or timeout
            serviceTimeInit();
        }

        if (wifi_isInited && mqtt_isInited && g_config.device.ntp_isSynchronized) {
            ms_startup.stop();
            startupLog("Startup complete.", 1);
        }

        // Timeout hit?
        if (ms_startup.justFinished()) {
            ms_startup.stop();
            startupLog("Startup incomplete.", 1);
        }

    }
      
    startupLog("", 1);
    changeScreen(SCREEN_TALLY);

    // Init task watchdog: 60s timeout, panic = true (print backtrace & reset)
    esp_task_wdt_init(60, true);
    // Watch the current (Arduino) task
    esp_task_wdt_add(NULL);

    #if TPS
        ms_tps.start(1000);
        ms_runningAvg.start(60000);
    #endif
    
}


void loop () {

    // Feed the watchdog
    esp_task_wdt_reset();

    M5.update();
    power_onLoop();
    WiFi_onLoop();
    g_mqtt.loop();
    serviceTimeInit();      // first time NTP
    events();               // ezTime

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

            case MqttCommandType::ResyncTime:
                Serial.println("MQTT: ResyncTime command received");
                requestTimeResync();
                break;

            case MqttCommandType::None:
            default:
                break;
        }
    }

    ButtonEvent ev = g_buttons.poll();
    if (ev.type != ButtonType::None) {
        g_buttonRouter.handle(ev);
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
