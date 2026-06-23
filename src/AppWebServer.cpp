#include "AppWebServer.h"
#include "FirmwareVersion.h"

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <Updater.h>
#elif defined(ESP32)
  #include <WiFi.h>
  #include <Update.h>
#endif
#include <time.h>

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

static String meterStatusText(const WaterData& data) {
  if (!data.valid) {
    return F("Meter Waiting");
  }

  String status;
  if (data.alarms.burst) {
    status += F("Burst alarm");
  }
  if (data.alarms.leak) {
    if (status.length() > 0) status += F(" + ");
    status += F("Leak suspected");
  }
  if (data.alarms.dry) {
    if (status.length() > 0) status += F(" + ");
    status += F("Dry / no water");
  }
  if (data.alarms.reverse) {
    if (status.length() > 0) status += F(" + ");
    status += F("Reverse flow");
  }

  return status.length() > 0 ? status : String("Meter OK");
}

static String meterStatusClass(const WaterData& data) {
  if (!data.valid) {
    return F("statusOff");
  }
  if (data.alarms.burst || data.alarms.leak) {
    return F("statusAlarm");
  }
  if (data.alarms.dry || data.alarms.reverse) {
    return F("statusWarn");
  }
  return F("statusOk");
}

static String radioRssiClass(const WaterData& data) {
  if (!data.radioRssiValid) {
    return F("statusOff");
  }
  if (data.radioRssiDbm >= -85) {
    return F("statusOk");
  }
  if (data.radioRssiDbm >= -100) {
    return F("statusWarn");
  }
  return F("statusAlarm");
}

static bool systemTimeSynced() {
  return time(nullptr) >= 1600000000;
}

static String systemTimeEpochString() {
  time_t now = time(nullptr);
  return now >= 1600000000 ? String((uint32_t) now) : String("not synced");
}

static time_t localTimeNow(int16_t timezoneOffsetMinutes) {
  time_t now = time(nullptr);
  if (now < 1600000000) {
    return 0;
  }
  return now + ((time_t) timezoneOffsetMinutes * 60);
}

static String twoDigits(int value) {
  return value < 10 ? String("0") + String(value) : String(value);
}

static const char* monthName(uint8_t month) {
  static const char* names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  return month < 12 ? names[month] : "";
}

static uint8_t graphCount(char period) {
  if (period == 'h') return 24;
  if (period == 'd') return 31;
  if (period == 'w') return 53;
  if (period == 'm') return 24;
  return 10;
}

static uint32_t graphValue(WaterHistory& history, char period, uint8_t age) {
  if (period == 'h') return history.getHourMilliM3(age);
  if (period == 'd') return history.getDayMilliM3(age);
  if (period == 'w') return history.getWeekMilliM3(age);
  if (period == 'm') return history.getMonthMilliM3(age);
  return history.getYearMilliM3(age);
}

static time_t shiftMonth(time_t now, int8_t offset) {
  struct tm* tm = gmtime(&now);
  if (tm == nullptr) {
    return 0;
  }
  int year = tm->tm_year + 1900;
  int month = tm->tm_mon - offset;
  while (month < 0) {
    month += 12;
    year--;
  }
  struct tm shifted = *tm;
  shifted.tm_year = year - 1900;
  shifted.tm_mon = month;
  shifted.tm_mday = 1;
  return mktime(&shifted);
}

static String formatGraphLabel(time_t now, int8_t offset, char period) {
  if (now == 0) {
    return offset == 0 ? String("now") : String(offset);
  }

  time_t point = now;
  if (period == 'h') {
    point = now - ((time_t) offset * 3600);
  } else if (period == 'd') {
    point = now - ((time_t) offset * 86400);
  } else if (period == 'w') {
    point = now - ((time_t) offset * 7 * 86400);
  } else if (period == 'm') {
    point = shiftMonth(now, offset);
  } else {
    point = shiftMonth(now, offset * 12);
  }

  struct tm* tm = gmtime(&point);
  if (tm == nullptr) {
    return offset == 0 ? String("now") : String(offset);
  }

  if (period == 'h') {
    return twoDigits(tm->tm_hour) + String(":00");
  }
  if (period == 'd') {
    return twoDigits(tm->tm_mday) + String("/") + twoDigits(tm->tm_mon + 1);
  }
  if (period == 'w') {
    uint8_t week = (tm->tm_yday / 7) + 1;
    return String("W") + twoDigits(week);
  }
  if (period == 'm') {
    return String(monthName(tm->tm_mon)) + String(" ") + String((tm->tm_year + 1900) % 100);
  }
  return String(tm->tm_year + 1900);
}

