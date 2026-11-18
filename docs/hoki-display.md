# Hoki display and Extreme battery saver

## Open issue: periodic bright flashes after 4.14.216 deployment

Recorded 2026-11-18: the user still sees a brighter flash roughly once per
second. Cause unresolved; the existing minute-update workaround is not a
verified fix for this report.

- Running image: `/nix/store/zg6vkq9hw91a41ipl5vcc2ih60dj37bq-nixos-system-hoki-26.11pre-git`.
- Kernel and Wi-Fi module: 4.14.216. Boot ID: `ed47b743-9f2d-405b-9a56-f5ef439f707f`.
- Compositor: zero restarts; no kernel oops found in the collected log.
- Environment: `QPA_HWC2_FORCE_DOZE_UPDATE=1`; experimental `QPA_HWC2_DOZE_UPDATE` absent.
- HWC logs show ambient presents switching briefly to ON, then DOZE_SUSPEND.
  A fresh 18-second capture did not establish a repeating one-second transition.
- Temporary `mcetool --unblank-screen` / `--blank-prevent` reached ON. Whether
  that stops the visible flashing is unconfirmed. No permanent setting changed.
- Wi-Fi disappeared intermittently while idle. Suspend stats recorded six
  successes and eight failures, including wcnss-wlan failures. SensorFW repeatedly
  failed `hw_get_module()` and restarted; its connection to flashing is unknown.

Last SSH address: `root@192.168.8.183` over Wi-Fi.
Logs: `/tmp/hoki-216-debug` on the development host (temporary). Previous boot
image: `/var/lib/hoki-kernel-debug/pre-216-boot.img` on the watch.
Next: capture timestamped HWC transitions and panel brightness during visible
flashing; compare awake/ambient mode, then isolate suspend/resume.

## Normal ambient mode

Defaults match upstream Hoki: `QPA_HWC_FORCE_GLES=1` and
`QPA_HWC2_FORCE_DOZE_UPDATE=1`. Lipstick wakes ambient updates on minute
boundaries; `WallClock.Minute` prevents a seconds-updating clock from repeatedly
triggering the full-ON workaround. `hoki-clear-sticky-doze-update` removes the
experimental `DOZE_UPDATE` flag from persistent compositor drop-ins.

MCE proximity monitoring defaults off because the sensor stream falsely
reported covered and blocked unblanking. Idle blanking, palm and power-key
handling remain available. Saved MCE settings can override this default.

## Extreme battery saver app

Enabled on Hoki by default. To remove the app and Sidekick services:

```nix
services.asteroid-desktop.timepiece.enable = false;
```

Opening the app only checks capabilities. **Continue → Start timepiece mode**
explicitly hands the clock to the MCU and powers Linux off. Apps, notifications,
Linux alarms, Wi-Fi, and Bluetooth stop. Hold the power button to boot the normal
watch again. There is no automatic idle handoff.

The app shows unavailable-backend and handoff errors. Its mock-daemon and UI
startup tests pass; its real hardware handoff still needs validation. The backend
remains the unmerged [Hoki timepiece PR](https://github.com/MagneFire/asteroid-coprocessor/pull/3),
pinned to `976d3432347e2f67d23e3de7baac60a852ae823f`.

From a `ceres` session, query support without entering timepiece mode:

```sh
busctl --user get-property org.asteroid.SecondDisplay /Display \
  org.asteroid.SecondDisplay.Display Capabilities
```

Capabilities `3` means time synchronization and timepiece mode. The CLI
`asteroid-seconddisplay --enter-timepiece` performs the same handoff;
`--prepare-timepiece` reloads MCU resources and is not a harmless probe.
`kernel-timezone` remains enabled independently for Android HALs.

## Wearing wrist

First-run setup asks left/right wrist after language selection. Left selects
0°, right selects 180°. Existing users can change **Settings → Display → Rotate
by 180°**. Both use `/desktop/asteroid/display-rotation`; the upstream nightstand
policy can override it while the orientation sensor has a valid reading.
