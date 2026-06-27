#include "DebugLog.h"

#include <stdarg.h>

DebugLog Debug;

DebugLog::DebugLog() : server(23), enabled(false), started(false) {
}

void DebugLog::begin(bool shouldEnable) {
  enabled = shouldEnable;
  if (!enabled || started) {
    return;
  }

  server.begin();
  server.setNoDelay(true);
  started = true;
  println("Telnet debug listening on port 23");
}

void DebugLog::loop() {
  if (!enabled || !started) {
    return;
  }

  if (client && !client.connected()) {
    client.stop();
  }

  if (!client || !client.connected()) {
#if defined(ESP8266)
    WiFiClient newClient = server.accept();
#else
    WiFiClient newClient = server.available();
#endif
    if (newClient) {
      client = newClient;
      client.setNoDelay(true);
      client.println();
      client.println("Multical 21 Reader telnet debug");
      client.println("Logs are mirrored from serial output.");
    }
  }
}

bool DebugLog::isEnabled() const {
  return enabled;
}

bool DebugLog::hasClient() {
  return (bool) client && client.connected();
}

size_t DebugLog::print(const String& value) {
  Serial.print(value);
  writeToClient((const uint8_t*) value.c_str(), value.length());
  return value.length();
}

size_t DebugLog::print(const char* value) {
  Serial.print(value);
  size_t len = strlen(value);
  writeToClient((const uint8_t*) value, len);
  return len;
}

size_t DebugLog::print(char value) {
  Serial.print(value);
  writeToClient((const uint8_t*) &value, 1);
  return 1;
}

size_t DebugLog::print(int value) {
  String text(value);
  return print(text);
}

size_t DebugLog::print(unsigned int value) {
  String text(value);
  return print(text);
}

size_t DebugLog::print(long value) {
  String text(value);
  return print(text);
}

size_t DebugLog::print(unsigned long value) {
  String text(value);
  return print(text);
}

size_t DebugLog::println() {
  Serial.println();
  static const char newline[] = "\r\n";
  writeToClient((const uint8_t*) newline, 2);
  return 2;
}

size_t DebugLog::println(const String& value) {
  size_t written = print(value);
  return written + println();
}

size_t DebugLog::println(const char* value) {
  size_t written = print(value);
  return written + println();
}

size_t DebugLog::printf(const char* format, ...) {
  char buffer[192];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  if (len <= 0) {
    return 0;
  }

  size_t writeLen = (size_t) min(len, (int) sizeof(buffer) - 1);
  Serial.print(buffer);
  writeToClient((const uint8_t*) buffer, writeLen);
  return writeLen;
}

void DebugLog::writeToClient(const uint8_t* data, size_t len) {
  if (enabled && client && client.connected()) {
    client.write(data, len);
  }
}
