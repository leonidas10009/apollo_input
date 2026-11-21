# Controller Compatibility Study — ApolloInput Nova

## Scope

This document studies the controllers that the Apollo host can expose to games
through the standalone `apollo-input` library, released as **ApolloInput Nova
0.2.0**. It covers:

- virtual controller profiles created on the host,
- the Windows ViGEmBus and Linux uinput backends,
- automatic profile selection from client-reported controller data,
- compatibility fallback when a backend cannot create the preferred profile,
- the physical controllers those profiles are intended to represent.

The library creates virtual devices on the host. Reading physical controllers
on the client is a different problem; projects such as duaLib, SwitchKit and
simple-xpad are useful references for that side and for HID protocol details.

## Input path

```text
physical controller -> Moonlight/Artemis client -> Apollo -> apollo-input -> game
physical controller <- Moonlight/Artemis client <- Apollo <- apollo-input <- game
```

A feature works only when every layer supports it. A virtual profile can
represent a feature that the physical controller or the client cannot send, and
a physical controller can report a feature that the selected virtual profile
cannot expose.

## Profiles

| Canonical profile | Aliases | Intended controller |
|---|---|---|
| `xbox_360` | `x360`, `xbox360` | Xbox 360; universal XInput fallback |
| `xbox_one` | `xone`, `xboxone` | Xbox One |
| `xbox_series` | `xseries`, `xboxseries` | Xbox Series with Share |
| `xbox_elite` | `elite`, `elite_2`, `xboxelite` | Xbox Elite Series 2 with paddles |
| `dualshock4` | `ds4`, `ps4` | DualShock 4 |
| `dualsense` | `ds5`, `ps5` | DualSense |
| `dualsense_edge` | `ds5_edge`, `ps5_edge` | DualSense Edge with paddles |
| `switch_pro` | `switch`, `pro_controller` | Switch Pro Controller |
| `steam_controller` | `steam`, `valve_steam_controller` | Steam Controller |
| `steam_deck` | `deck`, `steamdeck` | Steam Deck built-in controls |
| `generic` | `hid` | Generic HID gamepad |

Names and aliases are case-insensitive.

## Profile feature matrix

| Profile | Rumble | Trigger rumble | Lightbar | Player LEDs | IMU | Touchpad | Adaptive triggers | Battery | Paddles | Misc buttons |
|---|---|---|---|---|---|---|---|---|---|---|
| `xbox_360` | Yes | No | No | No | No | No | No | No | No | 0 |
| `xbox_one` | Yes | Yes | No | No | No | No | No | Yes | No | 0 |
| `xbox_series` | Yes | Yes | No | No | No | No | No | Yes | No | 1 |
| `xbox_elite` | Yes | Yes | No | No | No | No | No | Yes | Yes | 1 |
| `dualshock4` | Yes | No | Yes | No | Yes | Yes | No | Yes | No | 0 |
| `dualsense` | Yes | No | Yes | Yes | Yes | Yes | Yes | Yes | No | 1 |
| `dualsense_edge` | Yes | No | Yes | Yes | Yes | Yes | Yes | Yes | Yes | 1 |
| `switch_pro` | Yes | No | Yes | Yes | Yes | No | No | Yes | No | 1 |
| `steam_controller` | Yes | No | No | No | Yes | Yes | No | Yes | Yes | 0 |
| `steam_deck` | Yes | No | No | No | Yes | Yes | No | Yes | Yes | 1 |
| `generic` | Yes | No | No | No | No | No | No | No | No | 0 |

This matrix describes what the profile model can represent. A backend reports
the subset it can actually expose through `DeviceInfo::capabilities`.

## Backend matrix

| Backend | Platform | Profiles | Backend-specific capabilities | Requirements |
|---|---|---|---|---|
| ViGEmBus | Windows | Xbox 360, DualShock 4; other profiles via `create_compatible()` | Xbox 360 rumble; DS4 rumble, lightbar, IMU, touchpad | ViGEmBus 1.22.0 installed |
| uinput | Linux | Every built-in profile | Buttons, sticks, triggers, D-pad, paddles, Share, touchpad click | Writable `/dev/uinput` |
| Null | All | Every built-in profile | Simulated state and optional data | None |

ViGEmBus is end-of-life and does not expose Xbox One/Series, Xbox Elite,
DualSense, DualSense Edge, Switch Pro or Steam profiles. `create_compatible()`
keeps those requests usable by falling back to DualShock 4 or Xbox 360.

uinput cannot expose HID reports, IMU, touchpad contacts, battery, LEDs, or
force feedback. It does expose the full set of digital buttons, including
profile-specific paddles and misc buttons, using `BTN_TRIGGER_HAPPY*`.