static String formatGraphTitle(time_t now, char period) {
  uint8_t count = graphCount(period);
  if (now == 0) {
    if (period == 'h') return F("Last 24 hours");
    if (period == 'd') return F("Last 31 days");
    if (period == 'w') return F("Last 53 weeks");
    if (period == 'm') return F("Last 24 months");
    return F("Last 10 years");
  }

  time_t start = now;
  if (period == 'h') start = now - ((time_t) (count - 1) * 3600);
  else if (period == 'd') start = now - ((time_t) (count - 1) * 86400);
  else if (period == 'w') start = now - ((time_t) (count - 1) * 7 * 86400);
  else if (period == 'm') start = shiftMonth(now, count - 1);
  else start = shiftMonth(now, (count - 1) * 12);

  struct tm* startTm = gmtime(&start);
  struct tm startCopy;
  if (startTm == nullptr) {
    return F("History");
  }
  startCopy = *startTm;
  struct tm* endTm = gmtime(&now);
  if (endTm == nullptr) {
    return F("History");
  }

  if (period == 'h') {
    return twoDigits(startCopy.tm_mday) + String("/") + twoDigits(startCopy.tm_mon + 1) + String(" ")
         + twoDigits(startCopy.tm_hour) + String(":00 - ")
         + twoDigits(endTm->tm_mday) + String("/") + twoDigits(endTm->tm_mon + 1) + String(" ")
         + twoDigits(endTm->tm_hour) + String(":00");
  }
  if (period == 'd' || period == 'w') {
    return twoDigits(startCopy.tm_mday) + String("/") + twoDigits(startCopy.tm_mon + 1) + String("/") + String(startCopy.tm_year + 1900)
         + String(" - ") + twoDigits(endTm->tm_mday) + String("/") + twoDigits(endTm->tm_mon + 1) + String("/") + String(endTm->tm_year + 1900);
  }
  if (period == 'm') {
    return String(monthName(startCopy.tm_mon)) + String(" ") + String(startCopy.tm_year + 1900)
         + String(" - ") + String(monthName(endTm->tm_mon)) + String(" ") + String(endTm->tm_year + 1900);
  }
  return String(startCopy.tm_year + 1900) + String(" - ") + String(endTm->tm_year + 1900);
}

static String graphBars(WaterHistory& history, char period, time_t now) {
  const uint8_t count = graphCount(period);
  uint32_t maxValue = 0;
  uint32_t totalValue = 0;
  for (uint8_t i = 0; i < count; i++) {
    uint32_t value = graphValue(history, period, i);
    totalValue += value;
    if (value > maxValue) {
      maxValue = value;
    }
  }

  String out;
  out.reserve(count * 110);
  out += F("<div class=\"graphSummary\"><span>Total <strong>");
  out += formatM3(totalValue);
  out += F(" m3</strong></span><span>Peak <strong>");
  out += formatM3(maxValue);
  out += F(" m3</strong></span></div><div class=\"bars\">");
  for (int8_t i = count - 1; i >= 0; i--) {
    uint32_t value = graphValue(history, period, i);
    uint8_t height = value == 0 || maxValue == 0 ? 0 : (uint8_t) max(6UL, (unsigned long) value * 100UL / maxValue);
    String label = formatGraphLabel(now, i, period);
    out += F("<div class=\"barwrap\" title=\"");
    out += label;
    out += F(": ");
    out += formatM3(value);
    out += F(" m3\"><div class=\"bar\" style=\"height:");
    out += height;
    out += F("%\"></div><span>");
    out += label;
    out += F("</span><small>");
    out += formatM3(value);
    out += F("</small></div>");
  }
  out += F("</div>");
  return out;
}

static const char* graphTitle(char period) {
  if (period == 'h') return "Hourly water use";
  if (period == 'd') return "Daily water use";
  if (period == 'w') return "Weekly water use";
  if (period == 'm') return "Monthly water use";
  return "Yearly water use";
}

static char selectedGraphPeriod(const String& value) {
  if (value == "d") return 'd';
  if (value == "w") return 'w';
  if (value == "m") return 'm';
  if (value == "y") return 'y';
  return 'h';
}

static String graphTab(char period, char selected, const char* label) {
  String out;
  out += F("<a class=\"tab");
  if (period == selected) {
    out += F(" active");
  }
  out += F("\" href=\"/graphs?view=");
  out += period;
  out += F("\">");
  out += label;
  out += F("</a>");
  return out;
}

static bool isOpenNetwork(uint8_t index) {
#if defined(ESP8266)
  return WiFi.encryptionType(index) == ENC_TYPE_NONE;
#else
  return WiFi.encryptionType(index) == WIFI_AUTH_OPEN;
#endif
}

