#ifndef _FIRMWAREVERSION_H_
#define _FIRMWAREVERSION_H_

#include <Arduino.h>

#ifndef FW_VERSION
  #define FW_VERSION "1.1.2"
#endif

#ifndef FW_GIT_SHA
  #define FW_GIT_SHA "local"
#endif

#ifndef FW_BUILD_DATE
  #define FW_BUILD_DATE __DATE__ " " __TIME__
#endif

static inline String firmwareBoardName() {
#if defined(ESP8266)
  return F("ESP8266");
#elif defined(ESP32)
  return F("ESP32");
#else
  return F("Unknown");
#endif
}

static inline String firmwareVersion() {
  return F(FW_VERSION);
}

static inline String firmwareGitSha() {
  return F(FW_GIT_SHA);
}

static inline String firmwareBuildDate() {
  return F(FW_BUILD_DATE);
}

#endif
