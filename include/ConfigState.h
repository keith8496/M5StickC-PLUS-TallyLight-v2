#pragma once
#include <M5Unified.h>


#ifdef BUILD_DATETIME
static constexpr uint32_t kBuildUnixTime = static_cast<uint32_t>(BUILD_DATETIME);
#else
static constexpr uint32_t kBuildUnixTime = 0;
#endif


// How often we publish status by default (seconds)
constexpr uint16_t DEFAULT_STATUS_INTERVAL_SEC = 60;

// --- Enums ---------------------------------------------------

enum class WifiSleepMode : uint8_t {
    None,
    Modem,
    Light
};

enum class LogLevel : uint8_t {
    None,
    Error,
    Warn,
    Info,
    Debug
};

namespace ConfigDefaults {
    // MQTT / network / time
    constexpr const char* MQTT_SERVER   = "127.0.0.1";
    constexpr const uint16_t    MQTT_PORT     = 1883;
    constexpr const char* MQTT_USERNAME = "";
    constexpr const char* MQTT_PASSWORD = "";
    constexpr const char* NTP_SERVER_DEFAULT    = "us.pool.ntp.org";
    constexpr const char* TIMEZONE      = "Etc/UTC"; // Example time zones: "America/Chicago", "GMT-6"

    // Display / brightness
    constexpr uint8_t BRIGHTNESS             = 50;             // normal mode brightness (default 50%)
    constexpr uint8_t POWERSAVER_BRIGHTNESS  = 20;  // power-saver brightness (default 20%)
    constexpr uint8_t POWERSAVER_BATTERY_PCT = 50;  // enable power-saver mode below this battery %

    // Tally colors
    constexpr const char* TALLY_COLOR_PROGRAM = "#FF0000";
    constexpr const char* TALLY_COLOR_PREVIEW = "#00FF00";

    // Device-specific
    constexpr const char* FRIENDLY_NAME = "CamX";
    constexpr uint8_t ATEAM_INPUT_DEFAULT = 1;
    constexpr uint16_t BATTERY_CAPACITY_MAH = 2200; // mAh

    // Wi-Fi tuning
    constexpr int8_t        WIFI_TX_POWER_DBM = 8; // ESP32 TX power (0..20-ish)
    constexpr WifiSleepMode WIFI_SLEEP        = WifiSleepMode::Modem;

    // Status interval (seconds)
    constexpr uint16_t STATUS_INTERVAL_SEC = DEFAULT_STATUS_INTERVAL_SEC;

    // Idle dimming (seconds of no activity before dim)
    constexpr uint16_t IDLE_DIM_SECONDS = 60;

    // Logging
    constexpr LogLevel LOG_LEVEL = LogLevel::Info;
}

// --- Global (shared) config ---------------------------------

struct GlobalConfig {
    // Network / MQTT / time
    String mqttServer = ConfigDefaults::MQTT_SERVER;
    uint16_t mqttPort = ConfigDefaults::MQTT_PORT;
    String mqttUsername = ConfigDefaults::MQTT_USERNAME;
    String mqttPassword = ConfigDefaults::MQTT_PASSWORD;
    String ntpServer = ConfigDefaults::NTP_SERVER_DEFAULT;
    String timeZone = ConfigDefaults::TIMEZONE;

    // Display / tally brightness (0–100 logical scale)
    // These are "percent-ish" values that map directly into ScreenBreath(0–100).
    uint8_t brightness = ConfigDefaults::BRIGHTNESS;
    uint8_t powersaverBrightness = ConfigDefaults::POWERSAVER_BRIGHTNESS;  
    uint8_t powersaverBatteryPct = ConfigDefaults::POWERSAVER_BATTERY_PCT;  

    String tallyColorProgram = ConfigDefaults::TALLY_COLOR_PROGRAM;
    String tallyColorPreview = ConfigDefaults::TALLY_COLOR_PREVIEW;

    // Wi-Fi tuning
    int8_t wifiTxPowerDbm = ConfigDefaults::WIFI_TX_POWER_DBM;       
    WifiSleepMode wifiSleep = ConfigDefaults::WIFI_SLEEP;
    uint16_t statusIntervalSec = ConfigDefaults::STATUS_INTERVAL_SEC;

