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