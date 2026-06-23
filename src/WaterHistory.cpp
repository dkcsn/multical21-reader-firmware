#include "WaterHistory.h"

#if defined(ESP8266)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <LittleFS.h>
#endif

static const char* HISTORY_FILE = "/water-history.bin";
static const time_t VALID_TIME_EPOCH = 1600000000;

struct LegacyHistoryV1 {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t hourly[24];
  uint32_t daily[31];
  int32_t currentHourKey;
  int32_t currentDayKey;
  uint32_t lastTotalMilliM3;
  bool haveBaseline;
};

static int32_t monthKeyFromLocal(time_t localNow) {
  struct tm* tm = gmtime(&localNow);
  if (tm == nullptr) {
    return -1;
  }
  return (tm->tm_year + 1900) * 12 + tm->tm_mon;
}

static int32_t yearKeyFromLocal(time_t localNow) {
  struct tm* tm = gmtime(&localNow);
  if (tm == nullptr) {
    return -1;
  }
  return tm->tm_year + 1900;
}

void WaterHistory::begin() {
  setDefaults();
  if (!LittleFS.begin()) {
#if defined(ESP32)
    LittleFS.begin(true);
#endif
  }
  loaded = load();
}

void WaterHistory::update(const WaterData& data, time_t localNow) {
  if (!data.valid) {
    return;
  }

  rotateBuckets(localNow);

  if (!state.haveBaseline) {
    state.lastTotalMilliM3 = data.totalMilliM3;
    state.haveBaseline = true;
    dirty = true;
    return;
  }

  if (data.totalMilliM3 < state.lastTotalMilliM3) {
    state.lastTotalMilliM3 = data.totalMilliM3;
    dirty = true;
    return;
  }

  uint32_t delta = data.totalMilliM3 - state.lastTotalMilliM3;
  if (delta == 0) {
    return;
  }

  state.hourly[0] += delta;
  state.daily[0] += delta;
  state.weekly[0] += delta;
  state.monthly[0] += delta;
  state.yearly[0] += delta;
  state.lastTotalMilliM3 = data.totalMilliM3;
  dirty = true;
}

void WaterHistory::loop() {
  if (!dirty || millis() - lastSaveAttempt < SAVE_INTERVAL_MS) {
    return;
  }
  save();
}

uint32_t WaterHistory::getHourMilliM3(uint8_t age) const {
  if (age >= HOUR_BUCKETS) {
    return 0;
  }
  return state.hourly[age];
}

uint32_t WaterHistory::getDayMilliM3(uint8_t age) const {
  if (age >= DAY_BUCKETS) {
    return 0;
  }
  return state.daily[age];
}

uint32_t WaterHistory::getWeekMilliM3(uint8_t age) const {
  if (age >= WEEK_BUCKETS) {
    return 0;
  }
  return state.weekly[age];
}

uint32_t WaterHistory::getMonthMilliM3(uint8_t age) const {
  if (age >= MONTH_BUCKETS) {
    return 0;
  }
  return state.monthly[age];
}

uint32_t WaterHistory::getYearMilliM3(uint8_t age) const {
  if (age >= YEAR_BUCKETS) {
    return 0;
  }
  return state.yearly[age];
}

uint32_t WaterHistory::getTodayMilliM3() const {
  return state.daily[0];
}

uint32_t WaterHistory::getLast24HoursMilliM3() const {
  uint32_t total = 0;
  for (uint8_t i = 0; i < HOUR_BUCKETS; i++) {
    total += state.hourly[i];
  }
  return total;
}

uint32_t WaterHistory::getLast31DaysMilliM3() const {
  uint32_t total = 0;
  for (uint8_t i = 0; i < DAY_BUCKETS; i++) {
    total += state.daily[i];
  }
  return total;
}

uint32_t WaterHistory::getLast53WeeksMilliM3() const {
  uint32_t total = 0;
  for (uint8_t i = 0; i < WEEK_BUCKETS; i++) {
    total += state.weekly[i];
  }
  return total;
}

uint32_t WaterHistory::getLast24MonthsMilliM3() const {
  uint32_t total = 0;
  for (uint8_t i = 0; i < MONTH_BUCKETS; i++) {
    total += state.monthly[i];
  }
  return total;
}

uint32_t WaterHistory::getLast10YearsMilliM3() const {
  uint32_t total = 0;
  for (uint8_t i = 0; i < YEAR_BUCKETS; i++) {
    total += state.yearly[i];
  }
  return total;
}

bool WaterHistory::isTimeSynced() const {
  return state.currentHourKey > 0 && state.currentDayKey > 0;
}

bool WaterHistory::wasLoaded() const {
  return loaded;
}

void WaterHistory::setDefaults() {
  memset(&state, 0, sizeof(state));
  state.magic = HISTORY_MAGIC;
  state.version = HISTORY_VERSION;
  state.size = sizeof(PersistedHistory);
  state.currentHourKey = -1;
  state.currentDayKey = -1;
  state.currentWeekKey = -1;
  state.currentMonthKey = -1;
  state.currentYearKey = -1;
  dirty = false;
}

