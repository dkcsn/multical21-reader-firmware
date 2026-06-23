#include "AppConfig.h"
#include <EEPROM.h>

static const uint32_t CONFIG_MAGIC = 0x4D433235;
static const uint32_t CONFIG_MAGIC_V4 = 0x4D433234;
static const uint32_t CONFIG_MAGIC_V3 = 0x4D433233;
static const size_t EEPROM_SIZE = 1024;
static const int EEPROM_ADDRESS = 0;

struct AppConfigDataV3 {
  uint32_t magic;
  char wifiSsid[33];
  char wifiPassword[65];
  bool ntpEnabled;
  char ntpServer[65];
  int16_t timezoneOffsetMinutes;
  bool mqttEnabled;
  bool mqttRetain;
  bool mqttSecure;
  bool homeAssistantDiscovery;
  char homeAssistantPrefix[65];
  char mqttHost[129];
  uint16_t mqttPort;
  char mqttUsername[65];
  char mqttPassword[65];
  char mqttBaseTopic[65];
  uint8_t meterId[4];
  uint8_t encryptionKey[16];
};

struct AppConfigDataV4 {
  uint32_t magic;
  char wifiSsid[33];
  char wifiPassword[65];
  bool ntpEnabled;
  char ntpServer[65];
  int16_t timezoneOffsetMinutes;
  bool mqttEnabled;
  bool mqttRetain;
  bool mqttSecure;
  bool homeAssistantDiscovery;
  bool telnetDebugEnabled;
  char homeAssistantPrefix[65];
  char mqttHost[129];
  uint16_t mqttPort;
  char mqttUsername[65];
  char mqttPassword[65];
  char mqttBaseTopic[65];
  uint8_t meterId[4];
  uint8_t encryptionKey[16];
};

