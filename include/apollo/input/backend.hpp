/**
 * @file include/apollo/input/backend.hpp
 * @brief Backend and device interfaces for the Apollo input library.
 */
#pragma once

#include "apollo/input/gamepad.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace apollo::input {

  /**
   * @brief Kind of backend selected by the caller.
   */
  enum class BackendKind {
    automatic = 0,  ///< Best backend for the current platform.
    vigem,  ///< ViGEmBus on Windows.
    uinput,  ///< Linux uinput.
    null_backend,  ///< No-op backend useful for tests and previews.
  };

  /**
   * @brief Feedback message received from the virtual device.
   */
  enum class FeedbackType {
    rumble = 0,  ///< Rumble motors.
    led,  ///< LED or lightbar.
    adaptive_trigger,  ///< Adaptive trigger effect.
    unknown,
  };

  /**
   * @brief Feedback payload from the operating system or a game.
   */
  struct Feedback {
    FeedbackType type = FeedbackType::unknown;  ///< Feedback kind.
    std::uint16_t low_frequency = 0;  ///< Low-frequency rumble motor.
    std::uint16_t high_frequency = 0;  ///< High-frequency rumble motor.
    std::uint8_t led_red = 0;  ///< LED red component.
    std::uint8_t led_green = 0;  ///< LED green component.
    std::uint8_t led_blue = 0;  ///< LED blue component.
    std::uint8_t trigger = 0;  ///< Trigger index for adaptive effects.
    std::uint8_t trigger_mode = 0;  ///< Backend-defined effect mode.
  };

  /**
   * @brief Callback invoked when the virtual device receives feedback.
   */
  using FeedbackCallback = std::function<void(const Feedback &)>;

  /**
   * @brief One created virtual input device.
   */
  class Device {
  public:
    virtual ~Device() = default;

    /**
     * @brief Description of the virtual device.
     *
     * @return Immutable device information.
     */
    virtual const DeviceInfo &info() const = 0;

    /**
     * @brief Whether the device is still connected to its backend.
     *
     * @return True when updates can be submitted.
     */
    virtual bool connected() const = 0;

    /**
     * @brief Submit a complete gamepad report.
     *
     * @param state Buttons, triggers, and sticks.
     * @return True when the backend accepted the report.
     */
    virtual bool update(const GamepadState &state) = 0;

    /**
     * @brief Configure analog drift filtering for this device.
     *
     * Backends that forward analog axes apply the deadzone before submitting
     * the report. Devices that do not support analog filtering ignore it.
     *
     * @param deadzone Stick and trigger deadzone configuration.
     */
    virtual void set_deadzone(const AnalogDeadzone &deadzone) {
      (void) deadzone;
    }

    /**
     * @brief Submit a touchpad report when the backend supports it.
     *
     * @param state Touchpad state.
     * @return True when the backend accepted the report.
     */
    virtual bool update_touchpad(const TouchpadState &state) {
      (void) state;
      return false;
    }

    /**
     * @brief Submit an IMU sample when the backend supports it.
     *
     * @param sample Gyroscope and accelerometer sample.
     * @return True when the backend accepted the sample.
     */
    virtual bool update_imu(const ImuSample &sample) {
      (void) sample;
      return false;
    }

    /**
     * @brief Replace the feedback callback.
     *
     * @param callback Callback invoked from the backend thread.
     */
    virtual void set_feedback_callback(FeedbackCallback callback) {
      (void) callback;
    }

    /**
     * @brief Remove the virtual device from the system.
     */
    virtual void disconnect() = 0;
  };

  /**
   * @brief Factory for virtual input devices.
   */
  class Backend {
  public:
    virtual ~Backend() = default;

    /**
     * @brief Stable backend name.
     *
     * @return Static string such as "vigem".
     */
    virtual const char *name() const = 0;

    /**
     * @brief Check whether the backend can work on this machine.
     *
     * @param error Optional human-readable reason when unavailable.
     * @return True when a device can be created.
     */
    virtual bool available(std::string *error = nullptr) const = 0;

    /**
     * @brief Create a virtual device.
     *
     * @param profile Requested controller profile.
     * @param name Name shown by the operating system.
     * @param error Optional human-readable reason when creation fails.
     * @return Device, or null when the profile or backend is unavailable.
     */
    virtual std::unique_ptr<Device> create(Profile profile, const std::string &name, std::string *error = nullptr) = 0;

    /**
     * @brief Create a device, falling back to a simpler compatible profile.
     *
     * The requested profile is tried first. When the backend cannot create it,
     * progressively simpler profiles from @ref profile_compatibility_chain()
     * are tried. The created device reports the profile it actually emulates in
     * @ref DeviceInfo::profile.
     *
     * @param profile Preferred controller profile.
     * @param name Name shown by the operating system.
     * @param error Optional human-readable reason when every profile fails.
     * @return Device, or null when no compatible profile is available.
     */
    std::unique_ptr<Device> create_compatible(Profile profile, const std::string &name, std::string *error = nullptr);

    /**
     * @brief Profiles supported by this backend.
     *
     * @return Supported profile list.
     */
    virtual std::vector<Profile> supported_profiles() const = 0;
  };

  /**
   * @brief Create a backend for the current platform.
   *
   * @param kind Requested backend, or automatic.
   * @return Backend instance. Never null; falls back to the null backend.
   */
  std::unique_ptr<Backend> create_backend(BackendKind kind = BackendKind::automatic);

  /**
   * @brief Convert a backend kind to a stable short name.
   *
   * @param kind Backend kind.
   * @return Static string.
   */
  const char *to_string(BackendKind kind);

  /**
   * @brief Parse a backend kind from its short name or a compatible alias.
   *
   * Matching is case-insensitive. Aliases include "auto", "vigembus",
   * "linux_uinput", and "none".
   *
   * @param name Backend name.
   * @param kind Receives the parsed kind.
   * @return True when the name is valid.
   */
  bool backend_from_string(const std::string &name, BackendKind &kind);

}  // namespace apollo::input
