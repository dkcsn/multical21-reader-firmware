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
  bool historyImportSuccess;
  String historyImportMessage;
  String historyImportLine;
  uint16_t historyImportRows;
  uint16_t historyImportImported;
  uint16_t historyImportRejected;
  uint32_t historyImportTotalMilliM3;

  void handleRoot();
  void handleSetupPage();
  void handleGraphsPage();
  void handleHardwarePage();
  void handleFirmwarePage();
  void handleFirmwarePost();
  void handleFirmwareUpload();
  void handleHistoryImportPage();
  void handleHistoryImportPost();
  void handleHistoryImportUpload();
  void handleConfigJson();
  void handleDataJson();
  void handleDayPlotJson();
  void handleMonthPlotJson();
  void handleVersionJson();
  void handleDiagnosticsJson();
  void handleWifiScanJson();
  void handleWifiTestJson();
  void handleCaptiveRedirect();
  void handleSave();
  void handleReboot();
  void handleResetConfig();
  void handleFactoryReset();
  void processHistoryImportLine(const String& line);
  void sendHtml(const String& body);
};

#endif
