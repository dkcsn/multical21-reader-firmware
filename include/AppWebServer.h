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
  bool firmwareUploadSuccess;
  String firmwareUploadMessage;

  void handleRoot();
  void handleSetupPage();
  void handleGraphsPage();
  void handleFirmwarePage();
  void handleFirmwarePost();
  void handleFirmwareUpload();
  void handleConfigJson();
  void handleDataJson();
  void handleDayPlotJson();
  void handleMonthPlotJson();
  void handleVersionJson();
  void handleWifiScanJson();
  void handleWifiTestJson();
  void handleCaptiveRedirect();
  void handleSave();
  void handleReboot();
  void handleResetConfig();
  void sendHtml(const String& body);
};

#endif
