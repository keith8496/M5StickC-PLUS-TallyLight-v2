#pragma once

#include <Arduino.h>

// How often we publish status by default (seconds)
constexpr uint16_t DEFAULT_STATUS_INTERVAL_SEC = 30;

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

// --- Global (shared) config ---------------------------------

struct GlobalConfig {
    // Network / MQTT / time
    String wifiSsid;
    String wifiPassword;
    String mqttServer;
    uint16_t mqttPort = 1883;
    String mqttUsername;
    String mqttPassword;
    String ntpServer;
    String timezone;          // e.g. "America/Chicago" or "GMT-6"

    // Display / tally brightness (0–100 logical scale)
    // These are "percent-ish" values that map directly into ScreenBreath(0–100).
    uint8_t brightness = 50;            // normal mode brightness (default 50%)
    uint8_t powersaverBrightness = 20;  // power-saver brightness (default 20%)
    uint8_t powersaverBatteryPct = 25;  // enable power-saver mode below this battery %

    String tallyColorProgram = "#FF0000";
    String tallyColorPreview = "#00FF00";

    // Wi-Fi tuning
    int8_t wifiTxPowerDbm = 8;       // ESP32 TX power (0..20-ish)
    WifiSleepMode wifiSleep = WifiSleepMode::Modem;
    uint16_t statusIntervalSec = DEFAULT_STATUS_INTERVAL_SEC;

    // Logging
    LogLevel logLevel = LogLevel::Info;
};

// --- Per-device config ---------------------------------------

struct DeviceConfig {
    // Fixed identity for this build
    String deviceId;          // e.g. "7A90F3"
    String deviceName;        // e.g. "m5stick-7A90F3"

    // Mutable from MQTT
    String friendlyName;      // e.g. "Camera 3 Left"
    uint8_t atemInput = 0;    // "3" in MQTT → 3
    bool mqtt_isConnected;

    // Battery / SoC model
    uint16_t batteryCapacityMah = 2200; // SoC algorithm input
};

// --- Effective config (merged view) --------------------------

struct EffectiveConfig {
    // Identity
    String deviceId;
    String deviceName;
    String friendlyName;

    // Network
    String wifiSsid;
    String wifiPassword;
    String mqttServer;
    uint16_t mqttPort;
    String mqttUsername;
    String mqttPassword;
    String ntpServer;
    String timezone;

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

    // Per-device
    uint8_t atemInput;
    bool mqtt_isConnected;
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
        e.friendlyName = device.friendlyName.length() ? device.friendlyName : device.deviceId;

        e.wifiSsid       = global.wifiSsid;
        e.wifiPassword   = global.wifiPassword;
        e.mqttServer     = global.mqttServer;
        e.mqttPort       = global.mqttPort;
        e.mqttUsername   = global.mqttUsername;
        e.mqttPassword   = global.mqttPassword;
        e.ntpServer      = global.ntpServer;
        e.timezone       = global.timezone;

        e.brightness             = global.brightness;
        e.powersaverBrightness   = global.powersaverBrightness;
        e.powersaverBatteryPct   = global.powersaverBatteryPct;
        e.tallyColorProgram      = global.tallyColorProgram;
        e.tallyColorPreview      = global.tallyColorPreview;

        e.wifiTxPowerDbm   = global.wifiTxPowerDbm;
        e.wifiSleep        = global.wifiSleep;
        e.statusIntervalSec = global.statusIntervalSec;

        e.atemInput          = device.atemInput;
        e.mqtt_isConnected   = device.mqtt_isConnected;
        e.batteryCapacityMah = device.batteryCapacityMah;

        e.logLevel = global.logLevel;

        return e;
    }
};
