#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <stdarg.h>

#include "MqttClient.h"

extern ConfigState g_config;

// --- Constants ------------------------------------------------

static const char* TOPIC_ATEM_PREVIEW   = "sanctuary/atem/preview";
static const char* TOPIC_ATEM_PROGRAM   = "sanctuary/atem/program";
static const char* TOPIC_ATEM_INPUTS    = "sanctuary/atem/inputs";

static const char* TOPIC_GLOBAL_CONFIG_ROOT = "sanctuary/tally/config";
static const char* TOPIC_ALL_CMD            = "sanctuary/tally/all/cmd";

static const char* AVAILABILITY_SUBTOPIC = "availability";
static const char* STATUS_ROOT_SUBTOPIC  = "status";

static const char* MQTT_USERNAME = "REDACTED_USERNAME";   //temp
static const char* MQTT_PASSWORD = "REDACTED_PASSWORD";  //temp

static const uint32_t RECONNECT_INTERVAL_MS = 5000;

// We’ll use this static pointer so PubSubClient can call back into the instance
static MqttClient* s_instance = nullptr;

// Global logging helper implementation. This uses Serial for local debug and,
// when MQTT is connected, forwards the line to the device's log topic via
// MqttClient::publishLog().
void logf(LogLevel level, const char* fmt, ...) {
    char buf[192];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Always print to Serial (if available)
    Serial.print(buf);

    // If we have an MQTT client instance and it's marked connected, try to
    // forward the log line over MQTT as well.
    if (s_instance && g_config.device.mqtt_isConnected) {
        s_instance->publishLog(String(buf), level);
    }
}

// --- Ctor -----------------------------------------------------

MqttClient::MqttClient(ConfigState& cfg, TallyState& tally)
: _cfg(cfg), _tally(tally)
{
    s_instance = this;
}

// --- Public API -----------------------------------------------

void MqttClient::begin() {
    setupClient();
    // First connect attempt
    ensureConnected();
}

void MqttClient::loop() {
    if (!_mqtt) return;

    if (!_mqtt->loop()) {
        // Lost connection
        _connected = false;
    }

    if (!_connected) {
        g_config.device.mqtt_isConnected = false;
        uint32_t now = millis();
        if (now - _lastReconnectAttemptMs > RECONNECT_INTERVAL_MS) {
            _lastReconnectAttemptMs = now;
            ensureConnected();
        }
    }

    // Debounced publish for status/input
    if (_hasPendingSelectedInput && _connected && _mqtt) {
        constexpr uint32_t DEBOUNCE_INPUT_MS = 150;
        uint32_t now = millis();
        if (now - _pendingSelectedInputChangedAtMs >= DEBOUNCE_INPUT_MS &&
            _pendingSelectedInput != _lastPublishedSelectedInput) {
            const String root = topicDeviceRoot() + "/" + STATUS_ROOT_SUBTOPIC + "/";
            String topic   = root + "input";
            String payload = String(_pendingSelectedInput);
            _mqtt->publish(topic.c_str(), payload.c_str(), false);
            _lastPublishedSelectedInput  = _pendingSelectedInput;
            _hasPendingSelectedInput     = false;
        }
    }

    // Debounced publish for status/tally
    if (_hasPendingTallyColor && _connected && _mqtt) {
        constexpr uint32_t DEBOUNCE_TALLY_MS = 100;
        uint32_t now = millis();
        if (now - _pendingTallyColorChangedAtMs >= DEBOUNCE_TALLY_MS &&
            _pendingTallyColor != _lastPublishedTallyColor) {
            const String root = topicDeviceRoot() + "/" + STATUS_ROOT_SUBTOPIC + "/";
            String topic = root + "tally";
            _mqtt->publish(topic.c_str(), _pendingTallyColor.c_str(), false);
            _lastPublishedTallyColor  = _pendingTallyColor;
            _hasPendingTallyColor     = false;
        }
    }
}

void MqttClient::publishAvailability(const String& state) {
    if (!_connected) return;

    String topic = topicDeviceRoot() + "/" + AVAILABILITY_SUBTOPIC;
    _mqtt->publish(topic.c_str(), state.c_str(), true); // retained
}

void MqttClient::publishStatus(const StatusSnapshot& st)
{
    if (!_connected) return;

    const String root = topicDeviceRoot() + "/" + STATUS_ROOT_SUBTOPIC + "/";

    auto pub = [&](const String& sub, const String& value) {
        String topic = root + sub;
        _mqtt->publish(topic.c_str(), value.c_str(), false);
    };

    // Core status
    pub("uptime", String(st.uptimeSec));

    // Battery metrics
    pub("battery_mv", String(st.batteryMv));
    pub("battery_pct", String(st.batteryPct));
    pub("battery_pct_coulomb", String(st.batPercentageCoulomb));
    pub("battery_pct_hybrid", String(st.batPercentageHybrid));
    pub("battery_capacity_mah", String(st.batterCampacityMah));

    // Radio / environment
    pub("rssi", String(st.rssi));
    if (!isnan(st.temperatureC)) {
        pub("temperature", String(st.temperatureC, 1));
    }

    // Device metadata
    pub("restarts", String(st.restartCount));
    if (st.firmwareVersion.length()) {
        pub("firmware_version", st.firmwareVersion);
    }
    if (st.hwRevision.length()) {
        pub("hw_revision", st.hwRevision);
    }
}

