#include "AppWebServer.h"

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ESP32)
  #include <WiFi.h>
#endif

static String htmlEscape(const String& value) {
  String out = value;
  out.replace("&", "&amp;");
  out.replace("\"", "&quot;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  return out;
}

static void copyArg(char* dest, size_t len, const String& value) {
  String trimmed = value;
  trimmed.trim();
  strncpy(dest, trimmed.c_str(), len - 1);
  dest[len - 1] = '\0';
}

AppWebServer::AppWebServer(AppConfig& config, WaterData& waterData)
  : config(config), waterData(waterData), server(80) {
}

void AppWebServer::begin() {
  server.on("/", HTTP_GET, std::bind(&AppWebServer::handleRoot, this));
  server.on("/configuration.json", HTTP_GET, std::bind(&AppWebServer::handleConfigJson, this));
  server.on("/data.json", HTTP_GET, std::bind(&AppWebServer::handleDataJson, this));
  server.on("/save", HTTP_POST, std::bind(&AppWebServer::handleSave, this));
  server.on("/reboot", HTTP_POST, std::bind(&AppWebServer::handleReboot, this));
  server.onNotFound(std::bind(&AppWebServer::handleRoot, this));
  server.begin();
}

void AppWebServer::handleClient() {
  server.handleClient();
}

void AppWebServer::handleRoot() {
  const AppConfigData& cfg = config.data();
  String body;
  body.reserve(7000);
  body += F("<section><h2>Status</h2><dl>");
  body += F("<dt>WiFi</dt><dd>");
  body += WiFi.status() == WL_CONNECTED ? htmlEscape(WiFi.localIP().toString()) : F("Setup AP");
  body += F("</dd><dt>Meter configured</dt><dd>");
  body += config.hasMeter() ? F("Yes") : F("No");
  body += F("</dd><dt>Total</dt><dd>");
  body += waterData.valid ? String(waterData.totalM3(), 3) + String(" m3") : String("No frame yet");
  body += F("</dd><dt>Month usage</dt><dd>");
  body += waterData.valid ? String(waterData.monthUsageM3(), 3) + String(" m3") : String("-");
  body += F("</dd><dt>Water temp</dt><dd>");
  body += waterData.valid ? String(waterData.waterTemperatureC) + String(" C") : String("-");
  body += F("</dd><dt>Room temp</dt><dd>");
  body += waterData.valid ? String(waterData.ambientTemperatureC) + String(" C") : String("-");
  body += F("</dd></dl></section>");

  body += F("<section><h2>Setup</h2><form method=\"post\" action=\"/save\">");
  body += F("<label>WiFi SSID<input name=\"wifiSsid\" value=\"");
  body += htmlEscape(cfg.wifiSsid);
  body += F("\"></label>");
  body += F("<label>WiFi password<input name=\"wifiPassword\" type=\"password\" placeholder=\"");
  body += htmlEscape(config.maskedWifiPassword());
  body += F("\"></label>");
  body += F("<label>MQTT host<input name=\"mqttHost\" value=\"");
  body += htmlEscape(cfg.mqttHost);
  body += F("\"></label>");
  body += F("<label>MQTT port<input name=\"mqttPort\" inputmode=\"numeric\" value=\"");
  body += String(cfg.mqttPort);
  body += F("\"></label>");
  body += F("<label>MQTT username<input name=\"mqttUsername\" value=\"");
  body += htmlEscape(cfg.mqttUsername);
  body += F("\"></label>");
  body += F("<label>MQTT password<input name=\"mqttPassword\" type=\"password\" placeholder=\"");
  body += htmlEscape(config.maskedMqttPassword());
  body += F("\"></label>");
  body += F("<label>MQTT base topic<input name=\"mqttBaseTopic\" value=\"");
  body += htmlEscape(cfg.mqttBaseTopic);
  body += F("\"></label>");
  body += F("<label>Meter serial hex<input name=\"meterSerial\" value=\"");
  body += htmlEscape(config.meterSerialHex());
  body += F("\" maxlength=\"8\"></label>");
  body += F("<label>AES key hex<input name=\"encryptionKey\" type=\"password\" placeholder=\"");
  body += config.hasMeter() ? F("***") : F("32 hex chars");
  body += F("\" maxlength=\"32\"></label>");
  body += F("<button type=\"submit\">Save</button></form>");
  body += F("<form method=\"post\" action=\"/reboot\"><button type=\"submit\">Reboot</button></form></section>");
  sendHtml(body);
}

void AppWebServer::handleConfigJson() {
  const AppConfigData& cfg = config.data();
  String json;
  json.reserve(700);
  json += F("{\"wifiSsid\":\"");
  json += cfg.wifiSsid;
  json += F("\",\"wifiPassword\":\"");
  json += config.maskedWifiPassword();
  json += F("\",\"mqttHost\":\"");
  json += cfg.mqttHost;
  json += F("\",\"mqttPort\":");
  json += cfg.mqttPort;
  json += F(",\"mqttUsername\":\"");
  json += cfg.mqttUsername;
  json += F("\",\"mqttPassword\":\"");
  json += config.maskedMqttPassword();
  json += F("\",\"mqttBaseTopic\":\"");
  json += cfg.mqttBaseTopic;
  json += F("\",\"meterSerial\":\"");
  json += config.meterSerialHex();
  json += F("\",\"hasEncryptionKey\":");
  json += config.hasMeter() ? F("true") : F("false");
  json += F("}");
  server.send(200, "application/json", json);
}

void AppWebServer::handleDataJson() {
  String json;
  json.reserve(500);
  json += F("{\"valid\":");
  json += waterData.valid ? F("true") : F("false");
  json += F(",\"total_m3\":");
  json += String(waterData.totalM3(), 3);
  json += F(",\"month_start_m3\":");
  json += String(waterData.monthStartM3(), 3);
  json += F(",\"month_usage_m3\":");
  json += String(waterData.monthUsageM3(), 3);
  json += F(",\"water_temperature_c\":");
  json += waterData.waterTemperatureC;
  json += F(",\"ambient_temperature_c\":");
  json += waterData.ambientTemperatureC;
  json += F(",\"alarms\":{\"burst\":");
  json += waterData.alarms.burst ? F("true") : F("false");
  json += F(",\"leak\":");
  json += waterData.alarms.leak ? F("true") : F("false");
  json += F(",\"dry\":");
  json += waterData.alarms.dry ? F("true") : F("false");
  json += F(",\"reverse\":");
  json += waterData.alarms.reverse ? F("true") : F("false");
  json += F("},\"last_frame_age_s\":");
  json += waterData.valid ? String((millis() - waterData.lastFrameMillis) / 1000) : F("null");
  json += F("}");
  server.send(200, "application/json", json);
}

void AppWebServer::handleSave() {
  AppConfigData& cfg = config.data();

  copyArg(cfg.wifiSsid, sizeof(cfg.wifiSsid), server.arg("wifiSsid"));
  String wifiPassword = server.arg("wifiPassword");
  if (wifiPassword.length() > 0 && wifiPassword != "***") {
    copyArg(cfg.wifiPassword, sizeof(cfg.wifiPassword), wifiPassword);
  }

  copyArg(cfg.mqttHost, sizeof(cfg.mqttHost), server.arg("mqttHost"));
  cfg.mqttPort = (uint16_t) server.arg("mqttPort").toInt();
  if (cfg.mqttPort == 0) {
    cfg.mqttPort = 1883;
  }
  copyArg(cfg.mqttUsername, sizeof(cfg.mqttUsername), server.arg("mqttUsername"));
  String mqttPassword = server.arg("mqttPassword");
  if (mqttPassword.length() > 0 && mqttPassword != "***") {
    copyArg(cfg.mqttPassword, sizeof(cfg.mqttPassword), mqttPassword);
  }
  copyArg(cfg.mqttBaseTopic, sizeof(cfg.mqttBaseTopic), server.arg("mqttBaseTopic"));
  if (strlen(cfg.mqttBaseTopic) == 0) {
    strncpy(cfg.mqttBaseTopic, "watermeter", sizeof(cfg.mqttBaseTopic) - 1);
    cfg.mqttBaseTopic[sizeof(cfg.mqttBaseTopic) - 1] = '\0';
  }

  String meterSerial = server.arg("meterSerial");
  if (meterSerial.length() > 0 && !config.setMeterSerialHex(meterSerial)) {
    server.send(400, "text/plain", "Meter serial must be 8 hex characters");
    return;
  }

  String encryptionKey = server.arg("encryptionKey");
  if (encryptionKey.length() > 0 && encryptionKey != "***" && !config.setEncryptionKeyHex(encryptionKey)) {
    server.send(400, "text/plain", "AES key must be 32 hex characters");
    return;
  }

  config.save();
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "Saved");
}

