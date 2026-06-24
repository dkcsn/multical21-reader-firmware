# Fibaro HC3 BWT Salt Monitor

This QuickApp estimates salt level for a BWT AQA Life softener by reading the
Multical21 Reader local API.

The importable `.fqa` also includes a non-main `Documentation` file with the
calculation model and usage notes, so the explanation is visible inside the HC3
QuickApp editor.

## Data Source

The QuickApp reads:

```text
http://multical21.local/data.json
```

It uses `total_m3` as the source of truth. If the HC3 misses polls, the next
poll catches up by subtracting the previous stored total from the current total.

## BWT AQA Life Reference Values

From the BWT AQA Life manual:

- Maximum salt supply: `25 kg`
- Salt consumption per regeneration: approximately `0.25 kg`
- Regeneration time: approximately `17 min`
- Rinse water per regeneration at 4 bar: approximately `21 L`
- Capacity per kg regenerant: `4.3 mol`
- BWT recommends approximately `6 dH` output hardness

The QuickApp uses a conservative calibrated default for Glostrup/BWT AQA Life:

- Raw water hardness: approximately `25 dH`
- Target output hardness: `6 dH`
- Salt per regeneration: `0.25 kg`
- Capacity per kg salt: `4.3 mol`
- Active salt model: approximately `0.79 kg/m3` at `25 -> 6 dH`
- Estimated regeneration interval: approximately `317 L` at `25 -> 6 dH`
- Rinse water estimate: `58 L/m3`

The model is:

```text
removedDh = rawHardnessDh - targetHardnessDh
capacityM3DhPerRegen = saltKgPerRegen * capacityMolPerKgSalt * 5.6
capacityM3PerRegen = capacityM3DhPerRegen / removedDh
saltKgPerM3 = saltKgPerRegen / capacityM3PerRegen
```

Default values:

- `rawHardnessDh = 25`
- `targetHardnessDh = 6`
- `saltKgPerRegen = 0.25`
- `capacityMolPerKgSalt = 4.3`
- `m3DhPerMol = 5.6`
- `saltKgPerM3 = 0`
- `rinseWaterLiterPerM3 = 58`

The QuickApp UI has sliders for raw hardness and target hardness. If you want
to bypass the hardness model entirely, set `saltKgPerM3` to a value greater than
zero.

With the default factor:

- `25 -> 6 dH`: approximately `317 L/regen` and `0.79 kg/m3`
- `28 -> 6 dH`: approximately `274 L/regen` and `0.91 kg/m3`

## QuickApp Variables

- `waterMeterUrl`: Multical21 Reader JSON URL.
- `pollSeconds`: Poll interval, default `300`.
- `staleAfterSeconds`: Treat meter data as stale after this age, default `7200`.
- `initialSaltKg`: Initial salt estimate, default `25`.
- `saltRemainingKg`: Persisted remaining salt estimate.
- `lastTotalM3`: Persisted water total baseline.
- `alarmThresholdKg`: Alarm threshold, default `5`.
- `warningThresholdKg`: Warning threshold, default `10`.
- `rawHardnessDh`: Incoming water hardness.
- `targetHardnessDh`: Softened/mixed output hardness.
- `saltKgPerRegen`: Salt used per regeneration.
- `capacityMolPerKgSalt`: BWT capacity per kg regenerant.
- `m3DhPerMol`: Conversion factor from mol to m3*dH.
- `saltKgPerM3`: Optional manual override.
- `rinseWaterLiterPerM3`: Informational rinse water estimate.
- `pushEnabled`: `true` to send HC3 push alert.
- `pushUserId`: HC3 user ID for push alert.
- `emailEnabled`: `true` to send HC3 email alert.
- `emailUserId`: HC3 user ID for email alert.
- `emailSubject`: Email subject prefix.

## Calibration

Best calibration flow:

1. Fill a known amount of salt, for example `25 kg`.
2. Press `Reset to 25 kg` in the QuickApp.
3. Let it run until the salt level is visibly low.
4. Compare estimated remaining salt with reality.
5. Adjust `saltKgPerM3` if needed.

## Questions To Calibrate

- Do you normally pour full `25 kg` bags, or smaller amounts?
- At what remaining amount should Fibaro warn you, for example `5 kg`?
- Should it send push/email notifications, and to which HC3 user ID?

## Notifications

The QuickApp is a `com.fibaro.binarySwitch` on purpose:

- `OFF`: no critical salt alarm.
- `ON`: critical salt alarm.

This makes it easy to use the QuickApp as a Fibaro scene trigger. The QuickApp
can also send notifications directly:

- Push: `fibaro.alert("push", {pushUserId}, message)`
- Email: `fibaro.call(emailUserId, "sendEmail", subject, message)`

The email address itself is configured on the HC3 user account. SMS is best
handled by a Fibaro scene or external gateway/service reacting to the binary
switch alarm state.
