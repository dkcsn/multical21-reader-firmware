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
- Active salt model: approximately `0.9 kg/m3` at `25 -> 6 dH`
- Salt model factor: `0.0474 kg/m3 per removed dH`
- Rinse water estimate: `58 L/m3`

The model is:

```text
saltKgPerM3 = (rawHardnessDh - targetHardnessDh) * saltKgPerM3PerDh
```

Default values:

- `rawHardnessDh = 25`
- `targetHardnessDh = 6`
- `saltKgPerM3PerDh = 0.0474`
- `saltKgPerM3 = 0`
- `rinseWaterLiterPerM3 = 58`

The QuickApp UI has sliders for raw hardness and target hardness. If you want
to bypass the hardness model entirely, set `saltKgPerM3` to a value greater than
zero.

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
- `saltKgPerM3PerDh`: Calibrated hardness model factor.
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
