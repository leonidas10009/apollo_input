/**
 * @file src/backends/linux/uinput_backend.cpp
 * @brief Linux uinput backend for profile-aware virtual gamepads.
 */
#include "apollo/input/backend.hpp"

#include "backends/backend_factory.hpp"

#ifdef __linux__

  #include <linux/input.h>
  #include <linux/uinput.h>

  #include <fcntl.h>
  #include <sys/ioctl.h>
  #include <unistd.h>

  #include <array>
  #include <cerrno>
  #include <cstdint>
  #include <cstdio>
  #include <cstring>
  #include <mutex>
  #include <string>
  #include <utility>

namespace apollo::input::detail {
  namespace {
    /**
     * @brief Write one input event.
     *
     * @param fd uinput file descriptor.
     * @param type Event type.
     * @param code Event code.
     * @param value Event value.
     * @return True on success.
     */
    bool emit_event(int fd, std::uint16_t type, std::uint16_t code, std::int32_t value) {
      input_event event {};
      event.type = type;
      event.code = code;
      event.value = value;
      return ::write(fd, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event));
    }

    /**
     * @brief One logical button mapped to an evdev key code.
     */
    struct button_mapping_t {
      Button button;  ///< Portable button.
      std::uint16_t code;  ///< Linux evdev key code.
    };

    /**
     * @brief Buttons exposed by every uinput profile.
     *
     * The extra buttons use BTN_TRIGGER_HAPPY codes so share, touchpad click,
     * and paddles remain distinguishable without breaking standard gamepad
     * mappings. SDL and Steam can map them to misc and paddle controls.
     */
    constexpr std::array BUTTON_MAPPINGS {
      button_mapping_t {Button::a, BTN_A},
      button_mapping_t {Button::b, BTN_B},
      button_mapping_t {Button::x, BTN_X},
      button_mapping_t {Button::y, BTN_Y},
      button_mapping_t {Button::left_shoulder, BTN_TL},
      button_mapping_t {Button::right_shoulder, BTN_TR},
      button_mapping_t {Button::start, BTN_START},
      button_mapping_t {Button::back, BTN_SELECT},
      button_mapping_t {Button::guide, BTN_MODE},
      button_mapping_t {Button::left_thumb, BTN_THUMBL},
      button_mapping_t {Button::right_thumb, BTN_THUMBR},
      button_mapping_t {Button::dpad_up, BTN_DPAD_UP},
      button_mapping_t {Button::dpad_down, BTN_DPAD_DOWN},
      button_mapping_t {Button::dpad_left, BTN_DPAD_LEFT},
      button_mapping_t {Button::dpad_right, BTN_DPAD_RIGHT},
      button_mapping_t {Button::paddle1, BTN_TRIGGER_HAPPY1},
      button_mapping_t {Button::paddle2, BTN_TRIGGER_HAPPY2},
      button_mapping_t {Button::paddle3, BTN_TRIGGER_HAPPY3},
      button_mapping_t {Button::paddle4, BTN_TRIGGER_HAPPY4},
      button_mapping_t {Button::share, BTN_TRIGGER_HAPPY5},
      button_mapping_t {Button::touchpad_click, BTN_TRIGGER_HAPPY6},
      button_mapping_t {Button::misc2, BTN_TRIGGER_HAPPY7},
    };

    /**
     * @brief Collapse aliases that share one physical control.
     *
     * A caller may set options/start or touchpad/touchpad_click together. The
     * evdev device exposes one code per physical control, so normalize the
     * logical state before comparing it with the previous report.
     *
     * @param state Portable gamepad state.
     * @return Normalized button mask.
     */
    std::uint32_t effective_buttons(const GamepadState &state) {
      auto buttons = state.buttons;
      buttons &= ~button_bit(Button::options);
      buttons &= ~button_bit(Button::touchpad);
      if (state.pressed(Button::options)) {
        buttons |= button_bit(Button::start);
      }
      if (state.pressed(Button::touchpad)) {
        buttons |= button_bit(Button::touchpad_click);
      }
      return buttons;
    }

    /**
     * @brief One virtual gamepad backed by Linux uinput.
     */
    class UinputDevice final: public Device {
    public:
      UinputDevice(Profile profile, std::string name):
          info_ {profile, capabilities_for(profile), name.empty() ? "Apollo Virtual Gamepad" : std::move(name)} {
        const auto identity = profile_identity(profile);

        fd_ = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (fd_ < 0) {
          return;
        }

        ioctl(fd_, UI_SET_EVBIT, EV_KEY);
        ioctl(fd_, UI_SET_EVBIT, EV_ABS);
        for (const auto &mapping : BUTTON_MAPPINGS) {
          ioctl(fd_, UI_SET_KEYBIT, mapping.code);
        }

        for (const auto code : {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y}) {
          ioctl(fd_, UI_SET_ABSBIT, code);
        }

        uinput_user_dev device {};
        std::snprintf(device.name, sizeof(device.name), "%s", info_.name.c_str());
        device.id.bustype = BUS_USB;
        device.id.vendor = identity.vendor_id;
        device.id.product = identity.product_id;
        device.id.version = identity.version;

        set_abs(device, ABS_X, -32768, 32767);
        set_abs(device, ABS_Y, -32768, 32767);
        set_abs(device, ABS_RX, -32768, 32767);
        set_abs(device, ABS_RY, -32768, 32767);
        set_abs(device, ABS_Z, 0, 255);
        set_abs(device, ABS_RZ, 0, 255);
        set_abs(device, ABS_HAT0X, -1, 1);
        set_abs(device, ABS_HAT0Y, -1, 1);

        if (::write(fd_, &device, sizeof(device)) != static_cast<ssize_t>(sizeof(device))) {
          ::close(fd_);
          fd_ = -1;
          return;
        }
        if (ioctl(fd_, UI_DEV_CREATE) < 0) {
          ::close(fd_);
          fd_ = -1;
          return;
        }

        connected_ = true;
        sync_();
      }

