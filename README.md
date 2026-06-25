# Multical 21 Reader Firmware

Modern ESP8266/ESP32 firmware for Kamstrup Multical 21 water meters with an
AMS Reader inspired web interface, WiFi onboarding, local water usage graphs,
Home Assistant MQTT, Fibaro/local JSON API, browser OTA updates, and Telnet
debugging.

This project is a product-focused continuation of the ESP Multical 21 reader
idea. It keeps the useful wireless M-Bus / CC1101 / AES-CTR decoding foundation
and adds the practical firmware features needed to run a Multical 21 reader as
a small standalone appliance on a home network.

Keywords: Kamstrup Multical 21, Multical21, wireless M-Bus, wM-Bus, CC1101,
ESP8266, D1 mini, D1 mini Lite, ESP32, water meter, Home Assistant, MQTT,
Fibaro Home Center, OTA firmware, captive portal, water usage graphs.

## Highlights

- AMS-style dashboard with status top bar, live RX/radio/NTP/MQTT indicators,
  water total, hourly, daily, weekly usage, meter details, and a 60 minute live
  usage graph.
- WiFi onboarding with setup AP `Multical21-Setup`, captive portal redirects,
  WiFi scan, and WiFi connection testing from the browser.
- Browser setup UI for device name, WiFi, NTP, MQTT, Home Assistant discovery,
  Telnet debug, meter serial, and AES key.
- Runtime meter setup: no local `credentials.h` is required for meter serial or
  encryption key.
- Persistent history in LittleFS so hourly, daily, weekly, monthly, and yearly
  graph data can survive reboot.
- NTP-backed calendar buckets so graph days, weeks, months, and years follow
  real dates.
- MQTT JSON publishing and optional Home Assistant MQTT discovery.
- Local JSON endpoints for Fibaro, scripts, dashboards, and other home
  automation systems.
- Browser OTA firmware upload from `/firmware`.
- Optional Telnet debug on port 23 with mirrored serial diagnostics.
- GitHub Actions release builds for ESP32, ESP8266, and ESP8266 D1 mini Lite.

## Supported Hardware

- Kamstrup Multical 21 water meter with wireless M-Bus.
- CC1101 868 MHz radio module connected over SPI.
- ESP8266 D1 mini.
- ESP8266 D1 mini Lite.
- ESP32 boards supported by the PlatformIO `esp32` environment.

The default PlatformIO environment is `esp8266_lite`, because that is a common
small D1 mini Lite target with 1 MB flash. Use the matching firmware binary for
your board.

## Screenshots
<img width="1242" height="1207" alt="image" src="https://github.com/user-attachments/assets/c2f1ce47-d7eb-49d1-b4c0-c27d660cb772" />


<img width="1242" height="1207" alt="Multical 21 Reader setup and graphs" src="https://github.com/user-attachments/assets/82798402-5b35-46f1-8b8c-3aa0b3f0a1fe" />

## First Boot

1. Flash the matching firmware binary for your board.
2. Join the WiFi network `Multical21-Setup`.
3. Open the captive portal prompt, or browse to `http://192.168.4.1/`.
4. Scan/test WiFi.
5. Enter meter serial as 8 hex characters.
6. Enter AES key as 32 hex characters.
7. Configure MQTT/Home Assistant if needed.
8. Save and reboot.

Meter serial example:

```text
77513579
```

AES key example format:

```text
CB42BF02F313BCD5E24CECB586977267
```

Do not include `0x`, commas, or spaces in the web UI fields.

## Web UI

- `/` - dashboard with current water state and recent usage.
- `/setup` - WiFi, NTP, MQTT, Home Assistant, Telnet, meter serial, AES key.
- `/graphs` - hourly, daily, weekly, monthly, and yearly graphs.
- `/firmware` - browser OTA firmware upload.
- `/data.json` - local JSON state API.
- `/dayplot.json` - 24 hour plot data.
- `/monthplot.json` - 31 day plot data.
- `/version.json` - firmware version, build date, board, and git SHA.