## Analog precision and anti-drift

Ghost drift is usually one of three problems:

1. A physical controller or client reports a small non-zero rest value.
2. One axis crosses a per-axis threshold before the other, creating diagonal drift.
3. A paused session keeps the last stick or trigger value until fresh input arrives.

ApolloInput Nova addresses all three:

- `sanitize_gamepad_state()` applies a radial deadzone, so both axes are evaluated
  by vector magnitude and tiny values snap to an exact `0,0` center.
- Values outside the deadzone are rescaled, preserving full travel and direction.
- Triggers have an independent deadzone and are rescaled from the threshold.
- `Device::set_deadzone()` lets a host choose stricter or looser filtering.
- A neutral report must be sent when a paused session resumes; the Apollo host
  patch implements this before rebinding the virtual gamepad.

The default is 3% for sticks and 2% for triggers. A diagnostic drift test is
available in the controller tester.

## Automatic selection

`recommended_profile()` uses the controller family and reported capabilities:

| Client controller | Recommendation |
|---|---|
| Xbox family, no paddles | `xbox_series` |
| Xbox family with paddles | `xbox_elite` |
| PlayStation without touchpad/motion | `dualshock4` |
| PlayStation with touchpad/motion | `dualsense` |
| PlayStation with paddles | `dualsense_edge` |
| Nintendo | `switch_pro` |
| Steam Controller with touchpad/motion | `steam_controller` |
| Steam Deck with touchpad/motion/paddles | `steam_deck` |
| Unknown with touchpad/motion | `dualsense` |
| Unknown with paddles, no touchpad/motion | `xbox_elite` |
| Unknown, no advanced capabilities | `xbox_series` |

Streaming hosts should combine this recommendation with
`Backend::create_compatible()` so the host still creates a working controller
when the preferred profile is unavailable.

## Compatibility fallback

Each profile has a chain from richest to most compatible:

```text
dualsense_edge -> dualsense -> dualshock4 -> xbox_360 -> generic
xbox_elite     -> xbox_series -> xbox_one -> xbox_360 -> generic
steam_deck     -> steam_controller -> dualsense -> dualshock4 -> xbox_360 -> generic
switch_pro     -> xbox_360 -> generic
```

The device returned by `create_compatible()` reports the profile it actually
emulates in `DeviceInfo::profile`. Callers should show that actual profile in
logs and diagnostics.

## Physical controller notes

### Xbox 360

- Family: `xbox`.
- Recommended profile: `xbox_series` (or `xbox_360` when exact XInput identity
  is required).
- Buttons, sticks, triggers and rumble work with ViGEmBus and uinput.
- No Share, motion, touchpad, or battery.

### Xbox One

- Family: `xbox`.
- Recommended profile: `xbox_series`.
- ViGEmBus falls back to Xbox 360, losing trigger rumble and battery metadata.
- uinput exposes the profile identity but not trigger rumble or battery.
- A future HID backend can expose trigger rumble and battery.

### Xbox Series X|S

- Family: `xbox`.
- Recommended profile: `xbox_series`.
- Share maps to the model's `Button::share`. ViGEmBus cannot expose it; uinput
  exposes it as an extra evdev button.
- A future HID backend can expose Share and battery.

### Xbox Elite Series 2

- Family: `xbox`, `has_paddles = true`.
- Recommended profile: `xbox_elite`.
- ViGEmBus falls back to Xbox 360 and loses paddles.
- uinput exposes four paddles as `BTN_TRIGGER_HAPPY1..4`.
- SDL_GameControllerDB already maps Elite paddles as `paddle1..paddle4` on
  supported platforms, so exposing stable paddle buttons is useful.

### DualShock 3

- Family: `playstation`; usually reports no touchpad.
- Recommended profile: `dualshock4`.
- Motion may be forwarded when the client supports it.
- No touchpad or lightbar semantics.

### DualShock 4

- Family: `playstation`, touchpad and motion present.
- Recommended profile: `dualsense`; select `dualshock4` when a game specifically
  requires DS4 identity.
- ViGEmBus exposes DS4 rumble, lightbar, IMU and touchpad.
- uinput exposes only digital/analog input.

### DualSense

- Family: `playstation`, touchpad and motion present.
- Recommended profile: `dualsense`.
- ViGEmBus falls back to DualShock 4 and loses adaptive triggers and player LEDs.
- uinput exposes only digital/analog input.
- Adaptive trigger effect encoding is documented by projects such as duaLib and
  is a target for the future UHID/VHF backends.

### DualSense Edge

