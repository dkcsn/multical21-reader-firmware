# esp-multical21

## New project direction

This repository is being shaped into an AMS Reader inspired firmware for
Kamstrup Multical 21 water meters: WiFi onboarding, browser configuration,
runtime entry of meter serial and AES key, water usage graphs, MQTT/Home
Assistant integration, and OTA updates.

See [PROJECT_PLAN.md](PROJECT_PLAN.md) for the current architecture and
milestone plan.

Current firmware baseline:

- Builds for ESP32 and ESP8266 with PlatformIO.
- Starts a setup access point named `Multical21-Setup` when WiFi is not
  configured or cannot connect.
- Serves a small browser UI on port 80 for WiFi, MQTT, meter serial, and AES key
  setup.
- Stores configuration in EEPROM instead of requiring a local `credentials.h`.
- Supports NTP time sync for calendar-aligned graph buckets.
- Publishes decoded water meter state to MQTT as JSON when MQTT is enabled.
- Supports MQTT retain and secure MQTT/TLS mode. TLS currently uses an insecure
  client mode without CA validation, intended for local broker setups.
- Supports Home Assistant MQTT discovery for water totals, usage windows,
  temperatures, last frame age, and Multical alarm flags.
- Tracks in-memory hourly and daily consumption history from the cumulative
  Multical 21 counter.
- Persists graph history to LittleFS so charts can survive reboot.
- Exposes `/configuration.json`, `/data.json`, `/dayplot.json`, and
  `/monthplot.json` for UI/API use.
- Uses ESP32 mbedTLS AES-CTR and ESP8266 Crypto AES-CTR for Multical 21 frame
  decryption.

First boot:

1. Flash the firmware.
2. Join the WiFi network `Multical21-Setup`.
3. Open `http://192.168.4.1/`.
4. Enter WiFi, MQTT, meter serial as 8 hex characters, and AES key as 32 hex
   characters.
5. Save and reboot.

Fibaro Home Center can poll:

- `http://<device-ip>/data.json` for current state and sync-safe total counter.
- `http://<device-ip>/dayplot.json` for the 24 hour graph.
- `http://<device-ip>/monthplot.json` for the 31 day graph.

Use `total_m3` as the source of truth. If Fibaro misses polls, it can recover by
calculating the difference between the latest `total_m3` and the last value it
stored. Use `last_frame_age_s` to detect stale radio data.

Added MQTT data upload to the project from weetmuts original the values was only send to the serial terminal.
And how the data is written to the serial terminal.

Recieve MQTT Topics via
"/watermeter/mydatajson" and 
"/watermeter/mydata"

ESP8266 decrypts wireless MBus frames from a Multical21 water meter

A CC1101 868 MHz modul is connected via SPI to the ESP8266 an configured to receive Wireless MBus frames.
The Multical21 is sending every 16 seconds wireless MBus frames (Mode C1, frame type B). The encrypted
frames are received from the ESP8266 an it decrypts them with AES-128-CTR. The meter information 
(total counter, target counter, medium temperature, ambient temperature, alalm flags (BURST, LEAK, DRY,
REVERSE) are sent via MQTT to a smarthomeNG/smartVISU service (running on a raspberry).

Thanks to [weetmuts](https://github.com/weetmuts) for his great job on the wmbusmeters.
