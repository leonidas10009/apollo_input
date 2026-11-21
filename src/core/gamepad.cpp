/**
 * @file src/core/gamepad.cpp
 * @brief Profile names, aliases, compatibility chains, and capability helpers.
 */
#include "apollo/input/gamepad.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <vector>

namespace apollo::input {
  namespace {
    struct profile_name_t {
      Profile profile;
      std::string_view name;
    };

    constexpr std::array PROFILE_NAMES {
      profile_name_t {Profile::xbox_360, "xbox_360"},
      profile_name_t {Profile::xbox_one, "xbox_one"},
      profile_name_t {Profile::xbox_series, "xbox_series"},
      profile_name_t {Profile::xbox_elite, "xbox_elite"},
      profile_name_t {Profile::dualshock4, "dualshock4"},
      profile_name_t {Profile::dualsense, "dualsense"},
      profile_name_t {Profile::dualsense_edge, "dualsense_edge"},
      profile_name_t {Profile::switch_pro, "switch_pro"},
      profile_name_t {Profile::steam_controller, "steam_controller"},
      profile_name_t {Profile::steam_deck, "steam_deck"},
      profile_name_t {Profile::generic, "generic"},
    };

    struct profile_alias_t {
      std::string_view alias;
      Profile profile;
    };

    constexpr std::array PROFILE_ALIASES {
      profile_alias_t {"xbox_360", Profile::xbox_360},
      profile_alias_t {"xbox360", Profile::xbox_360},
      profile_alias_t {"x360", Profile::xbox_360},
      profile_alias_t {"xbox_one", Profile::xbox_one},
      profile_alias_t {"xboxone", Profile::xbox_one},
      profile_alias_t {"xone", Profile::xbox_one},
      profile_alias_t {"xbox_series", Profile::xbox_series},
      profile_alias_t {"xboxseries", Profile::xbox_series},
      profile_alias_t {"xseries", Profile::xbox_series},
      profile_alias_t {"xbox_elite", Profile::xbox_elite},
      profile_alias_t {"xboxelite", Profile::xbox_elite},
      profile_alias_t {"elite", Profile::xbox_elite},
      profile_alias_t {"elite_series_2", Profile::xbox_elite},
      profile_alias_t {"elite_2", Profile::xbox_elite},
      profile_alias_t {"dualshock4", Profile::dualshock4},
      profile_alias_t {"dualshock_4", Profile::dualshock4},
      profile_alias_t {"ds4", Profile::dualshock4},
      profile_alias_t {"ps4", Profile::dualshock4},
      profile_alias_t {"dualsense", Profile::dualsense},
      profile_alias_t {"dual_sense", Profile::dualsense},
      profile_alias_t {"ds5", Profile::dualsense},
      profile_alias_t {"ps5", Profile::dualsense},
      profile_alias_t {"dualsense_edge", Profile::dualsense_edge},
      profile_alias_t {"dual_sense_edge", Profile::dualsense_edge},
      profile_alias_t {"ds5_edge", Profile::dualsense_edge},
      profile_alias_t {"ds5edge", Profile::dualsense_edge},
      profile_alias_t {"ps5_edge", Profile::dualsense_edge},
      profile_alias_t {"switch_pro", Profile::switch_pro},
      profile_alias_t {"switchpro", Profile::switch_pro},
      profile_alias_t {"switch", Profile::switch_pro},
      profile_alias_t {"pro_controller", Profile::switch_pro},
      profile_alias_t {"steam_controller", Profile::steam_controller},
      profile_alias_t {"steamcontroller", Profile::steam_controller},
      profile_alias_t {"steam", Profile::steam_controller},
      profile_alias_t {"valve_steam_controller", Profile::steam_controller},
      profile_alias_t {"steam_deck", Profile::steam_deck},
      profile_alias_t {"steamdeck", Profile::steam_deck},
      profile_alias_t {"deck", Profile::steam_deck},
      profile_alias_t {"generic", Profile::generic},
      profile_alias_t {"hid", Profile::generic},
    };

    struct family_name_t {
      ControllerFamily family;
      std::string_view name;
    };

    constexpr std::array FAMILY_NAMES {
      family_name_t {ControllerFamily::unknown, "unknown"},
      family_name_t {ControllerFamily::xbox, "xbox"},
      family_name_t {ControllerFamily::playstation, "playstation"},
      family_name_t {ControllerFamily::nintendo, "nintendo"},
      family_name_t {ControllerFamily::steam, "steam"},
      family_name_t {ControllerFamily::steam_deck, "steam_deck"},
    };

    struct family_alias_t {
      std::string_view alias;
      ControllerFamily family;
    };

