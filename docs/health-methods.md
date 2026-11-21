# Health recording and method provenance

## What is implemented

HR and steps retain the upstream AsteroidOS SensorFW / Qt Sensors / sensorlogd
path. Logger reconfiguration destroys the previous plugins and their sensor
subscriptions before creating replacements. Settings are flushed before the
D-Bus reconfiguration call. Step acquisition retries stay non-blocking; failed
attempts rearm the interval timer. Step writes trigger companion summaries independently
of HR logging, rather than relying on a sleep/HR callback. Stored timestamps use the requested record
time, including step totals assigned to the preceding day at midnight.

The previous local sleep classifier has been removed from the final patched
source. Its custom weights were not a validated Cole–Kripke implementation.
Light/deep/REM stages and the associated quality score are no longer displayed
or exported as new health estimates. Existing `sleepMonitor/*.log` files are
preserved and remain accessible through the legacy data-loader API, explicitly
marked `legacy`; they do not earn inferred sleep durations. Changing today's
interval setting cannot change historical coverage.

The Sleep page now records motion for analysis. Collection is **off by default**,
even if the former `sleepMonitor/enabled` setting was true. An explicit toggle
starts a session lasting at most eight hours. Stopping or disabling logging
closes the file, stops the sensor and releases the Nemo keepalive CPU wakelock.
Recording keeps the processor awake and can substantially reduce battery life;
the watch UI explains this before starting. It does not poll HR or infer wrist
contact. A recording session is not itself evidence of sleep or of wearing the
watch.

Manual period start/end/flow history remains. Fertile-window dates, ovulation
phase labels, calendar regression and next-period forecasts have been removed.
No default 28-day cycle or five-day period is substituted for missing observations. Existing
history is preserved; new future dates are rejected. Writes are private and
atomic using Qt's QSaveFile. Unreadable history cannot be silently overwritten.
No symptothermal inference is attempted without the observations it requires.

## Recording format and quality

The configured sensorlogd log root (normally `~/.asteroid-sensorlogd/`) contains
`sleepMotion/<session>.csv` and a matching `.json` metadata file, readable only
by their owner. Sessions have unique names and are never appended to old files.
The CSV columns are `timestamp,x,y,z`: UTC seconds with microsecond precision,
and XYZ acceleration in **g**. Qt reports acceleration in m/s²; the recorder
converts using standard gravity, 9.80665 m/s². The requested rate is 20 Hz.
Actual delivered sensor events, not a timer repeatedly reading a cached value,
produce the samples. Qt's monotonic sensor timestamps are anchored to UTC at
the first valid event.

Metadata schema version 2 stores actual sample count, invalid readings, gaps,
start/end timestamps, acquisition status and observed coverage. Coverage sums
only positive adjacent intervals up to three requested sample periods (150 ms).
Larger intervals remain gaps; invalid, duplicate and reversed timestamps are
not turned into inactivity. Wall-clock discontinuities exceeding five seconds relative to the sensor clock terminate the
session rather than silently misdating it. Failed sensor starts, no
samples for ten seconds, failed writes, and less than 128 MiB free space stop
collection. Partial/failed files remain available for diagnosis, not automatic
sleep estimates. Abrupt power loss can leave a partial recording; analysis
checks metadata against CSV counts and timestamps before using it.

## Reused analysis implementation