void MqttClient::publishSelectedInput(uint8_t input) {
    // Schedule a debounced publish of the selected input.
    _pendingSelectedInput            = input;
    _hasPendingSelectedInput         = true;
    _pendingSelectedInputChangedAtMs = millis();
}

void MqttClient::publishTallyColor(const String& color) {
    _pendingTallyColor            = color;
    _hasPendingTallyColor         = true;
    _pendingTallyColorChangedAtMs = millis();
}

void MqttClient::publishLog(const String& line, LogLevel level) {
    if (!_connected) return;

    // Respect global log level
    if (static_cast<uint8_t>(level) > static_cast<uint8_t>(_cfg.device.logLevel)) {
        return;
    }

    String topic = topicDeviceRoot() + "/" + STATUS_ROOT_SUBTOPIC + "/log";
    _mqtt->publish(topic.c_str(), line.c_str(), false);
}

// --- Internal setup -------------------------------------------

void MqttClient::setupClient() {
    if (_wifiClient || _mqtt) return;

    _wifiClient = new WiFiClient();
    _mqtt = new PubSubClient(*_wifiClient);

    // ✅ Use the underlying persistent config instead
    _mqtt->setServer(_cfg.global.mqttServer.c_str(), _cfg.global.mqttPort);
    _mqtt->setCallback(&MqttClient::_mqttCallbackThunk);
}

void MqttClient::ensureConnected() {
    if (_connected || !_mqtt) return;

    // Don’t even try if Wi-Fi is down; avoids useless DNS/TCP attempts
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    if (connectOnce()) {
        _connected = true;
        g_config.device.mqtt_isConnected = true;
        subscribeAll();
        publishAvailability("online");
        publishLog("MQTT connected");
    }
}

bool MqttClient::connectOnce() {
    const auto eff = _cfg.effective();

    // Client ID = deviceId + random suffix
    String clientId = eff.deviceId + "-" + String((uint32_t)esp_random(), HEX);

    // LWT topic
    String lwtTopic = topicDeviceRoot() + "/" + AVAILABILITY_SUBTOPIC;
    const char* lwtPayload = "offline";

    if (eff.mqttUsername.length() > 0) {
        return _mqtt->connect(
            clientId.c_str(),
            eff.mqttUsername.c_str(),
            eff.mqttPassword.c_str(),
            lwtTopic.c_str(),
            0,      // qos
            true,   // retained
            lwtPayload
        );
    } else {
        return _mqtt->connect(
            clientId.c_str(),
            lwtTopic.c_str(),
            0,
            true,
            lwtPayload
        );
    }
}

void MqttClient::subscribeAll() {
    const auto eff = _cfg.effective();

    // 1) ATEM topics (global)
    _mqtt->subscribe(TOPIC_ATEM_PREVIEW);
    _mqtt->subscribe(TOPIC_ATEM_PROGRAM);
    _mqtt->subscribe(TOPIC_ATEM_INPUTS);

    // 2) Global config
    // subscribe to sanctuary/tally/config/#
    String globalCfg = String(TOPIC_GLOBAL_CONFIG_ROOT) + "/#";
    _mqtt->subscribe(globalCfg.c_str());

    // 3) Global commands
    _mqtt->subscribe(TOPIC_ALL_CMD);

    // 4) Per-device config + commands
    String devConfigRoot = topicDeviceConfigRoot() + "/#";  // sanctuary/tally/{device}/config/#
    _mqtt->subscribe(devConfigRoot.c_str());

    String devCmd = topicDeviceRoot() + "/cmd";
    _mqtt->subscribe(devCmd.c_str());
}

// --- Topic helpers --------------------------------------------

String MqttClient::topicDeviceRoot() const {
    return "sanctuary/tally/" + _cfg.device.deviceId;
}

String MqttClient::topicDeviceConfigRoot() const {
    return topicDeviceRoot() + "/config";
}

// --- Callback plumbing ----------------------------------------

void MqttClient::_mqttCallbackThunk(char* topic, uint8_t* payload, unsigned int length) {
    if (s_instance) {
        s_instance->handleIncoming(topic, payload, length);
    }
}

void MqttClient::handleIncoming(const char* topic, const uint8_t* payload, unsigned int length) {
    String t(topic);
    String p;
    p.reserve(length + 1);
    for (unsigned int i = 0; i < length; ++i) {
        p += static_cast<char>(payload[i]);
    }

    // Simple routing: just hand everything to user-provided handler.
    // Higher-level parsing (config vs commands vs atem vs inputs)
    // will live outside this class for now.
    if (_onMessage) {
        _onMessage(t, p);
    }
}
