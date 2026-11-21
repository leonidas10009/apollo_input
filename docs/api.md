# ApolloInput Nova API

Include the umbrella header:

```cpp
#include <apollo/input/input.hpp>
```

Namespace: `apollo::input`.

## Backend creation

```cpp
enum class BackendKind {
  automatic,
  vigem,
  uinput,
  null_backend,
};

std::unique_ptr<Backend> create_backend(BackendKind kind = BackendKind::automatic);
```

`automatic` selects ViGEmBus on Windows and uinput on Linux. Unsupported
platforms fall back to the null backend.

String helpers:

```cpp
const char *to_string(BackendKind kind);
bool backend_from_string(const std::string &name, BackendKind &kind);
```

Accepted aliases include `auto`, `vigembus`, `linux_uinput` and `none`.

## Backend

```cpp
class Backend {
public:
  virtual const char *name() const = 0;
  virtual bool available(std::string *error = nullptr) const = 0;
  virtual std::unique_ptr<Device> create(Profile profile,
                                         const std::string &name,
                                         std::string *error = nullptr) = 0;
  std::unique_ptr<Device> create_compatible(Profile profile,
                                            const std::string &name,
                                            std::string *error = nullptr);
  virtual std::vector<Profile> supported_profiles() const = 0;
};
```

`create()` only accepts profiles the backend implements. `create_compatible()`
tries the requested profile and then walks `profile_compatibility_chain()`.
The returned device reports the profile it actually emulates through
`DeviceInfo::profile`.

## Device

```cpp
class Device {
public:
  virtual const DeviceInfo &info() const = 0;
  virtual bool connected() const = 0;
  virtual bool update(const GamepadState &state) = 0;
  virtual void set_deadzone(const AnalogDeadzone &deadzone);
  virtual bool update_touchpad(const TouchpadState &state);
  virtual bool update_imu(const ImuSample &sample);
  virtual void set_feedback_callback(FeedbackCallback callback);
  virtual void disconnect() = 0;
};
```

Optional methods return `false` when the backend or profile does not support the
data.

## Profiles

```cpp
enum class Profile {
  xbox_360,
  xbox_one,
  xbox_series,
  xbox_elite,
  dualshock4,
  dualsense,
  dualsense_edge,
  switch_pro,
  steam_controller,
  steam_deck,
  generic,
};
```

Helpers:

```cpp
const char *to_string(Profile profile);
bool profile_from_string(const std::string &name, Profile &profile);
Capabilities profile_capabilities(Profile profile);
ProfileIdentity profile_identity(Profile profile);
```

`profile_from_string()` accepts canonical names and aliases such as `ds4`,
`ds5`, `xseries`, `elite`, `switch`, `steam`, `deck`, `ps4` and `ps5`.
Matching is case-insensitive.

## Buttons

```cpp
enum class Button : std::uint32_t {
  a, b, x, y,
  dpad_up, dpad_down, dpad_left, dpad_right,
  start, back, guide,
  left_thumb, right_thumb,
  left_shoulder, right_shoulder,
  touchpad, touchpad_click,
  share, options,
  paddle1, paddle2, paddle3, paddle4,
  misc2,
};

constexpr std::uint32_t button_bit(Button button);
```

`GamepadState` stores a bit mask of buttons plus sticks and triggers.

```cpp
struct GamepadState {
  std::uint32_t buttons = 0;
  std::uint8_t left_trigger = 0;
  std::uint8_t right_trigger = 0;
  std::int16_t left_x = 0;
  std::int16_t left_y = 0;
  std::int16_t right_x = 0;
  std::int16_t right_y = 0;

  bool pressed(Button button) const;
  void set(Button button, bool pressed);
  void clear();
};
```

Sticks use `-32768..32767`; triggers use `0..255`.

## Touchpad and IMU

```cpp
struct TouchpadState {
  bool active = false;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
};

struct ImuSample {
  float gyro_x = 0.0F;   // radians per second
  float gyro_y = 0.0F;
  float gyro_z = 0.0F;
  float accel_x = 0.0F;  // meters per second squared
  float accel_y = 0.0F;
  float accel_z = 0.0F;
  std::uint64_t timestamp_us = 0;
};
```

## Capabilities

