# Source and build status — 2026-11-21

## Pins

| Source | Revision |
| --- | --- |
| nixpkgs-unstable, flake and npins | `9996594bd38077de6a018ec17453ba3d99182787` |
| [meta-asteroid](https://github.com/AsteroidOS/meta-asteroid) | `aa42e158032bf4abbc0d0f898c5c3595a84538ef` |
| [meta-asteroid-community](https://github.com/AsteroidOS/meta-asteroid-community) | `5f4ed05c61ced9e48cec1e1c3aacf13607338dea` (recipe reference) |
| Launcher source (local PR 314/299 rotation patches retained) | `b83198f1ab809849220ddde3cb3f267e3c10a21c` |
| Settings source (local wrist rotation patch retained) | `b1717016d481d1f1b9dbd98b59ad7d2068f11b4c` |

Rechecked on 2026-11-21 against the upstream Git branch heads. Both AsteroidOS
recipe layers are already current: meta-asteroid remains at `aa42e158`, and
meta-asteroid-community remains at `5f4ed05c`. All 14 packaged core app revisions
match the latest nightly recipes. The meta-asteroid source archive hash was
independently verified as `sha256-Dinl7UFk39T/NMd8qeqEA/aeQRuNSzuTMW39AWX3oMw=`.
The launcher source and selected community apps retain their newer source pins;
this maintenance does not downgrade them to older recipe revisions.

Nixpkgs-unstable advanced from `e21fec32` to `9996594b` (2026-11-20).
The flake and npins now use the same revision and source hash:
`sha256-5q5/viwCcXSA4S2cSaKdjdYrHhk64qFbjLhCMlJnyeM=`.
The watch retains vendored Qt 6.11.3 and kernel 4.14.216. Hardware and firmware
pins are unchanged. The rotation PRs and MCU backend remain separate from the
nightly recipe baseline.

## Maintenance checks (2026-11-21)

- The complete patch series replays to source tree
  `36cb28938e52e7af55d2349b7964ea7d093551b2`, exactly matching the updated checkout.
- The flake's patched source output builds successfully.
- `nix flake check --no-build` passes on x86_64-linux, including all four
  configured Hoki/Beluga vendor and mainline systems. The check omits the
  aarch64-linux host output.
- Flake and direct evaluation select the same Hoki system derivation:
  `/nix/store/m6q9inm0a86blqa0cbl2pwax3kv2wawf-nixos-system-hoki-26.11pre-git.drv`.
- The full Hoki system build passes:
  `/nix/store/mwzkvrxdv2fsmfvacwnn7zbkwkxzwkn4-nixos-system-hoki-26.11pre-git`.
  No additional compatibility patches were needed for this Nixpkgs update.
- Deployed to Hoki at `192.168.8.183` and rebooted on 2026-11-21.
  Both `/run/booted-system` and the system profile match the build above.
  Boot ID: `53e2fd19-9509-450d-a5e8-be1a366a4f22`. Launcher and sensor logger
  are running with zero restarts; the watch user has no failed units.
  The existing firewall failure remains.

## Previous build status (2026-11-19)

The previous Nixpkgs pin needed an ARM libmad CMake correction (`and` → `AND`). Its
GCC already fixes the libgomp buffer issue; duplicate local compiler patches
and related warning/runtime overrides were removed. Legacy systemd 257 uses
its supported unit set while preserving declared application services and timers.

### Checks (2026-11-19)

- All 45 patches replay without three-way fallback to source tree
  `0d5079b15d4ffc2763fdf9c1c7e4e61571fdcd15`.
- Flake source output matches the reviewed files, excluding `.orig` patch backups.
- Flake and direct `nix-build` evaluate to the same Hoki system derivation.
- Extreme battery saver passes mock D-Bus tests and an installed offscreen UI
  startup check with Qt 6.11.3. Those tests never invoke a real MCU handoff.
- Rotation/setup QML has no syntax diagnostics; complete runtime import and
  touch behavior still need on-watch testing.
- Full Hoki system build passed:
  `/nix/store/wdbl1814vnh98qbbx2imfd004087jxsr-nixos-system-hoki-26.11pre-git`.
  The new features are not deployed.

Build the image with:

```sh
nix-build work/mobile-nixos/examples/asteroid --argstr device fossil-hoki \
  -A config.system.build.toplevel --no-out-link
```

The moWerk watch apps are included. Wanze is packaged with an inactive timer;
`sudo systemctl start wanze.timer` starts sampling without waking the watch.
Samples remain in `/var/log/wanze.csv`; uploads require explicit configuration.
Its native offline sampling check and ARM build pass. Heliograph uses Qt Sensors
for banner rotation.

## Open limitations

- [Hoki display flashes, Wi-Fi/suspend observations, and MCU app operation](hoki-display.md).
- SensorFW still uses a local CMake build; its sensor HAL probe failed on the
  last watch boot. Cross QML discovery warnings require runtime verification.
- Fscrypt options do not provision an encrypted home; migration and recovery
  remain unimplemented.
- Other devices' mainline kernels, graphics, audio, camera, and keyboard behavior
  need hardware validation. A Hoki build does not validate those devices.

## Health correction verification (2026-11-21)

The health changes replace unsupported sleep stages/scores and calendar fertility
predictions with manual history, bounded opt-in motion recording, and host-side
upstream scikit-digital-health analysis. See [method provenance and limitations](health-methods.md).

Validation: 68 upstream sleep tests, seven host integration tests, native Qt
regressions against the Nix-applied sensorlogd sources, flake evaluation, and the
full Hoki image build passed. The exported patch series replays to the committed
Mobile NixOS worktree. The image is:
`/nix/store/zf678djp675dfkpqac71ciarb3f0r2a2-nixos-system-hoki-26.11pre-git`.

These health changes have not been deployed. Overnight Hoki acquisition, battery
use, actual sample delivery, and comparison with reference sleep measurements
remain unvalidated. Synthetic integration tests do not establish clinical accuracy.
