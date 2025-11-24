
#pragma once
#include <Arduino.h>
#include <ezTime.h> // you already include this here :contentReference[oaicite:3]{index=3}

constexpr size_t FRIENDLY_NAME_MAX_LEN   = 16;  // what you want visible
constexpr size_t DEVICE_ID_MAX_LEN       = 16;
constexpr size_t DEVICE_NAME_MAX_LEN     = 32;
constexpr size_t IP_STR_MAX_LEN          = 15;  // 255.255.255.255
constexpr size_t URL_MAX_LEN             = 32;
constexpr size_t TIMEZONE_MAX_LEN        = 16;
constexpr size_t NTP_SERVER_MAX_LEN      = 32;

extern char deviceId[DEVICE_ID_MAX_LEN + 1];
extern char deviceName[DEVICE_NAME_MAX_LEN + 1];
extern char friendlyName[FRIENDLY_NAME_MAX_LEN + 1];
extern char nodeRED_ServerIP[IP_STR_MAX_LEN + 1];
extern char nodeRED_ServerUrl[URL_MAX_LEN + 1];
extern char localTimeZone[TIMEZONE_MAX_LEN + 1];
extern char ntpServer[NTP_SERVER_MAX_LEN + 1];
extern int nodeRED_ServerPort;

const char version[9] = "0.10.1";

extern int batteryCapacity;
extern int pmPowerSaverBatt;
extern int pmPowerSaverBright;

void preferences_setup();
void preferences_save();
void WiFi_onSaveParams();