    // Idle dimming
    uint16_t idleDimSeconds = ConfigDefaults::IDLE_DIM_SECONDS;
};

// --- Per-device config ---------------------------------------

struct DeviceConfig {
    // Fixed identity for this build
    String deviceId;
    String deviceName;
    
    // Raw Unix time from PlatformIO (-DBUILD_DATETIME=$UNIX_TIME)
    uint32_t buildUnixTime = kBuildUnixTime;

    // Printable version (decimal string)
    String   buildDateTime = String(buildUnixTime);

    // Mutable from MQTT
    String friendlyName = ConfigDefaults::FRIENDLY_NAME;
    uint8_t atemInput = ConfigDefaults::ATEAM_INPUT_DEFAULT; 
    //bool wifi_isConnected = false;
    bool mqtt_isConnected = false;
    bool ntp_isSynchronized = false;

    // Battery / SoC model
    uint16_t batteryCapacityMah = ConfigDefaults::BATTERY_CAPACITY_MAH; // SoC algorithm input

    // Per-device idle dimming override:
    //  - 0xFFFF  => no override, use global idleDimSeconds
    //  - 0..65534 => override value in seconds (0 means "disable idle dimming" for this device)
    uint16_t idleDimSecondsOverride = 0xFFFF;

    // Logging
    LogLevel logLevel = ConfigDefaults::LOG_LEVEL;
};

// --- Effective config (merged view) --------------------------

struct EffectiveConfig {
    // Identity
    String deviceId;
    String deviceName;
    String friendlyName;
    String buildDateTime;

    // Network
    String mqttServer;
    uint16_t mqttPort;
    String mqttUsername;
    String mqttPassword;
    String ntpServer;
    String timeZone;

    // Behaviour + display
    uint8_t brightness;
    uint8_t powersaverBrightness;
    uint8_t powersaverBatteryPct;
    String tallyColorProgram;
    String tallyColorPreview;

    // Wi-Fi tuning
    int8_t wifiTxPowerDbm;
    WifiSleepMode wifiSleep;
    uint16_t statusIntervalSec;

    // Idle dimming
    uint16_t idleDimSeconds;

    // Per-device
    uint8_t atemInput;
    bool wifi_isConnected;
    bool mqtt_isConnected;
    bool ntp_isSynchronized;
    uint16_t batteryCapacityMah;

    // Logging
    LogLevel logLevel;
};

// --- Top-level state container -------------------------------

struct ConfigState {
    GlobalConfig global;
    DeviceConfig device;

    EffectiveConfig effective() const {
        EffectiveConfig e;

        e.deviceId = device.deviceId;
        e.deviceName = device.deviceName;
        e.friendlyName = device.friendlyName;
        e.buildDateTime = device.buildDateTime;

        // Network: apply global config with sane defaults
        e.mqttServer   = global.mqttServer;
        e.mqttPort = global.mqttPort;
        e.mqttUsername = global.mqttUsername;
        e.mqttPassword = global.mqttPassword;

        e.ntpServer = global.ntpServer;
        e.timeZone  = global.timeZone;
        
        e.brightness             = global.brightness;
        e.powersaverBrightness   = global.powersaverBrightness;
        e.powersaverBatteryPct   = global.powersaverBatteryPct;
        e.tallyColorProgram      = global.tallyColorProgram;
        e.tallyColorPreview      = global.tallyColorPreview;

        e.wifiTxPowerDbm    = global.wifiTxPowerDbm;
        e.wifiSleep         = global.wifiSleep;
        e.statusIntervalSec = global.statusIntervalSec;

        // Per-device idle dim override: 0xFFFF means "no override, use global".
        if (device.idleDimSecondsOverride == 0xFFFF) {
            e.idleDimSeconds = global.idleDimSeconds;
        } else {
            e.idleDimSeconds = device.idleDimSecondsOverride;
        }

        e.atemInput          = device.atemInput;
        //e.wifi_isConnected   = device.wifi_isConnected;
        e.mqtt_isConnected   = device.mqtt_isConnected;
        e.ntp_isSynchronized = device.ntp_isSynchronized;
        e.batteryCapacityMah = device.batteryCapacityMah;

        e.logLevel = device.logLevel;

        return e;
    }
};