static int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool parseHexBytes(uint8_t* dest, size_t len, const String& input) {
  String hex = input;
  hex.trim();
  hex.replace(" ", "");
  hex.replace(":", "");
  hex.replace("-", "");
  hex.replace("0x", "");
  hex.replace("0X", "");

  if (hex.length() != len * 2) {
    return false;
  }

  for (size_t i = 0; i < len; i++) {
    int high = hexValue(hex[i * 2]);
    int low = hexValue(hex[i * 2 + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    dest[i] = (uint8_t)((high << 4) | low);
  }
  return true;
}

static String toHexString(const uint8_t* data, size_t len) {
  static const char* digits = "0123456789ABCDEF";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    out += digits[data[i] >> 4];
    out += digits[data[i] & 0x0F];
  }
  return out;
}

static String normalizeDeviceName(const String& input) {
  String name = input;
  name.trim();
  name.toLowerCase();
  String out;
  out.reserve(32);
  bool lastDash = false;
  for (uint16_t i = 0; i < name.length() && out.length() < 32; i++) {
    char c = name[i];
    bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (allowed) {
      out += c;
      lastDash = false;
    } else if ((c == '-' || c == '_' || c == ' ') && out.length() > 0 && !lastDash) {
      out += '-';
      lastDash = true;
    }
  }
  while (out.endsWith("-")) {
    out.remove(out.length() - 1);
  }
  return out.length() > 0 ? out : String("multical21");
}

bool AppConfig::begin() {
  EEPROM.begin(EEPROM_SIZE);
  return load();
}

bool AppConfig::load() {
  EEPROM.get(EEPROM_ADDRESS, config);
  if (config.magic == CONFIG_MAGIC_V3) {
    AppConfigDataV3 legacy;
    EEPROM.get(EEPROM_ADDRESS, legacy);
    setDefaults();
    memcpy(config.wifiSsid, legacy.wifiSsid, sizeof(config.wifiSsid));
    memcpy(config.wifiPassword, legacy.wifiPassword, sizeof(config.wifiPassword));
    config.ntpEnabled = legacy.ntpEnabled;
    memcpy(config.ntpServer, legacy.ntpServer, sizeof(config.ntpServer));
    config.timezoneOffsetMinutes = legacy.timezoneOffsetMinutes;
    config.mqttEnabled = legacy.mqttEnabled;
    config.mqttRetain = legacy.mqttRetain;
    config.mqttSecure = legacy.mqttSecure;
    config.homeAssistantDiscovery = legacy.homeAssistantDiscovery;
    memcpy(config.homeAssistantPrefix, legacy.homeAssistantPrefix, sizeof(config.homeAssistantPrefix));
    memcpy(config.mqttHost, legacy.mqttHost, sizeof(config.mqttHost));
    config.mqttPort = legacy.mqttPort;
    memcpy(config.mqttUsername, legacy.mqttUsername, sizeof(config.mqttUsername));
    memcpy(config.mqttPassword, legacy.mqttPassword, sizeof(config.mqttPassword));
    memcpy(config.mqttBaseTopic, legacy.mqttBaseTopic, sizeof(config.mqttBaseTopic));
    memcpy(config.meterId, legacy.meterId, sizeof(config.meterId));
    memcpy(config.encryptionKey, legacy.encryptionKey, sizeof(config.encryptionKey));
    config.telnetDebugEnabled = false;
    save();
  } else if (config.magic == CONFIG_MAGIC_V4) {
    AppConfigDataV4 legacy;
    EEPROM.get(EEPROM_ADDRESS, legacy);
    setDefaults();
    memcpy(config.wifiSsid, legacy.wifiSsid, sizeof(config.wifiSsid));
    memcpy(config.wifiPassword, legacy.wifiPassword, sizeof(config.wifiPassword));
    config.ntpEnabled = legacy.ntpEnabled;
    memcpy(config.ntpServer, legacy.ntpServer, sizeof(config.ntpServer));
    config.timezoneOffsetMinutes = legacy.timezoneOffsetMinutes;
    config.mqttEnabled = legacy.mqttEnabled;
    config.mqttRetain = legacy.mqttRetain;
    config.mqttSecure = legacy.mqttSecure;
    config.homeAssistantDiscovery = legacy.homeAssistantDiscovery;
    config.telnetDebugEnabled = legacy.telnetDebugEnabled;
    memcpy(config.homeAssistantPrefix, legacy.homeAssistantPrefix, sizeof(config.homeAssistantPrefix));
    memcpy(config.mqttHost, legacy.mqttHost, sizeof(config.mqttHost));
    config.mqttPort = legacy.mqttPort;
    memcpy(config.mqttUsername, legacy.mqttUsername, sizeof(config.mqttUsername));
    memcpy(config.mqttPassword, legacy.mqttPassword, sizeof(config.mqttPassword));
    memcpy(config.mqttBaseTopic, legacy.mqttBaseTopic, sizeof(config.mqttBaseTopic));
    memcpy(config.meterId, legacy.meterId, sizeof(config.meterId));
    memcpy(config.encryptionKey, legacy.encryptionKey, sizeof(config.encryptionKey));
    save();
  } else if (config.magic != CONFIG_MAGIC) {
    setDefaults();
    save();
    return false;
  }
  config.deviceName[sizeof(config.deviceName) - 1] = '\0';
  config.wifiSsid[sizeof(config.wifiSsid) - 1] = '\0';
  config.wifiPassword[sizeof(config.wifiPassword) - 1] = '\0';
  config.ntpServer[sizeof(config.ntpServer) - 1] = '\0';
  config.homeAssistantPrefix[sizeof(config.homeAssistantPrefix) - 1] = '\0';
  config.mqttHost[sizeof(config.mqttHost) - 1] = '\0';
  config.mqttUsername[sizeof(config.mqttUsername) - 1] = '\0';
  config.mqttPassword[sizeof(config.mqttPassword) - 1] = '\0';
  config.mqttBaseTopic[sizeof(config.mqttBaseTopic) - 1] = '\0';
  if (config.mqttPort == 0) {
    config.mqttPort = config.mqttSecure ? 8883 : 1883;
  }
  if (strlen(config.ntpServer) == 0) {
    strncpy(config.ntpServer, "pool.ntp.org", sizeof(config.ntpServer) - 1);
    config.ntpServer[sizeof(config.ntpServer) - 1] = '\0';
  }
  if (strlen(config.homeAssistantPrefix) == 0) {
    strncpy(config.homeAssistantPrefix, "homeassistant", sizeof(config.homeAssistantPrefix) - 1);
    config.homeAssistantPrefix[sizeof(config.homeAssistantPrefix) - 1] = '\0';
  }
  if (strlen(config.deviceName) == 0) {
    strncpy(config.deviceName, "multical21", sizeof(config.deviceName) - 1);
    config.deviceName[sizeof(config.deviceName) - 1] = '\0';
  } else {
    setDeviceName(config.deviceName);
  }
  return true;
}

bool AppConfig::save() {
  config.magic = CONFIG_MAGIC;
  EEPROM.put(EEPROM_ADDRESS, config);
  return EEPROM.commit();
}

void AppConfig::clear() {
  setDefaults();
  save();
}

AppConfigData& AppConfig::data() {
  return config;
}

const AppConfigData& AppConfig::data() const {
  return config;
}

bool AppConfig::hasWifi() const {
  return strlen(config.wifiSsid) > 0;
}

bool AppConfig::hasMqtt() const {
  return config.mqttEnabled && strlen(config.mqttHost) > 0;
}

bool AppConfig::hasMeter() const {
  bool hasId = false;
  bool hasKey = false;
  for (uint8_t i = 0; i < 4; i++) {
    hasId |= config.meterId[i] != 0;
  }
  for (uint8_t i = 0; i < 16; i++) {
    hasKey |= config.encryptionKey[i] != 0;
  }
  return hasId && hasKey;
}

bool AppConfig::setDeviceName(const String& value) {
  String normalized = normalizeDeviceName(value);
  strncpy(config.deviceName, normalized.c_str(), sizeof(config.deviceName) - 1);
  config.deviceName[sizeof(config.deviceName) - 1] = '\0';
  return true;
}

bool AppConfig::setMeterSerialHex(const String& value) {
  return parseHexBytes(config.meterId, sizeof(config.meterId), value);
}

bool AppConfig::setEncryptionKeyHex(const String& value) {
  return parseHexBytes(config.encryptionKey, sizeof(config.encryptionKey), value);
}

String AppConfig::meterSerialHex() const {
  return toHexString(config.meterId, sizeof(config.meterId));
}

String AppConfig::deviceName() const {
  return normalizeDeviceName(config.deviceName);
}

String AppConfig::encryptionKeyHex() const {
  return toHexString(config.encryptionKey, sizeof(config.encryptionKey));
}

String AppConfig::maskedWifiPassword() const {
  return strlen(config.wifiPassword) > 0 ? "***" : "";
}

String AppConfig::maskedMqttPassword() const {
  return strlen(config.mqttPassword) > 0 ? "***" : "";
}

void AppConfig::setDefaults() {
  memset(&config, 0, sizeof(config));
  config.magic = CONFIG_MAGIC;
  strncpy(config.deviceName, "multical21", sizeof(config.deviceName) - 1);
  config.ntpEnabled = true;
  strncpy(config.ntpServer, "pool.ntp.org", sizeof(config.ntpServer) - 1);
  config.timezoneOffsetMinutes = 60;
  config.mqttEnabled = false;
  config.mqttRetain = true;
  config.mqttSecure = false;
  config.homeAssistantDiscovery = false;
  config.telnetDebugEnabled = false;
  strncpy(config.homeAssistantPrefix, "homeassistant", sizeof(config.homeAssistantPrefix) - 1);
  config.mqttPort = 1883;
  strncpy(config.mqttBaseTopic, "watermeter", sizeof(config.mqttBaseTopic) - 1);
}
