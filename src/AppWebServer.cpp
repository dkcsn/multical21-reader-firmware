#include "AppWebServer.h"

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <Updater.h>
#elif defined(ESP32)
  #include <WiFi.h>
  #include <Update.h>
#endif

static String htmlEscape(const String& value) {
  String out = value;
  out.replace("&", "&amp;");
  out.replace("\"", "&quot;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  return out;
}

static String jsonEscape(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (uint16_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if ((uint8_t) c >= 0x20) {
      out += c;
    }
  }
  return out;
}

static void copyArg(char* dest, size_t len, const String& value) {
  String trimmed = value;
  trimmed.trim();
  strncpy(dest, trimmed.c_str(), len - 1);
  dest[len - 1] = '\0';
}

static String formatM3(uint32_t milliM3) {
  return String(milliM3 / 1000.0f, 3);
}

static String graphBars(WaterHistory& history, bool days) {
  const uint8_t count = days ? 31 : 24;
  uint32_t maxValue = 0;
  for (uint8_t i = 0; i < count; i++) {
    uint32_t value = days ? history.getDayMilliM3(i) : history.getHourMilliM3(i);
    if (value > maxValue) {
      maxValue = value;
    }
  }

  String out;
  out.reserve(days ? 2600 : 2200);
  out += F("<div class=\"bars\">");
  for (int8_t i = count - 1; i >= 0; i--) {
    uint32_t value = days ? history.getDayMilliM3(i) : history.getHourMilliM3(i);
    uint8_t height = maxValue == 0 ? 0 : (uint8_t) max(6UL, (unsigned long) value * 100UL / maxValue);
    out += F("<div class=\"barwrap\"><div class=\"bar\" style=\"height:");
    out += height;
    out += F("%\"></div><span>");
    out += i == 0 ? F("now") : String(i);
    out += F("</span><small>");
    out += formatM3(value);
    out += F("</small></div>");
  }
  out += F("</div>");
  return out;
}

static bool isOpenNetwork(uint8_t index) {
#if defined(ESP8266)
  return WiFi.encryptionType(index) == ENC_TYPE_NONE;
#else
  return WiFi.encryptionType(index) == WIFI_AUTH_OPEN;
#endif
}

AppWebServer::AppWebServer(AppConfig& config, WaterData& waterData, WaterHistory& history)
  : config(config), waterData(waterData), history(history), server(80),
    firmwareUploadSuccess(false), firmwareUploadMessage() {
}

void AppWebServer::begin() {
  server.on("/", HTTP_GET, std::bind(&AppWebServer::handleRoot, this));
  server.on("/firmware", HTTP_GET, std::bind(&AppWebServer::handleFirmwarePage, this));
  server.on("/firmware", HTTP_POST, std::bind(&AppWebServer::handleFirmwarePost, this), std::bind(&AppWebServer::handleFirmwareUpload, this));
  server.on("/configuration.json", HTTP_GET, std::bind(&AppWebServer::handleConfigJson, this));
  server.on("/data.json", HTTP_GET, std::bind(&AppWebServer::handleDataJson, this));
  server.on("/dayplot.json", HTTP_GET, std::bind(&AppWebServer::handleDayPlotJson, this));
  server.on("/monthplot.json", HTTP_GET, std::bind(&AppWebServer::handleMonthPlotJson, this));
  server.on("/wifiscan.json", HTTP_GET, std::bind(&AppWebServer::handleWifiScanJson, this));
  server.on("/wifitest.json", HTTP_POST, std::bind(&AppWebServer::handleWifiTestJson, this));
  server.on("/generate_204", HTTP_GET, std::bind(&AppWebServer::handleCaptiveRedirect, this));
  server.on("/hotspot-detect.html", HTTP_GET, std::bind(&AppWebServer::handleCaptiveRedirect, this));
  server.on("/library/test/success.html", HTTP_GET, std::bind(&AppWebServer::handleCaptiveRedirect, this));
  server.on("/connecttest.txt", HTTP_GET, std::bind(&AppWebServer::handleCaptiveRedirect, this));
  server.on("/ncsi.txt", HTTP_GET, std::bind(&AppWebServer::handleCaptiveRedirect, this));
  server.on("/fwlink", HTTP_GET, std::bind(&AppWebServer::handleCaptiveRedirect, this));
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
  body += F("</dd><dt>Today</dt><dd>");
  body += formatM3(history.getTodayMilliM3()) + String(" m3");
  body += F("</dd><dt>Last 24 hours</dt><dd>");
  body += formatM3(history.getLast24HoursMilliM3()) + String(" m3");
  body += F("</dd><dt>Month usage</dt><dd>");
  body += waterData.valid ? String(waterData.monthUsageM3(), 3) + String(" m3") : String("-");
  body += F("</dd><dt>Water temp</dt><dd>");
  body += waterData.valid ? String(waterData.waterTemperatureC) + String(" C") : String("-");
  body += F("</dd><dt>Room temp</dt><dd>");
  body += waterData.valid ? String(waterData.ambientTemperatureC) + String(" C") : String("-");
  body += F("</dd><dt>History</dt><dd>");
  body += history.wasLoaded() ? F("Loaded from flash") : F("New session");
  body += F("</dd><dt>Time sync</dt><dd>");
  body += history.isTimeSynced() ? F("NTP synced") : F("Waiting for NTP");
  body += F("</dd><dt>Telnet debug</dt><dd>");
  body += cfg.telnetDebugEnabled ? F("Enabled on port 23") : F("Disabled");
  body += F("</dd></dl></section>");

  body += F("<section><h2>Hourly water use</h2>");
  body += graphBars(history, false);
  body += F("</section><section><h2>Daily water use</h2>");
  body += graphBars(history, true);
  body += F("</section>");

  body += F("<section><h2>Fibaro / local API</h2><dl>");
  body += F("<dt>State JSON</dt><dd><code>http://");
  body += WiFi.status() == WL_CONNECTED ? htmlEscape(WiFi.localIP().toString()) : String("192.168.4.1");
  body += F("/data.json</code></dd><dt>Hourly plot</dt><dd><code>/dayplot.json</code></dd><dt>Daily plot</dt><dd><code>/monthplot.json</code></dd>");
  body += F("<dt>Sync rule</dt><dd>Use total_m3 as source of truth and treat data as stale when last_frame_age_s is high.</dd></dl></section>");

  body += F("<section><h2>Firmware</h2><dl><dt>Browser update</dt><dd><a class=\"buttonLink\" href=\"/firmware\">Upload firmware</a></dd></dl></section>");

  body += F("<section><h2>Setup</h2><form method=\"post\" action=\"/save\">");
  body += F("<label>WiFi SSID<input id=\"wifiSsid\" name=\"wifiSsid\" value=\"");
  body += htmlEscape(cfg.wifiSsid);
  body += F("\"></label>");
  body += F("<label>WiFi password<input id=\"wifiPassword\" name=\"wifiPassword\" type=\"password\" placeholder=\"");
  body += htmlEscape(config.maskedWifiPassword());
  body += F("\"></label>");
  body += F("<div class=\"wifiActions\"><button type=\"button\" onclick=\"scanWifi()\">Scan WiFi</button><button type=\"button\" onclick=\"testWifi()\">Test WiFi</button><span id=\"wifiResult\"></span></div><div id=\"wifiList\" class=\"wifiList\"></div>");
  body += F("<label>NTP enabled<select name=\"ntpEnabled\"><option value=\"1\"");
  body += cfg.ntpEnabled ? F(" selected") : F("");
  body += F(">Enabled</option><option value=\"0\"");
  body += !cfg.ntpEnabled ? F(" selected") : F("");
  body += F(">Disabled</option></select></label>");
  body += F("<label>NTP server<input name=\"ntpServer\" value=\"");
  body += htmlEscape(cfg.ntpServer);
  body += F("\"></label>");
  body += F("<label>Timezone offset minutes<input name=\"timezoneOffsetMinutes\" inputmode=\"numeric\" value=\"");
  body += String(cfg.timezoneOffsetMinutes);
  body += F("\"></label>");
  body += F("<label>MQTT enabled<select name=\"mqttEnabled\"><option value=\"1\"");
  body += cfg.mqttEnabled ? F(" selected") : F("");
  body += F(">Enabled</option><option value=\"0\"");
  body += !cfg.mqttEnabled ? F(" selected") : F("");
  body += F(">Disabled</option></select></label>");
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
  body += F("<label>MQTT retain<select name=\"mqttRetain\"><option value=\"1\"");
  body += cfg.mqttRetain ? F(" selected") : F("");
  body += F(">Enabled</option><option value=\"0\"");
  body += !cfg.mqttRetain ? F(" selected") : F("");
  body += F(">Disabled</option></select></label>");
  body += F("<label>Secure MQTT TLS<select name=\"mqttSecure\"><option value=\"1\"");
  body += cfg.mqttSecure ? F(" selected") : F("");
  body += F(">Enabled</option><option value=\"0\"");
  body += !cfg.mqttSecure ? F(" selected") : F("");
  body += F(">Disabled</option></select></label>");
  body += F("<label>Home Assistant discovery<select name=\"homeAssistantDiscovery\"><option value=\"1\"");
  body += cfg.homeAssistantDiscovery ? F(" selected") : F("");
  body += F(">Enabled</option><option value=\"0\"");
  body += !cfg.homeAssistantDiscovery ? F(" selected") : F("");
  body += F(">Disabled</option></select></label>");
  body += F("<label>HA discovery prefix<input name=\"homeAssistantPrefix\" value=\"");
  body += htmlEscape(cfg.homeAssistantPrefix);
  body += F("\"></label>");
  body += F("<label>Telnet debug<select name=\"telnetDebugEnabled\"><option value=\"1\"");
  body += cfg.telnetDebugEnabled ? F(" selected") : F("");
  body += F(">Enabled</option><option value=\"0\"");
  body += !cfg.telnetDebugEnabled ? F(" selected") : F("");
  body += F(">Disabled</option></select></label>");
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

void AppWebServer::handleFirmwarePage() {
  String body;
  body.reserve(1300);
  body += F("<section><h2>Firmware update</h2><dl>");
  body += F("<dt>Board</dt><dd>");
#if defined(ESP8266)
  body += F("ESP8266");
#else
  body += F("ESP32");
#endif
  body += F("</dd><dt>Free sketch space</dt><dd>");
  body += String(ESP.getFreeSketchSpace());
  body += F(" bytes</dd></dl>");
  body += F("<form class=\"uploadForm\" method=\"post\" action=\"/firmware\" enctype=\"multipart/form-data\">");
  body += F("<label>Firmware .bin<input name=\"firmware\" type=\"file\" accept=\".bin\" required></label>");
  body += F("<button type=\"submit\">Upload and restart</button></form>");
  body += F("<p class=\"hint\">Use the PlatformIO firmware binary from the matching board build.</p>");
  body += F("</section>");
  sendHtml(body);
}

void AppWebServer::handleFirmwarePost() {
  String body;
  body.reserve(900);
  body += F("<section><h2>Firmware update</h2><dl><dt>Status</dt><dd>");
  body += firmwareUploadSuccess ? F("Update complete") : F("Update failed");
  body += F("</dd><dt>Message</dt><dd>");
  body += htmlEscape(firmwareUploadMessage);
  body += F("</dd></dl>");
  if (firmwareUploadSuccess) {
    body += F("<p class=\"hint\">Device is restarting now. Reconnect to the same IP or setup portal when it comes back.</p>");
  } else {
    body += F("<p><a class=\"buttonLink\" href=\"/firmware\">Try again</a></p>");
  }
  body += F("</section>");
  sendHtml(body);

  if (firmwareUploadSuccess) {
    delay(600);
    ESP.restart();
  }
}

void AppWebServer::handleFirmwareUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    firmwareUploadSuccess = false;
    firmwareUploadMessage = String("Receiving ") + upload.filename;

    uint32_t maxSketchSpace = ESP.getFreeSketchSpace();
#if defined(ESP8266)
    maxSketchSpace = (maxSketchSpace - 0x1000) & 0xFFFFF000;
#endif
    if (!Update.begin(maxSketchSpace)) {
      firmwareUploadMessage = F("Not enough space or invalid update start");
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      firmwareUploadMessage = F("Write failed");
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      firmwareUploadSuccess = true;
      firmwareUploadMessage = String("Uploaded ") + upload.totalSize + String(" bytes");
    } else {
      firmwareUploadMessage = F("Update validation failed");
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.end();
    firmwareUploadMessage = F("Upload aborted");
  }
}

void AppWebServer::handleConfigJson() {
  const AppConfigData& cfg = config.data();
  String json;
  json.reserve(700);
  json += F("{\"wifiSsid\":\"");
  json += cfg.wifiSsid;
  json += F("\",\"wifiPassword\":\"");
  json += config.maskedWifiPassword();
  json += F("\",\"ntpEnabled\":");
  json += cfg.ntpEnabled ? F("true") : F("false");
  json += F(",\"ntpServer\":\"");
  json += cfg.ntpServer;
  json += F("\",\"timezoneOffsetMinutes\":");
  json += cfg.timezoneOffsetMinutes;
  json += F(",\"mqttEnabled\":");
  json += cfg.mqttEnabled ? F("true") : F("false");
  json += F(",\"mqttRetain\":");
  json += cfg.mqttRetain ? F("true") : F("false");
  json += F(",\"mqttSecure\":");
  json += cfg.mqttSecure ? F("true") : F("false");
  json += F(",\"homeAssistantDiscovery\":");
  json += cfg.homeAssistantDiscovery ? F("true") : F("false");
  json += F(",\"telnetDebugEnabled\":");
  json += cfg.telnetDebugEnabled ? F("true") : F("false");
  json += F(",\"homeAssistantPrefix\":\"");
  json += cfg.homeAssistantPrefix;
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
  json += F(",\"today_m3\":");
  json += formatM3(history.getTodayMilliM3());
  json += F(",\"last_24h_m3\":");
  json += formatM3(history.getLast24HoursMilliM3());
  json += F(",\"last_31d_m3\":");
  json += formatM3(history.getLast31DaysMilliM3());
  json += F(",\"history_loaded\":");
  json += history.wasLoaded() ? F("true") : F("false");
  json += F(",\"time_synced\":");
  json += history.isTimeSynced() ? F("true") : F("false");
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

void AppWebServer::handleDayPlotJson() {
  String json;
  json.reserve(420);
  json += F("{\"unit\":\"m3\",\"resolution\":\"hour\",\"values\":[");
  for (int8_t i = 23; i >= 0; i--) {
    json += formatM3(history.getHourMilliM3(i));
    if (i > 0) {
      json += ",";
    }
  }
  json += F("]}");
  server.send(200, "application/json", json);
}

void AppWebServer::handleMonthPlotJson() {
  String json;
  json.reserve(520);
  json += F("{\"unit\":\"m3\",\"resolution\":\"day\",\"values\":[");
  for (int8_t i = 30; i >= 0; i--) {
    json += formatM3(history.getDayMilliM3(i));
    if (i > 0) {
      json += ",";
    }
  }
  json += F("]}");
  server.send(200, "application/json", json);
}

void AppWebServer::handleWifiScanJson() {
  if (WiFi.getMode() == WIFI_AP) {
    WiFi.mode(WIFI_AP_STA);
  }

  int count = WiFi.scanNetworks();
  String json;
  json.reserve(256 + max(count, 0) * 96);
  json += F("{\"networks\":[");
  for (int i = 0; i < count; i++) {
    if (i > 0) {
      json += ",";
    }
    json += F("{\"ssid\":\"");
    json += jsonEscape(WiFi.SSID(i));
    json += F("\",\"rssi\":");
    json += WiFi.RSSI(i);
    json += F(",\"channel\":");
    json += WiFi.channel(i);
    json += F(",\"secure\":");
    json += isOpenNetwork(i) ? F("false") : F("true");
    json += F("}");
  }
  json += F("]}");
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

void AppWebServer::handleWifiTestJson() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  ssid.trim();

  if (ssid.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"SSID is required\"}");
    return;
  }

  if (WiFi.getMode() == WIFI_AP) {
    WiFi.mode(WIFI_AP_STA);
  }

  WiFi.begin(ssid.c_str(), password.c_str());
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 12000) {
    delay(250);
  }

  String json;
  json.reserve(180);
  json += F("{\"ok\":");
  json += WiFi.status() == WL_CONNECTED ? F("true") : F("false");
  json += F(",\"ssid\":\"");
  json += jsonEscape(ssid);
  json += F("\",\"status\":");
  json += WiFi.status();
  json += F(",\"ip\":\"");
  json += WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");
  json += F("\"}");
  server.send(200, "application/json", json);

  if (WiFi.status() != WL_CONNECTED && strlen(config.data().wifiSsid) > 0) {
    WiFi.begin(config.data().wifiSsid, config.data().wifiPassword);
  }
}

