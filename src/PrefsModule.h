
#include <ezTime.h> // set #define EZTIME_CACHE_NVS in this file

const char version[9] = "0.10.1";

extern char friendlyName[17];
extern uint16_t inputIds;
extern char nodeRED_ServerIP[16];
extern int nodeRED_ServerPort;
extern char nodeRED_ServerUrl[33];
extern char localTimeZone[17];
extern char ntpServer[33];
extern int batteryCapacity;
extern int pmPowerSaverBatt;
extern int pmPowerSaverBright;

extern char deviceId[17];
extern char deviceName[33];

extern bool time_isSet;
extern Timezone centralTime;

void preferences_setup();
void preferences_save();
void WiFi_onSaveParams();