If mDNS is available on your network, the device can also be reached by the
configured device name, for example:

```text
http://multical21.local
```

## Home Assistant And MQTT

MQTT can be enabled from the setup page. The firmware publishes decoded water
meter state as JSON and can publish Home Assistant discovery entities for:

- total water counter
- current hour usage
- daily usage
- weekly/monthly/yearly history windows
- water temperature
- room/ambient temperature
- last frame age
- Multical alarm flags: dry, reverse flow, leak, burst

Secure MQTT/TLS can be enabled, but certificate management is intentionally not
part of this firmware. TLS mode is intended for trusted local network broker
setups.

## Fibaro And Local API

Fibaro Home Center or any other controller can poll:

- `http://<device-ip>/data.json` for current state and sync-safe total counter.
- `http://<device-ip>/dayplot.json` for the 24 hour graph.
- `http://<device-ip>/monthplot.json` for the 31 day graph.

Use `total_m3` as the source of truth. If Fibaro misses polls, it can recover by
calculating the difference between the latest `total_m3` and the last value it
stored. Use `last_frame_age_s` to detect stale radio data.

The repository also contains a Fibaro Quick App for estimating BWT AQA Life salt
usage from the Multical water consumption API:

- [`fibaro/BWT_Salt_Monitor.lua`](fibaro/BWT_Salt_Monitor.lua)
- [`fibaro/README.md`](fibaro/README.md)

## Firmware Update

1. Build the matching PlatformIO target, or download a release binary.
2. Open `http://<device-ip>/firmware`.
3. Upload the matching `.bin` file.
4. The device validates the image and restarts when the upload succeeds.

For ESP8266 D1 mini Lite, use:

```text
multical21-reader-<version>-esp8266-lite.bin
```

## GitHub Release Binaries

Every pushed `v*` tag starts the release workflow. GitHub Actions builds and
attaches:

- `multical21-reader-<version>-esp32.bin`
- `multical21-reader-<version>-esp8266.bin`
- `multical21-reader-<version>-esp8266-lite.bin`
- `SHA256SUMS.txt`

## Build Locally

Install PlatformIO and build the default ESP8266 Lite target:

```sh
pio run
```

Build all supported environments:

```sh
pio run -e esp32 -e esp8266 -e esp8266_lite
```

## Force Setup / Captive Portal

- Open `http://<device-ip>/setup` and press `Reset setup`.
- The device clears runtime configuration and reboots into `Multical21-Setup`.
- Or erase flash before upload from PlatformIO if you want a fully clean first
  boot.

## Telnet Debug

1. Enable `Telnet debug` in the setup UI and reboot.
2. Connect with `telnet <device-ip> 23` or `nc <device-ip> 23`.
3. Serial debug output is mirrored to the Telnet session.

Telnet debug is useful for checking CC1101 packet interrupts, radio RSSI,
wireless M-Bus frame validation, meter serial matching, and AES decrypt status.

## Project Lineage

This firmware builds on the original open source Multical 21 reader work and
related wireless M-Bus decoding knowledge:

- Original project inspiration: [chester4444/esp-multical21](https://github.com/chester4444/esp-multical21)
- Additional ESP Multical 21 work: [derbmann/esp-multical21](https://github.com/derbmann/esp-multical21)
- Wireless M-Bus tooling: [wmbusmeters/wmbusmeters](https://github.com/wmbusmeters/wmbusmeters)
- Earlier MQTT/serial reader work by [weetmuts](https://github.com/weetmuts)

This repository is maintained as the product firmware version with browser
setup, persistent history, OTA update flow, local API, and home automation
integrations.

## Recommended GitHub Topics

Add these topics to the GitHub repository so the project is easier to find:

```text
kamstrup
multical21
water-meter
wireless-mbus
wmbus
cc1101
esp8266
esp32
home-assistant
mqtt
fibaro
ota
```

## License

This project follows the license inherited from the original ESP Multical 21
reader code. See [`LICENSE`](LICENSE).