static String buildSetupSection(AppConfig& config, bool onboardingMode) {
  const AppConfigData& cfg = config.data();
  String out;
  out += F("<section id=\"setup\" class=\"setupPanel");
  out += onboardingMode ? F(" onboardingPanel") : F("");
  out += F("\"><div class=\"sectionHead\"><h2>");
  out += onboardingMode ? F("WiFi onboarding") : F("Setup");
  out += F("</h2><span>");
  out += onboardingMode ? F("Setup AP") : F("Settings");
  out += F("</span></div><form class=\"setupForm\" method=\"post\" action=\"/save\">");
  out += F("<div class=\"formSection\"><h3>WiFi</h3><div class=\"formGrid\"><label>WiFi SSID<input id=\"wifiSsid\" name=\"wifiSsid\" value=\"");
  out += htmlEscape(cfg.wifiSsid);
  out += F("\"></label>");
  out += F("<label>WiFi password<input id=\"wifiPassword\" name=\"wifiPassword\" type=\"password\" placeholder=\"");
  out += htmlEscape(config.maskedWifiPassword());
  out += F("\"></label>");
  out += F("</div><div class=\"wifiActions\"><button type=\"button\" onclick=\"scanWifi()\">Scan WiFi</button><button type=\"button\" onclick=\"testWifi()\">Test WiFi</button><span id=\"wifiResult\"></span></div><div id=\"wifiList\" class=\"wifiList\"></div></div>");
  out += F("<div class=\"formSection\"><h3>Time</h3><div class=\"formGrid\"><label>NTP enabled<select name=\"ntpEnabled\"><option value=\"1\"");
  out += cfg.ntpEnabled ? F(" selected") : F("");
  out += F(">Enabled</option><option value=\"0\"");
  out += !cfg.ntpEnabled ? F(" selected") : F("");
  out += F(">Disabled</option></select></label>");
  out += F("<label>NTP server<input name=\"ntpServer\" value=\"");
  out += htmlEscape(cfg.ntpServer);
  out += F("\"></label>");
  out += F("<label>Timezone offset minutes<input name=\"timezoneOffsetMinutes\" inputmode=\"numeric\" value=\"");
  out += String(cfg.timezoneOffsetMinutes);
  out += F("\"></label><div class=\"statusLine\"><span>NTP status</span><strong>");
  out += systemTimeSynced() ? F("Synced") : F("Waiting");
  out += F("</strong><small>");
  out += systemTimeSynced() ? systemTimeEpochString() : F("Retries every 30 seconds after WiFi connects");
  out += F("</small></div></div></div>");
  out += F("<div class=\"formSection\"><h3>MQTT</h3><div class=\"formGrid\"><label>MQTT enabled<select name=\"mqttEnabled\"><option value=\"1\"");
  out += cfg.mqttEnabled ? F(" selected") : F("");
  out += F(">Enabled</option><option value=\"0\"");
  out += !cfg.mqttEnabled ? F(" selected") : F("");
  out += F(">Disabled</option></select></label>");
  out += F("<label>MQTT host<input name=\"mqttHost\" value=\"");
  out += htmlEscape(cfg.mqttHost);
  out += F("\"></label>");
  out += F("<label>MQTT port<input name=\"mqttPort\" inputmode=\"numeric\" value=\"");
  out += String(cfg.mqttPort);
  out += F("\"></label>");
  out += F("<label>MQTT username<input name=\"mqttUsername\" value=\"");
  out += htmlEscape(cfg.mqttUsername);
  out += F("\"></label>");
  out += F("<label>MQTT password<input name=\"mqttPassword\" type=\"password\" placeholder=\"");
  out += htmlEscape(config.maskedMqttPassword());
  out += F("\"></label>");
  out += F("<label>MQTT base topic<input name=\"mqttBaseTopic\" value=\"");
  out += htmlEscape(cfg.mqttBaseTopic);
  out += F("\"></label>");
  out += F("<label>MQTT retain<select name=\"mqttRetain\"><option value=\"1\"");
  out += cfg.mqttRetain ? F(" selected") : F("");
  out += F(">Enabled</option><option value=\"0\"");
  out += !cfg.mqttRetain ? F(" selected") : F("");
  out += F(">Disabled</option></select></label>");
  out += F("<label>Secure MQTT TLS<select name=\"mqttSecure\"><option value=\"1\"");
  out += cfg.mqttSecure ? F(" selected") : F("");
  out += F(">Enabled</option><option value=\"0\"");
  out += !cfg.mqttSecure ? F(" selected") : F("");
  out += F(">Disabled</option></select></label></div></div>");
  out += F("<div class=\"formSection\"><h3>Integrations</h3><div class=\"formGrid\"><label>Home Assistant discovery<select name=\"homeAssistantDiscovery\"><option value=\"1\"");
  out += cfg.homeAssistantDiscovery ? F(" selected") : F("");
  out += F(">Enabled</option><option value=\"0\"");
  out += !cfg.homeAssistantDiscovery ? F(" selected") : F("");
  out += F(">Disabled</option></select></label>");
  out += F("<label>HA discovery prefix<input name=\"homeAssistantPrefix\" value=\"");
  out += htmlEscape(cfg.homeAssistantPrefix);
  out += F("\"></label>");
  out += F("<label>Telnet debug<select name=\"telnetDebugEnabled\"><option value=\"1\"");
  out += cfg.telnetDebugEnabled ? F(" selected") : F("");
  out += F(">Enabled</option><option value=\"0\"");
  out += !cfg.telnetDebugEnabled ? F(" selected") : F("");
  out += F(">Disabled</option></select></label></div></div>");
  out += F("<div class=\"formSection\"><h3>Meter</h3><div class=\"formGrid\"><label>Meter serial hex<input name=\"meterSerial\" value=\"");
  out += htmlEscape(config.meterSerialHex());
  out += F("\" maxlength=\"8\"></label>");
  out += F("<label>AES key hex<input name=\"encryptionKey\" type=\"password\" placeholder=\"");
  out += config.hasMeter() ? F("***") : F("32 hex chars");
  out += F("\" maxlength=\"32\"></label></div></div>");
  out += F("<div class=\"actionRow\"><button type=\"submit\">Save settings</button></div></form>");
  out += F("<div class=\"formSection deviceActions\"><h3>Device actions</h3><div class=\"actionRow\"><form method=\"post\" action=\"/reboot\"><button type=\"submit\">Reboot</button></form>");
  out += F("<form method=\"post\" action=\"/reset-config\"><button class=\"danger\" type=\"submit\">Reset setup</button></form></div></div></section>");
  return out;
}

AppWebServer::AppWebServer(AppConfig& config, WaterData& waterData, WaterHistory& history)
  : config(config), waterData(waterData), history(history), server(80),
    firmwareUploadSuccess(false), firmwareUploadMessage() {
}

