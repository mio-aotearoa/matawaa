# Fossil Hoki health sensors (HR / SpO2 / sleep)

Status for matawaa bring-up of community AsteroidOS fitness sensing on
Fossil Gen 6 (`hoki`).

## Hardware (from Wear OS vendor tree)

| Role | Implementation |
|------|----------------|
| Optical PPG | PixArt **PAH8131** / **PAH8137** (SSC I2C slave `21`) |
| Algorithms | **LifeQ** modules inside `vendor/lib/sensors.ssc.so` |
| Host path | Qualcomm **SSC / SLPI** → `sensors.ssc.so` → Android Sensors HAL 1.0 |

Vendor string types observed in `sensors.ssc.so`:

- `android.sensor.heart_rate` (standard type **21**)
- `android.sensor.lifeq_lel_spo2`
- `android.sensor.lifeq_lel_sleep_wake` (+ `lifeq_lel_sac` sleep aggregation)
- `com.google.wear.sensor.ppg` (raw PPG)
- Related LifeQ metrics: calories, RHR, VO2, RR, movement, …

Configs live under `vendor/etc/sensors/config/` (`sdm429w_lw_pah_8131_*`,
`pah_8131_0.pah_8137.LifeQ_*.ppg_ex.json`, `sns_heart_rate.json`).

There is **no** mainline PAH driver and **no** SpO2/sleep DTS nodes; fitness
stays on the proprietary SSC path (same as stock Wear OS).

## Upstream / community software

| Layer | Project | Role |
|-------|---------|------|
| SensorFW hybris HR | [meta-asteroid `0002-…hybrishrm…`](https://github.com/AsteroidOS/meta-asteroid/blob/master/recipes-nemomobile/sensorfw/files/0002-Add-heart-rate-monitor-sensor-with-hybris-adaptor.patch) | Maps HAL type 21 → BPM + contact status |
| Demo UI | [asteroid-hrm](https://github.com/AsteroidOS/asteroid-hrm) | On-demand BPM |
| Logger | [asteroid-sensorlogd](https://github.com/AsteroidOS/asteroid-sensorlogd) | Background HR/steps when display wakes |
| Frontend | [asteroid-health](https://github.com/AsteroidOS/asteroid-health) | Steps + toggle HR logging (HR charts incomplete) |
| Watch page | [asteroidos.org/watches/hoki](https://asteroidos.org/watches/hoki/) | HR **good**, blood oxygen **bad** |
| SSC QMI demo | [1337Misom/python-ssc](https://github.com/1337Misom/python-ssc) | Direct SSC client: **spo2 works** on Gen 6; `fsl_sleep` stub; HR/accel/etc. |

Catfish-style nanohub sleep (always-on MCU) does **not** apply to hoki; sleep
here is LifeQ on SLPI (`lifeq_lel_sleep_wake` / `lifeq_lel_sac` in `sensors.ssc.so`),
plus a Fossil `fsl_sleep` SSC sensor that python-ssc has not implemented yet.

Hybrid Gen 6 SpO2 work in Gadgetbridge is a **different** product line (ePaper
hybrid MCU protocol), not this Wear OS SSC stack.

Vendor HAL type strings present in hoki `sensors.ssc.so` (numeric IDs are
runtime — dump with `test_sensors` on device):

`heart_rate` (21), `lifeq_lel_spo2`, `lifeq_lel_sleep_wake`, `lifeq_lel_sac`,
plus other LifeQ metrics (calories, RHR, VO2, RR, movement, …) and
`com.google.wear.sensor.ppg`.

## Matawaa packaging today

Already in the Asteroid desktop profile:

- `sensorfwd` + hybris adaptors (including `hybrishrmadaptor`)
- Qt Sensors `QHrmSensor` patches
- Apps: `asteroid-hrm`, `asteroid-health`, `asteroid-sensorlogd`
- BLE app-bridge protocol id `org.asteroidos.spo2` **without** a measurement backend
- Opt-in, eight-hour motion recording for host-side analysis with the pinned
  upstream scikit-digital-health pipeline; see [health methods](health-methods.md).
  The former local sleep-stage/score and fertility-date formulas are removed.
  Existing logs remain on disk. Recording is off by default and keeps the CPU awake.

Bring-up fixes in progress:

- `android-init` orders **before `sensorfwd`** (was stale `sensord`)
- `sensorfwd` **after/wants `android-init`**, short pre-sleep for SLPI
- SensorFW CMake plugins must install as `lib*-qt6.so` (Loader suffix);
  without that, `sensorfwd` logs `Plugin not available` for every sensor
  including HR

## Gaps

| Feature | Gap |
|---------|-----|
| Heart rate | End-to-end **unverified** on matawaa images (SSC up? type 21 visible? BPM on wrist?) |
| SpO2 | No SensorFW/Qt adaptor; need private HAL **type id** for `lifeq_lel_spo2`; upstream marks unsupported |
| Sleep | Raw motion recording and host sleep/wake analysis are implemented; no validated Hoki sleep staging or LifeQ sleep adaptor. See [methods and limits](health-methods.md). |
| Persist/calibration | Vendor expects `/persist/sensors` etc.; confirm provisioning under NixOS |

Hybris adaptors key off **numeric** `sensors_event_t.type`. Standard HR is `21`.
LifeQ SpO2/sleep are device-private types (`≥ 0x10000`); dump them on device
before writing adaptors (`scripts/probeHealthSensors.sh`).

## Deploy / probe

```bash
./scripts/deployWatch.py --target 127.0.0.1 --port 2222 --action switch --no-reboot
./scripts/probeHealthSensors.sh
```

`deployWatch.py` uses `nix copy --to ssh://user@host:port` so a localhost SSH
forward cannot silently copy to a local `:22` sshd.

Expect SensorFW plugins named `lib*-qt6.so` (e.g. `libhybrishrmadaptor-qt6.so`).
If `sensorfwd` still logs `Plugin not available`, check that it includes the
`-qt6.so` suffix fix now folded into middleware patch `0017`.

## Recommended next steps

1. Deploy the rebuilt image; confirm `sensorfwd` loads HR plugins.
2. Wear the watch, open **Heart Rate**; confirm BPM (Asteroid community demo).
3. Capture SpO2/sleep **numeric** HAL types from `test_sensors`; add
   `hybrisspo2adaptor` / sleep adaptor patches mirroring meta-asteroid HR.
4. Parallel demo path: package or side-load [python-ssc](https://github.com/1337Misom/python-ssc)
   (AGPL) for on-demand SpO2 over QMI while SensorFW adaptors catch up; sleep
   still needs SSC message RE (`fsl_sleep` / LifeQ sleepwake).
5. Keep SpO2 experimental until LifeQ enable/config (user profile + timeout)
   semantics are wired — python-ssc already shows the required config msgs.
