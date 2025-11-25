#pragma once

#include <Arduino.h>
#include <functional>
#include "ConfigState.h"
#include "TallyState.h"

class WiFiClient;
class PubSubClient;

struct StatusSnapshot {
    uint32_t uptimeSec = 0;
    uint8_t  batteryPct = 0;
    uint16_t batteryMv = 0;
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
