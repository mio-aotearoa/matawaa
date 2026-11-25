# Health Methods & References

## Implemented Algorithms

### Heart Rate & Steps (`asteroid-sensorlogd`)
- Uses AsteroidOS SensorFW / Qt Sensors (`QHrmSensor`, `QStepCounterSensor`).
- Reconfiguration destroys previous plugins before creating replacements; step retries stay non-blocking and trigger companion sync only when cumulative steps change.
- **PPG quality filtering** (Bent et al. 2020, *npj Digital Medicine* 3:18; Nelson et al. 2020, *npj Digital Medicine* 3:90): rejects physiologically implausible BPM (`< 30` or `> 220`) and takes the median of a short 3-sample lock-in burst (with timeout fallback) to suppress initial optical motion transients. Updates `lastRecordTime` on timer triggers and clamps backward clock jumps cleanly. HRV (RMSSD) is intentionally not derived from 10-minute scalar BPM readings (Shaffer & Ginsberg 2017, *Front. Public Health*).
- Companion sync (`/summary/today` on `org.asteroidos.health`) sends `latestHr`, `latestSteps`, plus bounded chronological `hrSamples` and changed-step `stepSamples` arrays so multi-hour BLE disconnects preserve intermediate HR readings and walking cadence (Tudor-Locke et al. 2018; Paluch et al. 2022). Gadgetbridge pulls `/summary/today` automatically on BLE connect (`DATA_GET`) and on manual sync (`onFetchRecordedData`), minute-aligns timestamps into `AsteroidOSActivitySample` in a single `runInTx` batch, and converts cumulative daily steps to per-interval deltas without fragmenting `SleepSessionSegmenter`.

### Sleep/Wake Detection (`SleepDetector` / `SleepMonitorPlugin`)
- **Papers**:
  - van Hees VT et al. (2013), *Separating Movement and Gravity Components in an Acceleration Signal and Implications for the Assessment of Human Daily Physical Activity*, PLOS ONE 8(4): e61691 ([doi:10.1371/journal.pone.0061691](https://doi.org/10.1371/journal.pone.0061691)).
  - van Hees VT et al. (2015), *A Novel, Open Access Method to Assess Sleep Duration Using a Wrist-Worn Accelerometer*, PLOS ONE 10(11): e0142533 ([doi:10.1371/journal.pone.0142533](https://doi.org/10.1371/journal.pone.0142533)).
  - van Hees VT et al. (2018), *Estimating sleep parameters using an accelerometer without sleep diary*, Scientific Reports 8:12975 ([doi:10.1038/s41598-018-31266-z](https://doi.org/10.1038/s41598-018-31266-z)).
  - Syed S et al. (2020, 2021), *Scientific Reports* 10:5866 & 11:8832 (non-wear edge & motionless-sleep trade-offs).
  - de Zambotti M et al. (2019), *MSSE*; Chinoy ED et al. (2021), *Sleep* (PSG benchmarks: high sleep sensitivity `>= 0.93`, low-to-moderate wake specificity `0.18–0.54`).
- **Open-source references**: [GGIR](https://github.com/wadpac/GGIR) (`R/detect_nonwear_clipping.R`, `R/HASPT.R`, `R/HASIB.R`, commit `b424aca4`, Apache-2.0); charger-state non-wear fusion inspired by [Bangle.js `sleeplog`](https://github.com/espruino/BangleApps) (MIT).
- **Method**:
  - Runs directly on-watch in C++ (~5 Hz `QAccelerometer`, fixed `m/s²` unit scaling, Linux `CLOCK_BOOTTIME` timestamps anchored at session start, coarse 15 s watchdog).
  - Nightly motion capture defaults to 22:00–10:00 in watch-local time. In Health → Sleep recordings, users can change either time, disable nightly recording, or start or stop a session manually. The first and last minutes classified as sleep give approximate onset and wake times; sleep extending outside the observed window is unknown.
  - **Non-wear detection** matches van Hees (2013) / GGIR `"2023"` (`R/detect_nonwear_clipping.R`): evaluates 60-minute windows (4 contiguous 15-minute blocks with `>= 80%` 5 s epoch coverage) using Welford/Chan variance aggregation. Flags all 4 blocks as non-wear if `>= 2/3` axes have `SD < 13 mg` **OR** `>= 2/3` axes have `range < 50 mg` (plus clipping guard `>= 7.5 g` on `>= 80%` of samples). Additionally, epochs recorded while the watch is on its charger (`/sys/class/power_supply/*/status == Charging|Full`) are immediately flagged as non-wear.
  - Aggregates triaxial readings into 5 s wrist-elevation angle epochs (`angle_z = atan2(mean_z, sqrt(mean_x^2 + mean_y^2)) * 180 / pi`).
  - Detects Sustained Inactivity Bouts (SIB: `< 5°` range over 5 min) and the HDCZA Sleep Period Time (SPT) window (symmetric 5-min rolling median of `|diff(angle_z)|`, 10th percentile × 15 clamped to `[0.13, 0.50]`, blocks `> 30 min`, gaps `< 60 min`, longest block).
  - Outputs and deduplicates per-minute Sleep (`1` → `ActivityKind.SLEEP_ANY` / Health Connect `STAGE_TYPE_SLEEPING`) vs. Awake (`0` → `ActivityKind.AWAKE_SLEEP` / `STAGE_TYPE_AWAKE`) records inside the SPT window when at least nine of twelve epochs are valid (omitting lower-coverage minutes from totals) on both write (`sleepMonitor.cpp`) and read (`sleepDataLoader.cpp`). Syncs run-length-encoded `sleepSegments` for yesterday and today to Gadgetbridge.
  - Legacy 4-field `sleepMonitor/*.log` files remain readable via `getDataForDate()` (`legacy=true`) and are excluded from sleep totals.

### Menstrual Cycle Tracking (`PeriodDataLoader`)
- **Open-source reference & literature**: [drip](https://gitlab.com/bloodyhealth/drip) by Bloody Health (`src/lib/cycle.js`, `src/lib/cycle-length.js`, commit `04e14cf5`, GPL-3.0); Bull JR et al. (2019), *Real-world menstrual cycle characteristics of more than 600,000 menstrual cycles*, npj Digital Medicine 2:83 ([doi:10.1038/s41746-019-0152-7](https://doi.org/10.1038/s41746-019-0152-7)); Li K et al. (2020), *npj Digital Medicine* (skipped/missed period log skew); Grieger JA & Norman RJ (2020), *JMIR*.
- **Method**:
  - Stores manual period start/end/flow/excluded history privately and atomically (`QSaveFile`, `0600`), supports logging past dates (`<= today`), rejects future dates, and refuses to overwrite corrupt history.
  - Computes cycle day (`1..99`, `maxCycleLength = 99`) and cycle-length statistics (min, max, mean, median, unbiased sample SD from unrounded mean), surfacing both mean and median in `PeriodDetailPage.qml` with a warning when a logged cycle exceeds 45 days and a per-cycle exclude toggle so atypical/missed-entry cycles can be excluded from predictions without deleting history.
  - Predicts the next 3 period start windows only after `>= 3` included completed cycles (`<= 99` days): `center = lastStart + round(mean)`, variation `±1` day if `SD < 1.5` else `±2` days (with drip's overlap guard `periodDistance - 5 < variation`).
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
