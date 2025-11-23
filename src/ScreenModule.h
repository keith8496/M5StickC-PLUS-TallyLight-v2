extern int currentScreen;
extern int currentBrightness;

void refreshScreen();
void changeScreen(int newScreen = -1);
void setBrightness(int newBrightness);
void startupLog(const char* in_logMessage, int in_textSize);