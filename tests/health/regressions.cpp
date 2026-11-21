#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>
#include <limits>
#include "common.h"
#include "qmlplugin/sleepDataLoader.h"
#include "qmlplugin/periodDataLoader.h"
#include "qmlplugin/loggerSettings.h"
#include "daemon/sensorPlugins/motionRecording.h"

#define CHECK(condition) do { if (!(condition)) qFatal("Failed: %s at line %d", #condition, __LINE__); } while (false)
int main(int argc, char **argv) {
    QTemporaryDir temp;
    qputenv("XDG_CONFIG_HOME", (temp.path()+"/config").toUtf8());
    qputenv("XDG_DATA_HOME", (temp.path()+"/data").toUtf8());
    QCoreApplication app(argc, argv);
    QSettings settings("asteroid", "sensorlogd");
    settings.setValue("loggerRootPath", temp.path()+"/logs/"); settings.sync();
    settings.setValue("sleepMonitor/enabled", true); settings.sync();
    LoggerSettings recordingSettings;
    CHECK(!recordingSettings.getSleepMonitorEnabled()); // Old heuristics being enabled is not consent to continuous capture.
    recordingSettings.setSleepMonitorEnabled(true);
    CHECK(recordingSettings.getSleepMonitorEnabled());
    settings.sync();
    CHECK(settings.value("sleepCapture/until").toLongLong() <= QDateTime::currentSecsSinceEpoch()+8*3600);
    recordingSettings.setSleepMonitorEnabled(false);
    CHECK(!recordingSettings.getSleepMonitorEnabled());
    settings.sync();
    const QDate day(2026, 11, 21);
    const qint64 startUs = QDateTime(day,QTime(20,0)).toMSecsSinceEpoch()*1000;
    SleepDataLoader loader;
    setupFilePath("sleepMonitor");
    {
        QFile file(fileNameForDate(day,"sleepMonitor")); CHECK(file.open(QIODevice::WriteOnly));
        QTextStream out(&file);
        out << startUs/1000000 << ":2:0:60\n" << startUs/1000000+300 << ":-1:0:0\n" << startUs/1000000+600 << ":2:0:60\ninvalid:2\n";
    }
    setupFilePath("stepCounter");
    const QDateTime past(day.addDays(-1), QTime(23,59,59));
    fileAddRecord("stepCounter", "42", past);
    QFile stepLog(fileNameForDate(past.date(), "stepCounter"));
    CHECK(stepLog.open(QIODevice::ReadOnly));
    CHECK(stepLog.readAll().startsWith(QByteArray::number(past.toSecsSinceEpoch()) + ":42"));
    auto legacy = loader.getSleepSummary(day).toMap();
    CHECK(legacy["legacyRecordCount"].toInt()==3);
    CHECK(!legacy["hasData"].toBool());
    CHECK(!legacy.contains("sleepScore"));
    CHECK(!legacy.contains("totalMinutes"));
    settings.setValue("sleepMonitor/interval",600000);settings.sync();
    CHECK(legacy==loader.getSleepSummary(day).toMap());
    // Actual writer: duplicate, reversed and non-finite readings are rejected;
    // gaps retain timestamps but never earn coverage or duplicate samples.
    {
        MotionRecording record(sensorDirPath("sleepMotion")); CHECK(record.open());
        CHECK(record.append(0,startUs,0,0,9.80665));
        CHECK(record.append(50000,startUs+50000,0,0,9.80665));
        CHECK(record.append(50000,startUs+50000,0,0,9.80665));
        CHECK(record.append(40000,startUs+40000,0,0,9.80665));
        CHECK(record.append(100000,startUs+100000,std::numeric_limits<double>::quiet_NaN(),0,9.80665));
        CHECK(record.append(100000,startUs+100000,0,0,9.80665));
        CHECK(record.append(2000000,startUs+2000000,0,0,9.80665));
        CHECK(record.sampleCount()==4);
        CHECK(record.metadata()["invalidSamples"].toInteger()==3);
        CHECK(record.metadata()["gaps"].toInteger()==1);
        CHECK(record.metadata()["coveredUs"].toInteger()==100000);
        record.close();
        const QString csv=QDir(sensorDirPath("sleepMotion")).filePath(record.metadata()["file"].toString());
        QFile file(csv);CHECK(file.open(QIODevice::ReadOnly));
        const QByteArray data=file.readAll(); CHECK(data.startsWith("timestamp,x,y,z\n"));CHECK(data.split('\n').size()==6);
        CHECK(!(file.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther)));
    }
    {
        MotionRecording clock(temp.path()+"/clock"); CHECK(clock.open());
        CHECK(clock.append(0,startUs,0,0,9.80665));
        CHECK(!clock.append(50000,startUs+10000000,0,0,9.80665));
        CHECK(clock.sampleCount()==1);CHECK(clock.metadata()["coveredUs"].toInteger()==0);
        CHECK(clock.error()=="clock-changed");
    }
    auto summary=loader.getSleepSummary(day).toMap();
    CHECK(summary["sampleCount"].toLongLong()==4); CHECK(summary["gaps"].toLongLong()==1);
    CHECK(summary["recordedSeconds"].toLongLong()==0); // 0.1s coverage, not the two-second wall-clock gap
    CHECK(summary["recordingCount"].toInt()==1);
    settings.setValue("sleepMonitor/interval",60000);settings.sync();
    CHECK(summary==loader.getSleepSummary(day).toMap());
    recordingSettings.setSleepMonitorEnabled(true);
    auto stale=loader.getSleepSummary(day).toMap();
    CHECK(stale["captureEnabled"].toBool());CHECK(!stale["isRecording"].toBool());
    recordingSettings.setSleepMonitorEnabled(false);
    PeriodDataLoader period;
    CHECK(period.metaObject()->indexOfProperty("predictedNextPeriod")==-1);
    period.logPeriodStart(QDate::currentDate().addDays(-10).toString(Qt::ISODate));
    CHECK(period.cycleCount()==1);
    CHECK(period.metaObject()->indexOfProperty("fertileWindowStart")==-1);
    CHECK(period.metaObject()->indexOfProperty("isFertileWindow")==-1);
    period.logPeriodStart(QDate::currentDate().addDays(1).toString(Qt::ISODate));CHECK(period.cycleCount()==1);
    period.logPeriodEnd(QDate::currentDate().addDays(-5).toString(Qt::ISODate));
    PeriodDataLoader reloaded;CHECK(reloaded.cycleCount()==1);CHECK(!reloaded.isPeriodActive());
    reloaded.deleteCycle(0);PeriodDataLoader empty;CHECK(empty.cycleCount()==0);
    const QString periodPath=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/asteroid-sensorlogd/period.json";
    { QFile corrupt(periodPath); CHECK(corrupt.open(QIODevice::WriteOnly|QIODevice::Truncate)); CHECK(corrupt.write("broken history")>0); }
    PeriodDataLoader corrupt;CHECK(!corrupt.storageError().isEmpty());
    corrupt.logPeriodStart(QDate::currentDate().toString(Qt::ISODate));
    QFile preserved(periodPath);CHECK(preserved.open(QIODevice::ReadOnly));CHECK(preserved.readAll()=="broken history");
    QTextStream(stdout)<<"PASS: legacy history, interval invariance, real sample coverage, invalid input, gaps, private recording, manual period persistence and no fertility guesses\n";
}
