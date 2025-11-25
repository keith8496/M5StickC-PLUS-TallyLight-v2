#include <WiFiManager.h>
#include <Preferences.h>
#include "ConfigState.h"
#include "PrefsModule.h"
#include "NetworkModule.h"
#include "WebSocketsModule.h"


WiFiManagerParameter wm_friendlyName("friendlyName", "Friendly Name");
WiFiManagerParameter wm_nodeRED_ServerIP("nr_ServerIP", "Node-RED Server IP");
WiFiManagerParameter wm_nodeRED_ServerPort("nr_ServerPort", "Node-RED Server Port");
WiFiManagerParameter wm_nodeRED_ServerUrl("nr_ServerUrl", "Node-RED Server URL");
WiFiManagerParameter wm_ntpServer("ntpServer", "NTP Server");
WiFiManagerParameter wm_localTimeZone("localTimeZone", "Local Timezone (restart req)");
WiFiManagerParameter wm_batteryCapacity("batteryCapacity", "Battery Capacity (mAh)");
WiFiManagerParameter wm_pmPowerSaverBatt("pmPowerSaverBatt", "Power Saver Battery Level");
WiFiManagerParameter wm_pmPowerSaverBright("pmPowerSaverBright", "Power Saver Screen Brightness");

WiFiManagerParameter wm_mqtt_server("mqtt_server", "MQTT Server IP/Host");
WiFiManagerParameter wm_mqtt_port("mqtt_port", "MQTT Server Port");
WiFiManagerParameter wm_mqtt_username("mqtt_username", "MQTT Username");
WiFiManagerParameter wm_mqtt_password("mqtt_password", "MQTT Password");


Preferences preferences;
char deviceId[DEVICE_ID_MAX_LEN + 1];
char deviceName[DEVICE_NAME_MAX_LEN + 1];
char friendlyName[FRIENDLY_NAME_MAX_LEN + 1];
char nodeRED_ServerIP[IP_STR_MAX_LEN + 1];
char nodeRED_ServerUrl[URL_MAX_LEN + 1];
char localTimeZone[TIMEZONE_MAX_LEN + 1];
char ntpServer[NTP_SERVER_MAX_LEN + 1];
int nodeRED_ServerPort;
int batteryCapacity;
int pmPowerSaverBatt;
int pmPowerSaverBright;

char mqtt_server[32];
int mqtt_port;
char mqtt_username[32];
char mqtt_password[32];

void preferences_setup() {
    
    preferences.begin("custom", true);
    snprintf(friendlyName, sizeof(friendlyName), "%s", preferences.getString("friendlyName", "CamX").c_str());
    snprintf(nodeRED_ServerIP, sizeof(nodeRED_ServerIP), "%s", preferences.getString("nr_ServerIP", "172.16.30.54").c_str());
    nodeRED_ServerPort = preferences.getInt("nr_ServerPort", 1880);
    snprintf(nodeRED_ServerUrl, sizeof(nodeRED_ServerUrl), "%s", preferences.getString("nr_ServerUrl", "/ws/tally").c_str());
    snprintf(localTimeZone, sizeof(localTimeZone), "%s", preferences.getString("localTimeZone", "America/Chicago").c_str());
    snprintf(ntpServer, sizeof(ntpServer), "%s", preferences.getString("ntpServer", "time.apple.com").c_str());
    batteryCapacity = preferences.getInt("batteryCapacity", 2200);
    pmPowerSaverBatt = preferences.getInt("pmPowerSaverBatt", 25);
    pmPowerSaverBright = preferences.getInt("pmPowerSaverBright", 30);
    
    snprintf(mqtt_server, sizeof(mqtt_server), "%s", preferences.getString("mqtt_server", "172.16.30.11").c_str());
    mqtt_port = preferences.getInt("mqtt_port", 1883);
    snprintf(mqtt_username, sizeof(mqtt_username), "%s", preferences.getString("mqtt_username", "").c_str());
    snprintf(mqtt_password, sizeof(mqtt_password), "%s", preferences.getString("mqtt_password", "").c_str()); // move me soon!
    
    preferences.end();

    // wm_addParameters
    wm.addParameter(&wm_friendlyName);
    wm.addParameter(&wm_nodeRED_ServerIP);
    wm.addParameter(&wm_nodeRED_ServerPort);
    wm.addParameter(&wm_nodeRED_ServerUrl);
    wm.addParameter(&wm_ntpServer);
    wm.addParameter(&wm_localTimeZone);
    wm.addParameter(&wm_batteryCapacity);
    wm.addParameter(&wm_pmPowerSaverBatt);
    wm.addParameter(&wm_pmPowerSaverBright);
    wm.addParameter(&wm_mqtt_server);
    wm.addParameter(&wm_mqtt_port);
    wm.addParameter(&wm_mqtt_username);
    wm.addParameter(&wm_mqtt_password);
    
    // set wm values
    constexpr size_t BUFF_MAX_LEN   = 32;
    char buff[BUFF_MAX_LEN + 1];
    wm_friendlyName.setValue(friendlyName, sizeof(friendlyName));
    //ultoa(inputIds, buff, 2);
    //wm_inputIds.setValue(buff, sizeof(buff));
    wm_nodeRED_ServerIP.setValue(nodeRED_ServerIP, sizeof(nodeRED_ServerIP));
    itoa(nodeRED_ServerPort, buff, 10);
    wm_nodeRED_ServerPort.setValue(buff, sizeof(buff));
    wm_nodeRED_ServerUrl.setValue(nodeRED_ServerUrl, sizeof(nodeRED_ServerUrl));
    wm_ntpServer.setValue(ntpServer, sizeof(ntpServer));
    wm_localTimeZone.setValue(localTimeZone, sizeof(localTimeZone));
    itoa(batteryCapacity, buff, 10);
    wm_batteryCapacity.setValue(buff, sizeof(buff));
    itoa(pmPowerSaverBatt, buff, 10);
    wm_pmPowerSaverBatt.setValue(buff, sizeof(buff));
    itoa(pmPowerSaverBright, buff, 10);
    wm_pmPowerSaverBright.setValue(buff, sizeof(buff));

    wm_mqtt_server.setValue(mqtt_server, sizeof(mqtt_server));
    itoa(mqtt_port, buff, 10);
    wm_mqtt_port.setValue(buff, sizeof(buff));
    wm_mqtt_username.setValue(mqtt_username, sizeof(mqtt_username));
    wm_mqtt_password.setValue(mqtt_password, sizeof(mqtt_password));

}


