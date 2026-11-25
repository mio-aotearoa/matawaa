#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>
#include <limits>

#include "common.h"
#include "daemon/sleepWindow.h"
#include "daemon/sensorPlugins/sleepDetector.h"
#include "qmlplugin/loggerSettings.h"
#include "qmlplugin/periodDataLoader.h"
#include "qmlplugin/sleepDataLoader.h"

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) qFatal("Failed: %s at line %d", #condition, __LINE__);       \
    } while (false)

int main(int argc, char **argv)
{
    QTemporaryDir temp;
    qputenv("XDG_CONFIG_HOME", (temp.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (temp.path() + "/data").toUtf8());
    QCoreApplication app(argc, argv);

    QSettings settings("asteroid", "sensorlogd");
    settings.setValue("loggerRootPath", temp.path() + "/logs/");
    settings.sync();

    // 1. Opt-in capture settings & midnight step timestamping
    settings.setValue("sleepMonitor/enabled", true);
    settings.sync();
    LoggerSettings recordingSettings;
    CHECK(recordingSettings.nightlyEnabled());
    CHECK(recordingSettings.sleepStart() == "22:00");
    CHECK(recordingSettings.sleepEnd() == "10:00");
    CHECK(!recordingSettings.saveSleepSchedule(true, "25:00", "10:00"));
    CHECK(!recordingSettings.saveSleepSchedule(true, "22:00", "22:00"));
    CHECK(recordingSettings.saveSleepSchedule(false, "23:15", "09:45"));
    CHECK(!recordingSettings.nightlyEnabled());
    CHECK(recordingSettings.sleepEnd() == "09:45");

    const QTimeZone zone("Australia/Sydney");
    for (const auto date : {QDate(2026, 4, 5), QDate(2026, 10, 4)}) {
        const QDateTime morning(date, QTime(8, 0), zone);
        const auto night = SleepWindow::currentOrNext(morning, QTime(22, 0), QTime(10, 0));
        CHECK(night.start.date() == date.addDays(-1));
        CHECK(night.end == QDateTime(date, QTime(10, 0), zone));
        CHECK(night.start.secsTo(night.end) == (date.month() == 4 ? 13 : 11) * 3600);
        CHECK(SleepWindow::currentOrNext(night.end, QTime(22, 0), QTime(10, 0)).start.date() == date);
        CHECK(SleepWindow::currentOrNext(night.start, QTime(22, 0), QTime(10, 0)).start == night.start);
    }
    CHECK(!recordingSettings.getSleepMonitorEnabled());
    recordingSettings.setSleepMonitorEnabled(true);
    CHECK(recordingSettings.getSleepMonitorEnabled());
    settings.sync();
    CHECK(settings.value("sleepCapture/until").toLongLong() <= QDateTime::currentSecsSinceEpoch() + 8 * 3600);
    recordingSettings.setSleepMonitorEnabled(false);
    CHECK(!recordingSettings.getSleepMonitorEnabled());
    settings.sync();

    const QDate day(2026, 11, 21);
    const qint64 dayBaseSec = QDateTime(day, QTime(0, 0)).toSecsSinceEpoch();
    setupFilePath("stepCounter");
    const QDateTime past(day.addDays(-1), QTime(23, 59, 59));
    fileAddRecord("stepCounter", "42", past);
    QFile stepLog(fileNameForDate(past.date(), "stepCounter"));
    CHECK(stepLog.open(QIODevice::ReadOnly));
    CHECK(stepLog.readAll().startsWith(QByteArray::number(past.toSecsSinceEpoch()) + ":42"));

    // 2. Legacy 4-field sleep logs are preserved as legacy and never promoted into sleepMinutes
    SleepDataLoader loader;
    setupFilePath("sleepMonitor");
    {
        QFile file(fileNameForDate(day, "sleepMonitor"));
        CHECK(file.open(QIODevice::WriteOnly));
        QTextStream out(&file);
        out << dayBaseSec << ":2:0:60\n"
            << dayBaseSec + 300 << ":-1:0:0\n"
            << dayBaseSec + 600 << ":2:0:60\n"
            << "invalid:2\n";
    }
    auto legacy = loader.getSleepSummary(day).toMap();
    CHECK(legacy["legacyRecordCount"].toInt() == 3);
    CHECK(!legacy["hasData"].toBool());
    CHECK(legacy["sleepMinutes"].toInt() == 0);
    CHECK(!legacy.contains("sleepScore"));
    settings.setValue("sleepMonitor/interval", 600000);
    settings.sync();
    CHECK(legacy == loader.getSleepSummary(day).toMap());

    // Awake observations at the edges are outside the estimated sleep times,
    // and duplicate per-minute timestamps are deduplicated (no double-counting).
    {
        QFile file(fileNameForDate(day, "sleepMonitor"));
        CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QTextStream out(&file);
        out << dayBaseSec << ":0\n" << dayBaseSec + 60 << ":1\n"
            << dayBaseSec + 60 << ":1\n"
            << dayBaseSec + 120 << ":1\n" << dayBaseSec + 180 << ":0\n";
    }
    const auto observed = loader.getSleepSummary(day).toMap();
    CHECK(observed["onsetSec"].toLongLong() == dayBaseSec + 60);
    CHECK(observed["wakeSec"].toLongLong() == dayBaseSec + 180);
    CHECK(observed["sleepMinutes"].toInt() == 2);
    CHECK(observed["awakeMinutes"].toInt() == 2);

    // 3. van Hees (2015, 2018) / GGIR SleepDetector:
    //    Synthetic 8-hour recording: 1h active -> 3h still sleep -> 10m awake arousal -> 3h still sleep -> 50m active
    {
        // 3a. Verify angle_z & sleep detection are invariant across m/s^2 and g unit scales.
        for (double scale : {9.80665, 1.0}) {
            SleepDetector det;
            const qint64 t0 = dayBaseSec;
            for (int ep = 0; ep < 8 * 720; ++ep) {
                const qint64 sec = t0 + ep * 5;
                double angleDeg = 10.0;
                if (ep < 720 || ep >= 720 + 6 * 720 + 120) {
                    // Active waking movement: oscillating wrist elevation +/- 35 deg
                    angleDeg = 35.0 * std::sin(ep * 0.7);
                } else if (ep >= 720 + 3 * 720 && ep < 720 + 3 * 720 + 120) {
                    // 10-minute mid-sleep arousal (shorter than 60-min HDCZA gap merge -> stays inside SPT as WASO)
                    angleDeg = 30.0 * std::sin(ep * 0.9);
                } else {
                    // Quiet worn sleep: < 5 deg range over 5 min, with realistic subtle wrist micro-motion
                    // (> 50 mg range AND > 13 mg SD over 60 min on >= 2 axes so van Hees 2013 distinguishes from non-wear)
                    angleDeg = 12.0 + 1.8 * std::sin(ep * 0.015);
                }
                const double rad = angleDeg * M_PI / 180.0;
                const double yMicro = 0.03 * std::sin(ep * 0.23);
                CHECK(det.addSample(sec, scale * std::cos(rad), scale * yMicro, scale * std::sin(rad)));
            }
            if (scale > 5.0) {
                // Invalid, zero-vector, and reversed samples are rejected
                CHECK(!det.addSample(t0 + 8 * 3600, std::numeric_limits<double>::quiet_NaN(), 0.0, 9.8));
                CHECK(!det.addSample(t0 + 8 * 3600, 0.0, 0.0, 0.0));
                CHECK(!det.addSample(t0, 1.0, 0.0, 0.0));
                CHECK(det.invalidSampleCount() == 3);

                // Non-mutating analyze() snapshot must not duplicate/mutate epochs when called mid-bucket
                const qint64 epochsBeforeAnalyze = det.epochCount();
                const SleepAnalysisResult snapRes = det.analyze();
                CHECK(det.epochCount() == epochsBeforeAnalyze);
                CHECK(snapRes.valid);
            }

            det.flushCurrentEpoch();

            const SleepAnalysisResult res = det.analyze();
            CHECK(res.valid);
            CHECK(res.nonWearMinutes == 0);
            CHECK(res.hdczaThreshold >= SleepDetector::kHdczaMinThreshold);
            CHECK(res.hdczaThreshold <= SleepDetector::kHdczaMaxThreshold);
            // Expected ~360 min sleep (two 3h blocks) and ~10 min awake inside SPT
            CHECK(res.sleepMinutes >= 340 && res.sleepMinutes <= 370);
            CHECK(res.awakeMinutes >= 5 && res.awakeMinutes <= 20);

            if (scale > 5.0) {
                // Write validated 2-field log and verify SleepDataLoader reads it cleanly
                QFile file(fileNameForDate(day, "sleepMonitor"));
                CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
                QTextStream out(&file);
                for (const auto &m : res.minutes) {
                    out << m.timestampSec << ":" << m.state << "\n";
                }
                file.close();

                auto summary = loader.getSleepSummary(day).toMap();
                CHECK(summary["hasData"].toBool());
                CHECK(summary["sleepMinutes"].toInt() == res.sleepMinutes);
                CHECK(summary["awakeMinutes"].toInt() == res.awakeMinutes);
                CHECK(summary["legacyRecordCount"].toInt() == 0);
            }
        }

        const qint64 t0 = dayBaseSec;

        SleepDetector sparseNonWear;
        for (int block = 0; block < 4; ++block) {
            const qint64 start = t0 + block * 900;
            for (int sample = 0; sample < 180; ++sample) {
                CHECK(sparseNonWear.addSample(start, 0.0, 0.0, 1.0));
            }
            sparseNonWear.flushCurrentEpoch();
            for (int epoch = 1; epoch < 180; ++epoch) {
                sparseNonWear.addEpoch(start + epoch * 5, 90.0);
            }
        }
        CHECK(sparseNonWear.epochCount() == 720);
        CHECK(sparseNonWear.analyze().nonWearMinutes == 0);

        // Isolated readings on either side of a well-observed hour must leave
        // both sleep-window boundaries at the edges of that hour.
        SleepDetector gapped;
        for (int ep = 0; ep < 2160; ++ep) {
            const bool continuous = ep >= 720 && ep < 1440;
            if (continuous || ep % 73 == 0) {
                gapped.addEpoch(t0 + ep * 5, continuous ? 12.0 : 30.0 * std::sin(ep));
            }
        }
        const auto gappedResult = gapped.analyze();
        CHECK(gappedResult.valid);
        CHECK(gappedResult.sptStartSec == t0 + 3600);
        CHECK(gappedResult.sptEndSec == t0 + 7200);

        SleepDetector interrupted;
        const qint64 sparseMinute = t0 + 4 * 3600;
        for (int ep = 0; ep < 8 * 720; ++ep) {
            const qint64 sec = t0 + ep * 5;
            if (sec > sparseMinute && sec < sparseMinute + 60) continue;
            const bool active = ep < 720 || ep >= 7 * 720;
            interrupted.addEpoch(sec, active ? 35.0 * std::sin(ep * 0.7) : 12.0);
        }
        const SleepAnalysisResult interruptedResult = interrupted.analyze();
        CHECK(interruptedResult.valid);
        CHECK(interruptedResult.sptStartSec < sparseMinute);
        CHECK(interruptedResult.sptEndSec > sparseMinute + 60);
        for (const auto &minute : interruptedResult.minutes) {
            CHECK(minute.timestampSec != sparseMinute);
        }

        // 3b. van Hees (2013) / GGIR non-wear: stationary watch on table for 8h (both m/s^2 and g units)
        for (double scale : {9.80665, 1.0}) {
            SleepDetector nwDet;
            for (int ep = 0; ep < 8 * 720; ++ep) {
                const qint64 sec = t0 + ep * 5;
                // Flat stationary sensor with tiny sensor floor noise (< 1 mg)
                const double noise = 0.0005 * std::sin(ep * 0.7);
                CHECK(nwDet.addSample(sec, scale * (0.01 + noise), scale * (-0.01 - noise), scale * (1.0 + noise)));
            }
            const SleepAnalysisResult nwRes = nwDet.analyze();
            CHECK(!nwRes.valid);
            CHECK(nwRes.sleepMinutes == 0);
            CHECK(nwRes.nonWearMinutes >= 420);
        }

        // 3c. van Hees (2013) OR criterion: a single nightstand vibration bump pushing range > 50 mg
        //     must still classify as non-wear because SD across the hour remains < 13 mg on >= 2 axes.
        {
            SleepDetector bumpedNw;
            bumpedNw.setUnits(SleepDetector::AccelUnits::Gravity);
            for (int ep = 0; ep < 4 * 720; ++ep) {
                const qint64 sec = t0 + ep * 5;
                const double bump = (ep % 720 == 360) ? 0.08 : 0.0;
                CHECK(bumpedNw.addSample(sec, 0.01 + bump, -0.01 - bump, 1.0 + bump));
            }
            const SleepAnalysisResult bumpRes = bumpedNw.analyze();
            CHECK(!bumpRes.valid);
            CHECK(bumpRes.sleepMinutes == 0);
            CHECK(bumpRes.nonWearMinutes >= 230);
        }

        // 3d. Charger-state non-wear fusion (Bangle.js sleeplog / Syed 2020): epochs on charger are non-wear
        //     even if moving slightly or shorter than a 60-minute accelerometer non-wear window.
        {
            SleepDetector chargerDet;
            chargerDet.setUnits(SleepDetector::AccelUnits::MetersPerSecSq);
            for (int ep = 0; ep < 2 * 720; ++ep) {
                const qint64 sec = t0 + ep * 5;
                const double angleDeg = 12.0 + 1.8 * std::sin(ep * 0.015);
                const double rad = angleDeg * M_PI / 180.0;
                CHECK(chargerDet.addSample(sec, 9.80665 * std::cos(rad), 0.3 * std::sin(ep * 0.23), 9.80665 * std::sin(rad), true));
            }
            const SleepAnalysisResult chRes = chargerDet.analyze();
            CHECK(!chRes.valid);
            CHECK(chRes.sleepMinutes == 0);
            CHECK(chRes.nonWearMinutes >= 115);
        }
    }

    // 4. PeriodDataLoader: bloodyhealth/drip port (cycle-length.spec.js & get-predicted-menses.spec.js)
    {
        PeriodDataLoader period;
        // No unsupported calendar fertility/ovulation properties
        CHECK(period.metaObject()->indexOfProperty("fertileWindowStart") == -1);
        CHECK(period.metaObject()->indexOfProperty("isFertileWindow") == -1);

        // Fewer than 3 completed cycles -> no prediction
        const QDate today = QDate::currentDate();
        period.logPeriodStart(today.addDays(-60).toString(Qt::ISODate));
        period.logPeriodStart(today.addDays(-32).toString(Qt::ISODate));
        period.logPeriodStart(today.addDays(-4).toString(Qt::ISODate));
        CHECK(period.cycleCount() == 3);
        CHECK(period.validCycleLengthCount() == 2);
        CHECK(!period.hasPrediction());
        CHECK(period.minCycleLength() == 28);
        CHECK(period.maxCycleLength() == 28);
        CHECK(std::abs(period.meanCycleLength() - 28.0) < 1e-6);
        CHECK(std::abs(period.cycleStdDeviation() - 0.0) < 1e-6);

        // Future dates rejected
        period.logPeriodStart(today.addDays(1).toString(Qt::ISODate));
        CHECK(period.cycleCount() == 3);

        // 3rd completed cycle (lengths: 28, 28, 29 -> mean=28.33, SD=0.58 < 1.5 -> variation = +/-1 day)
        period.logPeriodStart(today.addDays(-89).toString(Qt::ISODate));
        CHECK(period.validCycleLengthCount() == 3);
        CHECK(period.hasPrediction());
        CHECK(period.predictedVariationDays() == 1);
        CHECK(period.predictedPeriodStart() == today.addDays(-4 + 28).toString(Qt::ISODate));
        CHECK(period.predictedWindowStart() == today.addDays(-4 + 27).toString(Qt::ISODate));
        CHECK(period.predictedWindowEnd() == today.addDays(-4 + 29).toString(Qt::ISODate));
        CHECK(period.predictedMenses().size() == 3);

        // High variance cycle (add length 35 -> lengths 28, 28, 29, 35 -> SD > 1.5 -> variation = +/-2 days)
        period.logPeriodStart(today.addDays(-124).toString(Qt::ISODate));
        CHECK(period.validCycleLengthCount() == 4);
        CHECK(period.hasPrediction());
        CHECK(period.predictedVariationDays() == 2);

        // Excluding the atypical 35-day cycle restores tight 28/28/29 statistics without deleting history
        period.toggleExcludeCycle(4); // index 4 = start at today - 124 (the 35-day cycle)
        CHECK(period.cycleCount() == 5);
        CHECK(period.validCycleLengthCount() == 3);
        CHECK(period.predictedVariationDays() == 1);
        period.toggleExcludeCycle(4); // restore
        CHECK(period.validCycleLengthCount() == 4);
        CHECK(period.predictedVariationDays() == 2);

        // Cycle > 99 days excluded per drip maxCycleLength = 99
        period.logPeriodStart(today.addDays(-250).toString(Qt::ISODate));
        CHECK(period.validCycleLengthCount() == 4);

        // End date & persistence check
        period.logPeriodEnd(today.addDays(-1).toString(Qt::ISODate));
        PeriodDataLoader reloaded;
        CHECK(reloaded.cycleCount() == 6);
        CHECK(!reloaded.isPeriodActive());
        CHECK(reloaded.hasPrediction());

        // Corrupt history is preserved and never overwritten
        const QString periodPath =
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/asteroid-sensorlogd/period.json";
        {
            QFile corrupt(periodPath);
            CHECK(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
            CHECK(corrupt.write("broken history") > 0);
        }
        PeriodDataLoader corrupt;
        CHECK(!corrupt.storageError().isEmpty());
        corrupt.logPeriodStart(today.toString(Qt::ISODate));
        QFile preserved(periodPath);
        CHECK(preserved.open(QIODevice::ReadOnly));
        CHECK(preserved.readAll() == "broken history");
    }

    QTextStream(stdout) << "PASS: van Hees (2015/2018) sleep/wake detector, bloodyhealth/drip cycle stats & prediction, legacy isolation, and persistence\n";
    return 0;
}
