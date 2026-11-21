# Changelog

## 0.2.0 — 2026-11-21

- Added `xbox_elite`, `dualsense_edge`, `steam_controller` and `steam_deck`
  profiles.
- Added trigger rumble, player LEDs, battery, paddles, misc buttons and
  `Button::misc2`.
- Added `profile_capabilities()`, `profile_identity()`, `recommended_profile()`
  and `profile_compatibility_chain()`.
- Added `Backend::create_compatible()`.
- Added case-insensitive profile, family and backend aliases.
- Added radial stick deadzone, center snap, trigger deadzone and
  `Device::set_deadzone()`.
- Made the Linux uinput backend profile-aware with correct USB identity and
  extra evdev buttons.
- Added the Windows controller tester.
- Added the visual browser-based tester.
- Added the Apollo streaming precision/anti-drift patch.
- Added controller compatibility studies and guides.

## 0.1.0

- Initial standalone library with ViGEmBus, uinput and null backends.
