# Apollo streaming integration

`apollo-streaming-precision-0.2.0.patch` applies to the Apollo 2026 host at
`Apollo-git`, not to this standalone library.

It adds to the real streaming path:

- `gamepad_stick_deadzone` (0-30 %, default 3),
- `gamepad_trigger_deadzone` (0-30 %, default 2),
- radial stick filtering and exact center snap,
- trigger resting-noise filtering,
- neutral gamepad state when a paused session is resumed,
- `LI_CTYPE_STEAM` auto-selection,
- future paddle mapping,
- Web UI, documentation and tests.

## Apply

```powershell
cd E:\ProyectoProgramacion\apolo_V2026\_update\Apollo-git
git apply --check "path\to\integration\apollo-streaming-precision-0.2.0.patch"
git apply "path\to\integration\apollo-streaming-precision-0.2.0.patch"
```

Then build Apollo and run the full test suite:

```powershell
C:\Users\Leonidas\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c `
  "cd /e/ProyectoProgramacion/apolo_V2026/_update/Apollo-git/cmake-build-release && ninja sunshine test_sunshine"
```

## Why it is separate

The patch depends on Apollo host internals (`src/input.cpp`, `src/config.cpp`,
`src/platform/virtualhid_input.cpp`). The standalone library remains
host-agnostic and can be reused by other projects.
