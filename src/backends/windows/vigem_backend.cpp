/**
 * @file src/backends/windows/vigem_backend.cpp
 * @brief ViGEmBus backend for Windows virtual controllers.
 */
#include "apollo/input/backend.hpp"

#include "backends/backend_factory.hpp"

#ifdef _WIN32

  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>

  #include <ViGEm/Client.h>

  #include <algorithm>
  #include <atomic>
  #include <cstdio>
  #include <cstdint>
  #include <mutex>
  #include <string>
  #include <utility>

namespace apollo::input::detail {
  namespace {
    /**
     * @brief Format a ViGEm error for diagnostics.
     *
     * @param error ViGEm result code.
     * @return Human-readable error string.
     */
    std::string vigem_error_string(VIGEM_ERROR error) {
      char buffer[64];
      std::snprintf(buffer, sizeof(buffer), "ViGEmBus error 0x%08X", static_cast<unsigned int>(error));
      return buffer;
    }

    /**
     * @brief Convert a signed stick value to the DS4 0-255 range.
     *
     * @param value Signed 16-bit stick value.
     * @return Unsigned 8-bit stick value.
     */
    std::uint8_t ds4_stick(std::int16_t value) {
      return static_cast<std::uint8_t>((static_cast<std::int32_t>(value) + 32768) >> 8);
    }

    /**
     * @brief Convert a floating-point sensor value to a clamped DS4 short.
     *
     * @param value Sensor value.
     * @param scale Multiplier applied before conversion.
     * @return Clamped signed 16-bit value.
     */
    SHORT ds4_sensor(float value, float scale) {
      const float scaled = value * scale;
      const float clamped = std::clamp(scaled, -32768.0F, 32767.0F);
      return static_cast<SHORT>(clamped);
    }

    /**
     * @brief Scale factor from radians per second to DS4 gyro units.
     *
     * DS4 reports 16 units per degree per second.
     */
    constexpr float DS4_GYRO_SCALE = 16.0F * 57.2957795F;

    /**
     * @brief Scale factor from meters per second squared to DS4 accelerometer units.
     *
     * DS4 reports 8192 units at one standard gravity.
     */
    constexpr float DS4_ACCEL_SCALE = 8192.0F / 9.80665F;

    /**
     * @brief Build the DS4 D-pad direction from the button mask.
     *
     * @param state Gamepad state.
     * @return DS4 D-pad direction.
     */
    DS4_DPAD_DIRECTIONS ds4_dpad(const GamepadState &state) {
      const bool up = state.pressed(Button::dpad_up);
      const bool down = state.pressed(Button::dpad_down);
      const bool left = state.pressed(Button::dpad_left);
      const bool right = state.pressed(Button::dpad_right);

      if (up && left) {
        return DS4_BUTTON_DPAD_NORTHWEST;
      }
      if (up && right) {
        return DS4_BUTTON_DPAD_NORTHEAST;
      }
      if (down && left) {
        return DS4_BUTTON_DPAD_SOUTHWEST;
      }
      if (down && right) {
        return DS4_BUTTON_DPAD_SOUTHEAST;
      }
      if (up) {
        return DS4_BUTTON_DPAD_NORTH;
      }
      if (down) {
        return DS4_BUTTON_DPAD_SOUTH;
      }
      if (left) {
        return DS4_BUTTON_DPAD_WEST;
      }
      if (right) {
        return DS4_BUTTON_DPAD_EAST;
      }
      return DS4_BUTTON_DPAD_NONE;
    }

    /**
     * @brief One virtual controller backed by ViGEmBus.
     */
    class VigemDevice final: public Device {
    public:
      VigemDevice(PVIGEM_CLIENT client, PVIGEM_TARGET target, Profile profile, std::string name):
          client_(client),
          target_(target),
          info_ {profile, capabilities_for(profile), std::move(name)} {
        if (info_.profile == Profile::xbox_360) {
          vigem_target_x360_register_notification(client_, target_, &VigemDevice::x360_notification, this);
        } else {
          vigem_target_ds4_register_notification(client_, target_, &VigemDevice::ds4_notification, this);
        }
      }

      ~VigemDevice() override {
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

        if (info_.profile == Profile::xbox_360) {
          return send_x360(sanitized);
        }
        if (info_.profile == Profile::dualshock4) {
          update_ds4_state(sanitized);
          return send_ds4();
        }
        return false;
      }

      void set_deadzone(const AnalogDeadzone &deadzone) override {
        std::scoped_lock lock(mutex_);
        deadzone_ = deadzone;
      }

