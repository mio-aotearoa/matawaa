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

Updated 2026-11-20 (on `/nix/store/d5dy0gcr3dxgp67scy0m6d2zzn7ahfm4-nixos-system-hoki-26.11pre-git`):
- **Where & when it happens**:
  - **Charger plugged in**: **no flashing**.
  - **On battery (`Discharging`)**: active Main UI (`ON` mode, watchface and
    Settings) and late `bootanimation` (`~37s+`) flash bright ~1 Hz.
  - **Always-On Display (`LPM_ON`, `DOZE_SUSPEND`)** and early `bootanimation`
    (`0–37s`): **no flashing**.
- **Ruled out**:
  - `asteroid-launcher` / `WallClock` / QML / `qt6-qpa-hwcomposer-plugin` and
    `composer@2.1-service` active commits (`SIGSTOP` on `asteroid-launcher` and
    both `composer@2.1-service` instances did not stop flashing).
  - `bg-wear` / Blackghost co-processor and `asteroid-seconddisplayd` (flashed
    with `asteroid-seconddisplayd` masked and with `sidekickgraphics-hal-1-2`
    disabled so `subsys4` stayed `OFFLINING`).
  - `QPA_HWC_SYNC_BEFORE_SET=1`, `esd_check_enabled=N`,
    `msm_cmd_autorefresh_en=1`, `te_tear_check_en=0`, and brightness level
    (flashed at both 10% and 100%; `lcd-backlight/brightness` stayed steady).
  - AP-side display/clock/bus activity while paused: `ftrace` (`mdss:*`,
    `clk:*`, `regulator:*`, `i2c:*`, `spi:*`, `workqueue:*`) showed zero MDP
    commits, `dsi0` clocks off, and zero display regulator/I2C/SPI events.
- **Root-cause conclusion**: With zero AP display/clock/bus activity during
  `SIGSTOP`, and flashing disappearing immediately when external USB charger
  power is connected and returning immediately when unplugged, this is a
  **hardware / PMIC `VSYS` / battery-rail / AMOLED panel** sensitivity on battery
  in active `ON` mode (possibly aggravated by `sensorlogd` spinning ~60–70% CPU
  and `wcnss` load, or `pm-adc5` 1 Hz battery polling).
- **Side note**: `logcat` also showed two `android.hardware.graphics.composer@2.1-service`
  instances racing at boot (`android-hwcomposer.service` and Android `init`'s
  `vendor.hwcomposer-2-1`).
- **Next**: Fix `sensorfwd`/`sensorlogd` ~60–70% CPU spin and test if reducing
  battery current draw/spikes helps, and clean up the duplicate
  `android-hwcomposer` launch.

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
