# MQTT Specification – TallyLight v2.0

This document defines the MQTT topic structure, payload formats, and expected behavior for the TallyLight v2 ecosystem.
All tally devices MUST follow this specification for interoperability with Companion, Node-RED, and each other.

---

# 1. Overview

Each device is identified by a unique `{device}` string, such as:

```
7A90F3
```

This is usually derived from the MAC address of the device.

All topics originate under:

```
sanctuary/
```

Devices subscribe to:

- ATEM state topics
- Global configuration topics
- Per-device configuration topics
- Global & per-device commands

Devices publish:

- Availability (online/offline)
- Status (battery, RSSI, uptime)
- Logs (optional)
- Hardware health

All topics using `config/` are **retained** unless otherwise noted.

---

# 2. ATEM State Topics

## 2.1 Program / Preview Bus

| Purpose | Topic | Direction | Retained | Payload |
|--------|--------|-----------|----------|---------|
| Preview input ID | `sanctuary/atem/preview` | Companion → Devices | Yes | `"1"`, `"2"`, etc. |
| Program input ID | `sanctuary/atem/program` | Companion → Devices | Yes | `"1"`, `"2"`, etc. |

Devices must update their tally status immediately when these values change.

---

## 2.2 Input Configuration (Labels, Types)

| Topic | Direction | Retained | Payload |
|--------|-----------|----------|---------|
| `sanctuary/atem/inputs` | Companion → Devices | Yes | JSON map of inputs |

Example JSON:

```json
{
  "1": { "id": 1, "short_name": "CTR", "long_name": "Center Cam", "tally_enabled": "TRUE" },
  "2": { "id": 2, "short_name": "LFT", "long_name": "Left Cam",   "tally_enabled": "TRUE" }
}
```

- `short_name`: 3–4 character label used on the tally UI (e.g. `CTR`, `NWC`).
- `long_name`: human-readable label (e.g. `Center Cam`, `North West Corner`).
- `tally_enabled`: `"TRUE"` or `"FALSE"` (case-insensitive). Only inputs with `tally_enabled == "TRUE"` are eligible when cycling inputs on the device.

---

# 3. Global Configuration Topics

Global configuration applies to all tally devices unless overridden by per-device config (when applicable).

All global config topics are **retained**.

## 3.1 Network / Infrastructure

| Topic | Payload Example | Notes |
|--------|-----------------|-------|
| `sanctuary/tally/config/mqtt_server`   | `"172.16.30.11"` | MQTT broker hostname/IP (overrides WiFiManager default) |
| `sanctuary/tally/config/mqtt_port`     | `"1883"`         | MQTT broker port |
| `sanctuary/tally/config/mqtt_username` | `"tally"`        | MQTT username (optional) |
| `sanctuary/tally/config/mqtt_password` | `"secret"`       | MQTT password (optional) |
| `sanctuary/tally/config/ntp_server`    | `"us.pool.ntp.org"` | NTP server hostname/IP |
| `sanctuary/tally/config/timezone`      | `"America/Chicago"` or `"Etc/UTC"` | Device timezone (IANA string or fixed offset) |

WiFi SSID/password are configured via WiFiManager, not MQTT. The MQTT `*_server` and `*_port` topics can optionally override the WiFiManager defaults at runtime.

---

## 3.2 Display Settings

| Topic | Payload Example | Purpose |
|--------|------------------|---------|
| `sanctuary/tally/config/brightness` | `"50"` | Default screen brightness |
| `sanctuary/tally/config/powersaver_brightness` | `"20"` | Brightness when battery low |
| `sanctuary/tally/config/powersaver_battery_pct` | `"25"` | Battery threshold (%) for power saver mode |
| `sanctuary/tally/config/tally_color_program` | `"#FF0000"` | Color of program tally |
| `sanctuary/tally/config/tally_color_preview` | `"#00FF00"` | Color of preview tally |

---

## 3.3 Network / Wi-Fi Tuning

| Topic | Payload Example | Purpose |
|--------|-----------------|---------|
| `sanctuary/tally/config/wifi_tx_power` | `"8"` | Wi-Fi TX power (dBm) |
| `sanctuary/tally/config/wifi_sleep` | `"modem"` / `"light"` / `"none"` | ESP32 sleep mode |
| `sanctuary/tally/config/status_interval` | `"30"` | Status publish interval (seconds) |

---

## 3.4 OTA Configuration (Future Feature Only)

OTA is not implemented yet, but topics are reserved for future use.

| Topic | Example | Notes |
|--------|---------|-------|
| `sanctuary/tally/config/firmware_url` | `"http://10.0.20.15/fw/tally_v2.3.bin"` | URL for OTA firmware (future) |
| `sanctuary/tally/config/firmware_auto` | `"true"` | Whether to auto-apply updates |

Devices may ignore these until OTA support is added.

---

# 4. Per-Device Configuration (`{device}`)

These apply only to the designated tally device.

## 4.1 Per-Device Config Topics

