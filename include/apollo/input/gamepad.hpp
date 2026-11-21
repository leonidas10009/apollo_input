/**
 * @file include/apollo/input/gamepad.hpp
 * @brief Portable gamepad state, profiles, and capability definitions.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace apollo::input {

  /**
   * @brief Virtual controller profile exposed by a backend.
   */
  enum class Profile {
    xbox_360,
    xbox_one,
    xbox_series,
    xbox_elite,  ///< Xbox Elite Series 2 with rear paddles and Share.
    dualshock4,
    dualsense,
    dualsense_edge,  ///< DualSense Edge with rear paddles and extra function buttons.
    switch_pro,
    steam_controller,  ///< Valve Steam Controller layout.
    steam_deck,  ///< Valve Steam Deck built-in controls.
    generic,
  };

  /**
   * @brief Physical controller family reported by a streaming client.
   *
   * The family is a hint used to choose a compatible virtual profile. It does
   * not guarantee that every feature of the physical controller can be exposed
   * by the host backend.
   */
  enum class ControllerFamily {
    unknown,  ///< No reliable family information.
    xbox,  ///< Xbox 360, Xbox One, Xbox Series, or Xbox Elite controller.
    playstation,  ///< DualShock 3/4, DualSense, or another PlayStation-style pad.
    nintendo,  ///< Switch Pro Controller, Joy-Con, or another Nintendo-style pad.
    steam,  ///< Valve Steam Controller.
    steam_deck,  ///< Valve Steam Deck built-in controls.
  };

  /**
   * @brief Capabilities reported by a client controller.
   *
   * These flags describe the physical controller on the client side. The host
   * combines them with @ref recommended_profile() and the selected backend to
   * decide which virtual device can represent the most features.
   */
  struct ControllerDescription {
    ControllerFamily family = ControllerFamily::unknown;  ///< Client-reported controller family.
    bool has_touchpad = false;  ///< Reports touchpad contacts.
    bool has_motion = false;  ///< Reports accelerometer or gyroscope samples.
    bool has_paddles = false;  ///< Reports rear paddle or grip buttons.
    bool has_share = false;  ///< Reports a Share, Capture, or misc button.
    std::uint8_t misc_buttons = 0;  ///< Number of extra misc/share buttons reported by the client.
  };

  /**
   * @brief Digital gamepad buttons shared by all backends.
   */
  enum class Button : std::uint32_t {
    a = 0,
    b,
    x,
    y,
    dpad_up,
    dpad_down,
    dpad_left,
    dpad_right,
    start,
    back,
    guide,
    left_thumb,
    right_thumb,
    left_shoulder,
    right_shoulder,
    touchpad,
    touchpad_click,
    share,
    options,
    paddle1,
    paddle2,
    paddle3,
    paddle4,
    misc2,  ///< Second miscellaneous button, when a controller exposes one.
  };

  /**
   * @brief Bit mask for one digital button.
   *
   * @param button Button to encode.
   * @return Single-bit mask.
   */
  constexpr std::uint32_t button_bit(Button button) {
    return std::uint32_t {1} << static_cast<std::uint32_t>(button);
  }

  /**
   * @brief One complete gamepad input report.
   */
  struct GamepadState {
    std::uint32_t buttons = 0;  ///< Bit mask of Button values.
    std::uint8_t left_trigger = 0;  ///< 0-255.
    std::uint8_t right_trigger = 0;  ///< 0-255.
    std::int16_t left_x = 0;  ///< -32768 to 32767.
    std::int16_t left_y = 0;  ///< -32768 to 32767.
    std::int16_t right_x = 0;  ///< -32768 to 32767.
    std::int16_t right_y = 0;  ///< -32768 to 32767.

    /**
     * @brief Check whether a button is pressed.
     *
     * @param button Button to query.
     * @return True when the button bit is set.
     */
    bool pressed(Button button) const {
      return (buttons & button_bit(button)) != 0;
    }

    /**
     * @brief Set or clear a button bit.
     *
     * @param button Button to update.
     * @param pressed New pressed state.
     */
    void set(Button button, bool pressed) {
      if (pressed) {
        buttons |= button_bit(button);
      } else {
        buttons &= ~button_bit(button);
      }
    }

    /**
     * @brief Reset all buttons, triggers, and sticks.
     */
    void clear() {
      *this = {};
    }
  };

  /**
   * @brief Optional touchpad report.
   */
  struct TouchpadState {
    bool active = false;  ///< Whether a finger is touching the pad.
    std::uint16_t x = 0;  ///< Touch X coordinate.
    std::uint16_t y = 0;  ///< Touch Y coordinate.
  };

  /**
   * @brief Optional inertial measurement unit sample.
   */
  struct ImuSample {
    float gyro_x = 0.0F;  ///< Radians per second.
    float gyro_y = 0.0F;  ///< Radians per second.
    float gyro_z = 0.0F;  ///< Radians per second.
    float accel_x = 0.0F;  ///< Meters per second squared.
    float accel_y = 0.0F;  ///< Meters per second squared.
    float accel_z = 0.0F;  ///< Meters per second squared.
    std::uint64_t timestamp_us = 0;  ///< Host timestamp in microseconds.
  };

  /**
   * @brief Features a backend can expose for a profile.
   */
  struct Capabilities {
    bool rumble = false;  ///< Main rumble feedback.
    bool leds = false;  ///< LED or lightbar feedback.
    bool imu = false;  ///< Gyroscope and accelerometer.
    bool touchpad = false;  ///< Touchpad input.
    bool adaptive_triggers = false;  ///< Adaptive trigger feedback.
    bool raw_keyboard = false;  ///< Raw Input-visible keyboard.
    bool raw_mouse = false;  ///< Raw Input-visible mouse.
    bool trigger_rumble = false;  ///< Independent left/right trigger rumble.
    bool player_leds = false;  ///< Player indicator LEDs.
    bool battery = false;  ///< Battery state reported by the client.
    bool paddles = false;  ///< Rear paddle or grip buttons.
    std::uint8_t misc_buttons = 0;  ///< Number of Share, Capture, or misc buttons (0-2).
  };

  /**
   * @brief USB identity used by backends that create HID-style devices.
   */
  struct ProfileIdentity {
    std::uint16_t vendor_id = 0;  ///< USB vendor ID.
    std::uint16_t product_id = 0;  ///< USB product ID.
    std::uint16_t version = 0;  ///< Device version.
  };

  /**
   * @brief Host-side analog filtering used to remove drift and resting noise.
   */
  struct AnalogDeadzone {
    std::uint8_t stick_percent = 3;  ///< Radial stick deadzone percentage (0-30).
    std::uint8_t trigger_percent = 2;  ///< Trigger deadzone percentage (0-30).
  };

  /**
   * @brief Remove stick drift, trigger resting noise, and center jitter.
   *
   * The stick deadzone is radial, so diagonal drift is handled consistently on
   * both axes. Values outside the deadzone are rescaled to preserve full travel.
   *
   * @param state Gamepad state to sanitize in place.
   * @param deadzone Deadzone configuration.
   */
  void sanitize_gamepad_state(GamepadState &state, const AnalogDeadzone &deadzone = {});

  /**
   * @brief Description of a created virtual device.
   */
  struct DeviceInfo {
    Profile profile = Profile::generic;  ///< Active profile.
    Capabilities capabilities;  ///< Features exposed by the backend.
    std::string name;  ///< Device name shown by the operating system.
  };

  /**
   * @brief Convert a profile to a stable short name.
   *
   * @param profile Profile to convert.
   * @return Static string such as "xbox_360".
   */
  const char *to_string(Profile profile);

  /**
   * @brief Parse a profile from its short name or a compatible alias.
   *
   * Matching is case-insensitive. Aliases include Xbox 360/One/Series names,
   * DS4/DS5, Switch, Elite, Edge, Steam, and Deck variants.
   *
   * @param name Profile name or alias.
   * @param profile Receives the parsed profile.
   * @return True when the name is valid.
   */
  bool profile_from_string(const std::string &name, Profile &profile);

  /**
   * @brief Convert a controller family to a stable short name.
   *
   * @param family Controller family.
   * @return Static string such as "xbox", "playstation", or "steam_deck".
   */
  const char *to_string(ControllerFamily family);

  /**
   * @brief Parse a controller family from its short name or alias.
   *
   * @param name Family name or alias.
   * @param family Receives the parsed family.
   * @return True when the name is valid.
   */
  bool controller_family_from_string(const std::string &name, ControllerFamily &family);

  /**
   * @brief Features a profile can expose when the backend supports them.
   *
   * This describes the target profile itself, not the capabilities of a
   * specific backend. A backend intersects this matrix with its own support
   * and reports the result in @ref DeviceInfo::capabilities.
   *
   * @param profile Virtual controller profile.
   * @return Feature matrix for the profile.
   */
  Capabilities profile_capabilities(Profile profile);

  /**
   * @brief USB identity associated with a profile.
   *
   * Backends that expose HID-style devices can use this identity so SDL, Steam,
   * and udev apply the correct controller mapping.
   *
   * @param profile Virtual controller profile.
   * @return USB vendor, product, and version identifiers.
   */
  ProfileIdentity profile_identity(Profile profile);

  /**
   * @brief Pick the most capable virtual profile for a client controller.
   *
   * The recommendation keeps controller-specific features when possible:
   * touchpads and motion prefer PlayStation/Steam profiles, Nintendo pads use
   * the Switch Pro profile, and Xbox pads use Xbox Series or Xbox Elite.
   *
   * @param controller Client-reported controller description.
   * @return Recommended virtual profile.
   */
  Profile recommended_profile(const ControllerDescription &controller);

  /**
   * @brief Return a profile followed by progressively simpler alternatives.
   *
   * The first element is always the requested profile. Later elements are safe
   * substitutes that keep standard buttons, sticks, and triggers working when
   * the preferred profile is unavailable on a backend.
   *
   * @param profile Requested profile.
   * @return Compatibility chain starting with the requested profile.
   */
  std::vector<Profile> profile_compatibility_chain(Profile profile);

}  // namespace apollo::input