```cpp
struct Capabilities {
  bool rumble = false;
  bool leds = false;
  bool imu = false;
  bool touchpad = false;
  bool adaptive_triggers = false;
  bool raw_keyboard = false;
  bool raw_mouse = false;
  bool trigger_rumble = false;
  bool player_leds = false;
  bool battery = false;
  bool paddles = false;
  std::uint8_t misc_buttons = 0;
};
```

`profile_capabilities()` describes what the profile can represent.
`DeviceInfo::capabilities` describes what the selected backend actually exposes.

```cpp
struct ProfileIdentity {
  std::uint16_t vendor_id = 0;
  std::uint16_t product_id = 0;
  std::uint16_t version = 0;
};

struct DeviceInfo {
  Profile profile = Profile::generic;
  Capabilities capabilities;
  std::string name;
};
```

## Controller selection

```cpp
enum class ControllerFamily {
  unknown,
  xbox,
  playstation,
  nintendo,
  steam,
  steam_deck,
};

struct ControllerDescription {
  ControllerFamily family = ControllerFamily::unknown;
  bool has_touchpad = false;
  bool has_motion = false;
  bool has_paddles = false;
  bool has_share = false;
  std::uint8_t misc_buttons = 0;
};

Profile recommended_profile(const ControllerDescription &controller);
std::vector<Profile> profile_compatibility_chain(Profile profile);

const char *to_string(ControllerFamily family);
bool controller_family_from_string(const std::string &name,
                                   ControllerFamily &family);
```

`recommended_profile()` picks the most capable profile for the reported
controller. `profile_compatibility_chain()` returns the requested profile
followed by simpler substitutes.

Example chains:

```text
dualsense_edge -> dualsense -> dualshock4 -> xbox_360 -> generic
xbox_elite     -> xbox_series -> xbox_one -> xbox_360 -> generic
switch_pro     -> xbox_360 -> generic
```

## Analog precision

```cpp
struct AnalogDeadzone {
  std::uint8_t stick_percent = 3;
  std::uint8_t trigger_percent = 2;
};

void sanitize_gamepad_state(GamepadState &state,
                            const AnalogDeadzone &deadzone = {});
```

The stick deadzone is radial. Values inside it become exact `0,0`; values outside
are rescaled to preserve full travel. Triggers below their deadzone become `0`
and are rescaled above it.

Backends apply the deadzone in `Device::update()`. A host should also send a
neutral report when a paused session resumes.

## Feedback

```cpp
enum class FeedbackType {
  rumble,
  led,
  adaptive_trigger,
  unknown,
};

struct Feedback {
  FeedbackType type;
  std::uint16_t low_frequency;
  std::uint16_t high_frequency;
  std::uint8_t led_red;
  std::uint8_t led_green;
  std::uint8_t led_blue;
  std::uint8_t trigger;
  std::uint8_t trigger_mode;
};

using FeedbackCallback = std::function<void(const Feedback &)>;
```

The callback is invoked from the backend thread.

## Example

```cpp
#include <apollo/input/input.hpp>

#include <cstdio>
#include <string>

int main() {
  auto backend = apollo::input::create_backend();
  std::string error;
  if (!backend->available(&error)) {
    std::printf("Backend unavailable: %s\n", error.c_str());
    return 1;
  }

  auto pad = backend->create_compatible(
    apollo::input::Profile::dualsense,
    "Apollo Virtual Gamepad",
    &error
  );
  if (!pad) {
    std::printf("Unable to create device: %s\n", error.c_str());
    return 1;
  }

  std::printf("Emulating: %s\n", apollo::input::to_string(pad->info().profile));
  pad->set_deadzone({3, 2});
  pad->set_feedback_callback([](const apollo::input::Feedback &feedback) {
    if (feedback.type == apollo::input::FeedbackType::rumble) {
      std::printf("Rumble %u/%u\n", feedback.low_frequency, feedback.high_frequency);
    }
  });

  apollo::input::GamepadState state;
  state.set(apollo::input::Button::a, true);
  state.left_x = 32767;
  pad->update(state);

  state.clear();
  pad->update(state);
  pad->disconnect();
  return 0;
}
```

## Threading

- One `Device` should not be updated concurrently from multiple threads.
- Feedback callbacks run on the backend thread.
- Hosts should route feedback to the client through their own queue.