void preferences_save() {
    preferences.begin("custom", false);
    preferences.putString("friendlyName", friendlyName);
    preferences.putString("nr_ServerIP", nodeRED_ServerIP);
    preferences.putInt("nr_ServerPort", nodeRED_ServerPort);
    preferences.putString("nr_ServerUrl", nodeRED_ServerUrl);
    preferences.putString("ntpServer", ntpServer);
    preferences.putString("localTimeZone", localTimeZone);
    preferences.putInt("batteryCapacity", batteryCapacity);
    preferences.putInt("pmPowerSaverBatt", pmPowerSaverBatt);
    preferences.putInt("pmPowerSaverBright", pmPowerSaverBright);
    preferences.putString("mqtt_server", mqtt_server);
    preferences.putInt("mqtt_port", mqtt_port);
    preferences.putString("mqtt_username", mqtt_username);
    preferences.putString("mqtt_password", mqtt_password);

    preferences.end();
}


void WiFi_onSaveParams() {

    snprintf(friendlyName, sizeof(friendlyName), "%s", wm_friendlyName.getValue());
    snprintf(nodeRED_ServerIP, sizeof(nodeRED_ServerIP), "%s", wm_nodeRED_ServerIP.getValue());
    nodeRED_ServerPort = atoi(wm_nodeRED_ServerPort.getValue());
    snprintf(nodeRED_ServerUrl, sizeof(nodeRED_ServerUrl), "%s", wm_nodeRED_ServerUrl.getValue());
    snprintf(ntpServer, sizeof(ntpServer), "%s", wm_ntpServer.getValue());
    snprintf(localTimeZone, sizeof(localTimeZone), "%s", wm_localTimeZone.getValue());
    batteryCapacity = atoi(wm_batteryCapacity.getValue());
    pmPowerSaverBatt = atoi(wm_pmPowerSaverBatt.getValue());
    pmPowerSaverBright = atoi(wm_pmPowerSaverBright.getValue());

    snprintf(mqtt_server, sizeof(mqtt_server), "%s", wm_mqtt_server.getValue());
    mqtt_port = atoi(wm_mqtt_port.getValue());
    snprintf(mqtt_username, sizeof(mqtt_username), "%s", wm_mqtt_username.getValue());
    snprintf(mqtt_password, sizeof(mqtt_password), "%s", wm_mqtt_password.getValue());

    preferences_save();
    webSockets_getTally();

}


// Bridge: copy values loaded by preferences_setup() into ConfigState
void prefs_applyToConfig(ConfigState& cfg) {
    // Per-device config
    cfg.device.deviceId            = String(deviceId);
    cfg.device.friendlyName        = String(friendlyName);
    cfg.device.batteryCapacityMah  = static_cast<uint16_t>(batteryCapacity);

    // Global power-saver config (from WiFi portal / prefs)
    cfg.global.powersaverBatteryPct = static_cast<uint8_t>(pmPowerSaverBatt);

    // pmPowerSaverBright is stored as 0–100 and used directly as a brightness percentage
    int bright = pmPowerSaverBright;            // 0–100
    if (bright < 0)    bright = 0;
    if (bright > 100)  bright = 100;
    cfg.global.powersaverBrightness = static_cast<uint8_t>(bright);

    // NTP + timezone
    cfg.global.ntpServer = String(ntpServer);
    cfg.global.timezone  = String(localTimeZone);

    // MQTT server defaults:
    cfg.global.mqttServer = String(mqtt_server);
    cfg.global.mqttPort   = mqtt_port;  // Mosquitto default
    cfg.global.mqttUsername = String(mqtt_username);
    cfg.global.mqttPassword = String(mqtt_password);

    // Optional: set a default normal brightness if still unset (0)
    if (cfg.global.brightness == 0) {
        cfg.global.brightness = 50;  // 50% by default
    }
}
