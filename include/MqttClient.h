#pragma once

#include <M5Unified.h>
#include <functional>
#include "ConfigState.h"
#include "TallyState.h"

class WiFiClient;
class PubSubClient;

struct StatusSnapshot {
    uint32_t uptimeSec = 0;
    uint16_t batteryMv = 0;
    uint8_t  batteryPct = 0;
    uint8_t  batPercentageCoulomb = 0;
    uint8_t  batPercentageHybrid = 0;
    float    coulombCount = 0;
    int8_t   rssi = 0;
    float    temperatureC = NAN;
    uint32_t restartCount = 0;
    String   firmwareVersion;
    String   hwRevision;
};

// Thin wrapper managing topics + callbacks.
class MqttClient {
public:
    using MessageHandler = std::function<void(const String& topic, const String& payload)>;

    MqttClient(ConfigState& cfg, TallyState& tally);

    // Must be called from setup() after Wi-Fi is up.
    void begin();

    // Call from loop()
    void loop();

    // Publish a status snapshot (will use ConfigState for topics)
    void publishStatus(const StatusSnapshot& status);

    // Publish availability: "online" or "offline"
    void publishAvailability(const String& state);

    // Publish one-off log line if log_level allows
    void publishLog(const String& line, LogLevel level = LogLevel::Info);

    void publishSelectedInput(uint8_t input);

    void publishTallyColor(const String& color);

    // Set callback for *all* inbound topics we care about
    void setMessageHandler(MessageHandler handler) { _onMessage = handler; }

    bool isConnected() const { return _connected; }

private:
    ConfigState& _cfg;
    TallyState&  _tally;
    WiFiClient*  _wifiClient = nullptr;
    PubSubClient* _mqtt = nullptr;
    bool         _connected = false;
    uint32_t     _lastReconnectAttemptMs = 0;

    MessageHandler _onMessage;

    // Debounce state for /status/input publishes
    uint8_t  _pendingSelectedInput            = 0;
    bool     _hasPendingSelectedInput         = false;
    uint32_t _pendingSelectedInputChangedAtMs = 0;
    uint8_t  _lastPublishedSelectedInput      = 0;

    // Debounce state for /status/tally publishes
    String   _pendingTallyColor;
    bool     _hasPendingTallyColor            = false;
    uint32_t _pendingTallyColorChangedAtMs    = 0;
    String   _lastPublishedTallyColor;

    // Internal helpers
    void setupClient();
    void ensureConnected();
    bool connectOnce();
    void subscribeAll();

    // PubSub callback
    static void _mqttCallbackThunk(char* topic, uint8_t* payload, unsigned int length);
    void handleIncoming(const char* topic, const uint8_t* payload, unsigned int length);

    // Topic builders
    String topicDeviceRoot() const;       // sanctuary/tally/{device}
    String topicDeviceConfigRoot() const; // sanctuary/tally/{device}/config
};

// Global logging helper: prints to Serial and, if MQTT is connected,
// publishes to the device's log topic using MqttClient::publishLog().
void logf(LogLevel level, const char* fmt, ...);