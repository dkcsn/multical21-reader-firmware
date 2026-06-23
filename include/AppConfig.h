#ifndef _APPCONFIG_H_
#define _APPCONFIG_H_

#include <Arduino.h>

struct AppConfigData {
  uint32_t magic;
  char wifiSsid[33];
  char wifiPassword[65];
  char mqttHost[129];
  uint16_t mqttPort;
  char mqttUsername[65];
  char mqttPassword[65];
  char mqttBaseTopic[65];
  uint8_t meterId[4];
  uint8_t encryptionKey[16];
};

class AppConfig {
public:
  bool begin();
  bool load();
  bool save();
  void clear();

  AppConfigData& data();
  const AppConfigData& data() const;

  bool hasWifi() const;
  bool hasMqtt() const;
  bool hasMeter() const;

  bool setMeterSerialHex(const String& value);
  bool setEncryptionKeyHex(const String& value);

  String meterSerialHex() const;
  String encryptionKeyHex() const;
  String maskedWifiPassword() const;
  String maskedMqttPassword() const;

private:
  AppConfigData config;
  void setDefaults();
};

#endif
