#include "WebSocketsModule.h"

#if USE_WEBSOCKETS

#include <WebSocketsClient.h>
#include <millisDelay.h>
#include <ArduinoJson.h>
#include "ScreenModule.h"
#include "PowerModule.h"
#include "NetworkModule.h"

WebSocketsClient ws;
bool ws_isConnected = false;

int atem_pgm1_input_id = 0;
int atem_pvw1_input_id = 0;

char atem_pgm1_friendlyName[FRIENDLY_NAME_MAX_LEN + 1] = "";
char atem_pvw1_friendlyName[FRIENDLY_NAME_MAX_LEN + 1] = "";

millisDelay md_sendStatus;


// Define Functions
void webSockets_setup();
void webSockets_onLoop();
void webSockets_onEvent(WStype_t type, uint8_t* payload, size_t length);
void webSockets_onTally(JsonDocument doc);
void webSockets_getTally();


void webSockets_onLoop() {

    if (ws_isConnected && md_sendStatus.justFinished()) {
        
        md_sendStatus.repeat();
        
        //constexpr size_t BUFF_MAX_LEN   = 16;
        //char buff[BUFF_MAX_LEN + 1];
        //ultoa(inputIds, buff, 2);
        JsonDocument doc;
        
        doc["deviceId"] = deviceId;
        doc["MessageType"] = "DeviceStatus";
        doc["MessageData"]["friendlyName"] = friendlyName;
        //doc["MessageData"]["inputIds"] = buff;
        doc["MessageData"]["batVoltage"] = pwr.batVoltage;
        doc["MessageData"]["batPercentage"] = pwr.batPercentage;
        doc["MessageData"]["batCurrent"] = pwr.batCurrent;
        doc["MessageData"]["batChargeCurrent"] = pwr.batChargeCurrent;
        doc["MessageData"]["maxChargeCurrent"] = pwr.maxChargeCurrent;
        //doc["MessageData"]["batPercentageCoulomb"] = pwr.batPercentageCoulomb;
        doc["MessageData"]["coulombCount"] = pwr.coulombCount;
        doc["MessageData"]["tempInAXP192"] = pwr.tempInAXP192;
        doc["MessageData"]["powerMode"] = pwr.powerMode;
        doc["MessageData"]["currentBrightness"] = currentBrightness;
        doc["MessageData"]["currentScreen"] = currentScreen;
        doc["MessageData"]["webPortalActive"] = wm.getWebPortalActive();
        doc["MessageData"]["ntp"] = String(timeStatus());
        doc["MessageData"]["ssid"] = String(wm.getWiFiSSID());
        doc["MessageData"]["rssi"] = String(WiFi.RSSI());
        doc["MessageData"]["ip"] = WiFi.localIP().toString();
        doc["MessageData"]["hostname"] = wm.getWiFiHostname();

        String json;
        json.reserve(512);
        serializeJson(doc, json);
        //Serial.println(json);
        ws.sendTXT(json);

    }
    
    ws.loop();
}


void webSockets_setup() {
    
  Serial.println(F("Attempting to connect to websockets..."));
  ws.onEvent(webSockets_onEvent);
  ws.setReconnectInterval(5000);
  ws.begin(nodeRED_ServerIP, nodeRED_ServerPort, nodeRED_ServerUrl);
  ws.loop();

}


void webSockets_onEvent(WStype_t type, uint8_t* payload, size_t length) {

    switch(type) {
	
        case WStype_ERROR:
        Serial.println("Websockets error detected.");
        break;
        
        case WStype_DISCONNECTED:
            ws_isConnected = false;
            Serial.println("Websockets disconnected.");
            break;
        
        case WStype_CONNECTED:
            ws_isConnected = true;
            Serial.println(F("Websockets connected."));
            if (currentScreen == SCREEN_STARTUP) startupLog("Websockets connected.", 1);
            webSockets_getTally();
            md_sendStatus.start(60000);
            break;
            
        case WStype_TEXT: {
        
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload, length);

            if (error.code() != DeserializationError::Ok) {
                Serial.print(F("deserializeJson() failed: "));
                Serial.println(error.c_str());
                break;
            }
            
            const char* MessageType = doc["MessageType"];
            if (MessageType == nullptr) {
                Serial.println(F("MessageType is nullptr"));
                return;
            } else if (strcmp(MessageType, "SetTally") == 0) {
                webSockets_onTally(doc);
            }
            
            break;
        }
        
        case WStype_BIN:
            break;
        
        case WStype_PING: {
            //constexpr size_t BUFF_MAX_LEN   = 64;
            /*char buff[BUFF_MAX_LEN + 1];
            snprintf(buff, sizeof(buff), "Local Time: %s", localTime.dateTime(ISO8601).c_str());
            Serial.print(buff);
            Serial.println(F(" Websockets PING"));*/
            break;
        }
        
        case WStype_PONG: {
            /*char buff[65];
            snprintf(buff, sizeof(buff), "Local Time: %s", localTime.dateTime(ISO8601).c_str());
            Serial.print(buff);
            Serial.println(F(" Websockets PONG"));*/
            break;
        }
       
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
            break;
    }

}


