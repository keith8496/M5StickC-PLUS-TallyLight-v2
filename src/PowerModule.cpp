#include <M5Unified.h>
#include <millisDelay.h>
#include <RunningAverage.h>

// Enable power debugging logs
//#define DEBUG_POWER

#include "ScreenModule.h"
#include "ConfigState.h"
#include "PowerModule.h"


extern int currentBrightness;
extern ConfigState g_config;

power pwr;

static const int md_power_milliseconds = 100;
static millisDelay md_power;
//static millisDelay md_chargeControlWait;
static const int runningAvgCnt = 200;
static RunningAverage ravg_batVoltage(runningAvgCnt);

// --- AXP192 helpers on top of M5Unified --------------------------------------
//
// We mirror just enough of the original M5StickC AXP192 API to keep the old
// power logic working, but implemented in terms of M5.Power.Axp192.
//
static inline uint8_t axpRead8(uint8_t addr) {
    return M5.Power.Axp192.readRegister8(addr);
}

static inline void axpWrite8(uint8_t addr, uint8_t data) {
    M5.Power.Axp192.writeRegister8(addr, data);
}

static inline uint32_t axpRead32(uint8_t addr) {
    // AXP192 32-bit counters are stored big-endian across four consecutive
    // registers. We reconstruct the 32-bit value manually using the public
    // 8-bit accessor.
    uint32_t value = 0;
    value |= static_cast<uint32_t>(axpRead8(addr))     << 24;
    value |= static_cast<uint32_t>(axpRead8(addr + 1)) << 16;
    value |= static_cast<uint32_t>(axpRead8(addr + 2)) << 8;
    value |= static_cast<uint32_t>(axpRead8(addr + 3));
    return value;
}

// Coulomb-counter control (based on the old AXP192::Enable/ Clear /GetCoulombData)
static void axpEnableCoulombCounter() {
    // 0xB8: Coulomb counter control; bit7 = enable
    axpWrite8(0xB8, 0x80);
}

static void axpClearCoulombCounter() {
    // Set the clear flag (bit5) in 0xB8 while preserving other bits.
    uint8_t reg = axpRead8(0xB8);
    axpWrite8(0xB8, reg | 0x20);
}

static uint32_t axpGetCoulombChargeRaw() {
    // 0xB0–0xB3: charge coulomb counter (32-bit)
    return axpRead32(0xB0);
}

static uint32_t axpGetCoulombDischargeRaw() {
    // 0xB4–0xB7: discharge coulomb counter (32-bit)
    return axpRead32(0xB4);
}

// Net coulomb value in mAh, same formula as original AXP192::GetCoulombData.
static float axpGetCoulombNet_mAh() {
    uint32_t coin  = axpGetCoulombChargeRaw();
    uint32_t coout = axpGetCoulombDischargeRaw();

    uint32_t diff;
    bool negative = false;

    if (coin >= coout) {
        diff = coin - coout;
    } else {
        diff     = coout - coin;
        negative = true;
    }

    // From original AXP192 formula:
    //   c[mAh] = 65536 * current_LSB[mA] * (coin - coout) / 3600 / ADC_rate[Hz]
    // For AXP192: current_LSB = 0.5mA. We assume ADC_rate = 200Hz here.
    constexpr float currentLSB_mA = 0.5f;
    constexpr float adcRateHz     = 200.0f;

    float c_mAh = (65536.0f * currentLSB_mA * static_cast<float>(diff))
                  / 3600.0f / adcRateHz;

    if (negative) {
        c_mAh = -c_mAh;
    }
    return c_mAh;
}
// ---------------------------------------------------------------------------

static const int chargeControlSteps = 9;
static const uint8_t chargeControlArray[chargeControlSteps] = {0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8};
static const int chargeCurrentArray[chargeControlSteps] = {100, 190, 280, 360, 450, 550, 630, 700, 780};


// Define Functions
void power_onLoop();
void doPowerManagement();


