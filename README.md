# ApolloInput Nova

[![Documentation](https://github.com/leonidas10009/apollo_input/actions/workflows/docs.yml/badge.svg)](https://github.com/leonidas10009/apollo_input/actions/workflows/docs.yml)
[![Docs](https://img.shields.io/badge/docs-GitHub%20Pages-5eead4)](https://leonidas10009.github.io/apollo_input/)

**ApolloInput Nova** is a standalone, MIT-licensed C++ library for creating
virtual gamepads. It is the reusable input layer extracted from Apollo 2026.

It includes:

- a portable gamepad model with buttons, sticks, triggers, touchpads, IMU,
  paddles and misc buttons,
- profile selection and compatibility fallback,
- radial deadzone and anti-drift filtering,
- Windows ViGEmBus backend,
- Linux uinput backend,
- null backend for tests and previews,
- a native Windows diagnostic tester (CLI),
- a visual browser-based tester for physical and virtual controllers,
- an Apollo integration patch for the real streaming host.

## Repository layout

```text
apollo_input/
├── include/apollo/input/     Public C++ API
├── src/core/                 Portable profiles, fallback and precision
├── src/backends/             ViGEmBus, uinput and null backends
├── tests/                    Core and XInput integration tests
├── examples/                 CLI demo
├── tools/                    Windows controller tester
├── visual-tester/            Browser-based visual tester
├── docs/                     Compatibility study and guides
├── integration/              Apollo streaming patch
└── CMakeLists.txt
```

## Profiles

| Canonical profile | Aliases | Intended controller |
|---|---|---|
| `xbox_360` | `x360`, `xbox360` | Xbox 360, universal XInput fallback |
| `xbox_one` | `xone`, `xboxone` | Xbox One |
| `xbox_series` | `xseries`, `xboxseries` | Xbox Series with Share |
| `xbox_elite` | `elite`, `elite_2`, `xboxelite` | Xbox Elite with paddles |
| `dualshock4` | `ds4`, `ps4` | DualShock 4 |
| `dualsense` | `ds5`, `ps5` | DualSense |
| `dualsense_edge` | `ds5_edge`, `ps5_edge` | DualSense Edge with paddles |
| `switch_pro` | `switch`, `pro_controller` | Switch Pro Controller |
| `steam_controller` | `steam`, `valve_steam_controller` | Steam Controller |
| `steam_deck` | `deck`, `steamdeck` | Steam Deck controls |
| `generic` | `hid` | Generic HID gamepad |

Names and aliases are case-insensitive.

## Backends

| Backend | Platform | Profiles | Notes |
|---|---|---|---|
| ViGEmBus | Windows | Xbox 360, DualShock 4 | Other profiles use `create_compatible()` fallback |
| uinput | Linux | All built-in profiles | No IMU/touchpad/battery/FFB |
| Null | All | All | No-op for tests and previews |

ViGEmBus does not expose Xbox One/Series, DualSense, Switch Pro or Steam
profiles. A HID-capable backend (UHID on Linux, VHF/UMDF2 on Windows) is needed
for their extended features.

## Anti-drift

`sanitize_gamepad_state()` applies:

- a **radial deadzone** to both sticks,
- exact `0,0` center snap inside the deadzone,
- rescaling outside the deadzone to preserve full travel,
- an independent trigger deadzone,
- neutral state submission when a paused session is resumed by the host.

Defaults are 3 % for sticks and 2 % for triggers. `Device::set_deadzone()`
changes them per device. See [docs/controller-compatibility.md](docs/controller-compatibility.md).

## Quick start

### Windows (MSYS2 UCRT64)

```bash
git clone --recursive https://github.com/leonidas10009/apollo_input.git
cd apollo_input

cmake -S . -B cmake-build-apollo-input -G Ninja
cmake --build cmake-build-apollo-input --parallel
ctest --test-dir cmake-build-apollo-input --output-on-failure
```

The ViGEmClient dependency is included as a submodule at
`third-party/ViGEmClient`. To use a different checkout:

```bash
cmake -S . -B cmake-build-apollo-input -G Ninja \
  -DAPOLLO_INPUT_VIGEMCLIENT_DIR=/path/to/ViGEmClient
```

ViGEmBus itself is a separate signed driver and is not bundled. Install the
official 1.22.0 package from
<https://github.com/nefarius/ViGEmBus/releases>.

### Linux

```bash
cmake -S . -B cmake-build-apollo-input -G Ninja
cmake --build cmake-build-apollo-input --parallel
ctest --test-dir cmake-build-apollo-input --output-on-failure
```

uinput needs write access to `/dev/uinput`.

## Controller tester (CLI)

Windows build output:

```text
cmake-build-apollo-input/tools/apollo-input-controller-tester.exe
```

Examples:

```bash
./cmake-build-apollo-input/tools/apollo-input-controller-tester.exe list
./cmake-build-apollo-input/tools/apollo-input-controller-tester.exe monitor xinput 0
./cmake-build-apollo-input/tools/apollo-input-controller-tester.exe drift xinput 0 --seconds 10
./cmake-build-apollo-input/tools/apollo-input-controller-tester.exe bridge xinput 0 --profile ds5 --seconds 120
./cmake-build-apollo-input/tools/apollo-input-controller-tester.exe selftest --profile xseries --seconds 20
./cmake-build-apollo-input/tools/apollo-input-controller-tester.exe verify
```

## Visual tester

Start the local visual application:

```bash
cd visual-tester
python server.py
```

Or on Windows double-click `visual-tester/ABRIR_TESTER_VISUAL.bat`.

It opens `http://127.0.0.1:8765/` with:

- a centered controller drawing,
- live buttons, sticks, triggers and D-pad,
- guided button test,
- 5-second anti-drift test with adjustable deadzone,
- profile selection and virtual controller creation,
- physical → virtual bridge,
- self-test and Xbox 360 XInput verification,
- rumble and event log.

See [visual-tester/README.md](visual-tester/README.md) and
[docs/COMO_USAR_EL_TESTER.md](docs/COMO_USAR_EL_TESTER.md).

## Apollo integration

The `integration/` folder contains the host-side precision patch for Apollo
2026. It adds configurable deadzones, stick/trigger filtering, resume
neutralization and Steam Controller auto-selection.

```powershell
cd E:\ProyectoProgramacion\apolo_V2026\_update\Apollo-git
git apply --check "path\to\integration\apollo-streaming-precision-0.2.0.patch"
git apply "path\to\integration\apollo-streaming-precision-0.2.0.patch"
```

See [integration/README.md](integration/README.md).

## Documentation

- [Online API documentation (GitHub Pages)](https://leonidas10009.github.io/apollo_input/)
- [Building](docs/building.md)
- [API reference](docs/api.md)
- [Controller compatibility](docs/controller-compatibility.md)
- [Estudio de compatibilidad (Español)](docs/ESTUDIO_COMPATIBILIDAD_MANDOS.md)
- [Cómo usar el tester](docs/COMO_USAR_EL_TESTER.md)
- [Notas de ViGEmBus](docs/VIGEMBUS_NOTES.md)
- [Third-party notices](THIRD_PARTY.md)
- [Changelog](CHANGELOG.md)

## License

MIT. See [LICENSE](LICENSE).

ViGEmClient is MIT and ViGEmBus is BSD-3-Clause; both retain their own notices.
No code from LizardByte's Source-Available License 1.0 components is used.
