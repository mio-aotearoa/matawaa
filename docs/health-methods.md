# Health Methods & References

## Implemented Algorithms

### Heart Rate & Steps (`asteroid-sensorlogd`)
- Uses AsteroidOS SensorFW / Qt Sensors (`QHrmSensor`, `QStepCounterSensor`).
- Reconfiguration destroys previous plugins before creating replacements; step retries stay non-blocking and trigger companion sync independently of HR.
- Companion sync (`/summary/today` on `org.asteroidos.health`) sends `latestHr` (`<epoch_sec>:<bpm>`) and cumulative daily `latestSteps` (`<epoch_sec>:<steps>`). Gadgetbridge deduplicates by timestamp and converts cumulative daily steps to per-interval deltas.

### Sleep/Wake Detection (`SleepDetector` / `SleepMonitorPlugin`)
- **Papers**:
  - van Hees VT et al. (2013), *Separating Movement and Gravity Components in an Acceleration Signal and Implications for the Assessment of Human Daily Physical Activity*, PLOS ONE 8(4): e61691 ([doi:10.1371/journal.pone.0061691](https://doi.org/10.1371/journal.pone.0061691)).
  - van Hees VT et al. (2015), *A Novel, Open Access Method to Assess Sleep Duration Using a Wrist-Worn Accelerometer*, PLOS ONE 10(11): e0142533 ([doi:10.1371/journal.pone.0142533](https://doi.org/10.1371/journal.pone.0142533)).
  - van Hees VT et al. (2018), *Estimating sleep parameters using an accelerometer without sleep diary*, Scientific Reports 8:12975 ([doi:10.1038/s41598-018-31266-z](https://doi.org/10.1038/s41598-018-31266-z)).
- **Open-source reference**: [GGIR](https://github.com/wadpac/GGIR) (`R/detect_nonwear_clipping.R`, `R/HASPT.R`, `R/g.sib.det.R`, commit `b424aca4`, Apache-2.0).
- **Method**:
  - Runs directly on-watch in C++ (opt-in, off by default; ~5 Hz `QAccelerometer` matching GGIR's `"2023"` non-wear sampling rate, monotonic elapsed timestamping anchored at session start so NTP/CTS clock jumps cannot corrupt epochs).
  - Applies van Hees (2013) / GGIR non-wear detection before sleep scoring: 60-minute windows in 15-minute increments via exact Welford/Chan variance aggregation; blocks where `>= 2` of 3 axes have `range < 50 mg` **and** `SD < 13 mg` are excluded as non-wear (`nonWearMinutes` recorded in `status.json`) so an unworn watch on a table is not scored as sleep.
  - Aggregates triaxial readings into 5 s wrist-elevation angle epochs (`angle_z = atan2(mean_z, sqrt(mean_x^2 + mean_y^2)) * 180 / pi`), invariant to constant accelerometer scale factors.
  - Detects Sustained Inactivity Bouts (SIB: `< 5°` range over 5 min) and the HDCZA Sleep Period Time (SPT) window (5-min rolling median of 59 `|diff(angle_z)|` values across 60 epochs, 10th percentile × 15 clamped to `[0.13, 0.50]`, blocks `> 30 min`, gaps `< 60 min`, longest block).
  - Outputs per-minute Sleep (`1` → `ActivityKind.SLEEP_ANY` / Health Connect `STAGE_TYPE_SLEEPING`) vs. Awake (`0` → `ActivityKind.AWAKE_SLEEP` / `STAGE_TYPE_AWAKE`) inside the SPT window and syncs run-length-encoded `sleepSegments` across yesterday and today to Gadgetbridge (handling cross-midnight wake-ups and delayed BLE reconnections).
  - Legacy 4-field `sleepMonitor/*.log` files remain readable via `getDataForDate()` (`legacy=true`) and are excluded from sleep totals.

### Menstrual Cycle Tracking (`PeriodDataLoader`)
- **Open-source reference & literature**: [drip](https://gitlab.com/bloodyhealth/drip) by Bloody Health (`src/lib/cycle.js`, `src/lib/cycle-length.js`, commit `04e14cf5`, GPL-3.0); Bull JR et al. (2019), *Real-world menstrual cycle characteristics of more than 600,000 menstrual cycles*, npj Digital Medicine 2:83 ([doi:10.1038/s41746-019-0152-7](https://doi.org/10.1038/s41746-019-0152-7)).
- **Method**:
  - Stores manual period start/end/flow history privately and atomically (`QSaveFile`, `0600`), rejects future dates, and refuses to overwrite corrupt history.
  - Computes cycle day (`1..99`, `maxCycleLength = 99`) and cycle-length statistics (min, max, mean, median, unbiased sample SD), surfacing both mean and median in `PeriodDetailPage.qml` with a warning when a logged cycle exceeds 45 days (possible missed entry).
  - Predicts the next 3 period start windows only after `>= 3` completed cycles (`<= 99` days): `center = lastStart + round(mean)`, variation `±1` day if `SD < 1.5` else `±2` days (with drip's overlap guard `periodDistance - 5 < variation`).
  - Does not claim ovulation or fertile windows from calendar dates alone ([PMID:29749274](https://pubmed.ncbi.nlm.nih.gov/29749274/), Bull et al. 2019); fertility inference requires daily BBT and cervical-mucus observations (see [sympto](https://gitlab.com/bloodyhealth/sympto)).

## Comparison with Existing Scientific Approaches

| Domain | Approach | Reference | Trade-offs vs. Chosen Method |
| --- | --- | --- | --- |
| Sleep | **van Hees (2015, 2018) / GGIR** *(used)* | `doi:10.1371/journal.pone.0142533`, `doi:10.1038/s41598-018-31266-z` | Works on raw 3-axis acceleration without proprietary count calibration; scale-invariant; PSG-validated (UK Biobank); sleep/wake only (no staging) |
| Sleep | Cole–Kripke (1992) / Sadeh (1994) | `doi:10.1093/sleep/15.5.461`, `doi:10.1093/sleep/17.3.201` | Requires calibrated ActiGraph/Motionlogger activity counts (or Bai 2016 Activity Index); applying weights to raw ENMO is invalid |
| Sleep | Walch et al. (2019) | `doi:10.1093/sleep/zsz180`, [sleep_classifiers](https://github.com/ojwalch/sleep_classifiers) | 3-class Wake/NREM/REM staging requires continuous PPG + trained ML models; high overnight battery cost on Fossil Hoki |
| Cycle | **Bloody Health `drip`** *(used)* | [gitlab.com/bloodyhealth/drip](https://gitlab.com/bloodyhealth/drip) | Transparent local statistics, requires `>= 3` cycles, no unsupported calendar ovulation claims |
| Cycle | Symptothermal (`sympto`) | [gitlab.com/bloodyhealth/sympto](https://gitlab.com/bloodyhealth/sympto) | Establishes fertile window only with daily BBT + mucus/cervix inputs not collected by the watch |

## Verification

```sh
HEALTH_QT_PREFIX=/path/to/native/qtbase \
  tests/health/run.sh /path/to/patched/asteroid-sensorlogd
```

On-watch overnight battery draw and Fossil Hoki accelerometer accuracy remain to be validated on hardware.