// Function to estimate battery percentage with a non-linear discharge curve
float getBatPercentageVoltage(float voltage) {
    
  // Voltage-capacity segments for a single-cell LiPo battery
  const int numLevels = 21;
  const float batLookup_v3[numLevels][2] = {
    {4.20, 100.0}, 
    {4.12, 95.0}, 
    {4.04, 90.0}, 
    {3.98, 85.0}, 
    {3.92, 80.0}, 
    {3.86, 75.0}, 
    {3.80, 70.0}, 
    {3.75, 65.0}, 
    {3.71, 60.0}, 
    {3.68, 55.0}, 
    {3.66, 50.0}, 
    {3.64, 45.0}, 
    {3.62, 40.0}, 
    {3.61, 35.0}, 
    {3.60, 30.0}, 
    {3.56, 25.0}, 
    {3.54, 20.0}, 
    {3.50, 15.0}, 
    {3.47, 10.0}, 
    {3.40, 5.0}, 
    {3.00, 0.0}
  };

  // Check for out-of-range values
  if (voltage >= batLookup_v3[0][0]) {
      return 100.0; // Fully charged
  } else if (voltage <= batLookup_v3[numLevels - 1][0]) {
      return 0.0;   // Fully discharged
  }

  // Interpolate within the appropriate segment
  for (int i = 0; i < numLevels - 1; i++) {
      if (voltage <= batLookup_v3[i][0] && voltage > batLookup_v3[i + 1][0]) {
          // Linear interpolation between the two points
          float percentage = batLookup_v3[i][1] +
                            (voltage - batLookup_v3[i][0]) * 
                            (batLookup_v3[i + 1][1] - batLookup_v3[i][1]) /
                            (batLookup_v3[i + 1][0] - batLookup_v3[i][0]);
          return percentage;
      }
  }

  // Default return (should not reach here)
  return 0.0;

}

float getBatPercentageCoulomb() {
  const uint16_t g_batteryCapacity = g_config.device.batteryCapacityMah;
  const float bat = (g_batteryCapacity + pwr.coulombCount) / g_batteryCapacity * 100;
  if (bat > 100.0) {
    return 100.0;
  } else if (bat < 0.0) {
    return 0.0;
  } else {
    return bat;
  }
}

/*float getBatPercentage(float voltage) {
  //const float alpha = 0.7;
  //const float batPercentageVoltage = getBatPercentageVoltage(voltage);
  //const float batPercentageCoulomb = getBatPercentageCoulomb();
  //return (batPercentageVoltage * (1-alpha)) + (batPercentageCoulomb * alpha);
  return getBatPercentageCoulomb();
}*/

int getChargeCurrent() {
  const uint8_t chargeControlNow = axpRead8(0x33);
  for (int i = 0; i < chargeControlSteps; i++) {
    if (chargeControlNow == chargeControlArray[i]) {
      return chargeCurrentArray[i];
    }
  }
  return -1; // should never get here
}


/*void setChargeCurrent(int reqChargeCurrent) {
  
  if (reqChargeCurrent == 100) {
    md_chargeControlWait.stop();
  }
  if (reqChargeCurrent == getChargeCurrent()) {
    return;
  }
  if (md_chargeControlWait.isRunning()) {
    return;
  }
  
  uint8_t reqChargeControl = 0xc0;
  for (int i = 0; i < chargeControlSteps; i++) {
    if (reqChargeCurrent == chargeCurrentArray[i]) {
      reqChargeControl = chargeControlArray[i];
      break;
    }
  }
  
  axpWrite8(0x33, reqChargeControl);
  pwr.maxChargeCurrent = getChargeCurrent();
  md_chargeControlWait.start(20000);

}*/


void power_setup() {
  // Start the AXP192 coulomb counter and clear it so net mAh starts from 0.
  axpEnableCoulombCounter();
  axpClearCoulombCounter();

  doPowerManagement();
  ravg_batVoltage.fillValue(M5.Power.Axp192.getBatteryVoltage(), runningAvgCnt);
  md_power.start(md_power_milliseconds);
}


