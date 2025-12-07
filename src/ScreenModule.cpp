#include <M5Unified.h>
#include <millisDelay.h>

#include "NetworkModule.h"
#include "PowerModule.h"
#include "ScreenModule.h"

#include "ConfigState.h"
#include "TallyState.h"
#include "MqttClient.h"

extern ConfigState g_config;
extern TallyState  g_tally;
extern MqttClient g_mqtt;

millisDelay md_screenRefresh;

ScreenId currentScreen = SCREEN_STARTUP;
const int maxScreen = SCREEN_SETUP;

// Logical brightness in the 0–100 range.
// This is what MQTT / prefs / power mgmt all agree on.
int currentBrightness = 50;     // default to 50%
static const int minBrightness = 30;


const int tft_width = 240;
const int tft_heigth = 135;

LGFX_Sprite startupScreen(&M5.Display);
LGFX_Sprite tallyScreen(&M5.Display);
LGFX_Sprite powerScreen(&M5.Display);
LGFX_Sprite setupScreen(&M5.Display);

constexpr size_t LOG_MESSAGE_MAX_LEN     = 64;
struct startupLogData {
    char logMessage[LOG_MESSAGE_MAX_LEN + 1];
    int textSize;
};

startupLogData startupLogEntries[20];
int index_startupLog = -1;



