#ifndef _WATERHISTORY_H_
#define _WATERHISTORY_H_

#include <Arduino.h>
#include <time.h>
#include "WaterData.h"

class WaterHistory {
public:
  void begin();
  void update(const WaterData& data, time_t localNow);
  void loop();

  uint32_t getHourMilliM3(uint8_t age) const;
  uint32_t getDayMilliM3(uint8_t age) const;
  uint32_t getTodayMilliM3() const;
  uint32_t getLast24HoursMilliM3() const;
  uint32_t getLast31DaysMilliM3() const;
  bool isTimeSynced() const;
  bool wasLoaded() const;

private:
  static const uint8_t HOUR_BUCKETS = 24;
  static const uint8_t DAY_BUCKETS = 31;
  static const uint32_t HISTORY_MAGIC = 0x57483132;
  static const uint16_t HISTORY_VERSION = 1;
  static const unsigned long SAVE_INTERVAL_MS = 300000UL;

  struct PersistedHistory {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t hourly[HOUR_BUCKETS];
    uint32_t daily[DAY_BUCKETS];
    int32_t currentHourKey;
    int32_t currentDayKey;
    uint32_t lastTotalMilliM3;
    bool haveBaseline;
  };

  PersistedHistory state;
  bool loaded = false;
  bool dirty = false;
  unsigned long lastSaveAttempt = 0;

  void setDefaults();
  bool load();
  bool save();
  void rotateBuckets(time_t localNow);
};

#endif
