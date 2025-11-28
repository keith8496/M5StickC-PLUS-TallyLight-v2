#include <ArduinoJson.h>
#include "MqttRouter.h"
#include "MqttClient.h"
extern MqttClient g_mqtt;

// Spec constants
static const char* TOPIC_ATEM_PREVIEW = "sanctuary/atem/preview";
static const char* TOPIC_ATEM_PROGRAM = "sanctuary/atem/program";
static const char* TOPIC_ATEM_INPUTS  = "sanctuary/atem/inputs";

static const char* TOPIC_GLOBAL_CONFIG_ROOT = "sanctuary/tally/config";
static const char* TOPIC_ALL_CMD            = "sanctuary/tally/all/cmd";

// ---------- Helpers -------------------------------------------------

static String toLowerCopy(const String& in) {
    String s = in;
    s.toLowerCase();
    return s;
}

static WifiSleepMode parseWifiSleep(const String& s) {
    String v = toLowerCopy(s);
    if (v == "none")  return WifiSleepMode::None;
    if (v == "light") return WifiSleepMode::Light;
    if (v == "modem") return WifiSleepMode::Modem;
    return WifiSleepMode::Modem; // default
}

static LogLevel parseLogLevel(const String& s) {
    String v = toLowerCopy(s);
    if (v == "none")  return LogLevel::None;
    if (v == "error") return LogLevel::Error;
    if (v == "warn")  return LogLevel::Warn;
    if (v == "info")  return LogLevel::Info;
    if (v == "debug") return LogLevel::Debug;
    return LogLevel::Info;
}

static MqttCommandType parseCommand(const String& payload) {
    String v = toLowerCopy(payload);
    if (v == "deep_sleep")   return MqttCommandType::DeepSleep;
    if (v == "wakeup")       return MqttCommandType::Wakeup;
    if (v == "reboot")       return MqttCommandType::Reboot;
    if (v == "ota_update")   return MqttCommandType::OtaUpdate;
    if (v == "factory_reset")return MqttCommandType::FactoryReset;
    return MqttCommandType::None;
}

// ---------- ATEM routing --------------------------------------------

static void handleAtemMessage(TallyState& tally, const String& topic, const String& payload) {
    if (topic == TOPIC_ATEM_PROGRAM) {
        tally.programInput = static_cast<uint8_t>(payload.toInt());
        return;
    }
    if (topic == TOPIC_ATEM_PREVIEW) {
        tally.previewInput = static_cast<uint8_t>(payload.toInt());
        return;
    }
    if (topic == TOPIC_ATEM_INPUTS) {
        Serial.printf("[MQTT] ATEM_INPUTS topic received, payload length=%u\n", payload.length());

        JsonDocument doc;  // ArduinoJson 7: elastic capacity on heap
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            Serial.printf("[MQTT] ATEM inputs JSON parse failed: %s (len=%u)\n",
                        err.c_str(), payload.length());
            return;
        }

        tally.inputs.clear();

        JsonObject root = doc.as<JsonObject>();
        for (JsonPair kv : root) {
            const char* key = kv.key().c_str();   // "1", "2", ...
            uint8_t id = static_cast<uint8_t>(atoi(key));
            JsonObject obj = kv.value().as<JsonObject>();

            AtemInputInfo info;
            info.id = id;

            info.shortName = obj["short_name"].as<String>();
            info.longName  = obj["long_name"].as<String>();

            String enabledStr = obj["tally_enabled"] | "FALSE";
            enabledStr.toLowerCase();
            info.tallyEnabled = (enabledStr == "true");

            tally.inputs[id] = info;

            Serial.printf(
                "[MQTT] ATEM input %u: short=\"%s\" long=\"%s\" enabled=%d\n",
                id,
                info.shortName.c_str(),
                info.longName.c_str(),
                info.tallyEnabled ? 1 : 0
            );
        }

        tally.normalizeSelected();

        unsigned enabledCount = 0;

        // tally.inputs is a std::map<uint8_t, AtemInputInfo>
        for (std::map<uint8_t, AtemInputInfo>::const_iterator it = tally.inputs.begin();
            it != tally.inputs.end();
            ++it) {
            if (it->second.tallyEnabled) {
                ++enabledCount;
            }
        }

        Serial.printf(
            "[MQTT] ATEM inputs loaded, enabled_count=%u, total=%u\n",
            enabledCount,
            (unsigned)tally.inputs.size()
        );
    }
}

// ---------- Global config routing -----------------------------------

