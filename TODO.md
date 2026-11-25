# Matawaa - Smartwatch Issue Tracker & Remaining TODOs

This document catalogs and tracks remaining hardware, runtime, packaging, and architecture issues requiring physical hardware testing, on-device debugging, or ongoing vendor HAL reverse-engineering.

## 0. Packaging and Runtime Follow-ups

* **Fscrypt home-directory provisioning:** `modules/fscrypt.nix` exposes `user` and `policy`, but does not provision an encrypted home. Design and validate login, recovery, existing-data migration, and filesystem handling before enabling it on a device.
* **SensorFW upstream qmake build (fixed 2026-11-25):** Upstream qmake build is active in `0017` + `0064` (`enableBinder = true` on Hoki) with `QT6_INSTALL_LIBDIR=${placeholder "out"}/lib`, installed `primaryuse.conf`, and Qt 6 `QLocalSocket::errorOccurred` fix. Verified live on Fossil Hoki.
* **Cross-built QML module discovery:** Check the remaining target-tool discovery warnings against the installed QML modules on Fossil Hoki. Confirm actual imports on device before treating a warning as a missing module.
* **`deployWatch.py` stale `work/mobile-nixos` trap (fixed 2026-11-24):** `scripts/deployWatch.py` previously preferred `work/mobile-nixos/examples/asteroid` whenever that directory existed on disk, silently deploying stale worktree commits instead of flake `HEAD`. Now builds from the flake by default and requires explicit `--use-worktree` to build `work/mobile-nixos`.

---

## 1. On-Device Issues Requiring Physical Hardware Verification

### 1.1 Hardware Composer Layer Pipe Exhaustion
* **Observed in:** Kernel `dmesg` logs on Qualcomm Snapdragon Wear 4100
* **Log message:** `msm_mdss_get_res_byname: 'vbif_nrt_phys' resource not found`, `mdss_mdp_pipe_init: no 2 type pipes available`
* **Remaining Verification & Resolution Steps:**
  * Verify whether setting `QPA_HWC_FORCE_GLES=1` (set in `20-device.conf`) completely delegates non-root layers to GPU composition.
  * If pipe exhaustion still occurs during multi-app or video playback, evaluate capping the number of HW composer layers in `qt6-qpa-hwcomposer-plugin`.

---

### 1.2 Oppo Watch (`oppo-beluga`) Touch Calibration & Orientation
* **Observed on:** Oppo Watch 46mm (`oppo-beluga`)
* **Remaining Verification Steps:**
  * Flash `oppo-beluga` fastboot images and confirm touch coordinates match visual UI buttons.
  * If touch is offset by 90/180/270 degrees, configure `QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS=rotate=90` (or `invertx`, `inverty`) in `devices/oppo-beluga/default.nix`.

---

### 1.3 Audio Routing & Hardware Mixer Profiles (NXP TFA98xx & Mic)
* **Observed on:** Fossil Gen 6 (`fossil-hoki`)
* **Remaining Verification Steps:**
  * Test speaker audio playback:
    ```sh
    aplay -D default /run/current-system/sw/share/sounds/asteroid/stereo/bell.wav
    ```
  * Test microphone recording:
    ```sh
    arecord -D default -f cd -d 5 /tmp/test-mic.wav && aplay /tmp/test-mic.wav
    ```
  * Build full ALSA Use Case Manager (UCM) or PulseAudio profile configuration files for voice call routing.

---

### 1.4 MSM MDSS Display Frame Commit Warnings (`ctx=0 stopped already`)
* **Observed on:** Fossil Gen 6 (`fossil-hoki`)
* **Symptom:** Kernel `dmesg` logs show repeated messages such as:
  ```
  mdss_mdp_display_commit: ctl 0 error displaying frame
  mdss_mdp_display_commit: ctx=0 stopped already
  ```
* **Next check:** Correlate warnings with visible flashes, HWC presents, fences,
  and display power transitions before classifying them as benign or suppressing
  the logs.

---