void AppWebServer::begin() {
  server.on("/", HTTP_GET, std::bind(&AppWebServer::handleRoot, this));
  server.on("/setup", HTTP_GET, std::bind(&AppWebServer::handleSetupPage, this));
  server.on("/graphs", HTTP_GET, std::bind(&AppWebServer::handleGraphsPage, this));
  server.on("/firmware", HTTP_GET, std::bind(&AppWebServer::handleFirmwarePage, this));
  server.on("/firmware", HTTP_POST, std::bind(&AppWebServer::handleFirmwarePost, this), std::bind(&AppWebServer::handleFirmwareUpload, this));
  server.on("/configuration.json", HTTP_GET, std::bind(&AppWebServer::handleConfigJson, this));
  server.on("/data.json", HTTP_GET, std::bind(&AppWebServer::handleDataJson, this));
  server.on("/dayplot.json", HTTP_GET, std::bind(&AppWebServer::handleDayPlotJson, this));
  server.on("/monthplot.json", HTTP_GET, std::bind(&AppWebServer::handleMonthPlotJson, this));
  server.on("/version.json", HTTP_GET, std::bind(&AppWebServer::handleVersionJson, this));
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
  server.on("/reset-config", HTTP_POST, std::bind(&AppWebServer::handleResetConfig, this));
  server.onNotFound(std::bind(&AppWebServer::handleRoot, this));
  server.begin();
}

void AppWebServer::handleClient() {
  server.handleClient();
}

void AppWebServer::handleRoot() {
  const AppConfigData& cfg = config.data();
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  const bool onboardingMode = !wifiConnected || !config.hasWifi();
  const String deviceIp = wifiConnected ? WiFi.localIP().toString() : String("192.168.4.1");
  const uint32_t frameAgeSeconds = waterData.valid ? (millis() - waterData.lastFrameMillis) / 1000 : 0;
  const bool radioLive = waterData.valid && frameAgeSeconds < 90;

  String body;
  if (onboardingMode) {
    body += buildSetupSection(config, true);
  }
  body += F("<section class=\"hero\"><div><p class=\"eyebrow\">Kamstrup Multical 21</p><h2>Water reader dashboard</h2><p class=\"heroText\">");
  body += waterData.valid ? F("Live water meter data is being decoded and stored locally.") : F("Waiting for the first valid wireless M-Bus frame.");
  body += F("</p></div><div class=\"heroMeter\"><span>Total</span><strong id=\"heroTotal\">");
  body += waterData.valid ? String(waterData.totalM3(), 3) : String("--");
  body += F("</strong><small>m3</small></div></section>");

  body += F("<section class=\"cards\">");
  body += F("<article class=\"card accentRx\"><div class=\"cardTop\"><span>Multical RX</span><b id=\"rxChip\" class=\"chip ");
  body += radioLive ? F("ok\">Live") : (waterData.valid ? F("warn\">Stale") : F("off\">Waiting"));
  body += F("</b></div><strong id=\"rxAge\">");
  body += waterData.valid ? String(frameAgeSeconds) + String(" s") : String("--");
  body += F("</strong><small>Last wireless M-Bus frame</small></article>");

  body += F("<article class=\"card accentWater\"><div class=\"cardTop\"><span>Water</span><b id=\"waterChip\" class=\"chip ");
  body += waterData.valid ? F("ok\">Live") : F("warn\">Waiting");
  body += F("</b></div><strong><span id=\"waterTotal\">");
  body += waterData.valid ? String(waterData.totalM3(), 3) : String("--");
  body += F("</span> m3</strong><small>Month <span id=\"monthUsage\">");
  body += waterData.valid ? String(waterData.monthUsageM3(), 3) + String(" m3") : String("-");
  body += F("</span></small></article>");

  body += F("<article class=\"card accentUsage\"><div class=\"cardTop\"><span>Usage</span><b class=\"chip ok\">History</b></div><strong><span id=\"todayUsage\">");
  body += formatM3(history.getTodayMilliM3());
  body += F("</span> m3</strong><small>Today, <span id=\"last24Usage\">");
  body += formatM3(history.getLast24HoursMilliM3());
  body += F("</span> m3 last 24h</small></article>");

  body += F("<article class=\"card accentMqtt\"><div class=\"cardTop\"><span>MQTT</span><b class=\"chip ");
  body += cfg.mqttEnabled ? F("ok\">Enabled") : F("off\">Disabled");
  body += F("</b></div><strong>");
  body += cfg.mqttEnabled ? htmlEscape(cfg.mqttBaseTopic) : String("Off");
  body += F("</strong><small>");
  body += cfg.homeAssistantDiscovery ? F("Home Assistant discovery on") : F("Home Assistant discovery off");
  body += F("</small></article>");

  body += F("<article class=\"card accentMeter\"><div class=\"cardTop\"><span>Meter</span><b class=\"chip ");
  body += config.hasMeter() ? F("ok\">Configured") : F("warn\">Missing");
  body += F("</b></div><strong><span id=\"waterTemp\">");
  body += waterData.valid ? String(waterData.waterTemperatureC) + String(" C") : String("--");
  body += F("</span></strong><small>Water temp, room <span id=\"roomTemp\">");
  body += waterData.valid ? String(waterData.ambientTemperatureC) + String(" C") : String("--");
  body += F("</span></small></article>");

  body += F("<article class=\"card accentVersion\"><div class=\"cardTop\"><span>Version</span><b class=\"chip ok\">");
  body += htmlEscape(firmwareBoardName());
  body += F("</b></div><strong>");
  body += htmlEscape(firmwareVersion());
  body += F("</strong><small>");
  body += htmlEscape(firmwareGitSha());
  body += F(" / ");
  body += htmlEscape(firmwareBuildDate());
  body += F("</small></article>");
  body += F("</section>");

  body += F("<section><h2>Fibaro / local API</h2><dl>");
  body += F("<dt>State JSON</dt><dd><code>http://");
  body += htmlEscape(deviceIp);
  body += F("/data.json</code></dd><dt>Hourly plot</dt><dd><code>/dayplot.json</code></dd><dt>Daily plot</dt><dd><code>/monthplot.json</code></dd>");
  body += F("<dt>Sync rule</dt><dd>Use total_m3 as source of truth and treat data as stale when last_frame_age_s is high.</dd></dl></section>");

  sendHtml(body);
}

