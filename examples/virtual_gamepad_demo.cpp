/**
 * @file examples/virtual_gamepad_demo.cpp
 * @brief Small CLI that creates a virtual gamepad and animates its inputs.
 */
#include "apollo/input/input.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {
  /**
   * @brief Print command-line usage.
   *
   * @param program Program name.
   */
  void usage(const char *program) {
    std::printf(
      "Usage: %s [options]\n"
      "  --backend <auto|vigem|uinput|null>  Backend to use (default: auto)\n"
      "  --profile <name>                    Controller profile (default: xbox_360)\n"
      "                                      Aliases: x360, xone, xseries, xbox_elite, ds4, ds5,\n"
      "                                      ds5_edge, switch, steam, steam_deck, generic\n"
      "  --compatible                        Fall back to a simpler profile when needed\n"
      "  --seconds <n>                       How long to animate (default: 8)\n"
      "  --name <text>                       Device name\n"
      "  --help                              Show this help\n",
      program);
  }

  /**
   * @brief Parse a duration argument.
   *
   * @param text Numeric text.
   * @return Seconds, or -1 when invalid.
   */
  int parse_seconds(const char *text) {
    char *end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || value < 0) {
      return -1;
    }
    return static_cast<int>(value);
  }

  /**
   * @brief Sleep for a number of milliseconds.
   *
   * @param milliseconds Delay.
   */
  void sleep_ms(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
  }
}  // namespace

int main(int argc, char **argv) {
  apollo::input::BackendKind backend_kind = apollo::input::BackendKind::automatic;
  apollo::input::Profile profile = apollo::input::Profile::xbox_360;
  std::string device_name = "Apollo Virtual Gamepad";
  int seconds = 8;
  bool compatible = false;

  for (int index = 1; index < argc; ++index) {
    if (std::strcmp(argv[index], "--help") == 0) {
      usage(argv[0]);
      return 0;
    }
    if (std::strcmp(argv[index], "--backend") == 0 && index + 1 < argc) {
      if (!apollo::input::backend_from_string(argv[++index], backend_kind)) {
        std::fprintf(stderr, "Unknown backend: %s\n", argv[index]);
        return 2;
      }
      continue;
    }
    if (std::strcmp(argv[index], "--profile") == 0 && index + 1 < argc) {
      if (!apollo::input::profile_from_string(argv[++index], profile)) {
        std::fprintf(stderr, "Unknown profile: %s\n", argv[index]);
        return 2;
      }
      continue;
    }
    if (std::strcmp(argv[index], "--compatible") == 0) {
      compatible = true;
      continue;
    }
    if (std::strcmp(argv[index], "--seconds") == 0 && index + 1 < argc) {
      seconds = parse_seconds(argv[++index]);
      if (seconds < 0) {
        std::fprintf(stderr, "Invalid duration: %s\n", argv[index]);
        return 2;
      }
      continue;
    }
    if (std::strcmp(argv[index], "--name") == 0 && index + 1 < argc) {
      device_name = argv[++index];
      continue;
    }

    std::fprintf(stderr, "Unknown argument: %s\n", argv[index]);
    usage(argv[0]);
    return 2;
  }

  auto backend = apollo::input::create_backend(backend_kind);
  if (!backend) {
    std::fprintf(stderr, "No backend available\n");
    return 1;
  }

  std::string error;
  if (!backend->available(&error)) {
    std::fprintf(stderr, "Backend %s unavailable: %s\n", backend->name(), error.c_str());
    return 1;
  }

  auto device = compatible ? backend->create_compatible(profile, device_name, &error) : backend->create(profile, device_name, &error);
  if (!device) {
    std::fprintf(stderr, "Unable to create a device: %s\n", error.c_str());
    return 1;
  }

  const auto &info = device->info();
  std::printf("Backend: %s\n", backend->name());
  std::printf("Profile: %s\n", apollo::input::to_string(info.profile));
  std::printf("Name: %s\n", info.name.c_str());
  std::printf("Capabilities: rumble=%d trigger_rumble=%d leds=%d player_leds=%d imu=%d touchpad=%d adaptive=%d battery=%d paddles=%d misc=%u\n",
    info.capabilities.rumble,
    info.capabilities.trigger_rumble,
    info.capabilities.leds,
    info.capabilities.player_leds,
    info.capabilities.imu,
    info.capabilities.touchpad,
    info.capabilities.adaptive_triggers,
    info.capabilities.battery,
    info.capabilities.paddles,
    static_cast<unsigned int>(info.capabilities.misc_buttons));

  device->set_feedback_callback([](const apollo::input::Feedback &feedback) {
    if (feedback.type == apollo::input::FeedbackType::rumble) {
      std::printf("Feedback: rumble low=%u high=%u\n", feedback.low_frequency, feedback.high_frequency);
    } else if (feedback.type == apollo::input::FeedbackType::led) {
      std::printf("Feedback: led rgb=(%u,%u,%u)\n", feedback.led_red, feedback.led_green, feedback.led_blue);
    }
  });

  std::printf("Animating for %d second(s). Open joy.cpl to inspect the controller.\n", seconds);
  std::fflush(stdout);

  const auto start = std::chrono::steady_clock::now();
  int step = 0;
  while (std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds)) {
    apollo::input::GamepadState state;

    switch (step % 8) {
      case 0:
        state.set(apollo::input::Button::a, true);
        break;
      case 1:
        state.set(apollo::input::Button::b, true);
        break;
      case 2:
        state.set(apollo::input::Button::x, true);
        break;
      case 3:
        state.set(apollo::input::Button::y, true);
        break;
      case 4:
        state.set(apollo::input::Button::dpad_up, true);
        break;
      case 5:
        state.set(apollo::input::Button::left_shoulder, true);
        state.left_trigger = 200;
        break;
      case 6:
        state.set(apollo::input::Button::right_shoulder, true);
        state.right_trigger = 200;
        break;
      case 7:
        state.left_x = 32767;
        state.right_y = -32768;
        break;
      default:
        break;
    }

    device->update(state);

    if (info.capabilities.imu) {
      apollo::input::ImuSample imu;
      imu.gyro_x = 0.01F * static_cast<float>(step);
      imu.gyro_y = 0.02F * static_cast<float>(step);
      imu.accel_z = 9.81F;
      imu.timestamp_us = static_cast<std::uint64_t>(step) * 500000U;
      device->update_imu(imu);
    }

    if (info.capabilities.touchpad) {
      apollo::input::TouchpadState touch;
      touch.active = (step % 2) == 0;
      touch.x = static_cast<std::uint16_t>(400 + step * 10);
      touch.y = static_cast<std::uint16_t>(300 + step * 5);
      device->update_touchpad(touch);
    }

    ++step;
    sleep_ms(500);
  }

  device->disconnect();
  std::puts("Device removed");
  return 0;
}
