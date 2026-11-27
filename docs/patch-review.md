# Source & Build Status

## Pinned Revisions

| Source | Revision |
| --- | --- |
| `nixpkgs-unstable` (flake & npins) | `619c39f6eac5d1ed6b8f073a9414baf7034b2356` |
| [meta-asteroid](https://github.com/AsteroidOS/meta-asteroid) | `99bfd19711c39d7043ed74193106b0b75205bac0` |
| [meta-asteroid-community](https://github.com/AsteroidOS/meta-asteroid-community) | `5f4ed05c61ced9e48cec1e1c3aacf13607338dea` |
| [meta-smartwatch](https://github.com/AsteroidOS/meta-smartwatch) | `9a509e7bed838e34982057cfb6a79b8e3cb58390` |
| Launcher (with PR 314/299 rotation patches) | `30c4f3b0661464d485159ffe2757a7604ce1b488` |
| Settings (with wrist rotation patch) | `04c6841b45bdd0778a60f89c2932e925cf31265b` |

## Build

```sh
nix build .#nixosConfigurations.fossil-hoki.config.system.build.toplevel --no-link --print-out-paths
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