static void handleGlobalConfig(ConfigState& cfg, const String& key, const String& payload) {
    // key is the part after sanctuary/tally/config/
    if (key == "mqtt_server") {
        cfg.global.mqttServer = payload;
    } else if (key == "mqtt_port") {
        cfg.global.mqttPort = static_cast<uint16_t>(payload.toInt());
    } else if (key == "mqtt_username") {
        cfg.global.mqttUsername = payload;
    } else if (key == "mqtt_password") {
        cfg.global.mqttPassword = payload;
    } else if (key == "ntp_server") {
        cfg.global.ntpServer = payload;
    } else if (key == "timezone") {
        cfg.global.timeZone = payload;
    }

    // Display / tally
    else if (key == "brightness") {
        int v = payload.toInt();
        if (v >= 0 && v <= 100) {
            cfg.global.brightness = static_cast<uint8_t>(v);
        }
    } else if (key == "powersaver_brightness") {
        int v = payload.toInt();
        if (v >= 0 && v <= 100) {
            cfg.global.powersaverBrightness = static_cast<uint8_t>(v);
        }
    } else if (key == "powersaver_battery_pct") {
        int v = payload.toInt();
        if (v >= 0 && v <= 100) cfg.global.powersaverBatteryPct = static_cast<uint8_t>(v);
    } else if (key == "tally_color_program") {
        cfg.global.tallyColorProgram = payload;
    } else if (key == "tally_color_preview") {
        cfg.global.tallyColorPreview = payload;
    }

    // Wi-Fi tuning
    else if (key == "wifi_tx_power") {
        cfg.global.wifiTxPowerDbm = static_cast<int8_t>(payload.toInt());
    } else if (key == "wifi_sleep") {
        cfg.global.wifiSleep = parseWifiSleep(payload);
    } else if (key == "status_interval") {
        uint16_t v = static_cast<uint16_t>(payload.toInt());
        if (v == 0) v = DEFAULT_STATUS_INTERVAL_SEC;
        cfg.global.statusIntervalSec = v;
    }

    // Logging
    else if (key == "log_level") {
        cfg.global.logLevel = parseLogLevel(payload);
    }

    // OTA (future) – we just store them, even though we don't act on them yet
    else if (key == "firmware_url") {
        // optional: store in a global String if you add one later
        // e.g. cfg.global.firmwareUrl = payload;
    } else if (key == "firmware_auto") {
        // optional: parse as bool and store if you add it to ConfigState
    }
}

// ---------- Per-device config routing -------------------------------

static void handleDeviceConfig(ConfigState& cfg, const String& key, const String& payload) {
    // key is the part after sanctuary/tally/{device}/config/
    if (key == "name") {
        cfg.device.friendlyName = payload;
    } else if (key == "input") {
        int v = payload.toInt();
        if (v >= 0 && v <= 255) {
            cfg.device.atemInput = static_cast<uint8_t>(v);
        }
    } else if (key == "battery_capacity") {
        int v = payload.toInt();
        if (v > 0 && v < 100000) {
            cfg.device.batteryCapacityMah = static_cast<uint16_t>(v);
        }
    }
}

// ---------- Main router ---------------------------------------------

void handleMqttMessage(
    ConfigState& cfg,
    TallyState& tally,
    const String& topic,
    const String& payload,
    MqttCommand& outCommand
) {
    outCommand.type = MqttCommandType::None;

    // 1) ATEM state topics
    if (topic == TOPIC_ATEM_PREVIEW ||
        topic == TOPIC_ATEM_PROGRAM ||
        topic == TOPIC_ATEM_INPUTS) {

        handleAtemMessage(tally, topic, payload);
        return;
    }

    // 2) Global config: sanctuary/tally/config/...
    const String globalRoot = String(TOPIC_GLOBAL_CONFIG_ROOT) + "/";
    if (topic.startsWith(globalRoot)) {
        String key = topic.substring(globalRoot.length()); // part after config/
        handleGlobalConfig(cfg, key, payload);
        return;
    }

    // 3) Commands (global / per-device)
    if (topic == TOPIC_ALL_CMD) {
        outCommand.type = parseCommand(payload);
        return;
    }

    // Per-device base: sanctuary/tally/{device}
    String devRoot = "sanctuary/tally/" + cfg.device.deviceId;

    // 3a) Per-device command: sanctuary/tally/{device}/cmd
    String devCmd = devRoot + "/cmd";
    if (topic == devCmd) {
        outCommand.type = parseCommand(payload);
        return;
    }

    // 4) Per-device config: sanctuary/tally/{device}/config/...
    String devCfgRoot = devRoot + "/config/";
    if (topic.startsWith(devCfgRoot)) {
        String key = topic.substring(devCfgRoot.length()); // part after config/
        handleDeviceConfig(cfg, key, payload);

        // If the per-device input was changed via MQTT, sync it into TallyState
        if (key == "input") {
            uint8_t v = cfg.device.atemInput;
            tally.selectedInput = v;
            tally.normalizeSelected();
            Serial.printf("[MQTT] config/input set to %u, publishing status\n", cfg.device.atemInput);
            g_mqtt.publishSelectedInput(cfg.device.atemInput);
        }

        return;
    }

    // Anything else can be ignored or logged by caller if desired.
}
