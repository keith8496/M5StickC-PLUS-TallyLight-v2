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

    // Display / tally colours
    uint8_t brightness = 5;   // 0–7 or 0–10 depending on your mapping
    uint8_t powersaverBrightness = 2;
    uint8_t powersaverBatteryPct = 20; // %

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
    String deviceId;          // e.g. "m5stick-7A90F3"

    // Mutable from MQTT
    String friendlyName;      // e.g. "Camera 3 Left"
    uint8_t atemInput = 0;    // "3" in MQTT → 3

    // Battery / SoC model
    uint16_t batteryCapacityMah = 2200; // SoC algorithm input
};

// --- Effective config (merged view) --------------------------

struct EffectiveConfig {
    // Identity
    String deviceId;
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
        e.batteryCapacityMah = device.batteryCapacityMah;

        e.logLevel = global.logLevel;

        return e;
    }
};