      bool update_touchpad(const TouchpadState &state) override {
        std::scoped_lock lock(mutex_);
        if (!connected_ || info_.profile != Profile::dualshock4) {
          return false;
        }

        auto &report = ds4_report_.Report;
        report.bTouchPacketsN = state.active ? 1 : 0;
        report.sCurrentTouch.bPacketCounter = ++touch_packet_;
        report.sCurrentTouch.bIsUpTrackingNum1 = state.active ? 0x00 : 0x80;
        const auto x = static_cast<std::uint16_t>(std::min<std::uint32_t>(state.x, 1919));
        const auto y = static_cast<std::uint16_t>(std::min<std::uint32_t>(state.y, 942));
        report.sCurrentTouch.bTouchData1[0] = static_cast<BYTE>(x & 0xFF);
        report.sCurrentTouch.bTouchData1[1] = static_cast<BYTE>(((x >> 8) & 0x0F) | ((y & 0x0F) << 4));
        report.sCurrentTouch.bTouchData1[2] = static_cast<BYTE>((y >> 4) & 0xFF);
        return send_ds4();
      }

      bool update_imu(const ImuSample &sample) override {
        std::scoped_lock lock(mutex_);
        if (!connected_ || info_.profile != Profile::dualshock4) {
          return false;
        }

        auto &report = ds4_report_.Report;
        report.wGyroX = ds4_sensor(sample.gyro_x, DS4_GYRO_SCALE);
        report.wGyroY = ds4_sensor(sample.gyro_y, DS4_GYRO_SCALE);
        report.wGyroZ = ds4_sensor(sample.gyro_z, DS4_GYRO_SCALE);
        report.wAccelX = ds4_sensor(sample.accel_x, DS4_ACCEL_SCALE);
        report.wAccelY = ds4_sensor(sample.accel_y, DS4_ACCEL_SCALE);
        report.wAccelZ = ds4_sensor(sample.accel_z, DS4_ACCEL_SCALE);
        return send_ds4();
      }

      void set_feedback_callback(FeedbackCallback callback) override {
        std::scoped_lock lock(mutex_);
        feedback_ = std::move(callback);
      }

      void disconnect() override {
        PVIGEM_CLIENT client = nullptr;
        PVIGEM_TARGET target = nullptr;
        Profile profile = Profile::generic;
        {
          std::scoped_lock lock(mutex_);
          if (!connected_) {
            return;
          }
          connected_ = false;
          client = client_;
          target = target_;
          profile = info_.profile;
          client_ = nullptr;
          target_ = nullptr;
        }

        if (profile == Profile::xbox_360) {
          vigem_target_x360_unregister_notification(target);
        }
        vigem_target_remove(client, target);
        vigem_target_free(target);
        vigem_disconnect(client);
        vigem_free(client);
      }

    private:
      /**
       * @brief Capabilities advertised for one profile.
       *
       * @param profile Controller profile.
       * @return Capability flags.
       */
      static Capabilities capabilities_for(Profile profile) {
        auto capabilities = profile_capabilities(profile);

        // ViGEmBus only provides the features implemented by its X360 and DS4
        // reports. Callers should use create_compatible() for other profiles.
        capabilities.trigger_rumble = false;
        capabilities.player_leds = false;
        capabilities.battery = false;
        capabilities.paddles = false;
        capabilities.misc_buttons = 0;
        capabilities.adaptive_triggers = false;

        if (profile != Profile::dualshock4) {
          capabilities.leds = false;
          capabilities.imu = false;
          capabilities.touchpad = false;
        }
        return capabilities;
      }

