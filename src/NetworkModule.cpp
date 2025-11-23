#include <M5StickCPlus.h>
#include <WiFiManager.h>
#include <millisDelay.h>
#include <ezTime.h>             // set #define EZTIME_CACHE_NVS in this file
#include "PrefsModule.h"
#include "WebSocketsModule.h"
#include "ScreenModule.h"

WiFiManager wm;
millisDelay ms_WiFi;
Timezone localTime;


// Define Functions
void WiFi_setup();
void WiFi_onLoop();
void WiFi_onEvent(WiFiEvent_t event);
void WiFi_onSaveParams();


void WiFi_onLoop() {
    if (wm.getWebPortalActive()) wm.process();
}


void WiFi_setup () {

    WiFi.mode(WIFI_STA);
    WiFi.onEvent(WiFi_onEvent);
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    WiFi.setAutoReconnect(true);
    
    std::vector<const char *> menu = {"wifi","info","param","sep","restart","exit"};
    wm.setMenu(menu);
    wm.setConfigPortalBlocking(false);
    wm.setDebugOutput(false);
    wm.setSaveParamsCallback(WiFi_onSaveParams);
    wm.setClass("invert");                          // set dark theme
    wm.setCountry("US");
    wm.setHostname(deviceName);
    wm.setWiFiAutoReconnect(true);
    wm.setRemoveDuplicateAPs(false);
    
    if (!wm.autoConnect(deviceName)) {
        if (currentScreen == 0) {
            startupLog("No access point found!",1);
            startupLog("Config Portal Started.\r\nPress M5 to abort.",1);
        }
        while (wm.getConfigPortalActive()) {
            wm.process();
            M5.update();
            if (M5.BtnA.wasReleased()) wm.stopConfigPortal();
        }
        if (currentScreen == 0) startupLog("Config Portal Stopped...",1);
        //ESP.restart();
    }

    // ezTime
    if (currentScreen == 0) startupLog("Initializing ezTime...", 1);
    if (!localTime.setCache("timezone", "localTime")) localTime.setLocation(localTimeZone);
    localTime.setDefault();
    setServer(ntpServer);
    if (wm.getWLStatusString() != "WL_CONNECTED") {
        if (currentScreen == 0) startupLog("ezTime initialization incomplete...", 1);
        return;
    }
    waitForSync(60);
    if (timeStatus() == timeSet) {
        Serial.println("UTC Time: " + UTC.dateTime(ISO8601));
        Serial.println("Local Time: " + localTime.dateTime(ISO8601));
        char buff[65];
        const char* prefix = "Local Time: ";
        strcpy(buff, prefix);
        strcat(buff, localTime.dateTime(ISO8601).c_str());
        if (currentScreen == 0) startupLog(buff, 1);
    } else {
        if (currentScreen == 0) startupLog("ezTime initialization failed...", 1);
    }
    
}


void WiFi_onEvent(WiFiEvent_t event) {
  
  //Serial.printf("[WiFi-event] event: %d\n", event);

  switch (event) {
      
      case ARDUINO_EVENT_WIFI_READY: 
          Serial.println("WiFi interface ready");
          break;
      
      case ARDUINO_EVENT_WIFI_SCAN_DONE:
          Serial.println("Completed scan for access points");
          break;
      
      case ARDUINO_EVENT_WIFI_STA_START:
          Serial.println("WiFi client started");
          break;
      
      case ARDUINO_EVENT_WIFI_STA_STOP:
          Serial.println("WiFi clients stopped");
          break;
      
      case ARDUINO_EVENT_WIFI_STA_CONNECTED:          
          Serial.println(F("Connected to access point"));
          break;

      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
          Serial.println("Disconnected from WiFi access point");
          break;
      
      case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
          Serial.println("Authentication mode of access point has changed");
          break;
      
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
          Serial.print(F("Obtained IP address: "));
          Serial.println(WiFi.localIP());
          updateNTP();
          if (currentScreen == 0) {
            char buff[65];
            const char* prefix = "Obtained IP address: ";
            strcpy(buff, prefix);
            strcat(buff, WiFi.localIP().toString().c_str());
            startupLog(buff, 1);
          }
          break;
      
      case ARDUINO_EVENT_WIFI_STA_LOST_IP:
          Serial.println("Lost IP address and IP address is reset to 0");
          break;
      
      case ARDUINO_EVENT_WPS_ER_SUCCESS:
          Serial.println("WiFi Protected Setup (WPS): succeeded in enrollee mode");
          break;
      
      case ARDUINO_EVENT_WPS_ER_FAILED:
          Serial.println("WiFi Protected Setup (WPS): failed in enrollee mode");
          break;
      
      case ARDUINO_EVENT_WPS_ER_TIMEOUT:
          Serial.println("WiFi Protected Setup (WPS): timeout in enrollee mode");
          break;
      
      case ARDUINO_EVENT_WPS_ER_PIN:
          Serial.println("WiFi Protected Setup (WPS): pin code in enrollee mode");
          break;
      
      case ARDUINO_EVENT_WIFI_AP_START:
          Serial.println("WiFi access point started");
          break;
      
      case ARDUINO_EVENT_WIFI_AP_STOP:
          Serial.println("WiFi access point  stopped");
          break;
      
      case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
          Serial.println("Client connected");
          break;
      
      case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
          Serial.println("Client disconnected");
          break;
      
      case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
          Serial.println("Assigned IP address to client");
          break;
      
      case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
          Serial.println("Received probe request");
          break;
      
      case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
          Serial.println("AP IPv6 is preferred");
          break;
      
      case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
          Serial.println("STA IPv6 is preferred");
          break;
      
      case ARDUINO_EVENT_ETH_GOT_IP6:
          Serial.println("Ethernet IPv6 is preferred");
          break;
      
      case ARDUINO_EVENT_ETH_START:
          Serial.println("Ethernet started");
          break;
      
      case ARDUINO_EVENT_ETH_STOP:
          Serial.println("Ethernet stopped");
          break;
      
      case ARDUINO_EVENT_ETH_CONNECTED:
          Serial.println("Ethernet connected");
          break;
      
      case ARDUINO_EVENT_ETH_DISCONNECTED:
          Serial.println("Ethernet disconnected");
          break;
      
      case ARDUINO_EVENT_ETH_GOT_IP:
          Serial.println("Obtained IP address");
          break;
      
      default: 
        break;

  }

}

