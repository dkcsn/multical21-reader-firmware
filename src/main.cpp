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
#elif defined(ESP32)
  #include <WiFi.h>
  #include <ESPmDNS.h>
#endif

#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <PubSubClient.h>

#include "AppConfig.h"
#include "AppWebServer.h"
#include "WaterData.h"
#include "WaterMeter.h"
#include "hwconfig.h"

#define ESP_NAME "Multical21Reader"
#define SETUP_AP_NAME "Multical21-Setup"

#if defined(ESP32)
  #define LED_BUILTIN 4
#endif

AppConfig appConfig;
WaterData waterData;
WaterMeter waterMeter;
AppWebServer webServer(appConfig, waterData);

WiFiClient espMqttClient;
PubSubClient mqttClient(espMqttClient);
DNSServer dnsServer;

bool setupApMode = false;
bool radioStarted = false;
unsigned long lastMqttAttempt = 0;
unsigned long lastMqttPublish = 0;

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

static bool connectWifi() {
  if (!appConfig.hasWifi()) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(appConfig.data().wifiSsid, appConfig.data().wifiPassword);
  Serial.print("Connecting to WiFi ");
  Serial.println(appConfig.data().wifiSsid);

  for (uint8_t i = 0; i < 40; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      return true;
    }
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  return false;
}

static void startSetupAp() {
  setupApMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(SETUP_AP_NAME);
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.print("Setup AP started: ");
  Serial.print(SETUP_AP_NAME);
  Serial.print(" at ");
  Serial.println(WiFi.softAPIP());
}

static void setupOTA() {
  ArduinoOTA.setHostname(ESP_NAME);
  ArduinoOTA.onStart([]() {
    Serial.println("OTA update started");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA update finished");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA error[%u]\n", error);
  });
  ArduinoOTA.begin();
}

static bool mqttConnect() {
  if (!appConfig.hasMqtt() || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  mqttClient.setServer(appConfig.data().mqttHost, appConfig.data().mqttPort);
  String clientId = String(ESP_NAME) + "-" + chipIdHex();
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
    mqttClient.publish(onlineTopic.c_str(), "true", true);
    mqttClient.publish(topic("ip").c_str(), WiFi.localIP().toString().c_str(), true);
    Serial.println("MQTT connected");
  }
  return connected;
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
  payload += String(waterData.monthUsageM3(), 3);
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
  payload += "}";

  mqttClient.publish(topic("state").c_str(), payload.c_str(), true);
}

static void startRadioIfConfigured() {
  if (radioStarted || !appConfig.hasMeter()) {
    return;
  }

  waterMeter.begin();
  radioStarted = true;
  Serial.println("CC1101 receiver started");
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("Multical 21 Reader booting");

  appConfig.begin();

  if (!connectWifi()) {
    startSetupAp();
  } else {
    setupOTA();
    MDNS.begin(ESP_NAME);
  }

  webServer.begin();
  startRadioIfConfigured();
  Serial.println("Setup done");
}

void loop() {
  if (setupApMode) {
    dnsServer.processNextRequest();
  } else {
    ArduinoOTA.handle();
  }

  webServer.handleClient();
  startRadioIfConfigured();

  if (radioStarted && waterMeter.readFrame(waterData, appConfig.data())) {
    publishWaterData();
    lastMqttPublish = millis();
  }

  if (!setupApMode && appConfig.hasMqtt()) {
    if (!mqttClient.connected() && millis() - lastMqttAttempt > 5000) {
      lastMqttAttempt = millis();
      mqttConnect();
    }
    mqttClient.loop();

    if (waterData.valid && millis() - lastMqttPublish > 60000) {
      publishWaterData();
      lastMqttPublish = millis();
    }
  }
}
