--[[
BWT AQA Life Salt Monitor - Documentation

Purpose
-------
This Fibaro HC3 QuickApp estimates when the BWT AQA Life softener should be
checked/refilled with salt. The BWT unit has no connected salt sensor, so this
QuickApp uses water consumption from the Multical21 Reader as the source of
truth.

Data Source
-----------
The QuickApp reads:

  http://multical21.local/data.json

It uses total_m3 from the water meter. The total counter is important because
Fibaro does not need to poll at exactly the right moment. If HC3 misses a poll,
the next poll can calculate the difference between the last stored total and the
new total.

Default Polling
---------------
pollSeconds = 300

This means the water meter is read every 5 minutes.

Salt And Regeneration Model
---------------------------
The model is based on BWT AQA Life manual values:

  saltKgPerRegen       = 0.25 kg
  capacityMolPerKgSalt = 4.3 mol per kg regenerant
  m3DhPerMol           = 5.6 m3*dH per mol

The user chooses:

  rawHardnessDh     = incoming water hardness
  targetHardnessDh  = desired outgoing/mixed water hardness

The calculation is:

  removedDh = rawHardnessDh - targetHardnessDh
  capacityM3DhPerRegen = saltKgPerRegen * capacityMolPerKgSalt * m3DhPerMol
  capacityM3PerRegen = capacityM3DhPerRegen / removedDh
  saltKgPerM3 = saltKgPerRegen / capacityM3PerRegen

For Glostrup with 25 dH incoming water and 6 dH target:

  removedDh = 19
  capacityM3DhPerRegen = 0.25 * 4.3 * 5.6 = 6.02 m3*dH
  capacityM3PerRegen = 6.02 / 19 = 0.317 m3
  capacityM3PerRegen = about 317 L per regeneration
  saltKgPerM3 = 0.25 / 0.317 = about 0.79 kg/m3

For comparison, 28 -> 6 dH:

  removedDh = 22
  capacityM3PerRegen = 6.02 / 22 = 0.274 m3
  capacityM3PerRegen = about 274 L per regeneration
  saltKgPerM3 = about 0.91 kg/m3

This explains why "about 270 L per regeneration" fits the 28 -> 6 dH case,
while Glostrup 25 -> 6 dH is closer to 317 L.

GUI
---
The QuickApp UI shows:

  BWT salt estimate OK / warning / alarm
  estimated kg salt remaining
  water meter total and last 24h water usage
  hardness model: raw -> target dH, L/regen and kg/m3
  kg salt used and estimated regenerations since fill
  warning/alarm thresholds and estimated days left

Buttons
-------
Refresh:
  Reads the Multical21 Reader immediately.

Add 25 kg:
  Adds 25 kg to the estimated remaining salt. Use this if you pour in a 25 kg
  bag while there is already some salt left.

Add 10 kg:
  Adds 10 kg to the estimated remaining salt.

Reset to 25 kg:
  Sets the estimate directly to 25 kg. Use this when you want to restart from a
  known full amount.

Sliders
-------
Raw hardness dH:
  Incoming water hardness. Glostrup default is 25 dH.

Target hardness dH:
  Desired outgoing/mixed water hardness. Current target is 6 dH.

Alarms
------
warningThresholdKg defaults to 10 kg.
alarmThresholdKg defaults to 5 kg.

When the alarm threshold is reached, the QuickApp binary switch value becomes
ON. This can be used as a trigger in Fibaro scenes.

Push Notifications
------------------
The QuickApp can send Fibaro push notifications if these variables are set:

  pushEnabled = true
  pushUserId = your HC3 user ID

Email/SMS are best handled by a Fibaro scene reacting to the QuickApp alarm
state, because available delivery channels depend on the local HC3/Fibaro
setup.

Calibration
-----------
This is an estimate, not a physical salt sensor. Best calibration:

  1. Fill a known amount of salt.
  2. Press Reset to 25 kg, or Add 25 kg/Add 10 kg.
  3. Let it run until the salt level is visibly low.
  4. Compare estimated salt remaining with reality.
  5. Adjust saltKgPerRegen, capacityMolPerKgSalt or saltKgPerM3 if needed.

Manual Override
---------------
If saltKgPerM3 is greater than 0, the QuickApp uses that value directly and
bypasses the regeneration/hardness model.

For normal use, keep:

  saltKgPerM3 = 0

]]
