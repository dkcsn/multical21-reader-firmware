/*
 Copyright (C) 2020 chester4444@wolke7.net
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
*/

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266mDNS.h>
  #include <WiFiClientSecureBearSSL.h>
#elif defined(ESP32)
  #include <WiFi.h>
  #include <ESPmDNS.h>
  #include <WiFiClientSecure.h>
  #include <esp_wifi.h>
#endif

#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <PubSubClient.h>
#include <time.h>
#include <string.h>

#include "AppConfig.h"
#include "AppWebServer.h"
#include "DebugLog.h"
#include "FirmwareVersion.h"
#include "WaterData.h"
#include "WaterHistory.h"
#include "WaterMeter.h"
#include "hwconfig.h"

AppConfig appConfig;
WaterData waterData;
WaterHistory waterHistory;
WaterMeter waterMeter;
AppWebServer webServer(appConfig, waterData, waterHistory);

WiFiClient espMqttClient;
#if defined(ESP8266)
BearSSL::WiFiClientSecure secureMqttClient;
#else
WiFiClientSecure secureMqttClient;
#endif
PubSubClient mqttClient;
DNSServer dnsServer;

bool setupApMode = false;
bool radioStarted = false;
unsigned long lastRadioStartAttempt = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastMqttPublish = 0;
unsigned long lastNtpAttempt = 0;
unsigned long lastDebugHeartbeat = 0;
bool haDiscoveryPublished = false;
bool ntpConfigured = false;
bool ntpSyncLogged = false;
bool telnetDebugConfigured = false;

static String chipIdHex() {
#if defined(ESP32)
  uint64_t mac = ESP.getEfuseMac();
  return String((uint32_t)(mac & 0xFFFFFFFF), HEX);
#else
  return String(ESP.getChipId(), HEX);
#endif
}

static String topic(const char* suffix) {
  String base = appConfig.data().mqttBaseTopic;
  if (base.length() == 0) {
    base = "watermeter";
  }
  if (base.endsWith("/")) {
    return base + suffix;
  }
  return base + "/" + suffix;
}

static String haNodeId() {
  return String("multical21_") + chipIdHex();
}

static String setupApName() {
  String name = appConfig.deviceName() + "-Setup";
  if (name.length() > 31) {
    name = name.substring(0, 31);
  }
  return name;
}

static void applyWifiHostname() {
  String name = appConfig.deviceName();
#if defined(ESP8266)
  WiFi.hostname(name.c_str());
#else
  WiFi.setHostname(name.c_str());
#endif
}

static String discoveryPrefix() {
  String prefix = appConfig.data().homeAssistantPrefix;
  prefix.trim();
  if (prefix.length() == 0) {
    prefix = "homeassistant";
  }
  if (prefix.endsWith("/")) {
    prefix.remove(prefix.length() - 1);
  }
  return prefix;
}

static time_t localNow() {
  time_t now = time(nullptr);
  if (now < 1600000000) {
    return 0;
  }
  return now + ((time_t) appConfig.data().timezoneOffsetMinutes * 60);
}

static void setupNtp() {
  if (!appConfig.data().ntpEnabled || WiFi.status() != WL_CONNECTED) {
    return;
  }
  const char* primaryServer = strlen(appConfig.data().ntpServer) > 0 ? appConfig.data().ntpServer : "pool.ntp.org";
  configTime(0, 0, primaryServer, "pool.ntp.org", "time.google.com");
  lastNtpAttempt = millis();
  waterData.ntpLastAttemptMillis = lastNtpAttempt;
  waterData.ntpAttemptCount++;
  ntpConfigured = true;
  Debug.print("NTP configured: ");
  Debug.println(primaryServer);
}

static bool isNtpSynced() {
  return time(nullptr) >= 1600000000;
}

static void loopNtp() {
  if (setupApMode || !appConfig.data().ntpEnabled) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    ntpConfigured = false;
    ntpSyncLogged = false;
    return;
  }

  if (!ntpConfigured || (!isNtpSynced() && millis() - lastNtpAttempt > 30000)) {
    setupNtp();
  }

  if (!ntpSyncLogged && isNtpSynced()) {
    ntpSyncLogged = true;
    waterData.ntpLastSyncMillis = millis();
    waterData.ntpLastSyncEpoch = (uint32_t) time(nullptr);
    Debug.print("NTP synced: ");
    Debug.println(appConfig.data().ntpServer);
  }
}

static bool connectWifi() {
  if (!appConfig.hasWifi()) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  applyWifiHostname();
  WiFi.begin(appConfig.data().wifiSsid, appConfig.data().wifiPassword);
  Debug.print("Connecting to WiFi ");
  Debug.println(appConfig.data().wifiSsid);

  for (uint8_t i = 0; i < 40; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      Debug.print("IP address: ");
      Debug.println(WiFi.localIP().toString());
      return true;
    }
    digitalWrite(PIN_LED_BUILTIN, !digitalRead(PIN_LED_BUILTIN));
    delay(250);
    Debug.print(".");
  }
  Debug.println();
  return false;
}