void refreshTallyScreen() {

    // EffectiveConfig merges global + device config
    const auto eff = g_config.effective();

    // Black strip across the top to keep status elements readable
    const int statusBarHeight = 50;  // taller bar for two rows of info
    tallyScreen.fillRect(0, 0, tft_width, statusBarHeight, TFT_BLACK);

    // Use smallest FreeSans font for the status bar
    tallyScreen.setFont(&fonts::DejaVu12);
    tallyScreen.setTextSize(1);
    
    // --- Status Bar: Battery, WiFi, MQTT, Clock ---
    const int statusY = 4;
    int fontHeight = tallyScreen.fontHeight();

    // Define two rows within the taller status bar:
    // Row 0: clock + WiFi + MQTT + battery
    // Row 1: SEL label (left side)
    int row0Y = statusY + 2;                 // top row baseline area
    int row1Y = statusY + fontHeight + 8;    // second row, SEL row

    // Divide the status bar width into 8 segments:
    // Clock spans 4 segments (0–3), WiFi spans 1 (4), MQTT spans 1 (5), Battery spans 2 (6–7).
    int segWidth8     = tft_width / 8;
    // Clock center: middle of segments 0–3
    int clockCenterX  = 2 * segWidth8;
    // WiFi center: middle of segment 4
    int wifiCenterX   = (4 * segWidth8 + segWidth8 / 2) -4; // shift left 1px for better centering
    // MQTT center: middle of segment 5
    int mqttCenterX   = (5 * segWidth8 + segWidth8 / 2) -4; // shift left 1px for better centering
    // Battery center: middle of segments 6–7
    int batCenterX    = 7 * segWidth8;

    // Clock (first segment, centered, on the top row)
    auto now = localTime.dateTime("g:i:s A");
    String timeStr = now ? String(now) : String("--:--:--");
    tallyScreen.setTextColor(TFT_WHITE, TFT_BLACK);
    int16_t timeWidth = tallyScreen.textWidth(timeStr);
    int16_t timeX = clockCenterX - (timeWidth / 2);
    if (timeX < 0) timeX = 0;
    tallyScreen.setCursor(timeX, row0Y);
    tallyScreen.print(timeStr);

    // WiFi icon (second segment center on the top row), now using 1–4 bars based on RSSI
    int wifiX = wifiCenterX - 5;   // left edge of bars group
    int wifiY = row0Y + 2;         // baseline for the bars
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    int32_t rssi = WiFi.RSSI();

    // Map RSSI to number of bars (0–4)
    // Excellent:   > -60 dBm  -> 4 bars
    // Good:       -65 to -60  -> 3 bars
    // Acceptable: -70 to -65  -> 2 bars
    // Weak:       <= -70      -> 1 bar (if connected)
    uint8_t wifiBars = 0;
    if (wifiConnected) {
        if (rssi > -60) {
            wifiBars = 4;
        } else if (rssi > -65) {
            wifiBars = 3;
        } else if (rssi > -70) {
            wifiBars = 2;
        } else {
            wifiBars = 1;
        }
    }

    uint16_t wifiColor = wifiConnected ? TFT_WHITE : TFT_DARKGREY;

    // Draw up to 4 vertical bars, left to right, increasing height
    const int barWidth   = 2;
    const int barSpacing = 1;
    const int barBaseY   = wifiY + 10;  // bottom of the tallest bar

    for (int i = 0; i < 4; ++i) {
        int barHeight;
        switch (i) {
            case 0: barHeight = 3;  break;  // weakest
            case 1: barHeight = 6;  break;
            case 2: barHeight = 8;  break;
            case 3: barHeight = 10; break;  // strongest
            default: barHeight = 0; break;
        }

        int barX = wifiX + i * (barWidth + barSpacing);
        int barY = barBaseY - barHeight;

        if (wifiBars > i) {
            // Filled bar for active signal level
            tallyScreen.fillRect(barX, barY, barWidth, barHeight, wifiColor);
        } else {
            // Outline only for inactive bars (optional)
            tallyScreen.drawRect(barX, barY, barWidth, barHeight, wifiColor);
        }
    }

    // If disconnected, draw an "X" over the bar group to make it obvious
    if (!wifiConnected) {
        int groupLeft   = wifiX;
        int groupRight  = wifiX + 3 * (barWidth + barSpacing) + barWidth; // right edge of last bar
        int groupTop    = barBaseY - 10;  // top of tallest bar
        int groupBottom = barBaseY;       // bottom of bars

        tallyScreen.drawLine(groupLeft,  groupTop,    groupRight, groupBottom, wifiColor);
        tallyScreen.drawLine(groupLeft,  groupBottom, groupRight, groupTop,    wifiColor);
    }

    // MQTT icon (third segment center on the top row), aligned near the clock/SoC baseline
    int mqttX = mqttCenterX - 7;   // box is 14px wide
    int mqttY = row0Y + 2;         // positioned close to the Clock/SoC baseline
    bool mqttConnected = eff.mqtt_isConnected;
    uint16_t mqttColor = mqttConnected ? TFT_WHITE : TFT_DARKGREY;

    tallyScreen.drawRect(mqttX, mqttY, 14, 10, mqttColor);
    if (mqttConnected) {
        // check mark
        tallyScreen.drawLine(mqttX + 2, mqttY + 4, mqttX + 4, mqttY + 2, mqttColor);
        tallyScreen.drawLine(mqttX + 4, mqttY + 2, mqttX + 7, mqttY + 6, mqttColor);
    } else {
        // X
        tallyScreen.drawLine(mqttX + 2, mqttY + 2, mqttX + 7, mqttY + 7, mqttColor);
        tallyScreen.drawLine(mqttX + 7, mqttY + 2, mqttX + 2, mqttY + 7, mqttColor);
    }

    // Battery (top-right): horizontal icon with SoC inside
    float soc = pwr.batPercentageHybrid;
    if (soc < 0.0f) soc = 0.0f;
    if (soc > 100.0f) soc = 100.0f;

    // Battery body dimensions (rightmost segment, shifted 4px left)
    const int batWidth  = 40;
    const int batHeight = fontHeight - 1;   // match font height
    int batBodyX        = batCenterX - (batWidth / 2) - 4;   // shift left 4 px
    if (batBodyX < 0) batBodyX = 0;
    const int batBodyY  = row0Y;

    // Draw main battery rectangle
    tallyScreen.drawRect(batBodyX, batBodyY, batWidth, batHeight, TFT_WHITE);

    // Draw the positive terminal as a small tab on the right
    const int termWidth  = 4;
    const int termHeight = batHeight / 2;
    const int termX      = batBodyX + batWidth;
    const int termY      = batBodyY + (batHeight - termHeight) / 2;
    tallyScreen.drawRect(termX, termY, termWidth, termHeight, TFT_WHITE);

    // Fill level inside the battery
    int fillMaxWidth = batWidth - 4;   // leave a small margin inside
    int fillWidth    = static_cast<int>((fillMaxWidth * soc) / 100.0f);
    if (fillWidth < 0) fillWidth = 0;
    if (fillWidth > fillMaxWidth) fillWidth = fillMaxWidth;
    int fillX = batBodyX + 2;
    int fillY = batBodyY + 2;
    int fillH = batHeight - 4;

    uint16_t darkRed    = M5.Display.color565(150, 0, 0);
    uint16_t darkerGreen= M5.Display.color565(0, 120, 0);
    uint16_t fillColor = (soc <= 20.0f) ? darkRed : darkerGreen;
    tallyScreen.fillRect(fillX, fillY, fillWidth, fillH, fillColor);

    // SoC text horizontally centered inside the battery body (e.g., "100" or "75")
    char socText[8];
    snprintf(socText, sizeof(socText), "%.0f", soc);
    String socStr = socText;
    int16_t socW = tallyScreen.textWidth(socStr);
    int16_t socX = batBodyX + (batWidth - socW) / 2;
    // Place SoC number on the same baseline as the clock text (row0Y)
    int16_t socY = row0Y;

    // Draw SoC text transparently over the fill so color shows through
    tallyScreen.setTextColor(TFT_WHITE);
    tallyScreen.setCursor(socX, socY);
    tallyScreen.print(socStr);
    
    // Prefer the runtime-selected input; fall back to configured input if none.
    uint8_t selectedId = g_tally.selectedInput ? g_tally.selectedInput : eff.atemInput;


    // If no input is configured/selected yet, treat as idle & just show UI
    bool isProgram = false;
    bool isPreview = false;

    if (selectedId != 0) {
        isProgram = g_tally.isProgram(selectedId);
        isPreview = g_tally.isPreview(selectedId);
    }

    // --- Background color based on tally state ---
    enum class TallyColor {
        Black,
        Green,
        Red
    };
    static TallyColor lastColor = TallyColor::Black;

    TallyColor currentColor;
    if (isProgram) {
        currentColor = TallyColor::Red;
        tallyScreen.fillRect(0, statusBarHeight, tft_width, tft_heigth - statusBarHeight, TFT_RED);
    } else if (isPreview) {
        currentColor = TallyColor::Green;
        tallyScreen.fillRect(0, statusBarHeight, tft_width, tft_heigth - statusBarHeight, TFT_GREEN);
    } else {
        currentColor = TallyColor::Black;
        tallyScreen.fillRect(0, statusBarHeight, tft_width, tft_heigth - statusBarHeight, TFT_BLACK);
    }

    // If tally color changed since last frame, publish to MQTT
    if (currentColor != lastColor) {
        const char* colorStr = "black";
        switch (currentColor) {
            case TallyColor::Red:   colorStr = "red";   break;
            case TallyColor::Green: colorStr = "green"; break;
            case TallyColor::Black: colorStr = "black"; break;
        }
        g_mqtt.publishTallyColor(String(colorStr));
        lastColor = currentColor;
    }

    // --- Selected label (SEL) in the middle row, no border/background ---

    // Selected label from currently selected input
    String selectedLabel;
    if (selectedId != 0) {
        if (const AtemInputInfo* info = g_tally.findInput(selectedId)) {
            if (info->shortName.length()) {
                selectedLabel = info->shortName;
            } else if (info->longName.length()) {
                selectedLabel = info->longName;
            }
        }
        if (!selectedLabel.length()) {
            selectedLabel = String(selectedId);
        }
    }

    // Determine labels for current PREV and PROG buses by scanning known inputs
    String prevLabel;
    String progLabel;
    for (const auto& kv : g_tally.inputs) {
        uint8_t id = kv.first;
        const AtemInputInfo& info = kv.second;

        // Build a display label for this input
        String label;
        if (info.shortName.length()) {
            label = info.shortName;
        } else if (info.longName.length()) {
            label = info.longName;
        } else {
            label = String(id);
        }

        if (g_tally.isPreview(id) && !prevLabel.length()) {
            prevLabel = label;
        }
        if (g_tally.isProgram(id) && !progLabel.length()) {
            progLabel = label;
        }

        // If we've found both, we can stop early
        if (prevLabel.length() && progLabel.length()) {
            break;
        }
    }

    // Friendly name (from config), fallback to "Cam"
    String friendlyLabel = eff.friendlyName && eff.friendlyName[0] != '\0'
                           ? String(eff.friendlyName)
                           : String("Cam");

    // Placeholder for SEL font height if we later want to use it to adjust spacing above the friendly name
    int selFontHeight = 0;

    // Second row of the status bar: three subtle columns [SEL] [PREV] [PROG]
    {
        // Use a mid-size font that is easy to read but not overpowering
        tallyScreen.setFont(&fonts::DejaVu9);
        tallyScreen.setTextSize(2);
        tallyScreen.setTextColor(TFT_WHITE, TFT_BLACK);

        // Compute basic layout for three equal-width columns, with a small horizontal margin
        int marginX   = 4;                         // inset columns from left/right edges
        int innerW    = tft_width - (marginX * 2);
        int colWidth  = innerW / 3;
        int col1X     = marginX;
        int col2X     = marginX + colWidth;
        int col3X     = marginX + (colWidth * 2);

        int rowFontHeight = tallyScreen.fontHeight();
        int boxHeight     = rowFontHeight + 6;              // a little padding around the text

        // Center the box vertically around the text baseline for row1
        int boxTopY       = row1Y - (rowFontHeight/4)+2;  // adjust to better center around baseline
        if (boxTopY < 0) boxTopY = 0;

        // Dynamic border color for SEL column: match the current tally state
        uint16_t selBorderColor;
        if (isProgram) {
            selBorderColor = TFT_RED;
        } else if (isPreview) {
            selBorderColor = TFT_GREEN;
        } else {
            selBorderColor = TFT_BLACK;
        }

        // Column 1: SEL (selected input label or literal "SEL" if none)
        String selText = selectedLabel.length() ? selectedLabel : String("SEL");
        int16_t selTextW = tallyScreen.textWidth(selText);
        int16_t selTextX = col1X + (colWidth - selTextW) / 2;
        if (selTextX < col1X + 2) selTextX = col1X + 2;

        tallyScreen.drawRect(col1X + 1, boxTopY, colWidth - 2, boxHeight, selBorderColor);
        tallyScreen.setCursor(selTextX, row1Y);
        tallyScreen.print(selText);

        // Column 2: PREV (always green border) – show actual preview short name if available
        String prevText = prevLabel.length() ? prevLabel : String("PREV");
        int16_t prevTextW = tallyScreen.textWidth(prevText);
        int16_t prevTextX = col2X + (colWidth - prevTextW) / 2;
        if (prevTextX < col2X + 2) prevTextX = col2X + 2;

        tallyScreen.drawRect(col2X + 1, boxTopY, colWidth - 2, boxHeight, TFT_GREEN);
        tallyScreen.setCursor(prevTextX, row1Y);
        tallyScreen.print(prevText);

        // Column 3: PROG (always red border) – show actual program short name if available
        String progText = progLabel.length() ? progLabel : String("PROG");
        int16_t progTextW = tallyScreen.textWidth(progText);
        int16_t progTextX = col3X + (colWidth - progTextW) / 2;
        if (progTextX < col3X + 2) progTextX = col3X + 2;

        tallyScreen.drawRect(col3X + 1, boxTopY, colWidth - 2, boxHeight, TFT_RED);
        tallyScreen.setCursor(progTextX, row1Y);
        tallyScreen.print(progText);
    }

    // --- Friendly name at the bottom (large, left-aligned, using DejaVu56) ---
    if (friendlyLabel.length()) {
        // Use a large built-in DejaVu56 GFX font and fake a bold effect by overdrawing
        tallyScreen.setFont(&fonts::DejaVu72);
        tallyScreen.setTextSize(1);
        tallyScreen.setTextColor(TFT_WHITE);

        int nameFontHeight = tallyScreen.fontHeight();

        int16_t baseY = tft_heigth - nameFontHeight - 4;
        int16_t minY  = statusBarHeight + 12;
        if (baseY < minY) {
            baseY = minY;
        }

        int16_t nameX = 10;
        tallyScreen.setCursor(nameX, baseY);
        tallyScreen.print(friendlyLabel);
        tallyScreen.setCursor(nameX + 1, baseY);
        tallyScreen.print(friendlyLabel);
    }

    tallyScreen.pushSprite(0,0);
}


