# Fibaro HC3 BWT Salt Monitor

This QuickApp estimates salt level for a BWT AQA Life softener by reading the
Multical21 Reader local API.

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
- Active salt model: `0.9 kg/m3`
- Rinse water estimate: `58 L/m3`

The manual/model fallback is:

```text
saltKgPerM3 = (rawHardnessDh - targetHardnessDh) / capacityM3DhPerKg
```

Default values:

- `rawHardnessDh = 25`
- `targetHardnessDh = 6`
- `capacityM3DhPerKg = 24.1`
- `saltKgPerM3 = 0.9`
- `rinseWaterLiterPerM3 = 58`

Because `saltKgPerM3` is set to `0.9`, the calibrated value is used directly.
Set `saltKgPerM3` to `0` to use the fallback hardness formula instead.

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
- `capacityM3DhPerKg`: Capacity model.
- `saltKgPerM3`: Optional manual override.
- `rinseWaterLiterPerM3`: Informational rinse water estimate.
- `pushEnabled`: `true` to send HC3 push alert.
- `pushUserId`: HC3 user ID for push alert.

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
- Should it send push notifications, and to which HC3 user ID?