    constexpr std::array FAMILY_ALIASES {
      family_alias_t {"unknown", ControllerFamily::unknown},
      family_alias_t {"auto", ControllerFamily::unknown},
      family_alias_t {"xbox", ControllerFamily::xbox},
      family_alias_t {"xbox360", ControllerFamily::xbox},
      family_alias_t {"xbox_one", ControllerFamily::xbox},
      family_alias_t {"xboxseries", ControllerFamily::xbox},
      family_alias_t {"xbox_elite", ControllerFamily::xbox},
      family_alias_t {"elite", ControllerFamily::xbox},
      family_alias_t {"playstation", ControllerFamily::playstation},
      family_alias_t {"ps", ControllerFamily::playstation},
      family_alias_t {"ps3", ControllerFamily::playstation},
      family_alias_t {"ps4", ControllerFamily::playstation},
      family_alias_t {"ps5", ControllerFamily::playstation},
      family_alias_t {"dualshock", ControllerFamily::playstation},
      family_alias_t {"dualshock4", ControllerFamily::playstation},
      family_alias_t {"dualsense", ControllerFamily::playstation},
      family_alias_t {"dualsense_edge", ControllerFamily::playstation},
      family_alias_t {"ds4", ControllerFamily::playstation},
      family_alias_t {"ds5", ControllerFamily::playstation},
      family_alias_t {"nintendo", ControllerFamily::nintendo},
      family_alias_t {"switch", ControllerFamily::nintendo},
      family_alias_t {"switch_pro", ControllerFamily::nintendo},
      family_alias_t {"joycon", ControllerFamily::nintendo},
      family_alias_t {"joy_con", ControllerFamily::nintendo},
      family_alias_t {"steam", ControllerFamily::steam},
      family_alias_t {"steam_controller", ControllerFamily::steam},
      family_alias_t {"valve", ControllerFamily::steam},
      family_alias_t {"steam_deck", ControllerFamily::steam_deck},
      family_alias_t {"steamdeck", ControllerFamily::steam_deck},
      family_alias_t {"deck", ControllerFamily::steam_deck},
    };