void refreshPowerScreen() {

    powerScreen.fillSprite(TFT_BLACK);
    powerScreen.setTextColor(TFT_WHITE);
    powerScreen.setCursor(0,0);
    powerScreen.setTextSize(1);
    powerScreen.println(F("Power Management"));

    powerScreen.setTextSize(1);
    powerScreen.println(pwr.powerMode);
    powerScreen.printf("Bat: %s\r\n  V: %.3fv    %.1f%%/%.1f%%/ %.1f%%\r\n  Cap: %umAh -> %umAh\r\n", pwr.batWarningLevel, pwr.batVoltage, pwr.batPercentage, pwr.batPercentageCoulomb, pwr.batPercentageHybrid, pwr.learnedCapOld, pwr.learnedCapNew);
    powerScreen.printf("  I: %.3fma  Ic: %.3fma\r\n", pwr.batCurrent, pwr.batChargeCurrent);
    powerScreen.printf("  Imax: %ima  Bmm: (%.f%%/%.f%%) SB: %i\r\n", pwr.maxChargeCurrent, pwr.batPercentageMin, pwr.batPercentageMax, currentBrightness);
    powerScreen.printf("USB:\r\n  V: %.3fv  I: %.3fma\r\n", pwr.vbusVoltage, pwr.vbusCurrent);
    powerScreen.printf("5V-In:\r\n  V: %.3fv  I: %.3fma\r\n", pwr.vinVoltage, pwr.vinCurrent);
    powerScreen.printf("APS:\r\n  V: %.3fv\r\n", pwr.apsVoltage);
    powerScreen.printf("AXP:\r\n  Temp: %.1fc", pwr.tempInAXP192);

    powerScreen.pushSprite(5,10);

}


