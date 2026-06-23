# Multical 21 Water Reader Project Plan

This project starts from `CentauriDK/esp-multical21` and aims to turn the
working Multical 21 Wireless M-Bus decoder into a product-like ESP firmware
with the kind of onboarding, configuration, graphs, MQTT integration, and OTA
experience found in `UtilitechAS/amsreader-firmware`.

## Licensing Direction

- Keep this project GPLv3, because the current Multical 21 firmware is GPLv3.
- Do not copy AMS Reader source code directly into this repository. AMS Reader
  is Fair Source licensed with a 5 user limitation, so this project should
  reimplement product ideas and architecture patterns rather than importing
  code.
- It is fine to use AMS Reader as a reference for feature shape: captive portal,
  configuration categories, plot endpoints, MQTT payload concepts, OTA flow, and
  Home Assistant discovery.

## What Exists Today

The inherited firmware already has the core meter path:

- CC1101 868 MHz SPI setup for Wireless M-Bus C1 frame reception.
- Interrupt-driven packet receive via GDO0.
- Multical 21 meter serial filtering.
- AES-128-CTR decryption.
- Parsing of total volume, target/month-start volume, water temperature,
  ambient temperature, and alarm flags.
- Basic WiFi, MQTT publishing, and Arduino OTA.

The current limitations are product-level rather than radio-level:

- WiFi, MQTT, meter serial, and AES key are compile-time values in
  `credentials.h`.
- There is no browser onboarding or captive portal.
- There is no persistent settings UI.
- MQTT topics and JSON payloads are fixed and narrow.
- There is no local history for hourly, daily, or monthly water usage.
- There is no dashboard or graph UI.
- There is little separation between radio decode, state model, publishing, and
  application lifecycle.

## Target Architecture

### Core Model

Create a small `WaterData` state model similar in role to AMS Reader's
`AmsData`, but water-specific:

- `meterSerial`
- `totalLiters` or `totalMilliCubicMeters`
- `monthStartLiters`
- `consumptionSinceMonthStart`
- `waterTemperatureC`
- `ambientTemperatureC`
- `alarmBurst`
- `alarmLeak`
- `alarmDry`
- `alarmReverse`
- `rssi`
- `lastFrameMillis`
- `lastFrameTime`
- `lastError`

Keep the radio decoder focused on producing this model, not on printing or
publishing.

### Configuration

Replace compile-time secrets with persistent configuration:

- WiFi SSID and password.
- Hostname and mDNS toggle.
- MQTT host, port, username, password, base topic, retain flag, and payload
  format.
- Multical 21 meter serial number.
- AES-128 encryption key as a 32 character hex string.
- CC1101 pin mapping for common ESP8266 and ESP32 boards.
- UI preferences such as dark mode and graph visibility.

Secrets should be masked when returned to the browser. Existing secret values
should be kept if a save request sends `***` or an empty "unchanged" marker.

### Onboarding

Add a first boot flow inspired by AMS Reader:

- Start as access point when no WiFi config exists.
- Run a DNS captive portal and web server.
- Serve `/setup` as the first screen.
- Provide `/wifiscan.json`, `/wifitest.json`, `/configuration.json`, and
  `/save` endpoints.
- Reboot into station mode after a successful save.

### Local History And Graphs

Use the cumulative meter counter as the source of truth:

- Hour plot: 24 buckets of consumed liters/m3 per hour.
- Month plot: 31 buckets of consumed liters/m3 per day.
- Realtime plot: ring buffer of short-interval usage estimate when new frames
  arrive.

Storage can be compact LittleFS binary records or JSON during early development.
The first implementation should prefer clarity; storage compaction can follow
once the data model is stable.

### Web UI

Build an embedded UI with:

- Dashboard: current total, today's usage, month usage, temperatures, alarm
  badges, WiFi/MQTT/radio status, and last frame age.
- Day graph: hourly water usage.
- Month graph: daily water usage.
- Settings: WiFi, MQTT, meter serial, AES key, pins, OTA, reset.
- System page: firmware version, chip, heap, filesystem, RSSI, uptime.

The UI should be usable directly from the device without cloud services.

### MQTT And Home Assistant

Publish a stable JSON payload, for example:

```json
{
  "total_m3": 123.456,
  "today_m3": 0.321,
  "month_m3": 8.765,
  "water_temperature_c": 12,
  "ambient_temperature_c": 21,
  "alarms": {
    "burst": false,
    "leak": false,
    "dry": false,
    "reverse": false
  },
  "last_frame_age_s": 8
}
```

Add Home Assistant MQTT discovery once the base MQTT payload is stable.

## Suggested Milestones

1. Baseline cleanup
   - Rename project and firmware identity.
   - Remove hard dependency on `credentials.h`.
   - Introduce `WaterData` and make decoder return data instead of publishing.

2. Persistent config
   - Add config structs and LittleFS/Preferences persistence.
   - Move WiFi, MQTT, meter serial, and AES key to runtime config.
   - Add validation for meter serial and AES key.

3. WiFi onboarding
   - Add AP fallback, captive portal, WiFi scan, test, save, and reboot flow.

4. Web dashboard
   - Add web server routes and static UI.
   - Add `data.json`, `configuration.json`, and `save` endpoints.

5. Graph storage
   - Add day/month history buckets.
   - Add `dayplot.json` and `monthplot.json`.

6. MQTT/Home Assistant
   - Replace fixed topics with configurable base topic.
   - Add Home Assistant discovery messages.

7. OTA and release packaging
   - Keep Arduino OTA for development.
   - Add browser firmware upload.
   - Add GitHub Actions build artifacts.

## Hardware Notes

Default ESP32 pin mapping from the current project:

- CC1101 CSN: GPIO 4
- MOSI: GPIO 23
- MISO: GPIO 19
- SCK: GPIO 18
- GDO0: GPIO 32

Default ESP8266 / Wemos D1 mini mapping:

- CC1101 CSN: D8
- MOSI: D7
- MISO: D6
- SCK: D5
- GDO0: D2

## Open Decisions

- Preferred target board: ESP32 is recommended for web UI, TLS MQTT, OTA, and
  graph storage headroom.
- Whether to keep ESP8266 support after the product features land.
- Repository name under `dkcsn`.
- Whether the project should stay a fork of `CentauriDK/esp-multical21` or move
  to a fresh `dkcsn` repository with attribution.
