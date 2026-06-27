#ifndef _DEBUGLOG_H_
#define _DEBUGLOG_H_

#include <Arduino.h>
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ESP32)
  #include <WiFi.h>
#endif

class DebugLog {
public:
  DebugLog();

  void begin(bool enabled);
  void loop();
  bool isEnabled() const;
  bool hasClient();

  size_t print(const String& value);
  size_t print(const char* value);
  size_t print(char value);
  size_t print(int value);
  size_t print(unsigned int value);
  size_t print(long value);
  size_t print(unsigned long value);
  size_t println();
  size_t println(const String& value);
  size_t println(const char* value);
  size_t printf(const char* format, ...);

private:
  WiFiServer server;
  WiFiClient client;
  bool enabled;
  bool started;

  void writeToClient(const uint8_t* data, size_t len);
};

extern DebugLog Debug;

#endif