void refreshSetupScreen() {

    const auto eff = g_config.effective();
   
    String strTimeStatus;
    strTimeStatus.reserve(16);
    switch (timeStatus()) {
        case (timeNotSet):
            strTimeStatus= "Not Set";
            break;
        case (timeNeedsSync):
            strTimeStatus = "Needs Sync";
            break;
        case (timeSet):
            strTimeStatus = "Set";
            break;
        default:
            break;
    }
    
    setupScreen.fillSprite(TFT_BLACK);
    setupScreen.setTextColor(TFT_WHITE);
    setupScreen.setCursor(0,0);
    setupScreen.setTextSize(2);
    setupScreen.println(F("Setup Screen"));
    setupScreen.println();
    setupScreen.setTextSize(1);
    setupScreen.println("Build: " + String(eff.buildDateTime));
    setupScreen.println("SSID: " + String(wm.getWiFiSSID()) + " " + String(WiFi.RSSI()));
    setupScreen.println("Webportal Active: " + String(wm.getWebPortalActive()));
    setupScreen.println("Hostname: " + wm.getWiFiHostname());
    setupScreen.println("IP: " + WiFi.localIP().toString());
    setupScreen.println("NTP: " + strTimeStatus);
    setupScreen.println();
    setupScreen.println("MQTT Server: " + String(eff.mqttServer) + ":" + String(eff.mqttPort));
    setupScreen.println("Connected: " + String(eff.mqtt_isConnected ? "Yes" : "No"));
    setupScreen.pushSprite(10,10);

}


