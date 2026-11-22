# Hoki display and Extreme battery saver

## Resolved issue: periodic bright flashes (~1 Hz) in active display mode

Root cause identified and fixed on 2026-11-22:

- **Cause**: `nfcd.service` was auto-started via `wantedBy = [ "multi-user.target" ]`
  in `examples/asteroid/asteroid-desktop.nix`, whereas upstream AsteroidOS
  (`meta-asteroid/recipes-nemomobile/nfcd/nfcd_git.bb`) explicitly sets
  `SYSTEMD_AUTO_ENABLE:${PN} = "disable"`.
- With `nfcd` running, `nfcd-mce-plugin` powered on the `pn544` NFC controller
  whenever MCE reported the display `ON` (and powered it off in AOD/`LPM_ON`).
  PN544's autonomous ~1 Hz RF polling bursts caused the active AMOLED panel to
  flash whenever battery/rail voltage was not held up by active charging (both
  while discharging and after charge termination at `Full`), and explained why
  flashing began at ~37s during boot (`nfcd` startup) and persisted even with
  `asteroid-launcher` and `composer@2.1-service` paused (`SIGSTOP`).
- **Fix**: Removed `wantedBy = [ "multi-user.target" ]` from
  `systemd.services.nfcd` (matching upstream). Verified on a clean reboot of
  `/nix/store/gj4cvsyqimx9vmwvddhwx75g0j17svzx-nixos-system-hoki-26.11pre-git`
  (`4.14.216`, `nfcd=inactive`, `asteroid-launcher=active`): no flashing on
  battery or on charger.
- **Prior trials ruled out**: kernel `4.14.216` backports (tested on `4.14.206`),
  duplicate `composer@2.1-service`, `asteroid-launcher` / `WallClock` / HWC
  commits, `bg-wear` / `asteroid-seconddisplayd`, `underclock-hoki` (`lpm_levels`
  and secondary cores), CPU frequency scaling, CPU0 deep idle, `wlan` module /
  ConnMan, `esd_check_enabled`, `msm_cmd_autorefresh_en`, `te_tear_check_en`,
  and brightness / auto-brightness settings.
- **Side note**: `logcat` also showed two `android.hardware.graphics.composer@2.1-service`
  instances racing at boot (`android-hwcomposer.service` and Android `init`'s
  `vendor.hwcomposer-2-1`), which is separate from the flashing issue.

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