void AppWebServer::handleCaptiveRedirect() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "Multical 21 Reader setup");
}

void AppWebServer::handleSave() {
  AppConfigData& cfg = config.data();

  copyArg(cfg.wifiSsid, sizeof(cfg.wifiSsid), server.arg("wifiSsid"));
  String wifiPassword = server.arg("wifiPassword");
  if (wifiPassword.length() > 0 && wifiPassword != "***") {
    copyArg(cfg.wifiPassword, sizeof(cfg.wifiPassword), wifiPassword);
  }

  cfg.ntpEnabled = server.arg("ntpEnabled") == "1";
  copyArg(cfg.ntpServer, sizeof(cfg.ntpServer), server.arg("ntpServer"));
  if (strlen(cfg.ntpServer) == 0) {
    strncpy(cfg.ntpServer, "pool.ntp.org", sizeof(cfg.ntpServer) - 1);
    cfg.ntpServer[sizeof(cfg.ntpServer) - 1] = '\0';
  }
  cfg.timezoneOffsetMinutes = (int16_t) server.arg("timezoneOffsetMinutes").toInt();
  cfg.mqttEnabled = server.arg("mqttEnabled") == "1";
  cfg.mqttRetain = server.arg("mqttRetain") == "1";
  cfg.mqttSecure = server.arg("mqttSecure") == "1";
  cfg.homeAssistantDiscovery = server.arg("homeAssistantDiscovery") == "1";
  cfg.telnetDebugEnabled = server.arg("telnetDebugEnabled") == "1";
  copyArg(cfg.homeAssistantPrefix, sizeof(cfg.homeAssistantPrefix), server.arg("homeAssistantPrefix"));
  if (strlen(cfg.homeAssistantPrefix) == 0) {
    strncpy(cfg.homeAssistantPrefix, "homeassistant", sizeof(cfg.homeAssistantPrefix) - 1);
    cfg.homeAssistantPrefix[sizeof(cfg.homeAssistantPrefix) - 1] = '\0';
  }
  copyArg(cfg.mqttHost, sizeof(cfg.mqttHost), server.arg("mqttHost"));
  cfg.mqttPort = (uint16_t) server.arg("mqttPort").toInt();
  if (cfg.mqttPort == 0) {
    cfg.mqttPort = cfg.mqttSecure ? 8883 : 1883;
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
  html += F("code{font-size:12px;overflow-wrap:anywhere}label{display:grid;gap:5px;font-size:13px;color:#334e68}input,select{font:inherit;padding:10px;border:1px solid #bcccdc;border-radius:6px;background:white}");
  html += F("button,.buttonLink{font:inherit;padding:10px 14px;border:0;border-radius:6px;background:#0b7285;color:white;font-weight:700;cursor:pointer;align-self:end;text-decoration:none;display:inline-block}");
  html += F(".uploadForm{margin-top:14px}.hint{color:#52606d;font-size:13px;margin:12px 0 0}");
  html += F(".wifiActions{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.wifiActions span{font-size:13px;color:#334e68}.wifiList{grid-column:1/-1;display:grid;gap:6px}.wifiNet{display:flex;justify-content:space-between;gap:10px;border:1px solid #d9e2ec;border-radius:6px;padding:8px;background:#f8fafc;cursor:pointer}.wifiNet small{color:#52606d}");
  html += F(".bars{height:170px;display:grid;grid-auto-flow:column;grid-auto-columns:1fr;gap:4px;align-items:end;border-bottom:1px solid #bcccdc;padding-top:8px;overflow:hidden}");
  html += F(".barwrap{height:100%;display:grid;grid-template-rows:1fr auto auto;gap:3px;min-width:0;text-align:center;color:#52606d;font-size:10px}.bar{align-self:end;background:#0b7285;border-radius:4px 4px 0 0;min-height:1px}.barwrap small{font-size:9px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}");
  html += F("</style></head><body><header><h1>Multical 21 Reader</h1></header><main>");
  html += body;
  html += F("</main><script>");
  html += F("async function scanWifi(){const r=document.getElementById('wifiResult'),l=document.getElementById('wifiList');r.textContent='Scanning...';l.innerHTML='';try{const j=await (await fetch('/wifiscan.json')).json();r.textContent=j.networks.length+' networks';j.networks.forEach(n=>{const d=document.createElement('div');d.className='wifiNet';d.innerHTML='<strong></strong><small></small>';d.querySelector('strong').textContent=n.ssid||'(hidden)';d.querySelector('small').textContent=n.rssi+' dBm ch '+n.channel+(n.secure?' secure':' open');d.onclick=()=>{document.getElementById('wifiSsid').value=n.ssid};l.appendChild(d)})}catch(e){r.textContent='Scan failed'}}");
  html += F("async function testWifi(){const r=document.getElementById('wifiResult');r.textContent='Testing...';const body=new URLSearchParams({ssid:document.getElementById('wifiSsid').value,password:document.getElementById('wifiPassword').value});try{const j=await (await fetch('/wifitest.json',{method:'POST',body})).json();r.textContent=j.ok?'Connected: '+j.ip:'Failed, status '+j.status}catch(e){r.textContent='Test failed'}}");
  html += F("</script></body></html>");
  server.send(200, "text/html", html);
}
