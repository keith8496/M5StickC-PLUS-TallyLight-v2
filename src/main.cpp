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

// Idle dimming state
static uint32_t g_lastActivityMs  = 0;
static bool     g_isIdleDimmed    = false;

// Forward declarations
static void markUserActivity(const EffectiveConfig& eff);
void markUserActivity();   // non-static so other files can call it


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

// Mark user/activity events (buttons, screen rotation, etc.)
static void markUserActivity(const EffectiveConfig& eff)
{
    g_lastActivityMs = millis();
    if (g_isIdleDimmed) {
        // Restore to configured active brightness
        uint8_t target = eff.brightness;
        if (target != currentBrightness) {
            currentBrightness = target;
            setBrightness(currentBrightness);
        }
        g_isIdleDimmed = false;
    }
}

void markUserActivity()
{
    auto eff = g_config.effective();
    markUserActivity(eff);
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
    // Check at most every 250 ms to avoid jitter and wasted work (saves IMU + CPU power)
    static uint32_t lastCheckMs = 0;
    const uint32_t intervalMs   = 250;
    uint32_t now = millis();
    if (now - lastCheckMs < intervalMs) {
        return;
    }
    lastCheckMs = now;

    float ax, ay, az;
    // M5Unified fills ax/ay/az with acceleration in g's
    M5.Imu.getAccel(&ax, &ay, &az);

    // Detect movement (not just flips) to drive idle-activity.
    // We compare current accel to the previous sample and treat larger deltas
    // as "movement", ignoring small noise.
    {
        static bool  haveLastAccel = false;
        static float lastAx = 0.0f;
        static float lastAy = 0.0f;
        static float lastAz = 0.0f;

        // Tunable motion sensitivity (in g). Typical noise is ~0.02–0.05g, so 0.15g is a
        // reasonable "user moved the device" threshold.
        const float motionThreshold = 0.15f;

        if (haveLastAccel) {
            float dax = ax - lastAx;
            float day = ay - lastAy;
            float daz = az - lastAz;

            float sumAbs =
                (dax >= 0.0f ? dax : -dax) +
                (day >= 0.0f ? day : -day) +
                (daz >= 0.0f ? daz : -daz);

            if (sumAbs > motionThreshold) {
                // Any significant movement counts as activity
                markUserActivity();
            }
        }

        lastAx = ax;
        lastAy = ay;
        lastAz = az;
        haveLastAccel = true;
    }

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

    // Use M5Unified display API (start in normal landscape)
    g_displayRotation = 1;
    M5.Display.setRotation(g_displayRotation);
    updateScreenOrientationFromImu() ;  // initial orientation

    setCpuFrequencyMhz(80); //Save battery by turning down the CPU clock
    btStop();               //Save battery by turning off Bluetooth

    currentBrightness = 50;
    setBrightness(currentBrightness);
    g_buttons.begin(500);  // 500 ms long-press threshold

    g_lastActivityMs = millis();
    g_isIdleDimmed   = false;

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
        // Any button activity resets the idle timer and can restore brightness
        markUserActivity(eff);
    }

    // Update orientation (landscape only) from the accelerometer
    updateScreenOrientationFromImu();

    // Draw whichever screen is active (startup, tally, power, setup)
    refreshScreen();

    // Idle dimming: after a period of no activity, dim to powersaverBrightness
    {
        uint32_t nowIdle = millis();   // fresh timestamp (fixes flicker)
        if (!g_isIdleDimmed && eff.idleDimSeconds > 0) {
            uint32_t idleMs = (uint32_t)eff.idleDimSeconds * 1000UL;

            // Determine whether this tally is currently "active" (green/red) for its selected input.
            bool tallyActive = false;
            if (g_tally.selectedInput != 0) {
                // Use TallyState helpers instead of raw field comparisons.
                bool isProg = g_tally.isProgram(g_tally.selectedInput);
                bool isPrev = g_tally.isPreview(g_tally.selectedInput);
                const AtemInputInfo* info = g_tally.currentSelected();

                // Treat missing info as enabled (fail-safe to keep the light bright).
                bool enabled = (info == nullptr) || info->tallyEnabled;

                if (enabled && (isProg || isPrev)) {
                    tallyActive = true;
                }
            }

            if (tallyActive) {
                // Do NOT dim when tally is active (green/red); instead, treat it as activity.
                markUserActivity(eff);
            } else if (nowIdle - g_lastActivityMs > idleMs) {
                uint8_t dimTarget = eff.powersaverBrightness;

                // Only dim down; never brighten here
                if (dimTarget < currentBrightness) {
                    currentBrightness = dimTarget;
                    setBrightness(currentBrightness);
                }
                g_isIdleDimmed = true;
            }
        }
    }

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
