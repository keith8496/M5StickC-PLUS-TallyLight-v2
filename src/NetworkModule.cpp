#include <M5Unified.h>
#include <millisDelay.h>

#include "ConfigState.h"
#include "NetworkModule.h"
#include "ScreenModule.h"
#include "MqttClient.h"


extern ConfigState g_config;
extern MqttClient g_mqtt;

WiFiManager wm;
millisDelay ms_WiFi;
Timezone localTime;
static bool g_timeInitialized = false;
static bool     g_timeInitRequested     = false;
static uint32_t g_timeInitRequestedAtMs = 0;
static constexpr uint32_t TIME_INIT_DEBOUNCE_MS = 2000; // 2s debounce


// Define Functions
void WiFi_setup();
void WiFi_onLoop();
void WiFi_onEvent(WiFiEvent_t event);
void WiFi_onSaveParams();


void WiFi_onLoop() {
    if (wm.getWebPortalActive()) wm.process();
}


void WiFi_setup () {

    const auto eff = g_config.effective();
    String hostname = eff.deviceName.length() ? eff.deviceName : eff.deviceId;

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
    wm.setHostname(hostname.c_str());
    wm.setWiFiAutoReconnect(true);
    wm.setRemoveDuplicateAPs(false);
    
    if (!wm.autoConnect(hostname.c_str())) {
        if (currentScreen == SCREEN_STARTUP) {
            startupLog("No access point found!",1);
            startupLog("Config Portal Started.\r\nPress M5 to abort.",1);
        }
        while (wm.getConfigPortalActive()) {
            wm.process();
            M5.update();
            if (M5.BtnA.wasReleased()) wm.stopConfigPortal();
        }
        if (currentScreen == SCREEN_STARTUP) startupLog("Config Portal Stopped...",1);
        //ESP.restart();
    }
    
}

// Internal helper: perform RTC/NTP initialization once per boot.
static void doTimeInitOnce() {
    if (g_timeInitialized) {
        return;
    }

    // Require MQTT connection before initializing time
    if (!g_mqtt.isConnected()) {
        Serial.println("[net] doTimeInitOnce: MQTT not connected, aborting.");
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[net] doTimeInitOnce: WiFi not connected, aborting.");
        if (currentScreen == SCREEN_STARTUP) {
            startupLog("ezTime init: WiFi not connected", 1);
        }
        return;
    }

    if (currentScreen == SCREEN_STARTUP) {
        startupLog("Initializing ezTime...", 1);
    }

    // Use global config strings so ezTime doesn't hold pointers into temporaries.
    const String& ntpServer = g_config.global.ntpServer;
    const String& tz        = g_config.global.timeZone;

    Serial.println("[net] NTP Server: " + String(ntpServer));
    Serial.println("[net] Timezone: " + String(tz));

    if (!localTime.setCache("timezone", "localTime")) {
        localTime.setLocation(tz.c_str());
    }
    localTime.setDefault();

    setServer(ntpServer.c_str());

    waitForSync(15);
    if (timeStatus() == timeSet) {
        g_timeInitialized = true;
        g_config.device.ntp_isSynchronized = true;
        Serial.println("UTC Time: " + UTC.dateTime(ISO8601));
        Serial.println("Local Time: " + localTime.dateTime(ISO8601));
        constexpr size_t BUFF_MAX_LEN   = 65;
        char buff[BUFF_MAX_LEN];
        snprintf(buff, sizeof(buff), "Local Time: %s", localTime.dateTime(ISO8601).c_str());
        if (currentScreen == SCREEN_STARTUP) {
            startupLog(buff, 1);
        }
    } else {
        Serial.println("[net] ezTime initialization failed...");
        if (currentScreen == SCREEN_STARTUP) {
            startupLog("ezTime initialization failed...", 1);
        }
    }
}

void requestTimeInit() {
    g_timeInitRequested     = true;
    g_timeInitRequestedAtMs = millis();
}

void requestTimeResync() {
    // Allow a future NTP sync even if we already initialized once
    g_timeInitialized = false;
    g_config.device.ntp_isSynchronized = false;
    requestTimeInit();
}

void serviceTimeInit() {
    if (!g_timeInitRequested || g_timeInitialized) {
        return;
    }

    uint32_t now = millis();
    if (now - g_timeInitRequestedAtMs < TIME_INIT_DEBOUNCE_MS) {
        return; // still debouncing
    }

    // Don’t clear the request until network is actually ready
    if (!g_mqtt.isConnected() || WiFi.status() != WL_CONNECTED) {
        return; // keep g_timeInitRequested = true; we'll try again next loop
    }

    // Network is ready and debounce interval has passed: do one-time init
    g_timeInitRequested = false;
    doTimeInitOnce();
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
          if (currentScreen == SCREEN_STARTUP) {
            char buff[65];
            snprintf(buff, sizeof(buff), "Obtained IP address: %s", WiFi.localIP().toString().c_str());
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
