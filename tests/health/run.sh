#!/usr/bin/env bash
set -euo pipefail
# Takes the fully patched sensorlogd source tree. Use a native Qt SDK, never ARM libraries.
health_source=$(realpath "${1:?usage: run.sh PATCHED_SENSORLOGD_SOURCE}")
health_qt=${HEALTH_QT_PREFIX:-$(qmake -query QT_INSTALL_PREFIX)}
health_build=$(mktemp -d -t matawaa-health-tests-XXXXXX)
trap 'rm -rf "$health_build"' EXIT
for component in sleep period; do
    "$health_qt/libexec/moc" "$health_source/qmlplugin/${component}DataLoader.h" -o "$health_build/moc_${component}.cpp"
done
"$health_qt/libexec/moc" "$health_source/qmlplugin/loggerSettings.h" -o "$health_build/moc_settings.cpp"
health_includes=(-I"$health_source" -I"$health_qt/include" -I"$health_qt/include/QtCore" -I"$health_qt/include/QtDBus")
"${CXX:-c++}" -std=c++17 -fPIC "${health_includes[@]}" \
    "$(dirname "$0")/regressions.cpp" "$health_build/moc_sleep.cpp" "$health_build/moc_period.cpp" "$health_build/moc_settings.cpp" \
    "$health_source/common.cpp" "$health_source/qmlplugin/sleepDataLoader.cpp" \
    "$health_source/qmlplugin/periodDataLoader.cpp" "$health_source/qmlplugin/loggerSettings.cpp" "$health_source/daemon/sensorPlugins/sleepDetector.cpp" \
    -L"$health_qt/lib" -Wl,-rpath,"$health_qt/lib" -lQt6Core -lQt6DBus -o "$health_build/regressions"
"$health_build/regressions"
