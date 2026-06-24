-- BWT AQA Life Salt Monitor for Fibaro HC3
-- Reads Multical21 Reader /data.json and estimates remaining softener salt.

local VERSION = "0.1.4"

local function toNumber(value, fallback)
  local n = tonumber(value)
  if n == nil then
    return fallback
  end
  return n
end

local function toBool(value, fallback)
  if value == nil or value == "" then
    return fallback
  end
  value = tostring(value):lower()
  return value == "true" or value == "1" or value == "yes" or value == "on"
end

local function round(value, decimals)
  local factor = 10 ^ (decimals or 0)
  return math.floor(value * factor + 0.5) / factor
end

local function now()
  return os.time()
end

function QuickApp:getVar(name, fallback)
  local value = self:getVariable(name)
  if value == nil or value == "" then
    return fallback
  end
  return value
end

function QuickApp:setVar(name, value)
  self:setVariable(name, tostring(value))
end

function QuickApp:updateLabel(name, text)
  self:updateView(name, "text", tostring(text))
end

function QuickApp:config()
  local cfg = {}
  cfg.waterMeterUrl = self:getVar("waterMeterUrl", "http://multical21.local/data.json")
  cfg.pollSeconds = math.max(30, toNumber(self:getVar("pollSeconds", "300"), 300))
  cfg.staleAfterSeconds = math.max(60, toNumber(self:getVar("staleAfterSeconds", "7200"), 7200))
  cfg.alarmThresholdKg = math.max(0, toNumber(self:getVar("alarmThresholdKg", "5"), 5))
  cfg.warningThresholdKg = math.max(cfg.alarmThresholdKg, toNumber(self:getVar("warningThresholdKg", "10"), 10))
  cfg.pushEnabled = toBool(self:getVar("pushEnabled", "false"), false)
  cfg.pushUserId = toNumber(self:getVar("pushUserId", "0"), 0)
  cfg.rawHardnessDh = toNumber(self:getVar("rawHardnessDh", "25"), 25)
  cfg.targetHardnessDh = toNumber(self:getVar("targetHardnessDh", "6"), 6)
  cfg.saltKgPerRegen = toNumber(self:getVar("saltKgPerRegen", "0.25"), 0.25)
  cfg.capacityMolPerKgSalt = toNumber(self:getVar("capacityMolPerKgSalt", "4.3"), 4.3)
  cfg.m3DhPerMol = toNumber(self:getVar("m3DhPerMol", "5.6"), 5.6)
  cfg.manualSaltKgPerM3 = toNumber(self:getVar("saltKgPerM3", "0"), 0)
  cfg.rinseWaterLiterPerM3 = toNumber(self:getVar("rinseWaterLiterPerM3", "58"), 58)
  return cfg
end

function QuickApp:saltModel(cfg)
  local model = {}
  model.removedDh = math.max(0, cfg.rawHardnessDh - cfg.targetHardnessDh)
  model.capacityM3DhPerRegen = cfg.saltKgPerRegen * cfg.capacityMolPerKgSalt * cfg.m3DhPerMol
  model.capacityM3PerRegen = 0
  model.saltKgPerM3 = 0

  if model.removedDh > 0 and model.capacityM3DhPerRegen > 0 then
    model.capacityM3PerRegen = model.capacityM3DhPerRegen / model.removedDh
    model.saltKgPerM3 = cfg.saltKgPerRegen / model.capacityM3PerRegen
  end

  if cfg.manualSaltKgPerM3 > 0 then
    model.saltKgPerM3 = cfg.manualSaltKgPerM3
    if cfg.saltKgPerRegen > 0 then
      model.capacityM3PerRegen = cfg.saltKgPerRegen / cfg.manualSaltKgPerM3
    end
  end

  return model
end