void refreshStartupScreen() {
    startupScreen.fillSprite(TFT_BLACK);
    startupScreen.setTextColor(TFT_WHITE);
    startupScreen.setCursor(0,0);
    for (int i = 0; i <= index_startupLog; i++) {
        startupScreen.setTextSize(startupLogEntries[i].textSize);
        startupScreen.println(startupLogEntries[i].logMessage);
    }
    startupScreen.pushSprite(5,5);
}


void changeScreen(int newScreen) {

    Serial.println(F("changeScreen()"));
    if (newScreen < -1 || newScreen > maxScreen) {
        Serial.println(F("changeScreen() error: \"invalid screen number rejected\""));
        return;
    } else if (newScreen == -1) {
        if (currentScreen == maxScreen) currentScreen = SCREEN_STARTUP;  // reset
        currentScreen = static_cast<ScreenId>((static_cast<int>(currentScreen) + 1) % (maxScreen + 1));
    } else {
        currentScreen = static_cast<ScreenId>(newScreen);
    }

    if (wm.getWebPortalActive()) wm.stopWebPortal();
    
    startupScreen.deleteSprite();
    tallyScreen.deleteSprite();
    powerScreen.deleteSprite();
    setupScreen.deleteSprite();
    
    // clearScreen
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(0, 0);

    switch (currentScreen) {
        case SCREEN_STARTUP:
            // startupScreen
            startupScreen.createSprite(tft_width-5, tft_heigth-5);
            // startupScreen.setRotation(3);
            break;
        case SCREEN_TALLY:
            // tallyScreen
            tallyScreen.createSprite(tft_width, tft_heigth);
            // tallyScreen.setRotation(3);
            break;
        case SCREEN_POWER:
            // powerScreen
            powerScreen.createSprite(tft_width, tft_heigth);
            // powerScreen.setRotation(3);
            break;
        case SCREEN_SETUP:
            // setupScreen
            if (!wm.getWebPortalActive()) wm.startWebPortal();
            setupScreen.createSprite(tft_width, tft_heigth);
            // setupScreen.setRotation(3);
            break;
        default:
            M5.Display.println("Invalid Screen!");
            break; 
    }

    md_screenRefresh.start(1000 / 12); // ~12 fps to save power

}