      /**
       * @brief Send an Xbox 360 report.
       *
       * @param state Gamepad state.
       * @return True when ViGEmBus accepted the report.
       */
      bool send_x360(const GamepadState &state) {
        XUSB_REPORT report {};
        XUSB_REPORT_INIT(&report);

        if (state.pressed(Button::a)) {
          report.wButtons |= XUSB_GAMEPAD_A;
        }
        if (state.pressed(Button::b)) {
          report.wButtons |= XUSB_GAMEPAD_B;
        }
        if (state.pressed(Button::x)) {
          report.wButtons |= XUSB_GAMEPAD_X;
        }
        if (state.pressed(Button::y)) {
          report.wButtons |= XUSB_GAMEPAD_Y;
        }
        if (state.pressed(Button::dpad_up)) {
          report.wButtons |= XUSB_GAMEPAD_DPAD_UP;
        }
        if (state.pressed(Button::dpad_down)) {
          report.wButtons |= XUSB_GAMEPAD_DPAD_DOWN;
        }
        if (state.pressed(Button::dpad_left)) {
          report.wButtons |= XUSB_GAMEPAD_DPAD_LEFT;
        }
        if (state.pressed(Button::dpad_right)) {
          report.wButtons |= XUSB_GAMEPAD_DPAD_RIGHT;
        }
        if (state.pressed(Button::start) || state.pressed(Button::options)) {
          report.wButtons |= XUSB_GAMEPAD_START;
        }
        if (state.pressed(Button::back) || state.pressed(Button::share) || state.pressed(Button::misc2)) {
          report.wButtons |= XUSB_GAMEPAD_BACK;
        }
        if (state.pressed(Button::guide)) {
          report.wButtons |= XUSB_GAMEPAD_GUIDE;
        }
        if (state.pressed(Button::left_thumb)) {
          report.wButtons |= XUSB_GAMEPAD_LEFT_THUMB;
        }
        if (state.pressed(Button::right_thumb)) {
          report.wButtons |= XUSB_GAMEPAD_RIGHT_THUMB;
        }
        if (state.pressed(Button::left_shoulder)) {
          report.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER;
        }
        if (state.pressed(Button::right_shoulder)) {
          report.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER;
        }

        report.bLeftTrigger = state.left_trigger;
        report.bRightTrigger = state.right_trigger;
        report.sThumbLX = state.left_x;
        report.sThumbLY = state.left_y;
        report.sThumbRX = state.right_x;
        report.sThumbRY = state.right_y;

        return VIGEM_SUCCESS(vigem_target_x360_update(client_, target_, report));
      }

      /**
       * @brief Update the cached DS4 report from a gamepad state.
       *
       * @param state Gamepad state.
       */
      void update_ds4_state(const GamepadState &state) {
        auto &report = ds4_report_.Report;
        report.bThumbLX = ds4_stick(state.left_x);
        report.bThumbLY = ds4_stick(state.left_y);
        report.bThumbRX = ds4_stick(state.right_x);
        report.bThumbRY = ds4_stick(state.right_y);
        report.bTriggerL = state.left_trigger;
        report.bTriggerR = state.right_trigger;
        report.wButtons = 0;

        if (state.pressed(Button::a)) {
          report.wButtons |= DS4_BUTTON_CROSS;
        }
        if (state.pressed(Button::b)) {
          report.wButtons |= DS4_BUTTON_CIRCLE;
        }
        if (state.pressed(Button::x)) {
          report.wButtons |= DS4_BUTTON_SQUARE;
        }
        if (state.pressed(Button::y)) {
          report.wButtons |= DS4_BUTTON_TRIANGLE;
        }
        if (state.pressed(Button::left_shoulder)) {
          report.wButtons |= DS4_BUTTON_SHOULDER_LEFT;
        }
        if (state.pressed(Button::right_shoulder)) {
          report.wButtons |= DS4_BUTTON_SHOULDER_RIGHT;
        }
        if (state.left_trigger > 0) {
          report.wButtons |= DS4_BUTTON_TRIGGER_LEFT;
        }
        if (state.right_trigger > 0) {
          report.wButtons |= DS4_BUTTON_TRIGGER_RIGHT;
        }
        if (state.pressed(Button::left_thumb)) {
          report.wButtons |= DS4_BUTTON_THUMB_LEFT;
        }
        if (state.pressed(Button::right_thumb)) {
          report.wButtons |= DS4_BUTTON_THUMB_RIGHT;
        }
        if (state.pressed(Button::back) || state.pressed(Button::share)) {
          report.wButtons |= DS4_BUTTON_SHARE;
        }
        if (state.pressed(Button::start) || state.pressed(Button::options)) {
          report.wButtons |= DS4_BUTTON_OPTIONS;
        }

        report.wButtons &= ~0xF;
        report.wButtons |= static_cast<USHORT>(ds4_dpad(state));

        report.bSpecial = 0;
        if (state.pressed(Button::guide)) {
          report.bSpecial |= DS4_SPECIAL_BUTTON_PS;
        }
        if (state.pressed(Button::touchpad_click) || state.pressed(Button::touchpad)) {
          report.bSpecial |= DS4_SPECIAL_BUTTON_TOUCHPAD;
        }
      }

      /**
       * @brief Send the cached DS4 report.
       *
       * @return True when ViGEmBus accepted the report.
       */
      bool send_ds4() {
        return VIGEM_SUCCESS(vigem_target_ds4_update_ex(client_, target_, ds4_report_));
      }