function QuickApp:loadState()
  self.saltRemainingKg = toNumber(self:getVar("saltRemainingKg", "0"), 0)
  self.lastTotalM3 = toNumber(self:getVar("lastTotalM3", "0"), 0)
  self.lastFillEpoch = toNumber(self:getVar("lastFillEpoch", "0"), 0)
  self.lastAlarmLevel = self:getVar("lastAlarmLevel", "ok")
end

function QuickApp:saveState()
  self:setVar("saltRemainingKg", round(self.saltRemainingKg, 3))
  self:setVar("lastTotalM3", round(self.lastTotalM3, 3))
  self:setVar("lastFillEpoch", self.lastFillEpoch)
  self:setVar("lastAlarmLevel", self.lastAlarmLevel)
end

function QuickApp:setStatus(level, message)
  local value = false
  if level == "alarm" then
    value = true
  end
  self:updateProperty("value", value)
  self:updateProperty("log", message)
  self:updateLabel("lblStatus", message)
end

function QuickApp:notify(level, message)
  local cfg = self:config()
  if self.lastAlarmLevel == level then
    return
  end

  self.lastAlarmLevel = level
  self:saveState()

  if cfg.pushEnabled and cfg.pushUserId > 0 then
    fibaro.alert("push", {cfg.pushUserId}, message)
  end
end

function QuickApp:updateUi(data, model, consumedSinceFill)
  local cfg = self:config()
  local daysLeftText = "-"
  local last24 = toNumber(data.last_24h_m3, 0)
  local dailySalt = last24 * model.saltKgPerM3
  if dailySalt > 0 then
    daysLeftText = tostring(math.floor(self.saltRemainingKg / dailySalt)) .. " days"
  end
  local regenSinceFill = 0
  if cfg.saltKgPerRegen > 0 then
    regenSinceFill = consumedSinceFill / cfg.saltKgPerRegen
  end

  self:updateLabel("lblSalt", string.format("%.1f kg remaining", self.saltRemainingKg))
  self:updateLabel("lblWater", string.format("%.3f m3 total, %.3f m3 last 24h", toNumber(data.total_m3, 0), last24))
  self:updateLabel("lblModel", string.format("%.1f -> %.1f dH, %.0f L/regen, %.3f kg/m3", cfg.rawHardnessDh, cfg.targetHardnessDh, model.capacityM3PerRegen * 1000, model.saltKgPerM3))
  self:updateLabel("lblUsed", string.format("%.1f kg used, %.1f regen since fill", consumedSinceFill, regenSinceFill))
  self:updateLabel("lblEstimate", string.format("Warning %.1f kg, alarm %.1f kg, %s left", cfg.warningThresholdKg, cfg.alarmThresholdKg, daysLeftText))
end

function QuickApp:evaluate(data)
  local cfg = self:config()
  local model = self:saltModel(cfg)
  local totalM3 = toNumber(data.total_m3, nil)

  if not data.valid or totalM3 == nil then
    self:setStatus("warn", "No valid water meter data")
    return
  end

  if data.last_frame_age_s ~= nil and toNumber(data.last_frame_age_s, 0) > cfg.staleAfterSeconds then
    self:setStatus("warn", "Water meter data is stale")
    return
  end

  if self.lastTotalM3 <= 0 then
    self.lastTotalM3 = totalM3
    self:saveState()
  end

  local deltaM3 = totalM3 - self.lastTotalM3
  if deltaM3 < 0 then
    self:warning("Water total went backwards; resetting baseline")
    deltaM3 = 0
  end

  local consumedKg = deltaM3 * model.saltKgPerM3
  if consumedKg > 0 then
    self.saltRemainingKg = math.max(0, self.saltRemainingKg - consumedKg)
    self.lastTotalM3 = totalM3
    self:saveState()
  end

  local usedSinceFillKg = 0
  local fillKg = toNumber(self:getVar("lastFillKg", "0"), 0)
  if fillKg > 0 then
    usedSinceFillKg = math.max(0, fillKg - self.saltRemainingKg)
  end

  self:updateUi(data, model, usedSinceFillKg)

  if self.saltRemainingKg <= cfg.alarmThresholdKg then
    local msg = string.format("BWT salt alarm: %.1f kg estimated remaining", self.saltRemainingKg)
    self:setStatus("alarm", msg)
    self:notify("alarm", msg)
  elseif self.saltRemainingKg <= cfg.warningThresholdKg then
    local msg = string.format("BWT salt warning: %.1f kg estimated remaining", self.saltRemainingKg)
    self:setStatus("warn", msg)
    self:notify("warn", msg)
  else
    self:setStatus("ok", "BWT salt estimate OK")
    self.lastAlarmLevel = "ok"
    self:saveState()
  end
