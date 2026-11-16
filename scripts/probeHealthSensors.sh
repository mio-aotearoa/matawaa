#!/usr/bin/env bash
#
# probeHealthSensors.sh — Dump health-related sensor bring-up on a connected watch.
#
# Default: root@127.0.0.1:2222 (USB SSH forward). Override with WATCH_HOST / WATCH_PORT.
#
set -euo pipefail

HOST="${WATCH_HOST:-127.0.0.1}"
PORT="${WATCH_PORT:-2222}"
USER="${WATCH_USER:-root}"
SSH=(ssh -p "$PORT" -o ConnectTimeout=8 -o ServerAliveInterval=3 -o ServerAliveCountMax=3
  -o StrictHostKeyChecking=accept-new "$USER@$HOST")

echo "==> target ${USER}@${HOST}:${PORT}"
"${SSH[@]}" 'set +e
echo "=== hostname / system ==="
hostname
readlink /run/current-system

echo "=== units ==="
systemctl is-active android-init sensorfwd
systemctl show sensorfwd -p After -p Wants -p ActiveState --no-pager

echo "=== android sensor-related processes (busybox-safe) ==="
for d in /proc/[0-9]*; do
  cmd=$(tr "\0" " " <"$d/cmdline" 2>/dev/null) || continue
  case "$cmd" in
    *sscrpcd*|*sensors.qti*|*sensors@*|*adsprpcd*|*sensorfwd*)
      echo "${d#/proc/} $cmd"
      ;;
  esac
done

echo "=== sensorfwd journal (last 80) ==="
journalctl -u sensorfwd -b --no-pager 2>/dev/null | tail -80

echo "=== hybris / HAL bits ==="
ls -la /vendor/lib/hw/sensors* /system/lib/hw/sensors* 2>/dev/null
ls /vendor/lib/sensors.ssc.so 2>/dev/null
if test -f /etc/sensorfw/primaryuse.conf; then
  grep -E "hrm|step|wrist|adaptor" /etc/sensorfw/primaryuse.conf
fi

echo "=== SensorFW plugins (Loader wants lib*-qt6.so) ==="
SFW_BIN=$(command -v sensorfwd)
if test -n "$SFW_BIN"; then
  SFW_ROOT=$(dirname "$(dirname "$(readlink -f "$SFW_BIN")")")
  d="$SFW_ROOT/lib/sensord-qt6"
  echo "dir=$d"
  if test -d "$d"; then
    ls "$d" | grep -E "hybris|hrm|step|wrist" || ls "$d" | head -30
  fi
elif test -d /run/current-system/sw/lib/sensord-qt6; then
  d=/run/current-system/sw/lib/sensord-qt6
  echo "dir=$d"
  ls "$d" | grep -E "hybris|hrm|step|wrist" || ls "$d" | head -30
else
  echo "sensord-qt6 plugin dir not found"
fi

echo "=== sensorfwd plugin / HR log lines ==="
journalctl -u sensorfwd -b --no-pager 2>/dev/null | grep -iE "hrm|hybris|plugin|available|heart|error|spo2|sleep" | tail -60

echo "=== D-Bus SensorManager (if sensorfwd up) ==="
busctl --system tree local.SensorManager 2>/dev/null | head -40
busctl --system introspect local.SensorManager /SensorManager 2>/dev/null | head -40

echo "=== libhybris test_sensors (HAL type + name; need android-init) ==="
if command -v test_sensors >/dev/null 2>&1; then
  timeout 12 test_sensors 2>&1 | head -200
else
  echo "test_sensors not in PATH"
fi

echo "=== tip ==="
echo "Wear the watch and launch asteroid-hrm for a live BPM check."
echo "For SpO2 demo outside SensorFW, community python-ssc talks SSC/QMI directly (spo2 works; fsl_sleep stub)."
'
