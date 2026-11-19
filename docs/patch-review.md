# Source and build status — 2026-11-19

## Pins

| Source | Revision |
| --- | --- |
| nixpkgs-unstable, flake and npins | `e21fec321d73511849707a7585e4ea155ee4bf40` |
| [meta-asteroid](https://github.com/AsteroidOS/meta-asteroid) | `20a8d379f67038208a32b8b59a3093fac8410c73` |
| [meta-asteroid-community](https://github.com/AsteroidOS/meta-asteroid-community) | `5f4ed05c61ced9e48cec1e1c3aacf13607338dea` (recipe reference) |
| Launcher [PR 314](https://github.com/AsteroidOS/asteroid-launcher/pull/314), including PR 299 | `3c72b3c79c94a61f59f73133ce38f021e51d9286` |
| Settings [PR 153](https://github.com/AsteroidOS/asteroid-settings/pull/153) | `c32add32bf541787001135ca936b2ff228b95210` |

Rechecked on 2026-11-19: the AsteroidOS layers and launcher remain current. Settings, flashlight,
health, beatfork, and touchdown retain their newer upstream-head pins instead
of reverting to older recipe pins. The watch retains vendored Qt 6.11.3 and
kernel 4.14.216 (ten consecutive stable increments, .207–.216). Hardware and
firmware pins are unchanged. The rotation PRs and MCU backend are unmerged
upstream work, separate from the nightly recipe baseline.

The new nixpkgs pin needs an ARM libmad CMake correction (`and` → `AND`). Its
GCC already fixes the libgomp buffer issue; duplicate local compiler patches
and related warning/runtime overrides were removed. Legacy systemd 257 uses
its supported unit set while preserving declared application services and timers.

## Checks

- All 45 patches replay without three-way fallback to source tree
  `97f55b33e2435b513d2d9aee09d02548c7a70736`.
- Flake source output matches the reviewed files, excluding `.orig` patch backups.
- Flake and direct `nix-build` evaluate to the same Hoki system derivation.
- Extreme battery saver passes mock D-Bus tests and an installed offscreen UI
  startup check with Qt 6.11.3. Those tests never invoke a real MCU handoff.
- Rotation/setup QML has no syntax diagnostics; complete runtime import and
  touch behavior still need on-watch testing.
- Full Hoki system build passed with the refreshed pin and reuses the same output:
  `/nix/store/w72rqzz7lays15vxz1l5vywykdy1pjg4-nixos-system-hoki-26.11pre-git`.
  The new features are not deployed.

Build the image with:

```sh
nix-build work/mobile-nixos/examples/asteroid --argstr device fossil-hoki \
  -A config.system.build.toplevel --no-out-link
```

## Open limitations

- [Hoki display flashes, Wi-Fi/suspend observations, and MCU app operation](hoki-display.md).
- SensorFW still uses a local CMake build; its sensor HAL probe failed on the
  last watch boot. Cross QML discovery warnings require runtime verification.
- Fscrypt options do not provision an encrypted home; migration and recovery
  remain unimplemented.
- Other devices' mainline kernels, graphics, audio, camera, and keyboard behavior
  need hardware validation. A Hoki build does not validate those devices.
