#pragma once

// Set to 1 to re-enable legacy WebSockets tally path.
// Default 0: everything is MQTT-centric; WebSockets stubs do nothing.
#define USE_WEBSOCKETS 0

#include "PrefsModule.h"

extern bool ws_isConnected;
extern int atem_pgm1_input_id;
extern int atem_pvw1_input_id;
extern char atem_pgm1_friendlyName[FRIENDLY_NAME_MAX_LEN + 1];
extern char atem_pvw1_friendlyName[FRIENDLY_NAME_MAX_LEN + 1];

void webSockets_setup();
void webSockets_onLoop();
void webSockets_getTally();
void webSockets_returnTally(int tallyIndicator);