void AppWebServer::handleSetupPage() {
  const bool onboardingMode = WiFi.status() != WL_CONNECTED || !config.hasWifi();
  sendHtml(buildSetupSection(config, onboardingMode));
}

void AppWebServer::handleGraphsPage() {
  time_t now = localTimeNow(config.data().timezoneOffsetMinutes);
  char period = selectedGraphPeriod(server.arg("view"));
  String body;
  body.reserve(5200);
  body += F("<section class=\"graphPanel\"><div class=\"tabs\">");
  body += graphTab('h', period, "Hours");
  body += graphTab('d', period, "Days");
  body += graphTab('w', period, "Weeks");
  body += graphTab('m', period, "Months");
  body += graphTab('y', period, "Years");
  body += F("</div><div class=\"sectionHead\"><h2>");
  body += graphTitle(period);
  body += F("</h2><span>");
  body += formatGraphTitle(now, period);
  body += F("</span></div>");
  body += graphBars(history, period, now);
  body += F("</section>");
  sendHtml(body);
}

void AppWebServer::handleFirmwarePage() {
  String body;
  body.reserve(1300);
  body += F("<section><h2>Firmware update</h2><dl>");
  body += F("<dt>Version</dt><dd>");
  body += htmlEscape(firmwareVersion());
  body += F("</dd><dt>Build</dt><dd>");
  body += htmlEscape(firmwareBuildDate());
  body += F("</dd><dt>Commit</dt><dd>");
  body += htmlEscape(firmwareGitSha());
  body += F("</dd>");
  body += F("<dt>Board</dt><dd>");
  body += htmlEscape(firmwareBoardName());
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
  const AppConfigData& cfg = config.data();
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
  json += F(",\"last_53w_m3\":");
  json += formatM3(history.getLast53WeeksMilliM3());
  json += F(",\"last_24m_m3\":");
  json += formatM3(history.getLast24MonthsMilliM3());
  json += F(",\"last_10y_m3\":");
  json += formatM3(history.getLast10YearsMilliM3());
  json += F(",\"history_loaded\":");
  json += history.wasLoaded() ? F("true") : F("false");
  json += F(",\"time_synced\":");
  json += systemTimeSynced() ? F("true") : F("false");
  json += F(",\"ntp_enabled\":");
  json += cfg.ntpEnabled ? F("true") : F("false");
  json += F(",\"time_epoch\":");
  json += systemTimeSynced() ? String((uint32_t) time(nullptr)) : F("null");
  json += F(",\"water_temperature_c\":");
  json += waterData.waterTemperatureC;
  json += F(",\"ambient_temperature_c\":");
  json += waterData.ambientTemperatureC;
  json += F(",\"meter_status\":\"");
  json += jsonEscape(meterStatusText(waterData));
  json += F("\"");
  json += F(",\"radio_rssi_dbm\":");
  json += waterData.radioRssiValid ? String(waterData.radioRssiDbm) : F("null");
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

void AppWebServer::handleVersionJson() {
  String json;
  json.reserve(180);
  json += F("{\"version\":\"");
  json += jsonEscape(firmwareVersion());
  json += F("\",\"git_sha\":\"");
  json += jsonEscape(firmwareGitSha());
  json += F("\",\"build_date\":\"");
  json += jsonEscape(firmwareBuildDate());
  json += F("\",\"board\":\"");
  json += jsonEscape(firmwareBoardName());
  json += F("\"}");
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
  server.sendHeader("Location", "/setup", true);
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

void AppWebServer::handleResetConfig() {
  config.clear();
  server.send(200, "text/plain", "Configuration cleared. Rebooting to setup AP.");
  delay(400);
  ESP.restart();
}

void AppWebServer::sendHtml(const String& body) {
  const uint32_t frameAgeSeconds = waterData.valid ? (millis() - waterData.lastFrameMillis) / 1000 : 0;
  const bool radioLive = waterData.valid && frameAgeSeconds < 90;
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  const bool ntpSynced = systemTimeSynced();
  const AppConfigData& cfg = config.data();
  String html;
  html += F("<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  html += F("<title>Multical 21 Reader</title><style>");
  html += F("body{margin:0;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;background:#eef3f7;color:#111827}");
  html += F("header{background:#12344d;color:white;padding:12px 18px;border-bottom:4px solid #0b7285;display:grid;grid-template-columns:auto minmax(0,1fr);gap:12px;align-items:center}main{max-width:980px;margin:0 auto;padding:18px}");
  html += F("nav{display:flex;gap:8px;justify-content:flex-end;align-items:center;min-width:0}.topRight{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:10px;align-items:center;min-width:0}.statusGroup,.navLinks{display:flex;gap:6px;align-items:center;justify-content:flex-end;min-width:0}.statusGroup{overflow:hidden}.navLinks{flex-wrap:nowrap}nav a,.statusPill{color:white;text-decoration:none;border:1px solid #486581;border-radius:6px;padding:6px 8px;font-weight:700;font-size:12px;display:inline-flex;align-items:center;gap:6px;min-height:20px}nav svg{width:15px;height:15px;stroke:currentColor;fill:none;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}.statusPill{background:#0f2f46}.statusDot{width:8px;height:8px;border-radius:50%;background:#627d98;flex:0 0 auto}.statusOk .statusDot{background:#2f9e44;box-shadow:0 0 0 4px rgba(47,158,68,.18)}.statusWarn .statusDot{background:#b7791f;box-shadow:0 0 0 4px rgba(183,121,31,.18)}.statusAlarm .statusDot{background:#c92a2a;box-shadow:0 0 0 4px rgba(201,42,42,.2)}.statusOff .statusDot{background:#627d98}.statusText{max-width:112px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}");
  html += F("section{background:white;border:1px solid #d9e2ec;border-radius:8px;padding:16px;margin:0 0 16px}");
  html += F("h1{font-size:24px;margin:0}h2{font-size:18px;margin:0 0 12px}dl{display:grid;grid-template-columns:160px 1fr;gap:8px;margin:0}");
  html += F("dt{color:#52606d}dd{margin:0;font-weight:600}form{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px}");
  html += F("code{font-size:12px;overflow-wrap:anywhere}a{color:#0b7285}label{display:grid;gap:5px;font-size:13px;color:#334e68}input,select{font:inherit;padding:10px;border:1px solid #bcccdc;border-radius:6px;background:white}");
  html += F("button,.buttonLink{font:inherit;padding:10px 14px;border:0;border-radius:6px;background:#0b7285;color:white;font-weight:700;cursor:pointer;align-self:end;text-decoration:none;display:inline-block}");
  html += F(".danger{background:#b42318}");
  html += F(".uploadForm{margin-top:14px}.hint{color:#52606d;font-size:13px;margin:12px 0 0}");
  html += F(".hero{display:grid;grid-template-columns:minmax(0,1fr) 180px;gap:18px;align-items:center;background:#12344d;color:white;border-color:#12344d}.hero h2{font-size:28px;margin:0 0 8px}.eyebrow{margin:0 0 6px;color:#9fb3c8;font-size:12px;font-weight:800;text-transform:uppercase}.heroText{margin:0;color:#d9e2ec}.heroMeter{border:1px solid #486581;border-radius:8px;padding:14px;background:#0f2f46}.heroMeter span,.heroMeter small{display:block;color:#bcccdc}.heroMeter strong{display:block;font-size:34px;line-height:1.1;margin:4px 0}");
  html += F(".cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:12px;background:transparent;border:0;padding:0}.card{background:white;border:1px solid #d9e2ec;border-left:5px solid #0b7285;border-radius:8px;padding:14px;min-height:108px;display:grid;gap:10px}.cardTop{display:flex;justify-content:space-between;gap:8px;align-items:center}.cardTop span{font-size:12px;color:#52606d;font-weight:800;text-transform:uppercase}.card strong{font-size:22px;line-height:1.15;overflow-wrap:anywhere}.card small{color:#52606d;overflow-wrap:anywhere}.card a{font-size:22px;font-weight:800}.chip{border-radius:999px;padding:4px 8px;font-size:11px;color:white;white-space:nowrap}.ok{background:#147d64}.warn{background:#b7791f}.off{background:#627d98}.accentRx{border-left-color:#147d64}.accentWater{border-left-color:#0b7285}.accentUsage{border-left-color:#2f9e44}.accentWifi{border-left-color:#1864ab}.accentMqtt{border-left-color:#6741d9}.accentTime{border-left-color:#d9480f}.accentMeter{border-left-color:#c2410c}.accentVersion{border-left-color:#087f5b}");
  html += F(".sectionHead{display:flex;justify-content:space-between;gap:12px;align-items:baseline;margin:0 0 10px}.sectionHead h2{margin:0}.sectionHead span{color:#52606d;font-size:12px;font-weight:700;text-transform:uppercase}.graphPanel{padding-bottom:12px}.graphSummary{display:flex;gap:10px;flex-wrap:wrap;margin:0 0 10px}.graphSummary span{background:#f0f4f8;border:1px solid #d9e2ec;border-radius:6px;padding:7px 9px;color:#52606d;font-size:12px}.graphSummary strong{color:#102a43}.tabs{display:flex;gap:8px;flex-wrap:wrap;margin:0 0 14px}.tab{border:1px solid #bcccdc;background:#f0f4f8;color:#102a43;text-decoration:none;border-radius:6px;padding:8px 10px;font-weight:800;font-size:13px}.tab.active{background:#0b7285;border-color:#0b7285;color:white}");
  html += F(".setupPanel{border-left:5px solid #0b7285}.setupForm{display:block}.formSection{border-top:1px solid #d9e2ec;padding-top:14px;margin-top:14px}.formSection:first-of-type{border-top:0;padding-top:0}.formSection h3{font-size:15px;margin:0 0 10px;color:#102a43}.formGrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px}.actionRow{display:flex;gap:10px;flex-wrap:wrap;margin-top:12px}.actionRow form{display:block}.actionRow button{min-width:170px}.statusLine{border:1px solid #d9e2ec;border-radius:6px;padding:10px;background:#f8fafc;display:grid;gap:4px;color:#52606d}.statusLine strong{color:#102a43}.statusLine small{font-size:12px;color:#627d98}.deviceActions{padding-bottom:0}.onboardingPanel{border-color:#0b7285;background:#f8fcfd}.onboardingPanel .sectionHead h2{font-size:24px}");
  html += F(".wifiActions{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.wifiActions span{font-size:13px;color:#334e68}.wifiList{grid-column:1/-1;display:grid;gap:6px}.wifiNet{display:flex;justify-content:space-between;gap:10px;border:1px solid #d9e2ec;border-radius:6px;padding:8px;background:#f8fafc;cursor:pointer}.wifiNet small{color:#52606d}");
  html += F(".bars{height:180px;display:grid;grid-auto-flow:column;grid-auto-columns:1fr;gap:4px;align-items:end;border-bottom:1px solid #bcccdc;padding-top:8px;overflow:hidden;background:linear-gradient(to top,#f8fafc,#fff)}");
  html += F(".barwrap{height:100%;display:grid;grid-template-rows:1fr auto auto;gap:3px;min-width:0;text-align:center;color:#52606d;font-size:10px}.bar{align-self:end;background:#0b7285;border-radius:4px 4px 0 0}.barwrap:nth-child(2n) .bar{background:#147d64}.barwrap small{font-size:9px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}");
  html += F("@media(max-width:980px){header{grid-template-columns:1fr}.topRight{grid-template-columns:1fr}.statusGroup,.navLinks{justify-content:flex-start;flex-wrap:wrap}.statusGroup{overflow:visible}}@media(max-width:640px){main{padding:12px}.hero{grid-template-columns:1fr}.hero h2{font-size:24px}dl{grid-template-columns:1fr}.heroMeter strong{font-size:30px}nav a span{display:none}.statusText{max-width:96px}}");
  html += F("</style></head><body><header><h1>Multical 21 Reader</h1><nav class=\"topRight\"><div class=\"statusGroup\">");
  html += F("<span id=\"topFramePill\" class=\"statusPill ");
  html += radioLive ? F("statusOk") : (waterData.valid ? F("statusWarn") : F("statusOff"));
  html += F("\" title=\"Latest Multical wireless M-Bus frame\"><span class=\"statusDot\"></span><span id=\"topFrameText\" class=\"statusText\">");
  html += radioLive ? F("Frame live") : (waterData.valid ? String("Frame ") + String(frameAgeSeconds) + String("s") : String("No frame"));
  html += F("</span></span>");
  html += F("<span id=\"topSignalPill\" class=\"statusPill ");
  html += radioRssiClass(waterData);
  html += F("\" title=\"CC1101 received signal strength\"><span class=\"statusDot\"></span><span id=\"topSignalText\" class=\"statusText\">");
  html += waterData.radioRssiValid ? String("Signal ") + String(waterData.radioRssiDbm) : String("Signal --");
  html += F("</span></span>");
  html += F("<span id=\"topDataPill\" class=\"statusPill ");
  html += meterStatusClass(waterData);
  html += F("\" title=\"Meter status from Multical alarm bits\"><span class=\"statusDot\"></span><span id=\"topDataText\" class=\"statusText\">");
  html += waterData.valid ? htmlEscape(meterStatusText(waterData)) : String("No data");
  html += F("</span></span>");
  html += F("<span class=\"statusPill ");
  html += wifiConnected ? F("statusOk") : F("statusWarn");
  html += F("\" title=\"");
  html += wifiConnected ? htmlEscape(WiFi.localIP().toString()) : String("Setup AP 192.168.4.1");
  html += F("\"><span class=\"statusDot\"></span><span class=\"statusText\">");
  html += wifiConnected ? htmlEscape(cfg.wifiSsid) : String("Setup AP");
  html += F("</span></span>");
  html += F("<span id=\"topTimePill\" class=\"statusPill ");
  html += ntpSynced ? F("statusOk") : (cfg.ntpEnabled ? F("statusWarn") : F("statusOff"));
  html += F("\" title=\"");
  html += cfg.ntpEnabled ? htmlEscape(cfg.ntpServer) : String("NTP disabled");
  html += F("\"><span class=\"statusDot\"></span><span id=\"topTimeText\" class=\"statusText\">");
  html += ntpSynced ? F("Time OK") : (cfg.ntpEnabled ? F("Time wait") : F("Time off"));
  html += F("</span></span></div><div class=\"navLinks\">");
  html += F("<a href=\"/\" title=\"Dashboard\"><svg viewBox=\"0 0 24 24\"><path d=\"M3 12l9-9 9 9\"></path><path d=\"M5 10v10h14V10\"></path></svg><span>Dashboard</span></a>");
  html += F("<a href=\"/setup\" title=\"Setup\"><svg viewBox=\"0 0 24 24\"><circle cx=\"12\" cy=\"12\" r=\"3\"></circle><path d=\"M19.4 15a1.7 1.7 0 0 0 .3 1.9l.1.1-2.1 2.1-.1-.1a1.7 1.7 0 0 0-1.9-.3 1.7 1.7 0 0 0-1 1.5V20h-3v-.2a1.7 1.7 0 0 0-1-1.5 1.7 1.7 0 0 0-1.9.3l-.1.1-2.1-2.1.1-.1A1.7 1.7 0 0 0 5 15a1.7 1.7 0 0 0-1.5-1H3v-3h.5A1.7 1.7 0 0 0 5 10a1.7 1.7 0 0 0-.3-1.9l-.1-.1 2.1-2.1.1.1a1.7 1.7 0 0 0 1.9.3 1.7 1.7 0 0 0 1-1.5V4h3v.8a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.9-.3l.1-.1 2.1 2.1-.1.1A1.7 1.7 0 0 0 19 10a1.7 1.7 0 0 0 1.5 1h.5v3h-.5a1.7 1.7 0 0 0-1.1 1z\"></path></svg><span>Setup</span></a>");
  html += F("<a href=\"/graphs\" title=\"Graphs\"><svg viewBox=\"0 0 24 24\"><path d=\"M4 19V5\"></path><path d=\"M4 19h16\"></path><path d=\"M8 16v-4\"></path><path d=\"M12 16V8\"></path><path d=\"M16 16v-6\"></path></svg><span>Graphs</span></a>");
  html += F("<a href=\"/firmware\" title=\"Firmware\"><svg viewBox=\"0 0 24 24\"><path d=\"M12 3v12\"></path><path d=\"M8 7l4-4 4 4\"></path><path d=\"M5 15v4h14v-4\"></path></svg><span>Firmware</span></a>");
  html += F("</div></nav></header><main>");
  html += body;
  html += F("</main><script>");
  html += F("function byId(i){return document.getElementById(i)}function txt(i,v){const e=byId(i);if(e)e.textContent=v}function pill(i,c){const e=byId(i);if(e)e.className='statusPill '+c}function chip(i,c,t){const e=byId(i);if(e){e.className='chip '+c;e.textContent=t}}function rssiClass(v){if(v===null||v===undefined)return'statusOff';if(v>=-85)return'statusOk';if(v>=-100)return'statusWarn';return'statusAlarm'}");
  html += F("async function refreshData(){if(!byId('topFrameText'))return;try{const j=await (await fetch('/data.json',{cache:'no-store'})).json();const age=j.last_frame_age_s;const live=j.valid&&age<90;const a=j.alarms||{};const alarm=a.burst||a.leak;const warn=a.dry||a.reverse;pill('topFramePill',live?'statusOk':(j.valid?'statusWarn':'statusOff'));txt('topFrameText',live?'Frame live':(j.valid?'Frame '+age+'s':'No frame'));pill('topSignalPill',rssiClass(j.radio_rssi_dbm));txt('topSignalText',j.radio_rssi_dbm===null?'Signal --':'Signal '+j.radio_rssi_dbm);pill('topDataPill',!j.valid?'statusOff':(alarm?'statusAlarm':(warn?'statusWarn':'statusOk')));txt('topDataText',j.valid?(j.meter_status||'Meter OK'):'No data');pill('topTimePill',j.time_synced?'statusOk':(j.ntp_enabled?'statusWarn':'statusOff'));txt('topTimeText',j.time_synced?'Time OK':(j.ntp_enabled?'Time wait':'Time off'));txt('heroTotal',j.valid?Number(j.total_m3).toFixed(3):'--');txt('rxAge',j.valid?age+' s':'--');chip('rxChip',live?'ok':(j.valid?'warn':'off'),live?'Live':(j.valid?'Stale':'Waiting'));txt('waterTotal',j.valid?Number(j.total_m3).toFixed(3):'--');txt('monthUsage',j.valid?Number(j.month_usage_m3).toFixed(3)+' m3':'-');chip('waterChip',j.valid?'ok':'warn',j.valid?'Live':'Waiting');txt('todayUsage',Number(j.today_m3).toFixed(3));txt('last24Usage',Number(j.last_24h_m3).toFixed(3));txt('waterTemp',j.valid?j.water_temperature_c+' C':'--');txt('roomTemp',j.valid?j.ambient_temperature_c+' C':'--')}catch(e){}}setInterval(refreshData,5000);refreshData();");
  html += F("async function scanWifi(){const r=document.getElementById('wifiResult'),l=document.getElementById('wifiList');r.textContent='Scanning...';l.innerHTML='';try{const j=await (await fetch('/wifiscan.json')).json();r.textContent=j.networks.length+' networks';j.networks.forEach(n=>{const d=document.createElement('div');d.className='wifiNet';d.innerHTML='<strong></strong><small></small>';d.querySelector('strong').textContent=n.ssid||'(hidden)';d.querySelector('small').textContent=n.rssi+' dBm ch '+n.channel+(n.secure?' secure':' open');d.onclick=()=>{document.getElementById('wifiSsid').value=n.ssid};l.appendChild(d)})}catch(e){r.textContent='Scan failed'}}");
  html += F("async function testWifi(){const r=document.getElementById('wifiResult');r.textContent='Testing...';const body=new URLSearchParams({ssid:document.getElementById('wifiSsid').value,password:document.getElementById('wifiPassword').value});try{const j=await (await fetch('/wifitest.json',{method:'POST',body})).json();r.textContent=j.ok?'Connected: '+j.ip:'Failed, status '+j.status}catch(e){r.textContent='Test failed'}}");
  html += F("</script></body></html>");
  server.send(200, "text/html", html);
}
