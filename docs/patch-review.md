# Patch review — 2026-11-09

This review covers the 43-patch baseline and the follow-up fixes exported after it. It checks upstream recipe parity, Nix packaging, dependency declarations, and reproducible patch application. A successful build is not hardware validation or a claim that the entire downstream tree is ready for nixpkgs submission.

## Upstream references

The baseline pins AsteroidOS's then-current `meta-asteroid` revision, `c84c0bbd731f9dfa3b04295bac239dd7fa64c722`. Its existing source pins for qml-asteroid, mapplauncherd, bluebinder, and the virtual keyboard match the recipes at that revision. Updating those pins would not address the packaging differences found here.

- [qml-asteroid recipe](https://github.com/AsteroidOS/meta-asteroid/blob/c84c0bbd731f9dfa3b04295bac239dd7fa64c722/recipes-asteroid/qml-asteroid/qml-asteroid_git.bb)
- [Qt launch-cache recipe](https://github.com/AsteroidOS/meta-asteroid/blob/c84c0bbd731f9dfa3b04295bac239dd7fa64c722/recipes-nemomobile/mapplauncherd/mapplauncherd-qt_git.bb)
- [bluebinder recipe](https://github.com/AsteroidOS/meta-asteroid/blob/c84c0bbd731f9dfa3b04295bac239dd7fa64c722/recipes-core/bluebinder/bluebinder.bb)
- [machine-config recipe](https://github.com/AsteroidOS/meta-asteroid/blob/c84c0bbd731f9dfa3b04295bac239dd7fa64c722/recipes-asteroid/asteroid-machine-config/asteroid-machine-config_1.0.bb)

## Findings fixed

| Finding | Correction |
| --- | --- |
| `qml-asteroid` removed its actual launch-cache dependency and replaced `MDeclarativeCache` with inline stub functions. It also installed CMake modules claiming unavailable dependencies were found. | Package upstream `mapplauncherd-qt` at AsteroidOS's exact source pin and build its original qmake projects. Restore upstream source and CMake dependency discovery. Propagate the real dependency to consumers such as alarmclock. Remove the fake cache headers and `FindTimed` replacement. |
| The app builder accepted arbitrary attributes but discarded `postBuild` and `postInstall`. Hackwatch consequently lacked its keyboard helper. | Forward the supported hooks, compose `postFixup`, and reject unsupported arguments. Verify that the resulting hackwatch output contains `keyboard_interface`. |
| The app builder used all of `qml-asteroid` as a native tool dependency. | Package the unchanged upstream desktop-generator shell script separately for the build platform, with declared runtime tools. Keep `qml-asteroid` as the target library dependency. |
| Bluebinder bypassed its Makefile and hard-coded compiler, linker, and dependency flags. | Retain upstream's Makefile; make its pkg-config command configurable and select the cross wrapper explicitly. Let the compiler wrapper supply the Nix build flags. |
| Seven MCE patches and mapplauncherd's Sailjail-query patch duplicated upstream payloads. | Verify byte-for-byte equality and reference the files in the existing fixed-output `meta-asteroid` source. Remove the duplicate files. |
| Keyboard packaging reran `rime_deployer`, passed directory arguments to `--add-schema` as schema names, and ignored deployment/copy failures. | Reuse nixpkgs' already compiled `rime-data`, including its `build` directory. Remove the redundant native deployer and ignored failure paths. |
| `machine.conf` used manual INI interpolation and hard-coded Wi-Fi capability. | Generate the upstream sections and keys with `pkgs.formats.ini`; expose `hasWlan`, constrain gesture fractions to 0–1, and use integer division for the default screen radius. |
| Package metadata misstated qml-asteroid, mapplauncherd, and keyboard licenses. | Match the pinned upstream recipes: LGPL-2.1-only, LGPL-2.1-or-later, and GPL-3.0-only respectively. |
| The ELF audit could falsely report a library missing when `grep -q` caused its upstream `cut` to exit with SIGPIPE under `pipefail`. | Reproduce the failure (exit 141), build a SONAME file once, and search that file directly. The corrected audit passes for the restored cache components. |
| Overlay expressions contained unused dependencies/lambdas, legacy hash spellings, and substitutions that could silently miss their targets. | Remove unused bindings, normalize fixed-output hashes to SRI, use failing substitutions, and format with nixfmt. Check the entire AsteroidOS overlay with deadnix and statix. |
| Keepalive, libiodata, and timed replaced their upstream Qt 6 qmake projects with handwritten CMake. | Build the pinned upstream qmake projects with the cross Qt toolchain, reuse AsteroidOS's exact keepalive patches, and adjust install destinations for Nix. This restores qmake feature metadata and upstream library ABI while removing 331 lines from the patch series. |

## Scope of alignment

| Baseline patches | Treatment |
| --- | --- |
| 0001–0012: pinning, boot, modules, filesystems, kernel compatibility | Keep NixOS integration. Review watch/module generation separately from boot-sensitive behavior. |
| 0013–0019: Qt, graphics, middleware, launcher infrastructure | Keep the pinned vendor Qt and hardware adaptations. Restore real upstream launch-cache behavior and remove duplicate upstream payloads. |
| 0020–0026: sync, keyboards, apps, browser, Telegram, Wikipedia | Preserve intentional project features: phone input, companion camera control, streaming, and additional apps. Fix the shared packaging boundary rather than deleting those features to reduce the diff. |
| 0027–0029: old-systemd and shared device integration | Preserve kernel compatibility work; rebuilding does not prove old-kernel runtime behavior. |
| 0030–0038: vendor and experimental mainline devices | Preserve hardware source pins, firmware inputs, DTBs/DTS files, and device-specific fixes. No blanket kernel or firmware upgrade without device testing. |
| 0039–0043: installation, desktop, boot ordering | Preserve intentional NixOS deployment and boot integration. Exercise the resulting Fossil Hoki system build. |
| Keyboard availability is folded into patch 0021 and the camera QML resource fix is folded into patch 0022. These changes remain functional diffs, without patch-only whitespace cleanup. |

## Remaining findings and validation limits

1. **Fscrypt configuration is incomplete.** `modules/fscrypt.nix` exposes `user` and `policy`, but neither controls directory provisioning. Enabling the module runs filesystem setup and PAM integration; it does not encrypt the named user's home directory. Do not describe that option as an established encrypted-home implementation. A migration design must handle existing data, login, recovery, and the actual home filesystem before enabling encryption on hardware.
2. **SensorFW still uses a local CMake project.** Its pinned upstream source only provides qmake projects, but the Nix cross qmake setup fails the project's `qtConfig()` checks. The local CMake project mirrors its daemon, libraries, and plugins; it remains a downstream build-system substitution. The other previously flagged middleware packages now use the pinned upstream qmake projects and AsteroidOS recipe patches. Keepalive, libiodata, and timed were cross-built as part of the Fossil Hoki system; no third-party consumers beyond that system were checked.
3. **Hardware remains unverified.** Mainline device trees, Aurora's static vendor boot data, graphics pacing, Bluetooth pairing, suspend/wake, audio routing, and the actual camera/keyboard UX need on-device testing. This review does not deploy or flash a watch.
4. **Cross QML discovery warns about modules.** The full cross build cannot always run target QML discovery tools. Those warnings are not proof of missing runtime modules; packaging and the system profile must provide them, and on-device imports still need checking.

## Validation

- Applied all 43 baseline patches to a fresh checkout of the locked Mobile NixOS revision; its tree matched the working baseline exactly.
- Cross-built qml-asteroid with the real launch cache, bluebinder, and the virtual keyboard for Fossil Hoki.
- Cross-built camera and hackwatch; checked the installed keyboard helper.
- Ran nixfmt, deadnix, and statix over the AsteroidOS overlay and checked the touched watch modules.
- Applied the four review follow-ups after folding the keyboard and camera fixes into their owning patches. All 47 patches apply without three-way fallback, and the resulting tree exactly matches the committed working checkout.
- Built the final Fossil Hoki system: `/nix/store/x30cqpmnjj0a5cym5x1b14kz6cw7f8av-nixos-system-nixos-26.11pre-git`.
- Rebuilt Fossil Hoki with the upstream qmake packages: `/nix/store/93fx8m667yyd9b5yvhg9cxl4ni1w8zd9-nixos-system-nixos-26.11pre-git`. This compiles keepalive, libiodata for host and armv7l, timed for armv7l, and the existing pkg-config consumer in asteroid-launcher.
- Evaluated the complete system derivations for `oppo-beluga`, `google-aurora`, and `fossil-hoki-mainline`.
- Audited the launch-cache library, Qt booster, and Asteroid app library (7 ELF paths in a 2,014-ELF runtime closure): no missing NEEDED entries or unresolved symbols. Confirmed the launch-cache package does not reference the build machine's Qt runtime.
- Generated `machine.conf` with `borderGestureWidth = 0.15` and `hasWlan = false`; checked the upstream-compatible sections, boolean values, and fraction. Confirmed that `borderGestureWidth = 1.5` fails option validation.
- Ran `bash -n` on the corrected ELF audit script and reproduced the original SIGPIPE false positive with a large SONAME fixture.

The patch series has since been consolidated and reorganized: the launch-cache and middleware packaging work is in patch 0044, the Hoki display refresh and MCU timepiece work are in patches 0048 and 0049, and keyboard and camera fixes remain in patches 0021 and 0022. The 2026-11-10 follow-up below records the current replay and build results. No review changes were deployed or published.

## 2026-11-10 follow-up

- Replaced handwritten CMake descriptions for libconnman-qt, mce-qt6, ngf-qt6, and mlite with their pinned upstream Qt 6 qmake projects. Reused AsteroidOS's pinned mlite and libngf-qt recipe patches and kept Nix install paths. Removed mlite's now-unused local CMake file.
- Kept sensorfw on its local CMake project because the Nix qmake cross setup fails the upstream project's `qtConfig()` checks.
- Cross-built the four restored qmake packages for Fossil Hoki and checked that their libraries, pkg-config files, and QML plugins install under `/lib` and `/lib/qt-6/qml` as expected. Rebuilt the Fossil Hoki system successfully with those packages.
- Moved `0013-msm-fb-refresher-update-to-AsteroidOS-source.patch` from mobile-nixos upstream candidates to our customizations: the refresher supports the legacy-kernel path, which mobile-nixos upstream currently does not accept.
- Replayed the categorized patch series after these changes. It applies cleanly and reproduces the committed Mobile NixOS worktree tree exactly.

## 2026-11-15 follow-up

- Advanced the flake `nixpkgs-unstable` pin to `2074cc672349` (2026-11-12). `mobile-nixos` `development` is at `820abae034cf` (2026-11-15); the patch series replays cleanly on that tip.
- Advanced the live nightly `meta-asteroid` pin to tip `20a8d379f670` (settings Weblate bump only vs the 2026-11-09 baseline `c84c0bb`). Aligned `asteroid-settings` to recipe SRCREV `54ec1e317d10`.
- Re-reviewed category 10/25 pins against tip: launcher `29f4257`, middleware, and nfcd-binder-plugin already matched; no further recipe SRCREV deltas.

## 2026-11-17 follow-up

- `meta-asteroid` master is still `20a8d379f670`; recipe SRCREVs already matched the overlay.
- Bumped project tips ahead of recipes where GitHub moved: `asteroid-settings` `b1717016d481`, `asteroid-flashlight` `74a3638f4f45`, plus community `beatfork`, `docking-bay`, and `rag`. Folded into base patches `0022` / `0023` (no standalone fixup).
- Category 50 MCU timepiece / kernel-timezone work still tracks open meta-asteroid [PR 332](https://github.com/AsteroidOS/meta-asteroid/pull/332) (`3ca9e1e`); not merged into nightly. Timepiece user services are behind `services.asteroid-desktop.timepiece.enable` (default off). Category 40 keeps an opt-in `QPA_HWC2_DOZE_UPDATE` plugin path; Hoki device default matches upstream `QPA_HWC2_FORCE_DOZE_UPDATE`.
- Built `nixosConfigurations.fossil-hoki.config.system.build.toplevel` successfully after the pin bumps.

### Connectivity review and fixup consolidation

The outstanding changes preserve automatic phone input by default, the manual
phone-button option, keyboard hiding on phone text, credential submission from
the Wi-Fi tick button, and the `p2p0` blacklist that prevents duplicate SSIDs.
The previous Wi-Fi implementation was worse: it could leave the dialog on
"Connecting…" indefinitely and select an unusable WPA3 profile on Hoki.

- Kept the Prima SAE advertisement fix and added a Hoki-scoped ConnMan patch.
  ConnMan now checks interface SAE support before selecting SAE and optional
  PMF for WPA2/WPA3 transition networks. This preserves WPA2 fallback without
  disabling protected management frames globally.
- Kept the dialog's service-state watcher, failure reason, and retry guard.
  Checks of the actual JavaScript handlers cover stale idle state on retry,
  association/configuration progress, failure/disconnect, empty credentials,
  credential submission, retry requests, and success. These checks do not
  replace QML runtime or hardware testing.
- Built and deployed the system; the watch subsequently reported Wi-Fi online
  on Meow and an active Bluetooth link to the Pixel 9 Pro XL. The sync service
  was running. Temporary diagnostic credentials were removed.
- Live activation restarted systemd-rfkill and was followed by a Bluetooth
  soft-block. Clearing it restored Bluetooth; both saved Bluetooth rfkill states
  now contain `0`. The existing boot-time unblock remains in place. This is not
  a new reboot or battery-life validation.
- Folded standalone fixups `0050`–`0054` from category 30 into their owning
  patches: MCE timeout guards into `0044`, Hoki identity and Android ordering
  into `0031`, desktop time sync/sensor ordering/Bluetooth unblock into `0040`,
  and SensorFW plugin names into `0017`. Category 40's separate `0050` ambient
  display patch is retained.
- Formatted the changed Nix expression with nixfmt. Preserved unrelated patch
  sections and source formatting. Folding the fixups reproduces the same
  source tree, including the reviewed connectivity changes.
- Replayed all 45 consolidated patches without three-way fallback. The fresh
  checkout exactly matches source tree `d875863d8486dc46b0a93cc6cbced20c8d5f4acf`
  and builds to the deployed system
  `/nix/store/zc77x74hg2j05ab2rz0nkqd4l0ac807k-nixos-system-hoki-26.11pre-git`.

Earlier camera, companion-app, kernel comparison, minute-update AOD, and
default-off MCU timepiece requirements are preserved. This review does not
claim new APK, health-sensor, suspend, or camera validation; those components
were not changed. Commits remain local; nothing is pushed or published.

### Browser launch and search preferences

- The browser and WebKit processes started when tapped, but their window was
  invisible. A software-rendered launch displayed the UI on the watch. The
  browser wrapper now clears inherited Android EGL library overrides and sets
  GTK's Cairo renderer plus WebKit's software rendering switches.
- Startup and Home open DuckDuckGo. The search panel offers DuckDuckGo
  (default) or Google, saved in the user's browser settings. Corrected the Go
  button's callback so it receives the browser state rather than a button
  pointer.
- Checks of the actual C routing/settings functions cover default and saved
  engines, escaped queries, URLs, Home, the Go callback, and fallback for unknown
  or corrupt settings. These checks stub GTK/WebKit calls; they do not establish
  complete touchscreen or website compatibility.
- All 45 patches replay cleanly and reproduce source tree
  `62ab51301f1471cb2f22ef4f0f4529d32f20d187`. The resulting system,
  `/nix/store/xdgf63cvbllisy0cb3ic4y9hnfn065qf-nixos-system-hoki-26.11pre-git`,
  was installed without rebooting. Browser processes were running with no new
  errors in their log; Wi-Fi remained online and Bluetooth stayed connected.
- Applied nixfmt to the browser expression and folded the changes into browser
  patch `0024`, preserving unrelated patch sections.

### Browser HTTPS follow-up

The visible browser UI did not establish that web pages loaded. Instrumenting
page loading exposed `G_TLS_ERROR_UNAVAILABLE`: the package lacked
`glib-networking`, so GLib's TLS backend was absent even though curl could
reach DuckDuckGo. Added the dependency; the generated wrapper now exposes its
GIO modules. The user then confirmed the DuckDuckGo webpage was visible.

Loading, page errors, and renderer termination now have GTK status text instead
of an unexplained black page. Callback checks cover a TLS failure surviving the
subsequent finished event, successful retry clearing the error, cancellation,
and renderer termination. All 45 patches replay to source tree
`0c6e298010470f8aecda0641382dc48893e64ab0`; the system builds to
`/nix/store/40vs4k6qwbxvcdykiydxz8pbmbkkai08-nixos-system-hoki-26.11pre-git`.
The fix is folded into `0024`, with nixfmt and whitespace checks passing.

### Bluetooth popup labels

The reported raw `id-connected` appears in the watch's Bluetooth popup. The
installed English catalog contains that entry and resolves it to `Connected`
in a Qt translation probe; the runtime reason for the failed lookup remains
unconfirmed. Added a popup fallback for Bluetooth on/off and connected/not
connected. It retains successful localized translations and substitutes English
only when a lookup returns its raw ID or an empty string. Literal `qsTrId`
calls remain available to the translation extractor.

Checks cover missing, empty, and successful translations for all four labels.
The system builds successfully, and all 45 patches replay to source tree
`73eb04a2c08a3038a8b82ebd5a6757663bb81864`. The change is folded into app patch
`0022`; no standalone top-level fixup was added.