void power_onLoop() {
  if (md_power.justFinished()) {
      md_power.repeat();
      doPowerManagement();
  }
}


void doPowerManagement() {

  const int md_chargeToOff_milliseconds = 60000;
  static millisDelay md_chargeToOff;
  static millisDelay md_lowBattery;
  u_int16_t g_batteryCapacity = g_config.device.batteryCapacityMah;
  const uint8_t g_powersaverBrightness = g_config.global.powersaverBrightness;
  const uint8_t g_powersaverBatteryPct = g_config.global.powersaverBatteryPct;

  // Battery voltage and warning level (approximate low-voltage threshold).
  pwr.batVoltage = M5.Power.Axp192.getBatteryVoltage();
  ravg_batVoltage.addValue(pwr.batVoltage);

  const bool isBatWarningLevel = (pwr.batVoltage <= 3.40f);
  if (isBatWarningLevel) {
    const char* warning = "LOW BATTERY";
    snprintf(pwr.batWarningLevel, sizeof(pwr.batWarningLevel), "%s", warning);
  } else {
    const char* warning = "";
    snprintf(pwr.batWarningLevel, sizeof(pwr.batWarningLevel), "%s", warning);
  }

  pwr.batPercentage    = getBatPercentageCoulomb();
  pwr.batPercentageMin = getBatPercentageVoltage(ravg_batVoltage.getMinInBuffer());
  pwr.batPercentageMax = getBatPercentageVoltage(ravg_batVoltage.getMaxInBuffer());

  // Approximate net battery current from separate charge / discharge readings.
  float charge_mA    = M5.Power.Axp192.getBatteryChargeCurrent();
  float discharge_mA = M5.Power.Axp192.getBatteryDischargeCurrent();
  pwr.batChargeCurrent = charge_mA;
  pwr.batCurrent       = charge_mA - discharge_mA;  // positive = net charging

  pwr.maxChargeCurrent = getChargeCurrent();

  pwr.vbusVoltage = M5.Power.Axp192.getVBUSVoltage();
  pwr.vbusCurrent = M5.Power.Axp192.getVBUSCurrent();

  // In M5Unified, "VIN" is effectively ACIN (barrel power) on AXP192.
  pwr.vinVoltage = M5.Power.Axp192.getACINVoltage();
  pwr.vinCurrent = M5.Power.Axp192.getACINCurrent();

  pwr.apsVoltage   = M5.Power.Axp192.getAPSVoltage();
  pwr.tempInAXP192 = M5.Power.Axp192.getInternalTemperature();

  // Net battery coulomb count in mAh (positive = net charge in, negative = net discharge).
  pwr.coulombCount = axpGetCoulombNet_mAh();
  //pwr.batPercentageCoulomb = getBatPercentageCoulomb();


  // Power Mode
  if (pwr.vinVoltage > 3.8) {         // 5v IN Charge
    
    pwr.maxBrightness = 100;
    
    /*if (pwr.batPercentageMax < 70 && pwr.maxChargeCurrent < 780) {
      setChargeCurrent(780);
    } else if (pwr.batPercentageMax < 75 && pwr.maxChargeCurrent < 700) {
      setChargeCurrent(700);
    } else if (pwr.batPercentageMax < 80 && pwr.maxChargeCurrent < 630) {
      setChargeCurrent(630);
    } else if (pwr.batPercentageMax < 85 && pwr.maxChargeCurrent < 550) {
      setChargeCurrent(550);
    } else if (pwr.batPercentageMax < 90 && pwr.maxChargeCurrent < 450) {
      setChargeCurrent(450);
    } else if (pwr.batPercentageMax < 95 && pwr.maxChargeCurrent < 360) {
      setChargeCurrent(360);
    } else if (pwr.maxChargeCurrent != 280) {
      setChargeCurrent(280);
    }*/

    if (pwr.maxChargeCurrent != 780) {
      axpWrite8(0x33, 0xc8);
    }

    
    if (currentBrightness > 20) {

      const char* mode = "5v Charge";
      snprintf(pwr.powerMode, sizeof(pwr.powerMode), "%s", mode);
      md_chargeToOff.stop();

    } else {
                 
      // Charge to Off
      
      if (md_chargeToOff.justFinished()) {
        axpClearCoulombCounter();
        M5.Power.powerOff();
      }

      if (!md_chargeToOff.isRunning() && pwr.batVoltage >= 3.99 && floor(pwr.batChargeCurrent) == 0) {
        md_chargeToOff.start(md_chargeToOff_milliseconds);
      }
      
      if (md_chargeToOff.isRunning()) {
        constexpr size_t POWER_MODE_MAX_LEN   = 20;
        char powerMode[POWER_MODE_MAX_LEN];
        const int md_chargeToOffRemaining = floor(md_chargeToOff.remaining() / 1000);
        snprintf(powerMode, 20, "Charge to Off (%i)", md_chargeToOffRemaining);
        snprintf(pwr.powerMode, sizeof(pwr.powerMode), "%s", powerMode);
      } else {
        const char* mode = "Charge to Off";
        snprintf(pwr.powerMode, sizeof(pwr.powerMode), "%s", mode);
      }

    }

  } else if (pwr.vbusVoltage > 3.8) {   // 5v USB Charge

    md_chargeToOff.stop();
    const char* mode = "USB Charge";
    snprintf(pwr.powerMode, sizeof(pwr.powerMode), "%s", mode);
    pwr.maxBrightness = 100;
    
    if (pwr.maxChargeCurrent != 100) {
      axpWrite8(0x33, 0xc0);
    }

  } else {                              // 3v Battery

    md_chargeToOff.stop();
    if (pwr.maxChargeCurrent != 100) {
      axpWrite8(0x33, 0xc0);
    }

    if (isBatWarningLevel) {
      const char* mode = "Low Battery";
      snprintf(pwr.powerMode, sizeof(pwr.powerMode), "%s", mode);
      pwr.maxBrightness = g_powersaverBrightness;
      if (md_lowBattery.justFinished()) {
        md_lowBattery.repeat();
        // Coulomb counting is available again, but automatic capacity
        // recalibration can be risky. Leave this disabled for now until
        // behaviour is validated in the field.
        // g_batteryCapacity = pwr.coulombCount * (-1);
        // preferences_save();
      } else if (!md_lowBattery.isRunning()) {
        md_lowBattery.start(60000);
      }
    } else if (floor(pwr.batPercentageMin) <= g_powersaverBatteryPct) {
      const char* mode = "Power Saver";
      snprintf(pwr.powerMode, sizeof(pwr.powerMode), "%s", mode);
      pwr.maxBrightness = g_powersaverBrightness;
      if (currentBrightness > pwr.maxBrightness) setBrightness(pwr.maxBrightness);
    } else {
      const char* mode = "Balanced";
      snprintf(pwr.powerMode, sizeof(pwr.powerMode), "%s", mode);
      pwr.maxBrightness = 100;
    }

  }

  #ifdef DEBUG_POWER
  Serial.printf(
      "[POWER] Vbat=%.3fV  Vbus=%.3fV  Vin=%.3fV  Icharge=%.1fmA  Idischarge=%.1fmA  "
      "Inet=%.1fmA  Coulomb=%.2fmAh  SoC=%.1f%%  Mode=%s  Bright=%d/%d\n",
      pwr.batVoltage,
      pwr.vbusVoltage,
      pwr.vinVoltage,
      pwr.batChargeCurrent,
      (pwr.batChargeCurrent - pwr.batCurrent),   // discharge estimate
      pwr.batCurrent,
      pwr.coulombCount,
      pwr.batPercentage,
      pwr.powerMode,
      currentBrightness,
      pwr.maxBrightness
  );
  #endif
}