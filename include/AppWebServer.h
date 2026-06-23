#ifndef _APPWEBSERVER_H_
#define _APPWEBSERVER_H_

#include <Arduino.h>
#if defined(ESP8266)
  #include <ESP8266WebServer.h>
  using DeviceWebServer = ESP8266WebServer;
#elif defined(ESP32)
  #include <WebServer.h>
  using DeviceWebServer = WebServer;
#endif

#include "AppConfig.h"
#include "WaterData.h"
#include "WaterHistory.h"

class AppWebServer {
public:
  AppWebServer(AppConfig& config, WaterData& waterData, WaterHistory& history);
  void begin();
  void handleClient();

private:
  AppConfig& config;
  WaterData& waterData;
  WaterHistory& history;
  DeviceWebServer server;

  void handleRoot();
  void handleConfigJson();
  void handleDataJson();
  void handleDayPlotJson();
  void handleMonthPlotJson();
  void handleSave();
  void handleReboot();
  void sendHtml(const String& body);
};

#endif