| Topic | Payload Example | Purpose |
|--------|-------------------------|---------|
| `sanctuary/tally/{device}/config/name`              | `"Cam1"`     | Friendly device name (shown on screen) |
| `sanctuary/tally/{device}/config/input`             | `"1"`        | ATEM input this tally listens to (0–255) |
| `sanctuary/tally/{device}/config/battery_capacity`  | `"2200"`     | Battery capacity (mAh) used by SoC model |
| `sanctuary/tally/{device}/config/log_level`         | `"debug"`    | Per-device log level: `"none"`, `"error"`, `"warn"`, `"info"`, `"debug"` |

---

# 5. Commands

## 5.1 Global Commands (Broadcast)

| Topic | Payload | Purpose |
|--------|---------|---------|
| `sanctuary/tally/all/cmd` | `"deep_sleep"`    | Put all devices into deep sleep |
|                           | `"wakeup"`        | Wake all devices (if supported) |
|                           | `"reboot"`        | Reboot all devices |
|                           | `"ota_update"`    | Reserved for future OTA rollout |
|                           | `"factory_reset"` | Factory reset all devices (clear config/prefs, implementation-defined) |
|                           | `"resync_time"`   | Force all devices to re-run NTP/timezone sync |

Not retained.

---

## 5.2 Per-Device Commands

| Topic | Payload | Purpose |
|--------|---------|---------|
| `sanctuary/tally/{device}/cmd` | `"deep_sleep"`    | Deep sleep this device |
|                                | `"wakeup"`        | Wake this device (if supported) |
|                                | `"reboot"`        | Reboot this device |
|                                | `"ota_update"`    | Reserved for future OTA |
|                                | `"factory_reset"` | Factory reset this device (clear config/prefs, implementation-defined) |
|                                | `"resync_time"`   | Force this device to re-run NTP/timezone sync |

Not retained.

---

# 6. Device Status & Health

Devices publish status periodically (default: every 30s, overridable via `status_interval`).

## 6.1 Availability (LWT)

| Topic | Payload | Retained | Notes |
|--------|----------|----------|-------|
| `sanctuary/tally/{device}/availability` | `"online"` / `"offline"` | Yes | `"offline"` is LWT |

---

## 6.2 Core Status

| Topic | Payload Example | Notes |
|--------|-----------------|-------|
| `sanctuary/tally/{device}/status/uptime` | `"12345"` | Seconds since boot |
| `sanctuary/tally/{device}/status/battery_pct` | `"83"` | Battery percentage |
| `sanctuary/tally/{device}/status/battery_mv` | `"4090"` | Battery voltage |
| `sanctuary/tally/{device}/status/rssi` | `"-58"` | Wi-Fi RSSI |

---

## 6.3 Hardware Health

| Topic | Payload Example | Purpose |
|--------|-----------------|---------|
| `sanctuary/tally/{device}/status/temperature` | `"42.3"` | °C |
| `sanctuary/tally/{device}/status/restarts` | `"5"` | Boot count |
| `sanctuary/tally/{device}/status/firmware_version` | `"2.0.0-mqtt"` | Firmware version |
| `sanctuary/tally/{device}/status/hw_revision` | `"M5StickC-Plus-1.0"` | HW revision |

---

# 7. Logging & Diagnostics

## 7.1 Log Level

| Topic | Payload Options |
|--------|-----------------|
| `sanctuary/tally/{device}/config/log_level` | `"none"`, `"error"`, `"warn"`, `"info"`, `"debug"` |

## 7.2 Log Stream

| Topic | Payload Example |
|--------|-----------------|
| `sanctuary/tally/{device}/status/log` | `"WiFi connected: -58 dBm"` |

Log messages published to `.../status/log` are filtered on-device according to `sanctuary/tally/{device}/config/log_level`. For example, if `log_level == "info"`, `"debug"` logs are suppressed.

---

# 8. Topic Tree Summary

```
sanctuary/
  atem/
    preview
    program
    inputs

  tally/
    config/
      mqtt_server
      mqtt_port
      mqtt_username
      mqtt_password
      ntp_server
      timezone
      brightness
      powersaver_brightness
      powersaver_battery_pct
      tally_color_program
      tally_color_preview
      wifi_tx_power
      wifi_sleep
      status_interval
      firmware_url
      firmware_auto

    all/
      cmd

    {device}/
      config/
        name
        input
        battery_capacity
        log_level
      cmd
      availability
      status/
        uptime
        battery_pct
        battery_mv
        rssi
        temperature
        restarts
        firmware_version
        hw_revision
        log
```

---

# 9. Notes

- All topics under `config/` are **retained** unless explicitly noted.
- Status topics should NOT be retained.
- Commands must NOT be retained.
- Devices should save config values to NVS upon receipt.
- Devices should react to both global and per-device commands.

---

# 10. Version History

- v2.0 — Full rewrite for MQTT-based ecosystem
- - Added global config
- - Added per-device config
- - Added broadcast commands
- - Added power saver controls
- - Added status + hardware health
- - Added diagnostic logging
- - OTA reserved but not implemented