      ~UinputDevice() override {
        disconnect();
      }

      const DeviceInfo &info() const override {
        return info_;
      }

      bool connected() const override {
        return connected_;
      }

      bool update(const GamepadState &state) override {
        std::scoped_lock lock(mutex_);
        if (!connected_) {
          return false;
        }

        auto sanitized = state;
        sanitize_gamepad_state(sanitized, deadzone_);

        const auto previous = effective_buttons(state_);
        state_ = sanitized;
        const auto current = effective_buttons(state_);

        emit_buttons_(previous, current);
        emit_abs_(ABS_X, previous_state_.left_x, sanitized.left_x);
        emit_abs_(ABS_Y, previous_state_.left_y, sanitized.left_y);
        emit_abs_(ABS_RX, previous_state_.right_x, sanitized.right_x);
        emit_abs_(ABS_RY, previous_state_.right_y, sanitized.right_y);
        emit_abs_(ABS_Z, previous_state_.left_trigger, sanitized.left_trigger);
        emit_abs_(ABS_RZ, previous_state_.right_trigger, sanitized.right_trigger);

        const int hat_x = (sanitized.pressed(Button::dpad_right) ? 1 : 0) - (sanitized.pressed(Button::dpad_left) ? 1 : 0);
        const int hat_y = (sanitized.pressed(Button::dpad_down) ? 1 : 0) - (sanitized.pressed(Button::dpad_up) ? 1 : 0);
        emit_abs_(ABS_HAT0X, last_hat_x_, hat_x);
        emit_abs_(ABS_HAT0Y, last_hat_y_, hat_y);
        last_hat_x_ = hat_x;
        last_hat_y_ = hat_y;

        previous_state_ = sanitized;
        return sync_();
      }

      void set_deadzone(const AnalogDeadzone &deadzone) override {
        std::scoped_lock lock(mutex_);
        deadzone_ = deadzone;
      }

      void disconnect() override {
        std::scoped_lock lock(mutex_);
        if (!connected_) {
          return;
        }
        connected_ = false;
        ioctl(fd_, UI_DEV_DESTROY);
        ::close(fd_);
        fd_ = -1;
      }

    private:
      /**
       * @brief Capabilities exposed by the uinput backend for a profile.
       *
       * uinput provides digital buttons, sticks, and triggers. It cannot expose
       * HID reports, motion sensors, touchpads, LEDs, battery, or force
       * feedback, but profile-specific paddles and misc buttons remain usable.
       *
       * @param profile Virtual controller profile.
       * @return Capability flags.
       */
      static Capabilities capabilities_for(Profile profile) {
        auto capabilities = profile_capabilities(profile);
        capabilities.rumble = false;
        capabilities.trigger_rumble = false;
        capabilities.leds = false;
        capabilities.imu = false;
        capabilities.touchpad = false;
        capabilities.adaptive_triggers = false;
        capabilities.player_leds = false;
        capabilities.battery = false;
        return capabilities;
      }

      /**
       * @brief Configure one absolute axis.
       */
      static void set_abs(uinput_user_dev &device, std::uint16_t code, int minimum, int maximum) {
        device.absmin[code] = minimum;
        device.absmax[code] = maximum;
      }

      /**
       * @brief Emit changed digital buttons.
       */
      void emit_buttons_(std::uint32_t previous, std::uint32_t current) {
        for (const auto &mapping : BUTTON_MAPPINGS) {
          const bool was_pressed = (previous & button_bit(mapping.button)) != 0;
          const bool is_pressed = (current & button_bit(mapping.button)) != 0;
          if (was_pressed != is_pressed) {
            emit_event(fd_, EV_KEY, mapping.code, is_pressed ? 1 : 0);
          }
        }
      }

      /**
       * @brief Emit an absolute axis when it changes.
       */
      void emit_abs_(std::uint16_t code, std::int32_t previous, std::int32_t current) {
        if (previous != current) {
          emit_event(fd_, EV_ABS, code, current);
        }
      }

      /**
       * @brief Flush the report.
       */
      bool sync_() {
        return emit_event(fd_, EV_SYN, SYN_REPORT, 0);
      }

      int fd_ = -1;
      DeviceInfo info_;
      AnalogDeadzone deadzone_;
      GamepadState state_;
      GamepadState previous_state_;
      int last_hat_x_ = 0;
      int last_hat_y_ = 0;
      std::mutex mutex_;
      bool connected_ = false;
    };

    /**
     * @brief Linux uinput backend factory.
     */
    class UinputBackend final: public Backend {
    public:
      const char *name() const override {
        return "uinput";
      }

      bool available(std::string *error) const override {
        if (::access("/dev/uinput", W_OK) == 0) {
          if (error) {
            error->clear();
          }
          return true;
        }
        if (error) {
          *error = std::string {"/dev/uinput is not writable: "} + std::strerror(errno);
        }
        return false;
      }

      std::unique_ptr<Device> create(Profile profile, const std::string &name, std::string *error) override {
        auto device = std::make_unique<UinputDevice>(profile, name);
        if (!device->connected()) {
          if (error) {
            *error = "Unable to create a uinput device (check /dev/uinput permissions)";
          }
          return nullptr;
        }
        if (error) {
          error->clear();
        }
        return device;
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

  std::unique_ptr<Backend> create_uinput_backend() {
    return std::make_unique<UinputBackend>();
  }
}  // namespace apollo::input::detail

#endif
