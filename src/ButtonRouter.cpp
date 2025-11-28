#include "ButtonRouter.h"

#include "ConfigState.h"
#include "MqttClient.h"

#include <M5Unified.h>
#include "TallyState.h"
#include "ScreenModule.h"  // changeScreen()

extern MqttClient g_mqtt;

// cycleBrightness() currently lives in main.cpp in your project.
// Forward-declare it here; linker will resolve it.
void cycleBrightness();

ButtonRouter::ButtonRouter(ConfigState& cfg, TallyState& tally)
    : _cfg(cfg), _tally(tally) {
}

void ButtonRouter::handle(const ButtonEvent& ev) {
    if (ev.type == ButtonType::None || ev.id == ButtonID::None) {
        return;
    }

    switch (ev.id) {
        case ButtonID::A:
            if (ev.type == ButtonType::ShortPress) {
                // BtnA = cycle ATEM input
                _tally.selectNextInput();

                const AtemInputInfo* sel = _tally.currentSelected();
                if (sel) {
                    Serial.printf(
                        "BtnA -> selected input: %u %s (%s)\n",
                        sel->id,
                        sel->shortName.c_str(),
                        sel->longName.c_str()
                    );
                    _cfg.device.atemInput = sel->id;
                    Serial.printf("BtnA -> syncing cfg.device.atemInput=%u and publishing to MQTT\n", sel->id);
                    g_mqtt.publishSelectedInput(sel->id);
                } else {
                    Serial.println("BtnA -> no tally-enabled inputs available");
                    _cfg.device.atemInput = 0;
                    Serial.println("BtnA -> no tally-enabled inputs, setting atemInput=0 and publishing to MQTT");
                    g_mqtt.publishSelectedInput(0);
                }
            }
            // (Long press on A currently unused; easy to add later.)
            break;

        case ButtonID::B:
            if (ev.type == ButtonType::LongPress) {
                // BtnB long = change screen
                Serial.println("BtnB long press -> changeScreen(-1)");
                changeScreen(-1);
            } else if (ev.type == ButtonType::ShortPress) {
                // BtnB short = brightness
                Serial.println("BtnB short press -> cycleBrightness()");
                cycleBrightness();
            }
            break;

        case ButtonID::None:
        default:
            break;
    }
}