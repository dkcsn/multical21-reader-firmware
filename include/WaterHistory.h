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
  bool flush();
  bool clear();
  bool prepareForImport(time_t localNow);
  bool importDailyUsage(uint16_t year, uint8_t month, uint8_t day, uint32_t milliM3);
  void rebuildAggregatesFromDaily();

  uint32_t getHourMilliM3(uint8_t age) const;
  uint32_t getMinuteMilliM3(uint8_t age) const;
  uint32_t getDayMilliM3(uint16_t age) const;
  uint32_t getWeekMilliM3(uint16_t age) const;
  uint32_t getCurrentWeekMilliM3() const;
  uint32_t getMonthMilliM3(uint8_t age) const;
  uint32_t getYearMilliM3(uint8_t age) const;
  uint32_t getTodayMilliM3() const;
  uint32_t getLast24HoursMilliM3() const;
  uint32_t getLast31DaysMilliM3() const;
  uint32_t getLast53WeeksMilliM3() const;
  uint32_t getLast24MonthsMilliM3() const;
  uint32_t getLast10YearsMilliM3() const;
  bool isTimeSynced() const;
  bool wasLoaded() const;
  int32_t currentDayKey() const;
  int32_t currentWeekKey() const;
  int32_t currentMonthKey() const;
  int32_t currentYearKey() const;

private:
  static const uint8_t HOUR_BUCKETS = 24;
  static const uint8_t MINUTE_BUCKETS = 60;
  static const uint16_t DAY_BUCKETS = 1825;
  static const uint16_t WEEK_BUCKETS = 260;
  static const uint8_t MONTH_BUCKETS = 72;
  static const uint8_t YEAR_BUCKETS = 10;
  static const uint32_t HISTORY_MAGIC = 0x57483132;
  static const uint16_t HISTORY_VERSION = 3;
  static const unsigned long SAVE_INTERVAL_MS = 300000UL;

  struct PersistedHistory {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t hourly[HOUR_BUCKETS];
    uint32_t daily[DAY_BUCKETS];
    uint32_t weekly[WEEK_BUCKETS];
    uint32_t monthly[MONTH_BUCKETS];
    uint32_t yearly[YEAR_BUCKETS];
    int32_t currentHourKey;
    int32_t currentDayKey;
    int32_t currentWeekKey;
    int32_t currentMonthKey;
    int32_t currentYearKey;
    uint32_t lastTotalMilliM3;
    bool haveBaseline;
  };

  struct PersistedHistoryV2 {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t hourly[HOUR_BUCKETS];
    uint32_t daily[31];
    uint32_t weekly[53];
    uint32_t monthly[24];
    uint32_t yearly[YEAR_BUCKETS];
    int32_t currentHourKey;
    int32_t currentDayKey;
    int32_t currentWeekKey;
    int32_t currentMonthKey;
    int32_t currentYearKey;
    uint32_t lastTotalMilliM3;
    bool haveBaseline;
  };

  PersistedHistory state;
  uint32_t minute[MINUTE_BUCKETS];
  int32_t currentMinuteKey = -1;
  bool loaded = false;
  bool dirty = false;
  unsigned long lastSaveAttempt = 0;

  void setDefaults();
  bool load();
  bool save();
  void rotateMinuteBuckets(time_t localNow);
  void rotateBuckets(time_t localNow);
};

#endif
