#include "WaterHistory.h"

#if defined(ESP8266)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <LittleFS.h>
#endif

static const char* HISTORY_FILE = "/water-history.bin";
static const time_t VALID_TIME_EPOCH = 1600000000;

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
  file.close();

  if (bytes != sizeof(loadedState) ||
      loadedState.magic != HISTORY_MAGIC ||
      loadedState.version != HISTORY_VERSION ||
      loadedState.size != sizeof(PersistedHistory)) {
    return false;
  }

  state = loadedState;
  dirty = false;
  return true;
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

  if (state.currentHourKey < 0 || state.currentDayKey < 0) {
    state.currentHourKey = hourKey;
    state.currentDayKey = dayKey;
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
}
