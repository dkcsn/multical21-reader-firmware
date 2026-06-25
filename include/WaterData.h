#ifndef _WATERDATA_H_
#define _WATERDATA_H_

#include <Arduino.h>

struct WaterAlarms {
  bool burst = false;
  bool leak = false;
  bool dry = false;
  bool reverse = false;
};

struct WaterData {
  uint32_t totalMilliM3 = 0;
  uint32_t monthStartMilliM3 = 0;
  int8_t waterTemperatureC = 0;
  int8_t ambientTemperatureC = 0;
  int16_t radioRssiDbm = 0;
  bool radioRssiValid = false;
  bool radioPresent = false;
  bool radioStarted = false;
  uint8_t radioPartnum = 0;
  uint8_t radioVersion = 0;
  char radioStatus[64] = "Radio not started";
  WaterAlarms alarms;
  unsigned long lastFrameMillis = 0;
  bool valid = false;

  float totalM3() const {
    return totalMilliM3 / 1000.0f;
  }

  float monthStartM3() const {
    return monthStartMilliM3 / 1000.0f;
  }

  float monthUsageM3() const {
    if (totalMilliM3 < monthStartMilliM3) {
      return 0.0f;
    }
    return (totalMilliM3 - monthStartMilliM3) / 1000.0f;
  }
};

#endif