      /**
       * @brief Forward Xbox 360 rumble feedback.
       */
      static void CALLBACK x360_notification(PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR large_motor, UCHAR small_motor, UCHAR, LPVOID user_data) {
        if (!user_data) {
          return;
        }
        static_cast<VigemDevice *>(user_data)->emit_feedback(large_motor, small_motor, 0, 0, 0, false);
      }

      /**
       * @brief Forward DS4 rumble and lightbar feedback.
       */
      static void CALLBACK ds4_notification(PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR large_motor, UCHAR small_motor, DS4_LIGHTBAR_COLOR color, LPVOID user_data) {
        if (!user_data) {
          return;
        }
        static_cast<VigemDevice *>(user_data)->emit_feedback(large_motor, small_motor, color.Red, color.Green, color.Blue, true);
      }

      /**
       * @brief Invoke the user feedback callback.
       */
      void emit_feedback(UCHAR large_motor, UCHAR small_motor, UCHAR red, UCHAR green, UCHAR blue, bool has_lightbar) {
        FeedbackCallback callback;
        {
          std::scoped_lock lock(mutex_);
          callback = feedback_;
        }
        if (!callback) {
          return;
        }

        Feedback feedback;
        feedback.type = FeedbackType::rumble;
        feedback.low_frequency = static_cast<std::uint16_t>(large_motor) * 257U;
        feedback.high_frequency = static_cast<std::uint16_t>(small_motor) * 257U;
        callback(feedback);

        if (has_lightbar) {
          feedback = {};
          feedback.type = FeedbackType::led;
          feedback.led_red = red;
          feedback.led_green = green;
          feedback.led_blue = blue;
          callback(feedback);
        }
      }

      PVIGEM_CLIENT client_ = nullptr;
      PVIGEM_TARGET target_ = nullptr;
      DeviceInfo info_;
      DS4_REPORT_EX ds4_report_ {};
      std::uint8_t touch_packet_ = 0;
      AnalogDeadzone deadzone_;
      FeedbackCallback feedback_;
      std::mutex mutex_;
      std::atomic<bool> connected_ {true};
    };

    /**
     * @brief ViGEmBus backend factory.
     */
    class VigemBackend final: public Backend {
    public:
      const char *name() const override {
        return "vigem";
      }

      bool available(std::string *error) const override {
        PVIGEM_CLIENT client = vigem_alloc();
        if (!client) {
          if (error) {
            *error = "Unable to allocate a ViGEmBus client";
          }
          return false;
        }

        const auto result = vigem_connect(client);
        if (!VIGEM_SUCCESS(result)) {
          if (error) {
            *error = vigem_error_string(result);
          }
          vigem_free(client);
          return false;
        }

        vigem_disconnect(client);
        vigem_free(client);
        if (error) {
          error->clear();
        }
        return true;
      }

      std::unique_ptr<Device> create(Profile profile, const std::string &name, std::string *error) override {
        if (profile != Profile::xbox_360 && profile != Profile::dualshock4) {
          if (error) {
            *error = "ViGEmBus only supports Xbox 360 and DualShock 4 profiles; use create_compatible() to fall back";
          }
          return nullptr;
        }

        PVIGEM_CLIENT client = vigem_alloc();
        if (!client) {
          if (error) {
            *error = "Unable to allocate a ViGEmBus client";
          }
          return nullptr;
        }

        auto result = vigem_connect(client);
        if (!VIGEM_SUCCESS(result)) {
          if (error) {
            *error = vigem_error_string(result);
          }
          vigem_free(client);
          return nullptr;
        }

        PVIGEM_TARGET target = profile == Profile::xbox_360 ? vigem_target_x360_alloc() : vigem_target_ds4_alloc();
        if (!target) {
          if (error) {
            *error = "Unable to allocate a ViGEmBus target";
          }
          vigem_disconnect(client);
          vigem_free(client);
          return nullptr;
        }

        result = vigem_target_add(client, target);
        if (!VIGEM_SUCCESS(result)) {
          if (error) {
            *error = vigem_error_string(result);
          }
          vigem_target_free(target);
          vigem_disconnect(client);
          vigem_free(client);
          return nullptr;
        }

        if (error) {
          error->clear();
        }
        return std::make_unique<VigemDevice>(client, target, profile, name);
      }

      std::vector<Profile> supported_profiles() const override {
        return {Profile::xbox_360, Profile::dualshock4};
      }
    };
  }  // namespace

  std::unique_ptr<Backend> create_vigem_backend() {
    return std::make_unique<VigemBackend>();
  }
}  // namespace apollo::input::detail

#endif