### 1.5a Fossil Hoki Ambient ~1 Hz Flash
* **Resolved, 2026-11-22:** The 2026-11-18 ~1 Hz flashing report was caused by `nfcd.service` auto-starting (`wantedBy = [ "multi-user.target" ]`) and polling `pn544` in active display mode, unlike upstream (`SYSTEMD_AUTO_ENABLE = "disable"`). Fixed in `0040` and verified on a clean reboot on battery and charger. See [the investigation record](docs/hoki-display.md#resolved-issue-periodic-bright-flashes-1-hz-in-active-display-mode).
* **Earlier mechanism:** `QPA_HWC2_FORCE_DOZE_UPDATE` briefly brings the panel to full ON for each ambient present; `WallClock.Second` (or sticky `QPA_HWC2_DOZE_UPDATE`) drove presents every second.
* **Fix in tree:** default `FORCE_DOZE_UPDATE` + sticky `DOZE_UPDATE` clearer (`0048`); ambient `WallClock.Minute` (`0050`). Lipstick still wakes ambient once per minute.
* **Verify on device:** compositor env has `FORCE_DOZE_UPDATE=1` and no `DOZE_UPDATE`; ambient face updates on the minute without flashing each second.

---

### 1.5 Fossil Hoki Reboot Hang (Kernel Recursive Fault)
* **Observed on:** Fossil Gen 6 (`fossil-hoki`), Linux `4.14.355-287.768.amzn2`, 2026-11-15
* **Symptom:** After `reboot` / deploy `--reboot`, the display goes black and the watch never finishes power-cycling until a forced reboot. SSH over USB stays down; the local port-forward may still accept TCP but reset during KEX.
* **Log evidence (previous boot journal):**
  * `systemd-logind`: `The system will reboot now!` then normal unit stop (MCE, launcher, bluetooth, …).
  * Kernel oops during teardown: process `(dom-seed)` (likely random-seed / sysfs path) faults in `kernfs_fop_write` / `kernfs_fop_release`.
  * Kernel: `Fixing recursive fault but reboot is needed!`
  * Afterwards logind times out stopping `user-runtime-dir@0.service`; journal continues with PMI/FG messages for several minutes with no clean reboot completion.
* **Technical context:** Vendor 4.14 shutdown race (sysfs/cgroup half-torn-down). Not introduced by the DefaultIPAccounting / timed ConditionUser fixes. SSH-as-root spawning `user@0` may make teardown messier but the hard failure is the kernel fault.
* **Remaining Verification & Resolution Steps:**
  * Reproduce with a clean reboot (no root linger / no `user@0`) and capture `journalctl -b -1` around the oops again.
  * Soft mitigation: teach `deployWatch` to prefer `127.0.0.1`, and after a deadline escalate to `reboot -f` or sysrq-b so deploy does not leave a wedged black screen.
  * Only chase a kernel/sysfs fix if it reproduces often; full vendor-kernel debugging is high cost on this tree.

---

### 1.6 Fossil Hoki Heart Rate / SpO2 / Sleep (SSC + LifeQ)
* **Hardware:** PixArt PAH8131/8137 PPG on Qualcomm SSC/SLPI; LifeQ algorithms in `sensors.ssc.so` expose `android.sensor.heart_rate`, `android.sensor.lifeq_lel_spo2`, `android.sensor.lifeq_lel_sleep_wake` (also `com.google.wear.sensor.ppg`).
* **Upstream AsteroidOS status:** HR = good on [hoki](https://asteroidos.org/watches/hoki/); SpO2 = bad. Community stack: meta-asteroid hybrishrmadaptor → SensorFW → `QHrmSensor` → `asteroid-hrm` / `asteroid-health` / `asteroid-sensorlogd`.
* **Matawaa status:** HR path packaged (`sensorfwd`, hybrishrm, hrm/health/sensorlogd apps). SpO2/sleep have **no** SensorFW/Qt types yet; BLE app-bridge only stubs `org.asteroidos.spo2`.
* **Bring-up in tree:** sensorfwd after android-init (folded into `0031` / `0040`), libgbinder HIDL backend for Hoki (`0064`), `primaryuse.conf` + `QT6_INSTALL_LIBDIR` + Qt 6 `QLocalSocket::errorOccurred` (`0017`), `asteroid-sensorlogd` D-Bus wait for `com.nokia.SensorService` (`0055`), probe script + docs. Verified live on Hoki (2026-11-25): `accelerometersensor`, `hrmsensor`, `stepcountersensor`, and `pressuresensor` load cleanly, and `sensorlogd` records barometer/step logs on cold boot.
* **Community SpO2 WIP:** [python-ssc](https://github.com/1337Misom/python-ssc) reads Gen 6 SpO2 via SSC/QMI (not HAL); `fsl_sleep` still unimplemented there.
* **Remaining Verification & Resolution Steps:**
  * Deploy rebuilt image (`./scripts/deployWatch.py --target <ip>` — defaults to `--action boot --reboot`; avoid `--action switch` on Hoki) then run `./scripts/probeHealthSensors.sh`.
  * Validate overnight on-wrist `sleepMonitor` capture and on-wrist `asteroid-hrm` BPM; run `test_sensors` for SpO2/sleep type IDs.
  * Add hybrisspo2 / hybrissleep SensorFW+Qt adaptors once IDs known; optional python-ssc demo package for SpO2.
  * Expect proprietary LifeQ limits without Wellness glue; sleep needs more SSC RE.

---

### 1.7 Fossil Hoki WCNSS Wi-Fi Firmware Self-Symlink Loop & `wlan_mac.bin` Boot Timeout
* **Observed on:** Fossil Gen 6 (`fossil-hoki`), 2026-11-24
* **Symptoms:**
  * `/vendor/firmware/wlan/prima/{WCNSS_qcom_cfg.ini,WCNSS_qcom_wlan_nv.bin,WCNSS_wlan_dictionary.dat}` became self-referential symlink loops (`Too many levels of symbolic links`).
  * `dmesg` shows a ~62 s boot stall during `modprobe wlan`:
    ```
    firmware wlan!prima!wlan_mac.bin: _request_firmware_load: firmware state wait timeout: rc = -110
    ```
* **Root cause:**
  * In `patches/30-our-customizations/0031-fossil-hoki-add-Gen-6-hardware-firmware-audio-and-family-variants.patch`, `link-dynamic-firmware` symlinks `/lib/firmware/wlan -> /vendor/firmware/wlan`.
  * `wlan-firmware-hoki.sh` then runs `ln_force "/vendor/firmware/wlan/prima/$f" "/lib/firmware/wlan/prima/$f"`, which resolves through `/lib/firmware/wlan` to the same path and overwrites the original links in `/vendor/firmware/wlan/prima/` with links pointing to themselves.
  * The actual target files remain intact at `/vendor/etc/wifi/WCNSS_qcom_cfg.ini` and `/mnt/vendor/persist/{WCNSS_qcom_wlan_nv.bin,WCNSS_wlan_dictionary.dat}`.
* **Fix in tree (2026-11-24):**
  * Updated `0031` so `android-init` skips symlinking `/vendor/firmware/wlan` into `/lib/firmware/wlan`, and `wlan-firmware-hoki.sh` replaces any legacy `/lib/firmware/wlan` symlink with a real directory, links canonical `/vendor/etc/wifi/WCNSS_qcom_cfg.ini` and `/mnt/vendor/persist/{WCNSS_qcom_wlan_nv.bin,WCNSS_wlan_dictionary.dat}`, and heals broken self-symlinks in `/vendor/firmware/wlan/prima/`.
  * Patched `wlan-prima` (`0031` `wlan.nix`) to call `request_firmware_direct` for optional `wlan/prima/wlan_mac.bin` so its absence fails immediately instead of stalling `modprobe wlan` for 60 s.
* **Verified on Hoki (2026-11-24):**
  * After clean reboot into the updated build, `/lib/firmware/wlan` is a real directory, `/lib/firmware/wlan/prima/*` and `/vendor/firmware/wlan/prima/*` resolve cleanly to their canonical targets, and `modprobe wlan` completed in ~4.4 s (`[35.49s]` → `[39.90s]`) with no `wlan_mac.bin` timeout.

---

### 1.8 Fossil Hoki Kernel Deep Suspend Breaks Wi-Fi (`wlan-prima`) & Runs While Charging
* **Observed on:** Fossil Gen 6 (`fossil-hoki`), 2026-11-24 (after `edd9c8a` restored upstream MCE ambient timeouts and enabled `/sys/power/autosleep = mem`)
* **Symptoms:**
  * When the display blanks—even while on the charger at 100% / `Full`—Wi-Fi drops and stays unreachable even after waking the watch until Wi-Fi is re-triggered in Settings.
  * Kernel `suspend_stats` recorded `success: 42`, `fail: 32` (`failed_freeze: 14`, `failed_suspend: 18`), with `last_failed_dev: a000000.qcom,wcnss-wlan` and `alarmtimer`.
* **Root cause (verified from logs & `wlan-prima` source):**
  * Ruled out `WifiPowerManager`: MCE kept `Charger state: on` at `Full`/100% and ConnMan kept `Powered = True`.
  * Instead, MCE allows `/sys/power/autosleep = mem` while charging. On suspend entry, Linux `cfg80211` disconnects `wlan0` (`wpa_supplicant: CTRL-EVENT-DISCONNECTED ... reason=0 locally_generated=1`, no WoWLAN configured), then `wlan-prima`'s `wlan_suspend()` (`CORE/HDD/src/wlan_hdd_dev_pwr.c`) returns `-EPERM` (`-1`, *"not in IMPS/BMPS"*), aborting suspend.
  * ConnMan immediately retries association; ~0.2 s later the next kernel autosleep attempt interrupts the 4-way handshake (`WPA: 4-Way Handshake failed ... reason=WRONG_KEY`, `Interface wlan0 [ wifi ] state is failure`), leaving Wi-Fi stuck disconnected after wake.
  * Note: the missing/looped `WCNSS_qcom_cfg.ini` (see 1.7) also prevented prima from loading its vendor power-save/BMPS/IMPS config.
* **Fix in tree (2026-11-24):**
  * Added `/etc/mce/90-hoki-power.conf` (`0040`) setting `/system/osso/dsm/display/autosuspend_policy=3` (`SUSPEND_POLICY_DISABLE_ON_CHARGER`) so docked charging keeps Wi-Fi/SSH awake without suspend-abort storms.
  * Added lightweight `qInfo()` logs to `WifiPowerManager` (`0052`) for charging/Wi-Fi/timer transitions.
* **Verified on Hoki (2026-11-24):**
  * With `mcetool --set-suspend-policy=disable_on_charger` active on the dock, `/sys/power/autosleep` remained `off` with `Display state: off` and Wi-Fi/SSH stayed responsive.
* **Remaining Verification & Resolution Steps:**
  * Re-test on-battery suspend with the fixed `WCNSS_qcom_cfg.ini` (1.7) so prima's `gEnableImps`/`gEnableBmps`/`gEnableSuspend` settings actually apply, and verify whether on-battery suspend still needs `wlan-prima` / `cfg80211` WoWLAN or Wi-Fi power-off ordering to avoid `WRONG_KEY` / ConnMan `failure`.
  * Verify whether PM660 charge-termination hysteresis (100% → 98%) can flip `Charger state` to `off` while still physically docked (`Charger cable: connected`, `usb/online: 0`), and if so whether `WifiPowerManager` / MCE should also check cable state.

---

### 1.9 Fossil Hoki `asteroid-seconddisplay-sync-time.service` Wakeups & `start-limit-hit`
* **Observed on:** Fossil Gen 6 (`fossil-hoki`), 2026-11-24 / 2026-11-25
* **Symptom:** User journal repeatedly logged `start-limit-hit`, and overnight logs showed `OnClockChange=true` firing `asteroid-seconddisplay-sync-time.service` on every suspend/resume cycle (~873× overnight).
* **Fix in tree (2026-11-25):** Kept `StartLimitIntervalSec = 0` on `asteroid-seconddisplay-sync-time.service` and overrode `asteroid-seconddisplay-sync-time.timer` in `0049` with `OnClockChange = false`, `OnTimezoneChange = true`, `OnBootSec = "30s"`, and `OnCalendar = "hourly"`.
* **Verified on Hoki (2026-11-25):** Timer drop-in override applies cleanly (`OnClockChange=false`, hourly schedule) on cold boot.

---

## 2. Upstream Architecture & Long-Term Roadmap

### 2.1 Mainline Linux Kernel Bringup for Snapdragon Wear 4100 (`fossil-hoki-mainline`)
* **Remaining Hardware Verification & Bringup Work:**
  * Upstream DSI panel driver (Command Mode) for the 416x416 AMOLED display (transition from simplefb to native dynamic DRM modesetting).
  * Native DRM touch input binding for Raydium RM69330 I2C controller.
  * PMIC battery fuel gauge (BMS) and charger drivers.

---

### 2.2 Mainline Linux Kernel Bringup for Snapdragon Wear 3100 (`oppo-beluga-mainline`)
* **Target Overview:**
  * Standalone pure-mainline target using modern Linux 6.18, Freedreno DRM/KMS, simple-framebuffer (402x476 squircle), ramoops pstore console logging, btrfs zstd rootfs, and USB CDC-NCM gadget networking.
* **Remaining Hardware Verification & Bringup Work:**
  * Flash experimental `oppo-beluga-mainline` fastboot images and capture earlycon serial/pstore logs.
  * Verify simple-framebuffer handoff from continuous splash region (`0x83000000`).
  * Port Visionox 402x476 command-mode DSI panel driver to upstream DRM panel subsystem.
  * Native input binding for Cypress TrueTouch Gen5 (cyttsp5) I2C touchscreen controller.

---

### 2.3 WebKitGTK / WPE WebKit Sandbox Hardening
* **Remaining Work:**
  * Investigate if a tailored seccomp filter configuration or unprivileged namespace backport can enable sandboxed WebProcess execution on Linux 4.14 kernels without requiring `WEBKIT_FORCE_SANDBOX=0`.
