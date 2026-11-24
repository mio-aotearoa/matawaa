# Source & Build Status

## Pinned Revisions

| Source | Revision |
| --- | --- |
| `nixpkgs-unstable` (flake & npins) | `619c39f6eac5d1ed6b8f073a9414baf7034b2356` |
| [meta-asteroid](https://github.com/AsteroidOS/meta-asteroid) | `aa42e158032bf4abbc0d0f898c5c3595a84538ef` |
| [meta-asteroid-community](https://github.com/AsteroidOS/meta-asteroid-community) | `5f4ed05c61ced9e48cec1e1c3aacf13607338dea` |
| Launcher (with PR 314/299 rotation patches) | `1a7a6e95d33f2d64867cbf59b81469207334814d` |
| Settings (with wrist rotation patch) | `d8a50606b5686a98a00a33a9534d9c1f887b1f5a` |

## Build

```sh
nix-build work/mobile-nixos/examples/asteroid --argstr device fossil-hoki \
  -A config.system.build.toplevel --no-out-link
```

## Health Implementation

Watch-side health tracking lives in base patch `0055`:
- Sleep/wake detection: on-watch C++ port of van Hees et al. (2015, 2018) / GGIR, synced via AppBridge to Gadgetbridge & Health Connect.
- Menstrual cycle tracking: on-watch C++ port of `gitlab.com/bloodyhealth/drip` (`cycle.js`, `cycle-length.js`).
- Details and scientific comparison: [health-methods.md](health-methods.md).

## Open Limitations

- See [hoki-display.md](hoki-display.md) for display/suspend/MCU notes and [hoki-health-sensors.md](hoki-health-sensors.md) for Hoki sensor gaps.
- Fscrypt encrypted home provisioning is unimplemented.
- Non-Hoki mainline device targets remain unvalidated on hardware.
