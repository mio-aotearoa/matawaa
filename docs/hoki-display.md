# Hoki ambient display and MCU timepiece mode

These are separate display paths. Normal ambient mode keeps the NixOS session
available. MCU timepiece mode shuts Linux down; it is not a suspend/wake watchface.

## Normal ambient mode (default)

Match AsteroidOS meta-hoki:

- `QPA_HWC_FORCE_GLES=1`
- `QPA_HWC2_FORCE_DOZE_UPDATE=1` (upstream ambient present workaround)

Lipstick schedules ambient redraws with a realtime alarm at the next minute
boundary (`scheduleAmbientUpdate`: `mktime(...) + 60`), then disables updates
again after a short settle. That is the intended “minute updates” path — not
one present per second.

Launcher `MainScreen.qml` binds `WallClock` to `Minute` while
`displayAmbient` is true (patch `0050` / `ambient-wallclock-minute.patch`).
Leaving it at `Second` with `FORCE_DOZE_UPDATE` produced ~1 Hz full-ON present
flashes whenever ambient updates stayed enabled.

`QPA_HWC2_DOZE_UPDATE=1` remains an opt-in plugin path (ambient presents in
HWC2 `DOZE` then restore `DOZE_SUSPEND`) for experiments; it is not the device
default. Enabling it with a seconds-updating watchface caused ~1 Hz flashes and
worse idle drain.

Hoki-specific MCE policy (`mce-keep-aod.patch`) disables the `LPM_ON` →
`LPM_OFF` proximity blank timeout because this watch has no proximity sensor.
Touch or the power key still wakes the interactive display.

Hoki's MCE package also sets `/system/osso/dsm/proximity/ps_enabled=false`
through `90-hoki-proximity.conf`. The SensorFW proximity stream was repeatedly
reporting covered/uncovered and MCE refused to unblank while covered, even
after the browser was closed. This disables proximity monitoring on Hoki and
uses MCE's uncovered fallback; normal idle blanking and the separate palm and
power-key actions remain available. Other devices retain their existing MCE
defaults. Existing saved MCE settings can override the packaged default.

Upstream ambient implementation:
[meta-asteroid ambient patch](https://github.com/AsteroidOS/meta-asteroid/blob/20a8d379f67038208a32b8b59a3093fac8410c73/recipes-qt/qt6/qt6-qpa-hwcomposer-plugin/0001-Add-ambient-mode-display-support.patch).

System suspend while in ambient is still expected for good multi-day battery;
last long on-device boot recorded **zero** suspend entries. Getting autosleep
working is a separate follow-up from the HWC flag choice.

A oneshot (`hoki-clear-sticky-doze-update`, hardened in `0050`) strips leftover
`QPA_HWC2_DOZE_UPDATE=` lines from every
`/var/lib/environment/compositor/*.conf` drop-in and ensures
`FORCE_DOZE_UPDATE=1` is present in `20-device.conf`, so upgraded watches do not
keep the experimental 1 Hz present path.

## MCU timepiece mode (opt-in, default off)

```nix
services.asteroid-desktop.timepiece.enable = true;
```

Default is **false**. When enabled, the Sidekick `asteroid-seconddisplay`
daemon and sync-time timer are installed/started. Entering timepiece is still
an explicit action:

```sh
asteroid-seconddisplay --enter-timepiece
```

That hands the panel to the BG MCU and **powers Linux off**. Return requires a
hardware boot; this branch does not implement a reversible MCU AOD handoff.
`--prepare-timepiece` reloads BG firmware — not a harmless probe. Nothing enters
timepiece automatically on idle or suspend.

The package pins the upstream author's Hoki branch of
[asteroid-coprocessor PR 3](https://github.com/MagneFire/asteroid-coprocessor/pull/3)
at `976d3432347e2f67d23e3de7baac60a852ae823f` (open draft, not in nightly).

`kernel-timezone` stays enabled whenever the desktop is on (Android HALs read
the kernel offset); it is independent of the timepiece option. Helper source:
[meta-asteroid PR 332](https://github.com/AsteroidOS/meta-asteroid/pull/332).

From a **ceres** session when timepiece is enabled:

```sh
busctl --user get-property org.asteroid.SecondDisplay /Display \
  org.asteroid.SecondDisplay.Display Capabilities
asteroid-seconddisplay --sync-time
```

Capabilities `3` means time synchronization and timepiece mode.