void AppWebServer::handleReboot() {
  server.send(200, "text/plain", "Rebooting");
  delay(250);
  ESP.restart();
}

void AppWebServer::sendHtml(const String& body) {
  String html;
  html.reserve(body.length() + 1800);
  html += F("<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  html += F("<title>Multical 21 Reader</title><style>");
  html += F("body{margin:0;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;background:#f6f7f9;color:#111827}");
  html += F("header{background:#102a43;color:white;padding:18px 20px}main{max-width:880px;margin:0 auto;padding:18px}");
  html += F("section{background:white;border:1px solid #d9e2ec;border-radius:8px;padding:16px;margin:0 0 16px}");
  html += F("h1{font-size:24px;margin:0}h2{font-size:18px;margin:0 0 12px}dl{display:grid;grid-template-columns:160px 1fr;gap:8px;margin:0}");
  html += F("dt{color:#52606d}dd{margin:0;font-weight:600}form{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px}");
  html += F("label{display:grid;gap:5px;font-size:13px;color:#334e68}input{font:inherit;padding:10px;border:1px solid #bcccdc;border-radius:6px}");
  html += F("button{font:inherit;padding:10px 14px;border:0;border-radius:6px;background:#0b7285;color:white;font-weight:700;cursor:pointer;align-self:end}");
  html += F("</style></head><body><header><h1>Multical 21 Reader</h1></header><main>");
  html += body;
  html += F("</main></body></html>");
  server.send(200, "text/html", html);
}
