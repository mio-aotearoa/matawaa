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

    // 3. van Hees (2015, 2018) / GGIR SleepDetector:
    //    Synthetic 8-hour recording: 1h active -> 3h still sleep -> 10m awake arousal -> 3h still sleep -> 50m active
    {
        SleepDetector det;
        const qint64 t0 = dayBaseSec;
        // Scale invariance test: multiply acceleration by 9.80665 (m/s^2) vs 1.0 (g)
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
                // Quiet sleep: < 0.2 deg drift
                angleDeg = 12.0 + 0.1 * std::sin(ep * 0.05);
            }
            const double rad = angleDeg * M_PI / 180.0;
            const double scale = (ep % 2 == 0) ? 9.80665 : 1.0;
            CHECK(det.addSample(sec, scale * std::cos(rad), 0.0, scale * std::sin(rad)));
        }
        // Invalid, zero-vector, and reversed samples are rejected
        CHECK(!det.addSample(t0 + 8 * 3600, std::numeric_limits<double>::quiet_NaN(), 0.0, 9.8));
        CHECK(!det.addSample(t0 + 8 * 3600, 0.0, 0.0, 0.0));
        CHECK(!det.addSample(t0, 1.0, 0.0, 0.0));
        CHECK(det.invalidSampleCount() == 3);
        det.flushCurrentEpoch();

        const SleepAnalysisResult res = det.analyze();
        CHECK(res.valid);
        CHECK(res.hdczaThreshold >= SleepDetector::kHdczaMinThreshold);
        CHECK(res.hdczaThreshold <= SleepDetector::kHdczaMaxThreshold);
        // Expected ~360 min sleep (two 3h blocks) and ~10 min awake inside SPT
        CHECK(res.sleepMinutes >= 340 && res.sleepMinutes <= 370);
        CHECK(res.awakeMinutes >= 5 && res.awakeMinutes <= 20);

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