static void startSetupAp() {
  setupApMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  applyWifiHostname();
  String apName = setupApName();
  IPAddress apIp(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIp, gateway, subnet);
  const char* apPassword = nullptr;
  bool apStarted = WiFi.softAP(apName.c_str(), apPassword, 6, false, 4);
  delay(1000);
  dnsServer.start(53, "*", WiFi.softAPIP());
  Debug.print(apStarted ? "Setup AP started: " : "Setup AP start failed: ");
  Debug.print(apName);
  Debug.print(apPassword == nullptr ? " open" : " secured");
  Debug.print(" at ");
  Debug.print(WiFi.softAPIP().toString());
  Debug.print(" channel ");
  Debug.print(WiFi.channel());
  Debug.print(" mac ");
  Debug.println(WiFi.softAPmacAddress());
}

static void setupOTA() {
  ArduinoOTA.setHostname(appConfig.deviceName().c_str());
  ArduinoOTA.onStart([]() {
    Debug.println("OTA update started");
  });
  ArduinoOTA.onEnd([]() {
    Debug.println("\nOTA update finished");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Debug.printf("OTA progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Debug.printf("OTA error[%u]\n", error);
  });
  ArduinoOTA.begin();
}

static bool mqttConnect() {
  if (!appConfig.hasMqtt() || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  if (appConfig.data().mqttSecure) {
#if defined(ESP8266)
    secureMqttClient.setInsecure();
#else
    secureMqttClient.setInsecure();
#endif
    mqttClient.setClient(secureMqttClient);
  } else {
    mqttClient.setClient(espMqttClient);
  }
  mqttClient.setServer(appConfig.data().mqttHost, appConfig.data().mqttPort);
  mqttClient.setBufferSize(768);
  String clientId = appConfig.deviceName() + "-" + chipIdHex();
  String onlineTopic = topic("online");

  bool connected;
  if (strlen(appConfig.data().mqttUsername) > 0) {
    connected = mqttClient.connect(
      clientId.c_str(),
      appConfig.data().mqttUsername,
      appConfig.data().mqttPassword,
      onlineTopic.c_str(),
      0,
      true,
      "false"
    );
  } else {
    connected = mqttClient.connect(
      clientId.c_str(),
      onlineTopic.c_str(),
      0,
      true,
      "false"
    );
  }

  if (connected) {
    mqttClient.publish(onlineTopic.c_str(), "true", appConfig.data().mqttRetain);
    mqttClient.publish(topic("ip").c_str(), WiFi.localIP().toString().c_str(), appConfig.data().mqttRetain);
    Debug.println("MQTT connected");
    haDiscoveryPublished = false;
  }
  return connected;
}

static void publishHaSensor(const char* key, const char* name, const char* unit, const char* deviceClass, const char* stateClass, const char* valueTemplate) {
  String node = haNodeId();
  String discoveryTopic = discoveryPrefix() + "/sensor/" + node + "/" + key + "/config";
  String payload;
  payload.reserve(620);
  payload += "{\"name\":\"";
  payload += name;
  payload += "\",\"uniq_id\":\"";
  payload += node;
  payload += "_";
  payload += key;
  payload += "\",\"stat_t\":\"";
  payload += topic("state");
  payload += "\",\"avty_t\":\"";
  payload += topic("online");
  payload += "\",\"pl_avail\":\"true\",\"pl_not_avail\":\"false\",\"val_tpl\":\"";
  payload += valueTemplate;
  payload += "\"";
  if (strlen(unit) > 0) {
    payload += ",\"unit_of_meas\":\"";
    payload += unit;
    payload += "\"";
  }
  if (strlen(deviceClass) > 0) {
    payload += ",\"dev_cla\":\"";
    payload += deviceClass;
    payload += "\"";
  }
  if (strlen(stateClass) > 0) {
    payload += ",\"stat_cla\":\"";
    payload += stateClass;
    payload += "\"";
  }
  payload += ",\"dev\":{\"ids\":[\"";
  payload += node;
  payload += "\"],\"name\":\"Multical 21 Reader\",\"mf\":\"Kamstrup\",\"mdl\":\"Multical 21\"}}";
  mqttClient.publish(discoveryTopic.c_str(), payload.c_str(), true);
}

static void publishHaBinarySensor(const char* key, const char* name, const char* valueTemplate) {
  String node = haNodeId();
  String discoveryTopic = discoveryPrefix() + "/binary_sensor/" + node + "/" + key + "/config";
  String payload;
  payload.reserve(560);
  payload += "{\"name\":\"";
  payload += name;
  payload += "\",\"uniq_id\":\"";
  payload += node;
  payload += "_";
  payload += key;
  payload += "\",\"stat_t\":\"";
  payload += topic("state");
  payload += "\",\"avty_t\":\"";
  payload += topic("online");
  payload += "\",\"pl_avail\":\"true\",\"pl_not_avail\":\"false\",\"val_tpl\":\"";
  payload += valueTemplate;
  payload += "\",\"pl_on\":\"true\",\"pl_off\":\"false\",\"dev_cla\":\"problem\",\"dev\":{\"ids\":[\"";
  payload += node;
  payload += "\"],\"name\":\"Multical 21 Reader\",\"mf\":\"Kamstrup\",\"mdl\":\"Multical 21\"}}";
  mqttClient.publish(discoveryTopic.c_str(), payload.c_str(), true);
}

static void publishHomeAssistantDiscovery() {
  if (!mqttClient.connected() || !appConfig.data().homeAssistantDiscovery || haDiscoveryPublished) {
    return;
  }

  publishHaSensor("total", "Water total", "m3", "water", "total_increasing", "{{ value_json.total_m3 }}");
  publishHaSensor("today", "Water today", "m3", "water", "measurement", "{{ value_json.today_m3 }}");
  publishHaSensor("last_24h", "Water last 24h", "m3", "water", "measurement", "{{ value_json.last_24h_m3 }}");
  publishHaSensor("month", "Water month", "m3", "water", "measurement", "{{ value_json.month_usage_m3 }}");
  publishHaSensor("water_temp", "Water temperature", "C", "temperature", "measurement", "{{ value_json.water_temperature_c }}");
  publishHaSensor("ambient_temp", "Ambient temperature", "C", "temperature", "measurement", "{{ value_json.ambient_temperature_c }}");
  publishHaSensor("last_frame_age", "Water last frame age", "s", "duration", "measurement", "{{ value_json.last_frame_age_s }}");
  publishHaBinarySensor("alarm_burst", "Water alarm burst", "{{ 'true' if value_json.alarms.burst else 'false' }}");
  publishHaBinarySensor("alarm_leak", "Water alarm leak", "{{ 'true' if value_json.alarms.leak else 'false' }}");
  publishHaBinarySensor("alarm_dry", "Water alarm dry", "{{ 'true' if value_json.alarms.dry else 'false' }}");
  publishHaBinarySensor("alarm_reverse", "Water alarm reverse", "{{ 'true' if value_json.alarms.reverse else 'false' }}");
  haDiscoveryPublished = true;
  Debug.println("Home Assistant discovery published");
}

static void publishWaterData() {
  if (!mqttClient.connected() || !waterData.valid) {
    return;
  }

  String payload;
  payload.reserve(360);
  payload += "{\"total_m3\":";
  payload += String(waterData.totalM3(), 3);
  payload += ",\"month_start_m3\":";
  payload += String(waterData.monthStartM3(), 3);
  payload += ",\"month_usage_m3\":";
  payload += String(waterHistory.getMonthMilliM3(0) / 1000.0f, 3);
  payload += ",\"water_temperature_c\":";
  payload += waterData.waterTemperatureC;
  payload += ",\"ambient_temperature_c\":";
  payload += waterData.ambientTemperatureC;
  payload += ",\"alarms\":{\"burst\":";
  payload += waterData.alarms.burst ? "true" : "false";
  payload += ",\"leak\":";
  payload += waterData.alarms.leak ? "true" : "false";
  payload += ",\"dry\":";
  payload += waterData.alarms.dry ? "true" : "false";
  payload += ",\"reverse\":";
  payload += waterData.alarms.reverse ? "true" : "false";
  payload += "},\"last_frame_age_s\":";
  payload += String((millis() - waterData.lastFrameMillis) / 1000);
  payload += ",\"today_m3\":";
  payload += String(waterHistory.getTodayMilliM3() / 1000.0f, 3);
  payload += ",\"last_24h_m3\":";
  payload += String(waterHistory.getLast24HoursMilliM3() / 1000.0f, 3);
  payload += "}";

  mqttClient.publish(topic("state").c_str(), payload.c_str(), appConfig.data().mqttRetain);
}

static void startRadioIfConfigured() {
  if (radioStarted) {
    return;
  }

  if (lastRadioStartAttempt != 0 && millis() - lastRadioStartAttempt < 30000) {
    return;
  }
  lastRadioStartAttempt = millis();

  radioStarted = waterMeter.begin(waterData);
  Debug.println(radioStarted ? "CC1101 receiver started" : "CC1101 receiver not started");
}

static bool forceSetupRequested() {
  pinMode(FORCE_SETUP_PIN, INPUT_PULLUP);
  const unsigned long started = millis();
  bool requested = false;

  while (millis() - started < 2500) {
    if (digitalRead(FORCE_SETUP_PIN) == LOW) {
      requested = true;
      break;
    }
    delay(25);
  }

  if (requested) {
    Debug.println("Setup AP forced by BOOT/FLASH button");
  }
  return requested;
}

static void syncTelnetDebug() {
  const bool enabled = appConfig.data().telnetDebugEnabled;
  if (enabled == telnetDebugConfigured) {
    return;
  }
  telnetDebugConfigured = enabled;
  Debug.begin(enabled);
  Debug.println(enabled ? "Telnet debug enabled from setup" : "Telnet debug disabled from setup");
}

static void printDebugStatus(const char* reason) {
  Debug.print("Status ");
  Debug.print(reason);
  Debug.print(": mode ");
  Debug.print(setupApMode ? "setup-ap" : "wifi");
  Debug.print(", ip ");
  Debug.print(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
  Debug.print(", ntp ");
  Debug.print(isNtpSynced() ? "synced" : (appConfig.data().ntpEnabled ? "waiting" : "off"));
  Debug.print(", radio ");
  Debug.print(waterData.radioPresent ? (waterData.radioStarted ? "running" : "detected-not-running") : "not-detected");
  Debug.print(", meter ");
  Debug.print(appConfig.hasMeter() ? "configured" : "not-configured");
  Debug.print(", last frame ");
  Debug.println(waterData.valid ? String((millis() - waterData.lastFrameMillis) / 1000) + String(" s") : String("none"));
}

void setup() {
  pinMode(PIN_LED_BUILTIN, OUTPUT);
  digitalWrite(PIN_LED_BUILTIN, HIGH);

  Serial.begin(115200);
#if defined(BOARD_LOLIN_S2_MINI)
  unsigned long serialWaitStarted = millis();
  while (!Serial && millis() - serialWaitStarted < 2500) {
    delay(10);
  }
#endif
  delay(100);
  Debug.println();
  Debug.println("Multical 21 Reader booting");
  Debug.print("Firmware ");
  Debug.print(firmwareVersion());
  Debug.print(" ");
  Debug.print(firmwareBoardName());
  Debug.print(" ");
  Debug.println(firmwareGitSha());

  appConfig.begin();
  waterHistory.begin();

  const bool forceSetup = forceSetupRequested();

  if (forceSetup || !connectWifi()) {
    startSetupAp();
  } else {
    setupNtp();
    setupOTA();
    if (MDNS.begin(appConfig.deviceName().c_str())) {
      MDNS.addService("http", "tcp", 80);
      Debug.print("mDNS started: http://");
      Debug.print(appConfig.deviceName());
      Debug.println(".local");
    } else {
      Debug.println("mDNS start failed");
    }
  }

  webServer.begin();
  telnetDebugConfigured = appConfig.data().telnetDebugEnabled;
  Debug.begin(telnetDebugConfigured);
  if (!setupApMode) {
    startRadioIfConfigured();
  }
  Debug.println("Setup done");
}

void loop() {
  if (setupApMode) {
    dnsServer.processNextRequest();
  } else {
    ArduinoOTA.handle();
#if defined(ESP8266)
    MDNS.update();
#endif
  }

  webServer.handleClient();
  syncTelnetDebug();
  Debug.loop();
  static bool debugClientWasConnected = false;
  const bool debugClientConnected = Debug.hasClient();
  if (debugClientConnected && !debugClientWasConnected) {
    printDebugStatus("telnet-connected");
  }
  debugClientWasConnected = debugClientConnected;

  if (Debug.isEnabled() && millis() - lastDebugHeartbeat > 30000) {
    lastDebugHeartbeat = millis();
    printDebugStatus("heartbeat");
  }

  loopNtp();
  waterHistory.loop();
  if (!setupApMode) {
    startRadioIfConfigured();
  }

  if (radioStarted && appConfig.hasMeter()) {
    if (waterMeter.readFrame(waterData, appConfig.data())) {
      waterHistory.update(waterData, localNow());
      publishWaterData();
      lastMqttPublish = millis();
    }
    if (!waterData.radioStarted) {
      radioStarted = false;
    }
  } else if (radioStarted && !waterData.radioStarted) {
    radioStarted = false;
  }

  if (!setupApMode && appConfig.hasMqtt()) {
    if (!mqttClient.connected() && millis() - lastMqttAttempt > 5000) {
      lastMqttAttempt = millis();
      mqttConnect();
    }
    mqttClient.loop();
    publishHomeAssistantDiscovery();

    if (waterData.valid && millis() - lastMqttPublish > 60000) {
      publishWaterData();
      lastMqttPublish = millis();
    }
  }
}