void webSockets_onTally(JsonDocument doc) {

    /*
    Serial.println(F("webSockets_onTally()"));
    serializeJson(doc, Serial);
    Serial.println();
    */
    
    const char* Source = doc["deviceId"];
    const char* EventType = doc["MessageData"]["EventType"];
    int EventValue;

    Serial.println(F("webSockets_onTally() if/then/else"));
    if (Source == nullptr || EventType == nullptr) {
        Serial.println(F("nullptr"));
        return;
    } else if (strcmp(EventType, "atem_pgm1_input_id") == 0) {
        //Serial.println(F("webSockets_onTally() atem_pgm1_input_id"));
        //Serial.println(F("webSockets_onTally() EventValue"));
        EventValue = doc["MessageData"][EventType];
        atem_pgm1_input_id = EventValue;
        //Serial.println(F("webSockets_onTally() EventValue Success"));
        const char* tmp_atem_pgm1_friendlyName = doc["MessageData"]["atem_pgm1_friendlyName"];
        if (tmp_atem_pgm1_friendlyName) {
            strncpy(atem_pgm1_friendlyName, tmp_atem_pgm1_friendlyName, sizeof(atem_pgm1_friendlyName) - 1);
            atem_pgm1_friendlyName[sizeof(atem_pgm1_friendlyName) - 1] = '\0';
        } else {
            atem_pgm1_friendlyName[0] = '\0';
        }
    } else if (strcmp(EventType, "atem_pvw1_input_id") == 0) {
        //Serial.println(F("webSockets_onTally() atem_pvw1_input_id"));
        //Serial.println(F("webSockets_onTally() EventValue"));
        EventValue = doc["MessageData"][EventType];
        atem_pvw1_input_id = EventValue;
        //Serial.println(F("webSockets_onTally() EventValue Success"));
        const char* tmp_str_atem_pvw1_friendlyName = doc["MessageData"]["atem_pvw1_friendlyName"];
        if (tmp_str_atem_pvw1_friendlyName) {
            strncpy(atem_pvw1_friendlyName, tmp_str_atem_pvw1_friendlyName, sizeof(atem_pvw1_friendlyName) - 1);
            atem_pvw1_friendlyName[sizeof(atem_pvw1_friendlyName) - 1] = '\0';
        } else {
            atem_pvw1_friendlyName[0] = '\0';
        }
    }

    /*
    Serial.print("Source: ");
    Serial.println(Source);
    Serial.print("EventType: ");
    Serial.println(EventType);
    Serial.print("EventValue: ");
    Serial.println(EventValue);
    Serial.print("friendlyName: ");
    Serial.println(atem_pvw1_friendlyName);
    Serial.println(atem_pgm1_friendlyName);
    */

}


void webSockets_getTally() {
    ws.sendTXT("{\"deviceId\": \"" + String(deviceId) + "\", " +
                "\"MessageType\": \"GetTally\"}");
}


void webSockets_returnTally(int tallyIndicator) {
        
        JsonDocument doc;
        
        doc["deviceId"] = deviceId;
        doc["MessageType"] = "ReturnTally";
        doc["MessageData"]["friendlyName"] = friendlyName;
        doc["MessageData"]["tallyIndicator"] = tallyIndicator;

        String json;
        json.reserve(128);
        serializeJson(doc, json);
        ws.sendTXT(json);
}

#else   // USE_WEBSOCKETS == 0  → stubbed-out implementation

bool ws_isConnected = false;
int atem_pgm1_input_id = 0;
int atem_pvw1_input_id = 0;
char atem_pgm1_friendlyName[FRIENDLY_NAME_MAX_LEN + 1] = "";
char atem_pvw1_friendlyName[FRIENDLY_NAME_MAX_LEN + 1] = "";

// No-op versions so the rest of the firmware doesn’t care.
void webSockets_setup() {}
void webSockets_onLoop() {}
void webSockets_getTally() {}
void webSockets_returnTally(int) {}

#endif  // USE_WEBSOCKETS