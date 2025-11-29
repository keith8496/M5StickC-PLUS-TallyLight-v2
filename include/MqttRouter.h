#pragma once

#include <M5Unified.h>
#include "ConfigState.h"
#include "TallyState.h"

// Commands that higher-level code should respond to
enum class MqttCommandType : uint8_t {
    None,
    DeepSleep,
    Wakeup,
    Reboot,
    OtaUpdate,
    FactoryReset,
    ResyncTime
};

struct MqttCommand {
    MqttCommandType type = MqttCommandType::None;
};

// Route and handle a single MQTT message.
// - Updates cfg and tally in-place
// - Fills outCommand if a command (deep_sleep, reboot, etc.) was received
void handleMqttMessage(
    ConfigState& cfg,
    TallyState& tally,
    const String& topic,
    const String& payload,
    MqttCommand& outCommand
);
