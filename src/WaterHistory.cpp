#include "WaterHistory.h"

void WaterHistory::begin() {
  memset(hourly, 0, sizeof(hourly));
  memset(daily, 0, sizeof(daily));
  currentHour = 0;
  currentDay = 0;
  hourStarted = millis();
  dayStarted = hourStarted;
  lastTotalMilliM3 = 0;
  haveBaseline = false;
}

void WaterHistory::update(const WaterData& data) {
  if (!data.valid) {
    return;
  }

  rotateBuckets(millis());

  if (!haveBaseline) {
    lastTotalMilliM3 = data.totalMilliM3;
    haveBaseline = true;
    return;
  }

  if (data.totalMilliM3 < lastTotalMilliM3) {
    lastTotalMilliM3 = data.totalMilliM3;
    return;
  }

  uint32_t delta = data.totalMilliM3 - lastTotalMilliM3;
  if (delta == 0) {
    return;
  }

  hourly[currentHour] += delta;
  daily[currentDay] += delta;
  lastTotalMilliM3 = data.totalMilliM3;
}

uint32_t WaterHistory::getHourMilliM3(uint8_t age) const {
  if (age >= HOUR_BUCKETS) {
    return 0;
  }
  uint8_t index = (currentHour + HOUR_BUCKETS - age) % HOUR_BUCKETS;
  return hourly[index];
}

uint32_t WaterHistory::getDayMilliM3(uint8_t age) const {
  if (age >= DAY_BUCKETS) {
    return 0;
  }
  uint8_t index = (currentDay + DAY_BUCKETS - age) % DAY_BUCKETS;
  return daily[index];
}

uint32_t WaterHistory::getTodayMilliM3() const {
  return daily[currentDay];
}

uint32_t WaterHistory::getLast24HoursMilliM3() const {
  uint32_t total = 0;
  for (uint8_t i = 0; i < HOUR_BUCKETS; i++) {
    total += hourly[i];
  }
  return total;
}

uint32_t WaterHistory::getLast31DaysMilliM3() const {
  uint32_t total = 0;
  for (uint8_t i = 0; i < DAY_BUCKETS; i++) {
    total += daily[i];
  }
  return total;
}

void WaterHistory::rotateBuckets(unsigned long now) {
  while (now - hourStarted >= HOUR_MS) {
    hourStarted += HOUR_MS;
    currentHour = (currentHour + 1) % HOUR_BUCKETS;
    hourly[currentHour] = 0;
  }

  while (now - dayStarted >= DAY_MS) {
    dayStarted += DAY_MS;
    currentDay = (currentDay + 1) % DAY_BUCKETS;
    daily[currentDay] = 0;
  }
}