`nix run .#analyze-sleep` packages **scikit-digital-health 0.17.18**, revision
[`5c65ba36c2cb1993d56a4788e1a8019d9c45b8f2`](https://github.com/pfizer-opensource/scikit-digital-health/tree/5c65ba36c2cb1993d56a4788e1a8019d9c45b8f2)
(MIT). The upstream code is built unmodified. Its sleep tests run during the
package build. The wrapper calls upstream `GetDayWindowIndices` and `Sleep`,
including upstream activity-index preprocessing and sleep/wake rescoring. It
contains no replacement classifier, new fitted coefficients or sleep-stage
model. Analysis runs on the development host; the heavy Python scientific stack
is not added to the watch image.

Copy the CSV and matching metadata from the watch without changing them. For a
completed session during which the watch was worn throughout:

```sh
nix run .#analyze-sleep -- recording.csv \
  --timezone Australia/Canberra --assume-worn --output report.json
```

For a session with known non-wear periods, instead supply `--wear-intervals
wear.csv`. This CSV has a `start,end` header and ordered, non-overlapping UTC
seconds identifying **observed wear** intervals. The tool does not equate an
absent HR sample with taking the watch off. Reports identify the wear assumption.

The wrapper rejects legacy inputs, unfinished/failed sessions, non-finite data,
non-increasing timestamps, metadata mismatches, sparse sampling below 20 Hz and
gaps exceeding three measured sample periods. It preserves the upstream minimum
of six hours and noon-to-noon day windows, using the supplied IANA time zone for
DST. Invalid data is not interpolated into apparent rest. If the upstream engine
cannot identify an analysable sleep window, no report replaces an existing one.
Reports are written privately and atomically, include the exact upstream version,
revision and paper references, and retain the upstream endpoint names.

**Validation limit:** the sensor backend's calibration has not been independently
verified, and the report says so. Recorder and pipeline tests establish software
behavior, not sleep-detection accuracy on Hoki. Check calibration and acquisition
rate on hardware, assess an actual overnight recording against a sleep diary,
and obtain independent reference labels before making staging/accuracy claims.
The watch currently displays recorded motion coverage, not the host's research
estimates. It does not automatically upload motion recordings or analysis reports.

## Literature and alternatives

- Cole RJ et al. (1992), *Automatic sleep/wake identification from wrist activity*,
  [doi:10.1093/sleep/15.5.461](https://doi.org/10.1093/sleep/15.5.461).
  Establishes sleep/wake scoring and rescoring; not four-stage sleep detection.
  Published count-based parameters cannot be relabelled as arbitrary ENMO weights.
- Bai J et al. (2016), *An Activity Index for Raw Accelerometry Data and Its
  Comparison with Other Activity Metrics*,
  [doi:10.1371/journal.pone.0160644](https://doi.org/10.1371/journal.pone.0160644).
  SKDH's existing activity-index implementation supplies the model's input feature.
- van Hees VT et al. (2015), *A Novel, Open Access Method to Assess Sleep Duration
  Using a Wrist-Worn Accelerometer*,
  [doi:10.1371/journal.pone.0142533](https://doi.org/10.1371/journal.pone.0142533),
  and van Hees VT et al. (2018), *Estimating sleep parameters using an accelerometer
  without sleep diary*,
  [doi:10.1038/s41598-018-31266-z](https://doi.org/10.1038/s41598-018-31266-z).
  These inform the upstream sleep-opportunity pipeline. [GGIR](https://github.com/wadpac/GGIR)
  remains an independent reference implementation for comparison; it is not
  claimed to be equivalent to every SKDH default or applicable to sparse old logs.
- Walch O et al. (2019), *Sleep stage prediction with raw acceleration and
  photoplethysmography heart rate data derived from a consumer wearable device*,
  [doi:10.1093/sleep/zsz180](https://doi.org/10.1093/sleep/zsz180), with
  [reference code](https://github.com/ojwalch/sleep_classifiers) and
  [PSG-labelled recordings](https://physionet.org/content/sleep-accel/1.0.0/).
  A future staging evaluation should reuse this work and its richer acquisition
  requirements. Its wake/NREM/REM result is not a drop-in light/deep classifier
  or evidence of performance on Fossil Hoki.
- *Can apps and calendar methods predict ovulation with accuracy?* (2018),
  [PMID:29749274](https://pubmed.ncbi.nlm.nih.gov/29749274/).
  Calendar-only predictions do not establish ovulation. For future symptom-based
  support, prefer [drip](https://gitlab.com/bloodyhealth/drip) and its
  [sympto](https://gitlab.com/bloodyhealth/sympto) library, including their tests
  and required temperature/mucus/cervix observations, over a new date formula.
- [python-ssc](https://github.com/1337Misom/python-ssc) already implements Gen 6
  LifeQ SpO₂ transport and is the preferred integration reference for that work.
  Its Fossil sleep sensor remains unimplemented. No new SpO₂ estimator or claimed
  Fossil sleep backend is added by this change.

## Regression checks

The native tests compile the actual patched sensorlogd loaders and motion writer:

```sh
HEALTH_QT_PREFIX=/path/to/native/qtbase \
  tests/health/run.sh /path/to/fully-patched/asteroid-sensorlogd
nix build .#checks.x86_64-linux.sleep-analysis
```

The C++ cases cover legacy gap handling, interval independence, invalid and
out-of-order readings, coverage across actual gaps, g units, private files,
manual period persistence, future-date rejection, corrupt-history preservation,
and record-time handling at midnight. Python checks cover input validation,
wear intervals, private/atomic reports and a real upstream pipeline invocation.
The latter uses synthetic motion to check integration, not clinical accuracy.