bool WaterHistory::load() {
  if (!LittleFS.exists(HISTORY_FILE)) {
    return false;
  }

  File file = LittleFS.open(HISTORY_FILE, "r");
  if (!file) {
    return false;
  }

  PersistedHistory loadedState;
  size_t bytes = file.read((uint8_t*) &loadedState, sizeof(loadedState));
  if (bytes == sizeof(loadedState) &&
      loadedState.magic == HISTORY_MAGIC &&
      loadedState.version == HISTORY_VERSION &&
      loadedState.size == sizeof(PersistedHistory)) {
    file.close();
    state = loadedState;
    dirty = false;
    return true;
  }

  file.seek(0, SeekSet);
  LegacyHistoryV1 legacy;
  bytes = file.read((uint8_t*) &legacy, sizeof(legacy));
  file.close();
  if (bytes == sizeof(legacy) &&
      legacy.magic == HISTORY_MAGIC &&
      legacy.version == 1 &&
      legacy.size == sizeof(LegacyHistoryV1)) {
    setDefaults();
    memcpy(state.hourly, legacy.hourly, sizeof(legacy.hourly));
    memcpy(state.daily, legacy.daily, sizeof(legacy.daily));
    state.weekly[0] = legacy.daily[0];
    for (uint8_t i = 0; i < 7 && i < 31; i++) {
      state.weekly[0] += i == 0 ? 0 : legacy.daily[i];
    }
    state.monthly[0] = 0;
    for (uint8_t i = 0; i < 31; i++) {
      state.monthly[0] += legacy.daily[i];
    }
    state.yearly[0] = state.monthly[0];
    state.currentHourKey = legacy.currentHourKey;
    state.currentDayKey = legacy.currentDayKey;
    state.currentWeekKey = legacy.currentDayKey >= 0 ? legacy.currentDayKey / 7 : -1;
    state.currentMonthKey = -1;
    state.currentYearKey = -1;
    state.lastTotalMilliM3 = legacy.lastTotalMilliM3;
    state.haveBaseline = legacy.haveBaseline;
    dirty = true;
    return true;
  }

  return false;
}

bool WaterHistory::save() {
  lastSaveAttempt = millis();
  File file = LittleFS.open(HISTORY_FILE, "w");
  if (!file) {
    return false;
  }

  state.magic = HISTORY_MAGIC;
  state.version = HISTORY_VERSION;
  state.size = sizeof(PersistedHistory);
  size_t bytes = file.write((const uint8_t*) &state, sizeof(state));
  file.close();
  dirty = bytes == sizeof(state) ? false : dirty;
  return bytes == sizeof(state);
}

void WaterHistory::rotateBuckets(time_t localNow) {
  if (localNow < VALID_TIME_EPOCH) {
    return;
  }

  int32_t hourKey = localNow / 3600;
  int32_t dayKey = localNow / 86400;
  int32_t weekKey = dayKey / 7;
  int32_t monthKey = monthKeyFromLocal(localNow);
  int32_t yearKey = yearKeyFromLocal(localNow);

  if (state.currentHourKey < 0 || state.currentDayKey < 0) {
    state.currentHourKey = hourKey;
    state.currentDayKey = dayKey;
    state.currentWeekKey = weekKey;
    state.currentMonthKey = monthKey;
    state.currentYearKey = yearKey;
    dirty = true;
    return;
  }

  int32_t hourDelta = hourKey - state.currentHourKey;
  if (hourDelta > 0) {
    uint8_t shifts = hourDelta > HOUR_BUCKETS ? HOUR_BUCKETS : hourDelta;
    for (int8_t i = HOUR_BUCKETS - 1; i >= 0; i--) {
      state.hourly[i] = i >= shifts ? state.hourly[i - shifts] : 0;
    }
    state.currentHourKey = hourKey;
    dirty = true;
  }

  int32_t dayDelta = dayKey - state.currentDayKey;
  if (dayDelta > 0) {
    uint8_t shifts = dayDelta > DAY_BUCKETS ? DAY_BUCKETS : dayDelta;
    for (int8_t i = DAY_BUCKETS - 1; i >= 0; i--) {
      state.daily[i] = i >= shifts ? state.daily[i - shifts] : 0;
    }
    state.currentDayKey = dayKey;
    dirty = true;
  }

  int32_t weekDelta = weekKey - state.currentWeekKey;
  if (state.currentWeekKey < 0) {
    state.currentWeekKey = weekKey;
    dirty = true;
  } else if (weekDelta > 0) {
    uint8_t shifts = weekDelta > WEEK_BUCKETS ? WEEK_BUCKETS : weekDelta;
    for (int8_t i = WEEK_BUCKETS - 1; i >= 0; i--) {
      state.weekly[i] = i >= shifts ? state.weekly[i - shifts] : 0;
    }
    state.currentWeekKey = weekKey;
    dirty = true;
  }

  int32_t monthDelta = monthKey - state.currentMonthKey;
  if (state.currentMonthKey < 0) {
    state.currentMonthKey = monthKey;
    dirty = true;
  } else if (monthDelta > 0) {
    uint8_t shifts = monthDelta > MONTH_BUCKETS ? MONTH_BUCKETS : monthDelta;
    for (int8_t i = MONTH_BUCKETS - 1; i >= 0; i--) {
      state.monthly[i] = i >= shifts ? state.monthly[i - shifts] : 0;
    }
    state.currentMonthKey = monthKey;
    dirty = true;
  }

  int32_t yearDelta = yearKey - state.currentYearKey;
  if (state.currentYearKey < 0) {
    state.currentYearKey = yearKey;
    dirty = true;
  } else if (yearDelta > 0) {
    uint8_t shifts = yearDelta > YEAR_BUCKETS ? YEAR_BUCKETS : yearDelta;
    for (int8_t i = YEAR_BUCKETS - 1; i >= 0; i--) {
      state.yearly[i] = i >= shifts ? state.yearly[i - shifts] : 0;
    }
    state.currentYearKey = yearKey;
    dirty = true;
  }
}