end

function QuickApp:poll()
  local cfg = self:config()
  net.HTTPClient():request(cfg.waterMeterUrl, {
    options = {
      method = "GET",
      timeout = 8000,
      headers = {["Accept"] = "application/json"}
    },
    success = function(response)
      if response.status < 200 or response.status >= 300 then
        self:setStatus("warn", "Water meter HTTP " .. tostring(response.status))
        return
      end

      local ok, data = pcall(json.decode, response.data)
      if not ok or type(data) ~= "table" then
        self:setStatus("warn", "Water meter JSON parse failed")
        return
      end

      self:evaluate(data)
    end,
    error = function(err)
      self:setStatus("warn", "Water meter request failed: " .. tostring(err))
    end
  })
end

function QuickApp:refillSalt(kg)
  kg = toNumber(kg, nil)
  if kg == nil or kg <= 0 then
    self:warning("refillSalt requires kg > 0")
    return
  end

  self.saltRemainingKg = self.saltRemainingKg + kg
  self.lastFillEpoch = now()
  self:setVar("lastFillKg", self.saltRemainingKg)
  self.lastAlarmLevel = "ok"
  self:saveState()
  self:poll()
end

function QuickApp:buttonAdd25()
  self:refillSalt(25)
end

function QuickApp:buttonAdd10()
  self:refillSalt(10)
end

function QuickApp:buttonReset25()
  self.saltRemainingKg = 25
  self.lastFillEpoch = now()
  self:setVar("lastFillKg", 25)
  self.lastAlarmLevel = "ok"
  self:saveState()
  self:poll()
end

function QuickApp:buttonRefresh()
  self:poll()
end

function QuickApp:onSliderRawHardnessChanged(event)
  local value = toNumber(event.values and event.values[1], self:config().rawHardnessDh)
  self:setVar("rawHardnessDh", value)
  self:updateView("sliderRawHardness", "value", tostring(value))
  self:poll()
end

function QuickApp:onSliderTargetHardnessChanged(event)
  local value = toNumber(event.values and event.values[1], self:config().targetHardnessDh)
  self:setVar("targetHardnessDh", value)
  self:updateView("sliderTargetHardness", "value", tostring(value))
  self:poll()
end

function QuickApp:scheduleNextPoll()
  local cfg = self:config()
  fibaro.setTimeout(cfg.pollSeconds * 1000, function()
    self:poll()
    self:scheduleNextPoll()
  end)
end

function QuickApp:onInit()
  self:debug("BWT Salt Monitor " .. VERSION .. " starting, deviceId " .. tostring(self.id))
  self:loadState()

  if self.saltRemainingKg <= 0 then
    self.saltRemainingKg = toNumber(self:getVar("initialSaltKg", "25"), 25)
    self:setVar("lastFillKg", self.saltRemainingKg)
    self.lastFillEpoch = now()
    self:saveState()
  end

  self:updateLabel("lblTitle", "BWT AQA Life Salt Monitor " .. VERSION)
  self:updateView("sliderRawHardness", "value", tostring(self:config().rawHardnessDh))
  self:updateView("sliderTargetHardness", "value", tostring(self:config().targetHardnessDh))
  self:poll()
  self:scheduleNextPoll()
end
