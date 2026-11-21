/**
 * @file src/core/null_backend.cpp
 * @brief No-op backend for tests, previews, and unsupported platforms.
 */
#include "apollo/input/backend.hpp"

#include "backends/backend_factory.hpp"

#include <array>
#include <utility>

namespace apollo::input::detail {
  namespace {
    class NullDevice final: public Device {
    public:
      NullDevice(Profile profile, std::string name):
          info_ {profile, profile_capabilities(profile), std::move(name)} {
      }

      const DeviceInfo &info() const override {
        return info_;
      }

      bool connected() const override {
        return connected_;
      }

      bool update(const GamepadState &state) override {
        auto sanitized = state;
        sanitize_gamepad_state(sanitized, deadzone_);
        state_ = sanitized;
        return connected_;
      }

      void set_deadzone(const AnalogDeadzone &deadzone) override {
        deadzone_ = deadzone;
      }

      bool update_touchpad(const TouchpadState &state) override {
        if (!connected_ || !info_.capabilities.touchpad) {
          return false;
        }
        touchpad_ = state;
        return true;
      }

      bool update_imu(const ImuSample &sample) override {
        if (!connected_ || !info_.capabilities.imu) {
          return false;
        }
        imu_ = sample;
        return true;
      }

      void disconnect() override {
        connected_ = false;
      }

    private:
      DeviceInfo info_;
      AnalogDeadzone deadzone_;
      GamepadState state_ {};
      TouchpadState touchpad_ {};
      ImuSample imu_ {};
      bool connected_ = true;
    };

    class NullBackend final: public Backend {
    public:
      const char *name() const override {
        return "null";
      }

      bool available(std::string *error) const override {
        if (error) {
          error->clear();
        }
        return true;
      }

      std::unique_ptr<Device> create(Profile profile, const std::string &name, std::string *error) override {
        if (error) {
          error->clear();
        }
        return std::make_unique<NullDevice>(profile, name);
      }

      std::vector<Profile> supported_profiles() const override {
        return {
          Profile::xbox_360,
          Profile::xbox_one,
          Profile::xbox_series,
          Profile::xbox_elite,
          Profile::dualshock4,
          Profile::dualsense,
          Profile::dualsense_edge,
          Profile::switch_pro,
          Profile::steam_controller,
          Profile::steam_deck,
          Profile::generic,
        };
      }
    };
  }  // namespace

  std::unique_ptr<Backend> create_null_backend() {
    return std::make_unique<NullBackend>();
  }
}  // namespace apollo::input::detail
