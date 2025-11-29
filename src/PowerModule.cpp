#include <M5Unified.h>
#include <millisDelay.h>
#include <RunningAverage.h>

// Enable power debugging logs
//#define DEBUG_POWER

#include "ScreenModule.h"
#include "ConfigState.h"
#include "PowerModule.h"
#include "PrefsModule.h"
#include "MqttClient.h"


extern int currentBrightness;
extern ConfigState g_config;

power pwr;

static const int md_power_milliseconds = 250;
static millisDelay md_power;
//static millisDelay md_chargeControlWait;
static const int runningAvgCnt = 16;
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

static bool s_capacityLearnedThisCycle = false;

// Learn effective battery capacity (mAh) from a near-full-to-near-empty discharge
// cycle. This should only be called when we are truly near empty and running on
// battery power. It updates g_config.device.batteryCapacityMah using a slow EMA
// so random glitches don't cause big jumps.
static void learnBatteryCapacityFromCycle() {
    if (s_capacityLearnedThisCycle) {
        return;
    }

    // Only when running on battery (no external VIN/VBUS present).
    if (pwr.vinVoltage > 3.8f || pwr.vbusVoltage > 3.8f) {
        return;
    }

    // Only when we're really near empty, not just at the low-battery threshold.
    // We treat <= 3.30V as "effectively empty" for learning purposes.
    if (pwr.batVoltage > 3.30f) {
        return;
    }

    uint16_t oldCap = g_config.device.batteryCapacityMah;
    if (oldCap == 0) {
        return;  // misconfigured; nothing to learn against
    }

    // We anchor the coulomb counter at full (0 mAh == 100% SoC). As we discharge,
    // pwr.coulombCount goes negative. The total discharged capacity for this cycle
    // is therefore -coulombCount.
    float discharged_mAh = -pwr.coulombCount;
    if (discharged_mAh <= 0.0f) {
        return;
    }

    // Sanity check: require the observed capacity to be within a reasonable band
    // around the current configured capacity. This filters out partial cycles and
    // wild readings.
    float minCap = oldCap * 0.5f;
    float maxCap = oldCap * 1.5f;
    if (discharged_mAh < minCap || discharged_mAh > maxCap) {
        return;
    }

    // Exponential moving average: mostly trust the existing capacity, but slowly
    // fold in the new observation.
    float newCap = oldCap * 0.9f + discharged_mAh * 0.1f;
    uint16_t newCapRounded = static_cast<uint16_t>(newCap + 0.5f);

    // Update the in-memory config so subsequent SoC calculations use the
    // learned capacity for the rest of this run.
    g_config.device.batteryCapacityMah = newCapRounded;    
    
    // Expose last-learned values for the power screen / debug UI.
    pwr.learnedCapOld = oldCap;
    pwr.learnedCapNew = newCapRounded;

    #ifdef DEBUG_POWER
    logf(
        LogLevel::Debug,
        "[POWER] Capacity learn: old=%umAh  new=%umAh  discharged=%.1fmAh  Vbat=%.3fV\n",
        static_cast<unsigned>(oldCap),
        static_cast<unsigned>(newCapRounded),
        discharged_mAh,
        pwr.batVoltage
    );
    #endif

    // NOTE: preferences_save() currently only persists MQTT settings. The
    // effective battery capacity still comes from MQTT at boot. Once we move
    // batteryCapacityMah into NVS, we can persist newCapRounded here as well.

    s_capacityLearnedThisCycle = true;
}


// Define Functions
void power_onLoop();
void doPowerManagement();