- Family: `playstation`, `has_paddles = true`.
- Recommended profile: `dualsense_edge`.
- The model treats the two function buttons and two rear paddles as four paddle
  buttons (`paddle1..paddle4`).
- ViGEmBus falls back to DualShock 4 and loses paddles; uinput exposes them as
  extra evdev buttons.

### Nintendo Switch Pro Controller

- Family: `nintendo`.
- Recommended profile: `switch_pro`.
- Features: rumble, motion, player LEDs, battery and Capture.
- ViGEmBus falls back to Xbox 360, losing motion and LEDs.
- uinput exposes a Switch Pro identity plus digital/analog input.

### Nintendo Joy-Con

- Family: `nintendo`.
- Recommended profile: `switch_pro` when a combined controller is presented.
- Single Joy-Con layouts have fewer buttons; the client is responsible for
  presenting a combined or individual device.
- Switch 2 controllers are not supported by the current SwitchKit reference and
  have no virtual profile yet.

### Steam Controller

- Family: `steam`.
- Recommended profile: `steam_controller` when touchpad/motion are present.
- The library models one touchpad and one motion sensor. Dual touchpads are not
  separately representable yet.
- Paddles/grips map to `paddle1..paddle4`; uinput exposes them as extra buttons.
- ViGEmBus falls back to DualShock 4 or Xbox 360.

### Steam Deck

- Family: `steam_deck`.
- Recommended profile: `steam_deck`.
- Same limitations as the Steam Controller plus a misc button.
- SDL_GameControllerDB maps Deck rear buttons as paddles, confirming the model.

### 8BitDo and generic controllers

- XInput mode: report as `xbox`, use `xbox_series`.
- Switch mode: report as `nintendo`, use `switch_pro`.
- Generic HID: use `generic` or let `recommended_profile()` choose.
- Bluetooth vs USB mode can change which features the client exposes. The host
  should treat client-reported capabilities as authoritative.

## References

| Project | License | Useful for |
|---|---|---|
| [WujekFoliarz/duaLib](https://github.com/WujekFoliarz/duaLib) | MIT | DS4/DualSense reports, adaptive triggers, Edge extra buttons |
| [Meorge/SwitchKit](https://github.com/Meorge/SwitchKit) | MIT | Switch Pro/Joy-Con reports, HD Rumble, IMU, player lights |
| [adamczykpiotr/simple-xpad](https://github.com/adamczykpiotr/simple-xpad) | See repository | Xbox One native/Bluetooth behavior and rumble limits |
| [mdqinc/SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) | zlib | Controller GUIDs, standard mappings, paddle/misc conventions |

These projects are references, not runtime dependencies. No code from
LizardByte's Source-Available License 1.0 components is used by `apollo-input`.

## Known gaps

1. Linux uinput cannot expose IMU, touchpad contacts, battery or LEDs.
2. ViGEmBus cannot create Xbox One/Series, DualSense, Switch Pro or Steam
   profiles, nor adaptive triggers or paddles.
3. Steam dual touchpads are collapsed into one logical touchpad.
4. Switch 2 and Joy-Con 2 protocols are not modeled.
5. No macOS backend in `apollo-input`.
6. No force feedback in the uinput backend.

## Roadmap

1. **ApolloInput Nova 0.2.0**: profile model, aliases, recommendation, fallback,
   profile-aware uinput, ViGEm capability reporting. *(current)*
2. **Linux UHID backend**: full HID profiles, IMU, touchpad and adaptive triggers.
3. **Windows VHF/UMDF2 backend**: full HID profiles on Windows without a
   proprietary license; requires driver signing.
4. **macOS IOHID/DriverKit backend**: requires entitlements and notarization.
5. **Switch 2 / Steam dual-touchpad model**: extend the profile model once the
   report formats are validated.

## Validation checklist

- [x] Core unit tests for profiles, aliases, capabilities, identities,
      recommendations, fallback, anti-drift filtering and null backend.
- [x] Windows ViGEmBus XInput loopback test.
- [x] Windows ViGEmBus compatibility fallback test (DualSense -> DualShock 4).
- [x] Controller tester: list, monitor, self-test and XInput verification with
      virtual Xbox 360 and DualShock 4 devices.
- [x] DualSense `054C:0CE6` on the target machine: drift PASS (202 samples,
      0 after filtering) and physical → DualShock 4 bridge.
- [ ] Linux uinput test on real hardware (`evtest`, `jstest`, SDL3 testcontroller).
- [ ] Steam Input recognition for each uinput profile.
- [ ] DualSense adaptive trigger test with a future HID backend.
- [ ] Switch Pro motion/rumble test with a future HID backend.