    /**
     * @brief Compare two ASCII strings without case sensitivity.
     *
     * @param left First string.
     * @param right Second string.
     * @return True when both strings contain the same ASCII text.
     */
    bool equals_ignore_case(std::string_view left, std::string_view right) {
      return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), [](char lhs, char rhs) {
        return std::tolower(static_cast<unsigned char>(lhs)) == std::tolower(static_cast<unsigned char>(rhs));
      });
    }

    /**
     * @brief Append a profile to a compatibility chain if it is not present.
     *
     * @param chain Compatibility chain being built.
     * @param profile Profile to append.
     */
    void append_unique(std::vector<Profile> &chain, Profile profile) {
      if (std::find(chain.begin(), chain.end(), profile) == chain.end()) {
        chain.push_back(profile);
      }
    }
  }  // namespace

  const char *to_string(Profile profile) {
    for (const auto &entry : PROFILE_NAMES) {
      if (entry.profile == profile) {
        return entry.name.data();
      }
    }
    return "generic";
  }

  bool profile_from_string(const std::string &name, Profile &profile) {
    for (const auto &entry : PROFILE_ALIASES) {
      if (equals_ignore_case(entry.alias, name)) {
        profile = entry.profile;
        return true;
      }
    }
    return false;
  }

  const char *to_string(ControllerFamily family) {
    for (const auto &entry : FAMILY_NAMES) {
      if (entry.family == family) {
        return entry.name.data();
      }
    }
    return "unknown";
  }

  bool controller_family_from_string(const std::string &name, ControllerFamily &family) {
    for (const auto &entry : FAMILY_ALIASES) {
      if (equals_ignore_case(entry.alias, name)) {
        family = entry.family;
        return true;
      }
    }
    return false;
  }

  Capabilities profile_capabilities(Profile profile) {
    Capabilities capabilities;
    switch (profile) {
      case Profile::xbox_360:
        capabilities.rumble = true;
        return capabilities;
      case Profile::xbox_one:
        capabilities.rumble = true;
        capabilities.trigger_rumble = true;
        capabilities.battery = true;
        return capabilities;
      case Profile::xbox_series:
        capabilities.rumble = true;
        capabilities.trigger_rumble = true;
        capabilities.battery = true;
        capabilities.misc_buttons = 1;
        return capabilities;
      case Profile::xbox_elite:
        capabilities.rumble = true;
        capabilities.trigger_rumble = true;
        capabilities.battery = true;
        capabilities.paddles = true;
        capabilities.misc_buttons = 1;
        return capabilities;
      case Profile::dualshock4:
        capabilities.rumble = true;
        capabilities.leds = true;
        capabilities.imu = true;
        capabilities.touchpad = true;
        capabilities.battery = true;
        return capabilities;
      case Profile::dualsense:
        capabilities.rumble = true;
        capabilities.leds = true;
        capabilities.imu = true;
        capabilities.touchpad = true;
        capabilities.adaptive_triggers = true;
        capabilities.player_leds = true;
        capabilities.battery = true;
        capabilities.misc_buttons = 1;
        return capabilities;
      case Profile::dualsense_edge:
        capabilities = profile_capabilities(Profile::dualsense);
        capabilities.paddles = true;
        return capabilities;
      case Profile::switch_pro:
        capabilities.rumble = true;
        capabilities.leds = true;
        capabilities.player_leds = true;
        capabilities.imu = true;
        capabilities.battery = true;
        capabilities.misc_buttons = 1;
        return capabilities;
      case Profile::steam_controller:
        capabilities.rumble = true;
        capabilities.imu = true;
        capabilities.touchpad = true;
        capabilities.battery = true;
        capabilities.paddles = true;
        return capabilities;
      case Profile::steam_deck:
        capabilities.rumble = true;
        capabilities.imu = true;
        capabilities.touchpad = true;
        capabilities.battery = true;
        capabilities.paddles = true;
        capabilities.misc_buttons = 1;
        return capabilities;
      case Profile::generic:
        capabilities.rumble = true;
        return capabilities;
    }

    return capabilities;
  }

  ProfileIdentity profile_identity(Profile profile) {
    switch (profile) {
      case Profile::xbox_360:
        return {0x045E, 0x028E, 0x0114};
      case Profile::xbox_one:
        return {0x045E, 0x02EA, 0x0408};
      case Profile::xbox_series:
        return {0x045E, 0x0B12, 0x0500};
      case Profile::xbox_elite:
        return {0x045E, 0x0B05, 0x0903};
      case Profile::dualshock4:
        return {0x054C, 0x05C4, 0x0100};
      case Profile::dualsense:
        return {0x054C, 0x0CE6, 0x8111};
      case Profile::dualsense_edge:
        return {0x054C, 0x0DF2, 0x8111};
      case Profile::switch_pro:
        return {0x057E, 0x2009, 0x0210};
      case Profile::steam_controller:
        return {0x28DE, 0x1142, 0x0001};
      case Profile::steam_deck:
        return {0x28DE, 0x1205, 0x0001};
      case Profile::generic:
        return {0x1209, 0x0001, 0x0001};
    }

    return {0x1209, 0x0001, 0x0001};
  }

  Profile recommended_profile(const ControllerDescription &controller) {
    switch (controller.family) {
      case ControllerFamily::xbox:
        return controller.has_paddles ? Profile::xbox_elite : Profile::xbox_series;
      case ControllerFamily::playstation:
        if (controller.has_paddles) {
          return Profile::dualsense_edge;
        }
        if (controller.has_touchpad || controller.has_motion) {
          return Profile::dualsense;
        }
        return Profile::dualshock4;
      case ControllerFamily::nintendo:
        return Profile::switch_pro;
      case ControllerFamily::steam:
        return (controller.has_touchpad || controller.has_motion) ? Profile::steam_controller : Profile::xbox_series;
      case ControllerFamily::steam_deck:
        return (controller.has_touchpad || controller.has_motion || controller.has_paddles) ? Profile::steam_deck : Profile::xbox_series;
      case ControllerFamily::unknown:
        if (controller.has_paddles) {
          return controller.has_touchpad || controller.has_motion ? Profile::dualsense_edge : Profile::xbox_elite;
        }
        if (controller.has_touchpad || controller.has_motion) {
          return Profile::dualsense;
        }
        return controller.misc_buttons > 1 ? Profile::switch_pro : Profile::xbox_series;
    }

    return Profile::xbox_series;
  }

  std::vector<Profile> profile_compatibility_chain(Profile profile) {
    std::vector<Profile> chain;
    append_unique(chain, profile);

    switch (profile) {
      case Profile::xbox_360:
        append_unique(chain, Profile::generic);
        break;
      case Profile::xbox_one:
        append_unique(chain, Profile::xbox_series);
        append_unique(chain, Profile::xbox_360);
        append_unique(chain, Profile::generic);
        break;
      case Profile::xbox_series:
        append_unique(chain, Profile::xbox_one);
        append_unique(chain, Profile::xbox_360);
        append_unique(chain, Profile::generic);
        break;
      case Profile::xbox_elite:
        append_unique(chain, Profile::xbox_series);
        append_unique(chain, Profile::xbox_one);
        append_unique(chain, Profile::xbox_360);
        append_unique(chain, Profile::generic);
        break;
      case Profile::dualshock4:
        append_unique(chain, Profile::xbox_360);
        append_unique(chain, Profile::generic);
        break;
      case Profile::dualsense:
        append_unique(chain, Profile::dualshock4);
        append_unique(chain, Profile::xbox_360);
        append_unique(chain, Profile::generic);
        break;
      case Profile::dualsense_edge:
        append_unique(chain, Profile::dualsense);
        append_unique(chain, Profile::dualshock4);
        append_unique(chain, Profile::xbox_360);
        append_unique(chain, Profile::generic);
        break;
      case Profile::switch_pro:
        append_unique(chain, Profile::xbox_360);
        append_unique(chain, Profile::generic);
        break;
      case Profile::steam_controller:
        append_unique(chain, Profile::dualsense);
        append_unique(chain, Profile::dualshock4);
        append_unique(chain, Profile::xbox_360);
        append_unique(chain, Profile::generic);
        break;
      case Profile::steam_deck:
        append_unique(chain, Profile::steam_controller);
        append_unique(chain, Profile::dualsense);
        append_unique(chain, Profile::dualshock4);
        append_unique(chain, Profile::xbox_360);
        append_unique(chain, Profile::generic);
        break;
      case Profile::generic:
        append_unique(chain, Profile::xbox_360);
        break;
    }

    return chain;
  }

  namespace {
    /**
     * @brief Clamp a deadzone to the supported 0-30 percent range.
     *
     * @param percent Configured percentage.
     * @return Clamped percentage.
     */
    std::uint8_t clamp_deadzone(std::uint8_t percent) {
      return std::min<std::uint8_t>(percent, 30);
    }

    /**
     * @brief Normalize a signed axis with symmetric endpoints.
     *
     * @param value Signed 16-bit axis value.
     * @return Axis in the inclusive range -1.0 to 1.0.
     */
    float normalize_axis(std::int16_t value) {
      return value < 0 ? static_cast<float>(value) / 32768.0F : static_cast<float>(value) / 32767.0F;
    }

    /**
     * @brief Convert a normalized axis back to the signed 16-bit range.
     *
     * @param value Normalized axis.
     * @return Signed 16-bit axis value.
     */
    std::int16_t denormalize_axis(float value) {
      const auto scaled = value < 0 ? value * 32768.0F : value * 32767.0F;
      return static_cast<std::int16_t>(std::clamp<std::int32_t>(static_cast<std::int32_t>(std::lround(scaled)), -32768, 32767));
    }

    /**
     * @brief Apply a radial deadzone to one stick.
     *
     * @param x Horizontal axis.
     * @param y Vertical axis.
     * @param percent Deadzone percentage.
     * @return Sanitized horizontal and vertical axes.
     */
    std::pair<std::int16_t, std::int16_t> sanitize_stick(std::int16_t x, std::int16_t y, std::uint8_t percent) {
      const auto deadzone = static_cast<float>(clamp_deadzone(percent)) / 100.0F;
      if (deadzone <= 0.0F) {
        return {x, y};
      }

      auto nx = normalize_axis(x);
      auto ny = normalize_axis(y);
      const auto magnitude = std::sqrt(nx * nx + ny * ny);
      if (magnitude <= deadzone) {
        return {0, 0};
      }

      const auto clamped_magnitude = std::min(magnitude, 1.0F);
      const auto scaled_magnitude = (clamped_magnitude - deadzone) / (1.0F - deadzone);
      const auto scale = scaled_magnitude / magnitude;
      return {denormalize_axis(nx * scale), denormalize_axis(ny * scale)};
    }

    /**
     * @brief Apply a deadzone to one trigger.
     *
     * @param value Trigger value.
     * @param percent Deadzone percentage.
     * @return Sanitized trigger value.
     */
    std::uint8_t sanitize_trigger(std::uint8_t value, std::uint8_t percent) {
      const auto deadzone = clamp_deadzone(percent);
      if (deadzone == 0) {
        return value;
      }

      const auto threshold = static_cast<int>(std::lround(255.0 * deadzone / 100.0));
      if (value <= threshold) {
        return 0;
      }
      if (threshold >= 255) {
        return 255;
      }
      return static_cast<std::uint8_t>(std::lround((value - threshold) * 255.0 / (255 - threshold)));
    }
  }  // namespace

  void sanitize_gamepad_state(GamepadState &state, const AnalogDeadzone &deadzone) {
    std::tie(state.left_x, state.left_y) = sanitize_stick(state.left_x, state.left_y, deadzone.stick_percent);
    std::tie(state.right_x, state.right_y) = sanitize_stick(state.right_x, state.right_y, deadzone.stick_percent);
    state.left_trigger = sanitize_trigger(state.left_trigger, deadzone.trigger_percent);
    state.right_trigger = sanitize_trigger(state.right_trigger, deadzone.trigger_percent);
  }
}  // namespace apollo::input
