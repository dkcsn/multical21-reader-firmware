#ifndef _WATERHISTORY_H_
#define _WATERHISTORY_H_

#include <Arduino.h>
#include "WaterData.h"

class WaterHistory {
public:
  void begin();
  void update(const WaterData& data);

  uint32_t getHourMilliM3(uint8_t age) const;
  uint32_t getDayMilliM3(uint8_t age) const;
  uint32_t getTodayMilliM3() const;
  uint32_t getLast24HoursMilliM3() const;
  uint32_t getLast31DaysMilliM3() const;

private:
  static const uint8_t HOUR_BUCKETS = 24;
  static const uint8_t DAY_BUCKETS = 31;
  static const unsigned long HOUR_MS = 3600000UL;
  static const unsigned long DAY_MS = 86400000UL;

  uint32_t hourly[HOUR_BUCKETS];
  uint32_t daily[DAY_BUCKETS];
  uint8_t currentHour = 0;
  uint8_t currentDay = 0;
  unsigned long hourStarted = 0;
  unsigned long dayStarted = 0;
  uint32_t lastTotalMilliM3 = 0;
  bool haveBaseline = false;

  void rotateBuckets(unsigned long now);
};

#endif
