#include <Preferences.h>
#include <WiFiManager.h>
#include "NetworkModule.h"
#include "WebSocketsModule.h"


WiFiManagerParameter wm_friendlyName("friendlyName", "Friendly Name");
//WiFiManagerParameter wm_inputIds("inputIds", "Input IDs (0000000000000001)");
WiFiManagerParameter wm_nodeRED_ServerIP("nr_ServerIP", "Node-RED Server IP");
WiFiManagerParameter wm_nodeRED_ServerPort("nr_ServerPort", "Node-RED Server Port");
WiFiManagerParameter wm_nodeRED_ServerUrl("nr_ServerUrl", "Node-RED Server URL");
WiFiManagerParameter wm_ntpServer("ntpServer", "NTP Server");
WiFiManagerParameter wm_localTimeZone("localTimeZone", "Local Timezone (restart req)");
WiFiManagerParameter wm_batteryCapacity("batteryCapacity", "Battery Capacity (mAh)");
WiFiManagerParameter wm_pmPowerSaverBatt("pmPowerSaverBatt", "Power Saver Battery Level");
WiFiManagerParameter wm_pmPowerSaverBright("pmPowerSaverBright", "Power Saver Screen Brightness");

Preferences preferences;
char friendlyName[17];
//uint16_t inputIds = 0b0000000000000001;
char nodeRED_ServerIP[16];
int nodeRED_ServerPort;
char nodeRED_ServerUrl[33];
char localTimeZone[17];
char ntpServer[33];
int batteryCapacity;
int pmPowerSaverBatt;
int pmPowerSaverBright;


void preferences_setup() {
    
    preferences.begin("custom", true);
    strcpy(friendlyName, preferences.getString("friendlyName", "CamX").c_str());
    //if (preferences.getBytesLength("inputIds") > 0) preferences.getBytes("inputIds", &inputIds, 2);
    strcpy(nodeRED_ServerIP, preferences.getString("nr_ServerIP", "192.168.13.54").c_str());
    nodeRED_ServerPort = preferences.getInt("nr_ServerPort", 1880);
    strcpy(nodeRED_ServerUrl, preferences.getString("nr_ServerUrl", "/ws/tally").c_str());
    strcpy(localTimeZone, preferences.getString("localTimeZone", "America/Chicago").c_str());
    strcpy(ntpServer, preferences.getString("ntpServer", "time.apple.com").c_str());
    batteryCapacity = preferences.getInt("batteryCapacity", 2200);
    pmPowerSaverBatt = preferences.getInt("pmPowerSaverBatt", 25);
    pmPowerSaverBright = preferences.getInt("pmPowerSaverBright", 30);
    preferences.end();

    // wm_addParameters
    wm.addParameter(&wm_friendlyName);
    //wm.addParameter(&wm_inputIds);
    wm.addParameter(&wm_nodeRED_ServerIP);
    wm.addParameter(&wm_nodeRED_ServerPort);
    wm.addParameter(&wm_nodeRED_ServerUrl);
    wm.addParameter(&wm_ntpServer);
    wm.addParameter(&wm_localTimeZone);
    wm.addParameter(&wm_batteryCapacity);
    wm.addParameter(&wm_pmPowerSaverBatt);
    wm.addParameter(&wm_pmPowerSaverBright);
    
    // set wm values
    char buff[33];
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

}


void preferences_save() {
    preferences.begin("custom", false);
    preferences.putString("friendlyName", friendlyName);
    //preferences.putBytes("inputIds", &inputIds, 2);
    preferences.putString("nr_ServerIP", nodeRED_ServerIP);
    preferences.putInt("nr_ServerPort", nodeRED_ServerPort);
    preferences.putString("nr_ServerUrl", nodeRED_ServerUrl);
    preferences.putString("ntpServer", ntpServer);
    preferences.putString("localTimeZone", localTimeZone);
    preferences.putInt("batteryCapacity", batteryCapacity);
    preferences.putInt("pmPowerSaverBatt", pmPowerSaverBatt);
    preferences.putInt("pmPowerSaverBright", pmPowerSaverBright);
    preferences.end();
}


void WiFi_onSaveParams() {

    strcpy(friendlyName, wm_friendlyName.getValue());
    //inputIds = static_cast<uint16_t>(strtol(wm_inputIds.getValue(), NULL, 2));
    strcpy(nodeRED_ServerIP, wm_nodeRED_ServerIP.getValue());
    nodeRED_ServerPort = atoi(wm_nodeRED_ServerPort.getValue());
    strcpy(nodeRED_ServerUrl, wm_nodeRED_ServerUrl.getValue());
    strcpy(ntpServer, wm_ntpServer.getValue());
    strcpy(localTimeZone, wm_localTimeZone.getValue());
    batteryCapacity = atoi(wm_batteryCapacity.getValue());
    pmPowerSaverBatt = atoi(wm_pmPowerSaverBatt.getValue());
    pmPowerSaverBright = atoi(wm_pmPowerSaverBright.getValue());

    preferences_save();
    webSockets_getTally();

}