void toggleMainTab() {
    if (currentScreen == SCREEN_TALLY) {
        changeScreen(SCREEN_POWER);   // Diagnostics tab
    } else if (currentScreen == SCREEN_POWER) {
        changeScreen(SCREEN_SETUP);   // Scren Setup tab
    } else {
        // If we're in Startup/Setup and user hits B, just go to Tally.
        changeScreen(SCREEN_TALLY);
    }
}





void refreshScreen() {

    // Limit refresh rate (set in changeScreen, currently ~12 FPS)
    if(!md_screenRefresh.justFinished()) return;
    md_screenRefresh.repeat();
    
    switch (currentScreen) {
        case SCREEN_STARTUP:
            refreshStartupScreen();
            break;
        case SCREEN_TALLY:
            refreshTallyScreen();
            break;
        case SCREEN_POWER:
            refreshPowerScreen();
            break;
        case SCREEN_SETUP:
            refreshSetupScreen();
            break;
        default:
            break; 
    }

}


void setBrightness(int newBrightness) {
    if (newBrightness < minBrightness) {
        newBrightness = minBrightness;
    }
    if (newBrightness > pwr.maxBrightness) {
        newBrightness = pwr.maxBrightness;
    }

    currentBrightness = newBrightness;

    std::uint8_t hwBrightness = static_cast<std::uint8_t>(
        (currentBrightness <= 0) ? 0 : (currentBrightness * 255) / 100
    );
    M5.Display.setBrightness(hwBrightness);
}


void startupLog(const char* in_logMessage, int in_textSize) {
    index_startupLog++;
    if (index_startupLog > 19) {
        Serial.println(F("Too many log entries."));
        return;
    }
    strncpy(startupLogEntries[index_startupLog].logMessage, in_logMessage, 64);
    startupLogEntries[index_startupLog].logMessage[64] = '\0';
    startupLogEntries[index_startupLog].textSize = in_textSize;
    refreshStartupScreen();
}