# Fossil Hoki Health Sensors

Status for AsteroidOS health sensing on Fossil Gen 6 (`hoki`).

## Hardware

| Role | Implementation |
| --- | --- |
| Optical PPG | PixArt **PAH8131** / **PAH8137** (SSC I2C slave `21`) |
| Algorithms | **LifeQ** modules inside `vendor/lib/sensors.ssc.so` |
| Host path | Qualcomm **SSC / SLPI** → `sensors.ssc.so` → Android Sensors HAL 1.0 |

Observed vendor HAL types: `android.sensor.heart_rate` (21), `android.sensor.lifeq_lel_spo2`, `android.sensor.lifeq_lel_sleep_wake`, `lifeq_lel_sac`, `com.google.wear.sensor.ppg`.

## Packaged Stack

- `sensorfwd` + hybris adaptors (`lib*-qt6.so`, ordered after `android-init`) + Qt Sensors `QHrmSensor`
- `asteroid-hrm`, `asteroid-health`, `asteroid-sensorlogd`
- On-watch sleep/wake detection (van Hees 2015/2018 / GGIR) and menstrual cycle tracking (`bloodyhealth/drip` port) — see [health-methods.md](health-methods.md)

## Open Gaps

| Feature | Status |
| --- | --- |
| Accelerometer / Barometer / Steps | Verified live on Hoki via SensorFW HIDL binder backend (`primaryuse.conf` + `QT6_INSTALL_LIBDIR` + `sensorlogd` D-Bus wait) |
| Heart rate | Packaged via SensorFW hybris binder adaptor (HAL type 21); PPG acquisition starts on Hoki, on-wrist BPM verification pending |
| SpO2 | No SensorFW/Qt adaptor for private HAL type `lifeq_lel_spo2`; reference: [1337Misom/python-ssc](https://github.com/1337Misom/python-ssc) |
| Sleep | Watch-side van Hees (2013/2015/2018) sleep/wake + charger non-wear fusion implemented and synced cleanly to Gadgetbridge (minute-aligned, `DATA_GET` on connect); Phase 2 `sensorfw` HAL `batch()` FIFO latency (`maxReportLatencyNs`) planned to allow AP suspend during overnight capture |

## Probe Commands

```sh
./scripts/deployWatch.py --target 127.0.0.1 --port 2222
./scripts/probeHealthSensors.sh
```
