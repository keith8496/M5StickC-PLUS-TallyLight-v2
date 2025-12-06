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

#define DEBUG_IMU_ORIENTATION 0

millisDelay ms_startup;

// Global state
ConfigState g_config;
TallyState  g_tally;
MqttClient  g_mqtt(g_config, g_tally);

ButtonManager g_buttons;
ButtonRouter  g_buttonRouter(g_config, g_tally);

uint32_t g_bootMillis;

MqttCommand g_pendingCommand;  // global or static

// Track current display rotation for IMU-based orientation (landscape only)
static int g_displayRotation = 1;

// Example status snapshot builder
StatusSnapshot buildStatusSnapshot() {
    auto eff = g_config.effective();
    
    StatusSnapshot st;
    st.uptimeSec = (millis() - g_bootMillis) / 1000;
    st.batteryMv  = static_cast<uint16_t>(pwr.batVoltage * 1000.0f);
    float batPct = pwr.batPercentage;
    if (batPct < 0.0f) batPct = 0.0f;
    else if (batPct > 100.0f) batPct = 100.0f;
    st.batteryPct = static_cast<uint8_t>(batPct + 0.5f);
    st.batPercentageCoulomb = static_cast<uint8_t>(pwr.batPercentageCoulomb + 0.5f);
    st.batPercentageHybrid   = static_cast<uint8_t>(pwr.batPercentageHybrid + 0.5f);
    st.coulombCount   = pwr.coulombCount;
    st.rssi       = static_cast<int8_t>(WiFi.RSSI());
    st.temperatureC = pwr.tempInAXP192;
    st.firmwareVersion = F("2.0.0-mqtt");
    st.buildDateTime   = eff.buildDateTime;
    st.hwRevision      = F("M5StickC-Plus");
    return st;
}

void onMqttMessage(const String& topic, const String& payload) {
    // Route into ConfigState + TallyState, and capture any command
    handleMqttMessage(g_config, g_tally, topic, payload, g_pendingCommand);

    // Optional debug
    Serial.printf("[MQTT] %s => %s\n", topic.c_str(), payload.c_str());
}




// --------------------------------------------------------------
// Accelerometer-driven screen orientation (landscape only)
// --------------------------------------------------------------
void updateScreenOrientationFromImu()
{
    // Check at most every 100 ms to avoid jitter and wasted work
    static uint32_t lastCheckMs = 0;
    const uint32_t intervalMs   = 100;
    uint32_t now = millis();
    if (now - lastCheckMs < intervalMs) {
        return;
    }
    lastCheckMs = now;

    float ax, ay, az;
    // M5Unified fills ax/ay/az with acceleration in g's
    M5.Imu.getAccel(&ax, &ay, &az);

    // Decide which axis to use for landscape orientation based on which has greater magnitude.
    // On some board orientations X may dominate, on others Y will; this makes us robust.
    float absAx = (ax >= 0.0f) ? ax : -ax;
    float absAy = (ay >= 0.0f) ? ay : -ay;
    float v;
    const char* axisName;
    if (absAx > absAy) {
        v = ax;
        axisName = "x";
    } else {
        v = ay;
        axisName = "y";
    }

    #if DEBUG_IMU_ORIENTATION
        Serial.printf("IMU raw: ax=%.2f ay=%.2f az=%.2f  v(%s)=%.2f rot=%d\n",
                      ax, ay, az, axisName, v, g_displayRotation);
    #endif

    // We only care about "upright" vs "upside-down" in landscape.
    // Use asymmetric thresholds (hysteresis) on whatever axis currently has the strongest gravity signal.
    // Higher magnitude here means you have to tilt closer to "fully upside-down" before the flip happens.
    const float upThreshold    = 0.7f;   // must be above this to be considered "upright"
    const float downThreshold  = -0.7f;  // must be below this to be considered "upside-down"

    int desiredRotation = g_displayRotation;

    if (g_displayRotation == 1) {
        // Currently upright; only flip if we are clearly upside-down
        if (v < downThreshold) {
            desiredRotation = 3;
        }
    } else {
        // Currently upside-down; only flip if we are clearly upright
        if (v > upThreshold) {
            desiredRotation = 1;
        }
    }

    // If ay is in the middle band (between thresholds), keep the current rotation.

    if (desiredRotation != g_displayRotation) {
        g_displayRotation = desiredRotation;
        M5.Display.setRotation(g_displayRotation);

        // Force a redraw immediately so the UI matches the new rotation
        refreshScreen();
    }
}


void setup () {

    Serial.begin(115200);

    // Initialize M5Unified for M5StickC-Plus
    auto cfg = M5.config();
    // Default config is usually fine for StickC-Plus; tweak here if needed later.
    M5.begin(cfg);

    updateScreenOrientationFromImu() ;  // initial orientation

    // Use M5Unified display API (start in normal landscape)
    g_displayRotation = 1;
    M5.Display.setRotation(g_displayRotation);

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

    auto eff = g_config.effective();
    Serial.printf("BUILD_DATETIME from config: '%s'\n", eff.buildDateTime.c_str());
    
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

            case MqttCommandType::selectNextInput:
                Serial.println("MQTT: selectNextInput command received");
                g_tally.selectNextInput();
                g_mqtt.publishSelectedInput(g_tally.selectedInput);
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

    // Update orientation (landscape only) from the accelerometer
    updateScreenOrientationFromImu();

    // Draw whichever screen is active (startup, tally, power, setup)
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