// Function to estimate battery percentage with a non-linear discharge curve
float getBatPercentageVoltage(float voltage) {
  const int numLevels = 21;
  // {Voltage, SoC%}, sorted high → low voltage
  const float batLookup_v4[numLevels][2] = {
    {4.20f, 100.0f},
    {4.12f,  95.0f},
    {4.06f,  90.0f},
    {4.02f,  85.0f},
    {3.98f,  80.0f},
    {3.94f,  75.0f},
    {3.90f,  70.0f},
    {3.86f,  65.0f},
    {3.82f,  60.0f},
    {3.78f,  55.0f},
    {3.74f,  50.0f},
    {3.70f,  45.0f},
    {3.66f,  40.0f},
    {3.62f,  35.0f},
    {3.58f,  30.0f},
    {3.52f,  25.0f},
    {3.46f,  20.0f},
    {3.40f,  15.0f},
    {3.34f,  10.0f},
    {3.28f,   5.0f},
    {3.20f,   0.0f}
  };

  if (voltage >= batLookup_v4[0][0]) {
    return 100.0f;
  }
  if (voltage <= batLookup_v4[numLevels - 1][0]) {
    return 0.0f;
  }

  for (int i = 0; i < numLevels - 1; i++) {
    float vHigh = batLookup_v4[i][0];
    float vLow  = batLookup_v4[i + 1][0];

    if (voltage <= vHigh && voltage > vLow) {
      float socHigh = batLookup_v4[i][1];
      float socLow  = batLookup_v4[i + 1][1];

      float t = (voltage - vHigh) / (vLow - vHigh);  // 0..1
      return socHigh + t * (socLow - socHigh);
    }
  }

  // Should never hit this, but be safe:
  return 0.0f;
}

float getBatPercentageCoulomb() {
  const uint16_t cap = g_config.device.batteryCapacityMah;

  if (cap == 0) {
      return NAN;  // misconfigured; treat as invalid
  }

  // We assume the coulomb counter was cleared at a known-full state (e.g. during
  // bench calibration or a proper "charge-to-off" cycle), so 0 mAh corresponds
  // to 100% SoC. As the device discharges, pwr.coulombCount becomes negative,
  // reducing the computed SoC.
  float bat = (cap + pwr.coulombCount) / cap * 100.0f;

  if (bat > 100.0f) bat = 100.0f;
  if (bat <   0.0f) bat = 0.0f;
  return bat;
}

float getBatPercentageHybrid() {
    float socV = pwr.batPercentage;          // voltage-based (already smoothed)
    float socC = pwr.batPercentageCoulomb;   // NAN if not calibrated

    if (isnan(socC)) return socV;

    // Coulomb is great in the middle; trust voltage at edges
    if (socV < 10.0f || socV > 95.0f) {
        return socV;
    }

    // Reject insane CC readings
    if (fabsf(socC - socV) > 20.0f) {
        return socV;
    }

    // Blend them
    constexpr float alpha = 0.6f;   // weight toward CC
    return socV * (1.0f - alpha) + socC * alpha;
}

int getChargeCurrent() {
  const uint8_t chargeControlNow = axpRead8(0x33);
  for (int i = 0; i < chargeControlSteps; i++) {
    if (chargeControlNow == chargeControlArray[i]) {
      return chargeCurrentArray[i];
    }
  }
  return -1; // should never get here
}


void power_setup() {
  axpEnableCoulombCounter();
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

  // 1) Voltage SoC (production)
  pwr.batPercentage    = getBatPercentageVoltage(ravg_batVoltage.getFastAverage());
  pwr.batPercentageMin = getBatPercentageVoltage(ravg_batVoltage.getMinInBuffer());
  pwr.batPercentageMax = getBatPercentageVoltage(ravg_batVoltage.getMaxInBuffer());

  // 2) Coulomb SoC (experimental, debug-only for now)
  float socC = getBatPercentageCoulomb();
  pwr.batPercentageCoulomb = socC;

  // 3) Hybrid SoC (voltage + coulomb blended)
  pwr.batPercentageHybrid = getBatPercentageHybrid();

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
        axpClearCoulombCounter();   // 0 mAh == 100%
        pwr.coulombCount = 0.0f;    // keep RAM in sync
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
        learnBatteryCapacityFromCycle();
      } else if (!md_lowBattery.isRunning()) {
        // First time we drop into the low-battery region for this cycle; arm
        // the timer and allow a capacity-learning attempt once the system has
        // been stably low for a while.
        s_capacityLearnedThisCycle = false;
        md_lowBattery.start(60000);  // 60s of sustained low-battery before learning
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
  logf(
      LogLevel::Debug,
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