#include <WiFiManager.h>

#include <Preferences.h>
#include "PrefsModule.h"
#include "NetworkModule.h"


WiFiManagerParameter wm_mqtt_server("mqtt_server", "MQTT Server IP/Host");
WiFiManagerParameter wm_mqtt_port("mqtt_port", "MQTT Server Port");
WiFiManagerParameter wm_mqtt_username("mqtt_username", "MQTT Username");
WiFiManagerParameter wm_mqtt_password("mqtt_password", "MQTT Password");


Preferences preferences;
char mqtt_server[32];
int mqtt_port;
char mqtt_username[32];
char mqtt_password[32];

void preferences_setup() {
    
    preferences.begin("custom", true);
    snprintf(mqtt_server, sizeof(mqtt_server), "%s", preferences.getString("mqtt_server", "172.16.30.11").c_str());
    mqtt_port = preferences.getInt("mqtt_port", 1883);
    snprintf(mqtt_username, sizeof(mqtt_username), "%s", preferences.getString("mqtt_username", "").c_str());
    snprintf(mqtt_password, sizeof(mqtt_password), "%s", preferences.getString("mqtt_password", "").c_str());
    
    preferences.end();

    // wm_addParameters
    wm.addParameter(&wm_mqtt_server);
    wm.addParameter(&wm_mqtt_port);
    wm.addParameter(&wm_mqtt_username);
    wm.addParameter(&wm_mqtt_password);
    
    // set wm values
    constexpr size_t BUFF_MAX_LEN   = 32;
    char buff[BUFF_MAX_LEN + 1];
    wm_mqtt_server.setValue(mqtt_server, sizeof(mqtt_server));
    itoa(mqtt_port, buff, 10);
    wm_mqtt_port.setValue(buff, sizeof(buff));
    wm_mqtt_username.setValue(mqtt_username, sizeof(mqtt_username));
    wm_mqtt_password.setValue(mqtt_password, sizeof(mqtt_password));

}


void preferences_save() {
    preferences.begin("custom", false);
    preferences.putString("mqtt_server", mqtt_server);
    preferences.putInt("mqtt_port", mqtt_port);
    preferences.putString("mqtt_username", mqtt_username);
    preferences.putString("mqtt_password", mqtt_password);

    preferences.end();
}


void WiFi_onSaveParams() {

    snprintf(mqtt_server, sizeof(mqtt_server), "%s", wm_mqtt_server.getValue());
    mqtt_port = atoi(wm_mqtt_port.getValue());
    snprintf(mqtt_username, sizeof(mqtt_username), "%s", wm_mqtt_username.getValue());
    snprintf(mqtt_password, sizeof(mqtt_password), "%s", wm_mqtt_password.getValue());

    preferences_save();

}


// Bridge: copy values loaded by preferences_setup() into ConfigState
void prefs_applyToConfig(ConfigState& cfg) {
    // MQTT server defaults:
    cfg.global.mqttServer = String(mqtt_server);
    cfg.global.mqttPort   = mqtt_port;  // Mosquitto default
    cfg.global.mqttUsername = String(mqtt_username);
    cfg.global.mqttPassword = String(mqtt_password);
}