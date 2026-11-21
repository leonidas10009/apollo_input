/**
 * @file tests/core_tests.cpp
 * @brief Dependency-free unit tests for the apollo-input core.
 */
#include "apollo/input/input.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
  int failures = 0;

  void check(bool condition, const char *expression, const char *file, int line) {
    if (!condition) {
      std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
      ++failures;
    }
  }

  #define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

  /**
   * @brief Minimal device used to exercise backend behavior without hardware.
   */
  class FakeDevice final: public apollo::input::Device {
  public:
    FakeDevice(apollo::input::Profile profile, std::string name):
        info_ {profile, apollo::input::profile_capabilities(profile), std::move(name)} {
    }

    const apollo::input::DeviceInfo &info() const override {
      return info_;
    }

    bool connected() const override {
      return connected_;
    }

    bool update(const apollo::input::GamepadState &) override {
      return connected_;
    }

    void disconnect() override {
      connected_ = false;
    }

  private:
    apollo::input::DeviceInfo info_;
    bool connected_ = true;
  };

  /**
   * @brief Backend that records every profile requested by create().
   */
  class FakeBackend final: public apollo::input::Backend {
  public:
    explicit FakeBackend(std::vector<apollo::input::Profile> supported):
        supported_(std::move(supported)) {
    }

    const char *name() const override {
      return "fake";
    }

    bool available(std::string *error) const override {
      if (error) {
        error->clear();
      }
      return true;
    }

    std::unique_ptr<apollo::input::Device> create(apollo::input::Profile profile, const std::string &name, std::string *error) override {
      attempts.push_back(profile);
      if (std::find(supported_.begin(), supported_.end(), profile) == supported_.end()) {
        if (error) {
          *error = "unsupported profile";
        }
        return nullptr;
      }
      if (error) {
        error->clear();
      }
      return std::make_unique<FakeDevice>(profile, name);
    }

    std::vector<apollo::input::Profile> supported_profiles() const override {
      return supported_;
    }

    std::vector<apollo::input::Profile> attempts;

  private:
    std::vector<apollo::input::Profile> supported_;
  };

  void test_profiles() {
    apollo::input::Profile profile = apollo::input::Profile::generic;
    CHECK(std::string {apollo::input::to_string(apollo::input::Profile::xbox_360)} == "xbox_360");
    CHECK(std::string {apollo::input::to_string(apollo::input::Profile::xbox_elite)} == "xbox_elite");
    CHECK(std::string {apollo::input::to_string(apollo::input::Profile::dualsense)} == "dualsense");
    CHECK(std::string {apollo::input::to_string(apollo::input::Profile::dualsense_edge)} == "dualsense_edge");
    CHECK(std::string {apollo::input::to_string(apollo::input::Profile::steam_controller)} == "steam_controller");
    CHECK(std::string {apollo::input::to_string(apollo::input::Profile::steam_deck)} == "steam_deck");

    CHECK(apollo::input::profile_from_string("switch_pro", profile));
    CHECK(profile == apollo::input::Profile::switch_pro);
    CHECK(apollo::input::profile_from_string("Switch", profile));
    CHECK(profile == apollo::input::Profile::switch_pro);
    CHECK(apollo::input::profile_from_string("DS4", profile));
    CHECK(profile == apollo::input::Profile::dualshock4);
    CHECK(apollo::input::profile_from_string("ps5", profile));
    CHECK(profile == apollo::input::Profile::dualsense);
    CHECK(apollo::input::profile_from_string("xseries", profile));
    CHECK(profile == apollo::input::Profile::xbox_series);
    CHECK(apollo::input::profile_from_string("elite", profile));
    CHECK(profile == apollo::input::Profile::xbox_elite);
    CHECK(apollo::input::profile_from_string("steamdeck", profile));
    CHECK(profile == apollo::input::Profile::steam_deck);
    CHECK(!apollo::input::profile_from_string("not-a-profile", profile));
  }

  void test_controller_families() {
    apollo::input::ControllerFamily family = apollo::input::ControllerFamily::unknown;
    CHECK(apollo::input::controller_family_from_string("XBOX", family));
    CHECK(family == apollo::input::ControllerFamily::xbox);
    CHECK(apollo::input::controller_family_from_string("ps5", family));
    CHECK(family == apollo::input::ControllerFamily::playstation);
    CHECK(apollo::input::controller_family_from_string("joycon", family));
    CHECK(family == apollo::input::ControllerFamily::nintendo);
    CHECK(apollo::input::controller_family_from_string("steam_deck", family));
    CHECK(family == apollo::input::ControllerFamily::steam_deck);
    CHECK(std::string {apollo::input::to_string(apollo::input::ControllerFamily::steam)} == "steam");
    CHECK(!apollo::input::controller_family_from_string("not-a-family", family));
  }

  void test_buttons() {
    apollo::input::GamepadState state;
    CHECK(!state.pressed(apollo::input::Button::a));
    state.set(apollo::input::Button::a, true);
    CHECK(state.pressed(apollo::input::Button::a));
    CHECK(!state.pressed(apollo::input::Button::b));
    state.set(apollo::input::Button::a, false);
    CHECK(!state.pressed(apollo::input::Button::a));

    state.set(apollo::input::Button::misc2, true);
    CHECK(state.pressed(apollo::input::Button::misc2));
    state.set(apollo::input::Button::misc2, false);
    CHECK(!state.pressed(apollo::input::Button::misc2));

    state.left_trigger = 200;
    state.right_trigger = 100;
    state.left_x = 1234;
    CHECK(state.left_trigger == 200);
    CHECK(state.right_trigger == 100);
    CHECK(state.left_x == 1234);
    state.clear();
    CHECK(state.buttons == 0);
    CHECK(state.left_x == 0);
    CHECK(state.left_trigger == 0);
  }

  void test_profile_capabilities() {
    const auto xbox_360 = apollo::input::profile_capabilities(apollo::input::Profile::xbox_360);
    CHECK(xbox_360.rumble);
    CHECK(!xbox_360.trigger_rumble);
    CHECK(!xbox_360.imu);

    const auto xbox_series = apollo::input::profile_capabilities(apollo::input::Profile::xbox_series);
    CHECK(xbox_series.trigger_rumble);
    CHECK(xbox_series.battery);
    CHECK(xbox_series.misc_buttons == 1);

    const auto xbox_elite = apollo::input::profile_capabilities(apollo::input::Profile::xbox_elite);
    CHECK(xbox_elite.paddles);
    CHECK(xbox_elite.trigger_rumble);

    const auto dualshock4 = apollo::input::profile_capabilities(apollo::input::Profile::dualshock4);
    CHECK(dualshock4.rumble);
    CHECK(dualshock4.leds);
    CHECK(dualshock4.imu);
    CHECK(dualshock4.touchpad);
    CHECK(!dualshock4.adaptive_triggers);

    const auto dualsense = apollo::input::profile_capabilities(apollo::input::Profile::dualsense);
    CHECK(dualsense.adaptive_triggers);
    CHECK(dualsense.player_leds);
    CHECK(dualsense.misc_buttons == 1);

    const auto dualsense_edge = apollo::input::profile_capabilities(apollo::input::Profile::dualsense_edge);
    CHECK(dualsense_edge.paddles);
    CHECK(dualsense_edge.adaptive_triggers);

    const auto switch_pro = apollo::input::profile_capabilities(apollo::input::Profile::switch_pro);
    CHECK(switch_pro.imu);
    CHECK(switch_pro.player_leds);
    CHECK(switch_pro.battery);

    const auto steam_controller = apollo::input::profile_capabilities(apollo::input::Profile::steam_controller);
    CHECK(steam_controller.touchpad);
    CHECK(steam_controller.imu);
    CHECK(steam_controller.paddles);

    const auto steam_deck = apollo::input::profile_capabilities(apollo::input::Profile::steam_deck);
    CHECK(steam_deck.touchpad);
    CHECK(steam_deck.paddles);
    CHECK(steam_deck.misc_buttons == 1);
  }

  void test_profile_identities() {
    const auto xbox_360 = apollo::input::profile_identity(apollo::input::Profile::xbox_360);
    CHECK(xbox_360.vendor_id == 0x045E);
    CHECK(xbox_360.product_id == 0x028E);

    const auto dualsense_edge = apollo::input::profile_identity(apollo::input::Profile::dualsense_edge);
    CHECK(dualsense_edge.vendor_id == 0x054C);
    CHECK(dualsense_edge.product_id == 0x0DF2);

    const auto steam_deck = apollo::input::profile_identity(apollo::input::Profile::steam_deck);
    CHECK(steam_deck.vendor_id == 0x28DE);
    CHECK(steam_deck.product_id == 0x1205);
  }

  void test_precision_filtering() {
    using apollo::input::AnalogDeadzone;
    using apollo::input::GamepadState;
    using apollo::input::sanitize_gamepad_state;

    const AnalogDeadzone deadzone {5, 2};

    GamepadState centered;
    sanitize_gamepad_state(centered, deadzone);
    CHECK(centered.left_x == 0);
    CHECK(centered.left_y == 0);
    CHECK(centered.left_trigger == 0);

    GamepadState drift;
    drift.left_x = 900;
    drift.left_y = -1200;
    drift.right_x = -800;
    drift.right_y = 700;
    drift.left_trigger = 4;
    drift.right_trigger = 5;
    sanitize_gamepad_state(drift, deadzone);
    CHECK(drift.left_x == 0);
    CHECK(drift.left_y == 0);
    CHECK(drift.right_x == 0);
    CHECK(drift.right_y == 0);
    CHECK(drift.left_trigger == 0);
    CHECK(drift.right_trigger == 0);

    GamepadState full_range;
    full_range.left_x = 32767;
    full_range.right_x = -32768;
    full_range.left_trigger = 255;
    full_range.right_trigger = 254;
    sanitize_gamepad_state(full_range, deadzone);
    CHECK(full_range.left_x == 32767);
    CHECK(full_range.right_x == -32768);
    CHECK(full_range.left_trigger == 255);
    CHECK(full_range.right_trigger >= 253);

    GamepadState diagonal;
    diagonal.left_x = 20000;
    diagonal.left_y = 20000;
    sanitize_gamepad_state(diagonal, deadzone);
    CHECK(diagonal.left_x > 0);
    CHECK(diagonal.left_y > 0);
    CHECK(std::abs(diagonal.left_x - diagonal.left_y) <= 1);

    GamepadState disabled;
    disabled.left_x = 1000;
    disabled.left_trigger = 5;
    sanitize_gamepad_state(disabled, {0, 0});
    CHECK(disabled.left_x == 1000);
    CHECK(disabled.left_trigger == 5);
  }

  void test_recommended_profile() {
    using apollo::input::ControllerDescription;
    using apollo::input::ControllerFamily;
    using apollo::input::Profile;

    ControllerDescription controller;
    controller.family = ControllerFamily::xbox;
    CHECK(apollo::input::recommended_profile(controller) == Profile::xbox_series);
    controller.has_paddles = true;
    CHECK(apollo::input::recommended_profile(controller) == Profile::xbox_elite);

    controller = {};
    controller.family = ControllerFamily::playstation;
    CHECK(apollo::input::recommended_profile(controller) == Profile::dualshock4);
    controller.has_touchpad = true;
    CHECK(apollo::input::recommended_profile(controller) == Profile::dualsense);
    controller.has_paddles = true;
    CHECK(apollo::input::recommended_profile(controller) == Profile::dualsense_edge);

    controller = {};
    controller.family = ControllerFamily::nintendo;
    CHECK(apollo::input::recommended_profile(controller) == Profile::switch_pro);

    controller = {};
    controller.family = ControllerFamily::steam;
    controller.has_motion = true;
    CHECK(apollo::input::recommended_profile(controller) == Profile::steam_controller);

    controller = {};
    controller.family = ControllerFamily::steam_deck;
    controller.has_touchpad = true;
    CHECK(apollo::input::recommended_profile(controller) == Profile::steam_deck);

    controller = {};
    CHECK(apollo::input::recommended_profile(controller) == Profile::xbox_series);
    controller.has_touchpad = true;
    CHECK(apollo::input::recommended_profile(controller) == Profile::dualsense);
    controller.has_paddles = true;
    CHECK(apollo::input::recommended_profile(controller) == Profile::dualsense_edge);
  }

  void test_compatibility_chains() {
    using apollo::input::Profile;
    using apollo::input::profile_compatibility_chain;

    const auto dualsense = profile_compatibility_chain(Profile::dualsense);
    CHECK(!dualsense.empty());
    CHECK(dualsense.front() == Profile::dualsense);
    CHECK(std::find(dualsense.begin(), dualsense.end(), Profile::dualshock4) != dualsense.end());
    CHECK(std::find(dualsense.begin(), dualsense.end(), Profile::generic) != dualsense.end());

    const auto edge = profile_compatibility_chain(Profile::dualsense_edge);
    CHECK(edge.front() == Profile::dualsense_edge);
    CHECK(std::find(edge.begin(), edge.end(), Profile::dualsense) != edge.end());

    const auto deck = profile_compatibility_chain(Profile::steam_deck);
    CHECK(deck.front() == Profile::steam_deck);
    CHECK(std::find(deck.begin(), deck.end(), Profile::steam_controller) != deck.end());
    CHECK(std::find(deck.begin(), deck.end(), Profile::xbox_360) != deck.end());
  }

  void test_create_compatible() {
    using apollo::input::Profile;

    FakeBackend backend {std::vector<Profile> {Profile::xbox_360}};
    std::string error;
    auto device = backend.create_compatible(Profile::dualsense, "Compatible Pad", &error);
    CHECK(device != nullptr);
    CHECK(error.empty());
    CHECK(device->info().profile == Profile::xbox_360);
    CHECK((backend.attempts == std::vector<Profile> {Profile::dualsense, Profile::dualshock4, Profile::xbox_360}));

    backend.attempts.clear();
    device = backend.create_compatible(Profile::xbox_series, "Compatible Pad", &error);
    CHECK(device != nullptr);
    CHECK(device->info().profile == Profile::xbox_360);
    CHECK((backend.attempts == std::vector<Profile> {Profile::xbox_series, Profile::xbox_one, Profile::xbox_360}));

    backend.attempts.clear();
    device = backend.create_compatible(Profile::switch_pro, "Compatible Pad", &error);
    CHECK(device != nullptr);
    CHECK(device->info().profile == Profile::xbox_360);
    CHECK((backend.attempts == std::vector<Profile> {Profile::switch_pro, Profile::xbox_360}));

    FakeBackend unsupported {std::vector<Profile> {}};
    device = unsupported.create_compatible(Profile::dualsense, "No Pad", &error);
    CHECK(device == nullptr);
    CHECK(!error.empty());
  }

  void test_null_backend() {
    apollo::input::BackendKind kind = apollo::input::BackendKind::automatic;
    CHECK(apollo::input::backend_from_string("null", kind));
    CHECK(kind == apollo::input::BackendKind::null_backend);
    CHECK(apollo::input::backend_from_string("none", kind));
    CHECK(kind == apollo::input::BackendKind::null_backend);
    CHECK(apollo::input::backend_from_string("AUTO", kind));
    CHECK(kind == apollo::input::BackendKind::automatic);
    CHECK(apollo::input::backend_from_string("vigembus", kind));
    CHECK(kind == apollo::input::BackendKind::vigem);
    CHECK(apollo::input::backend_from_string("linux_uinput", kind));
    CHECK(kind == apollo::input::BackendKind::uinput);
    CHECK(std::string {apollo::input::to_string(apollo::input::BackendKind::vigem)} == "vigem");
    CHECK(!apollo::input::backend_from_string("not-a-backend", kind));

    auto backend = apollo::input::create_backend(apollo::input::BackendKind::null_backend);
    CHECK(backend != nullptr);
    CHECK(std::string {backend->name()} == "null");

    std::string error;
    CHECK(backend->available(&error));
    CHECK(error.empty());

    auto device = backend->create(apollo::input::Profile::xbox_360, "Test Pad", &error);
    CHECK(device != nullptr);
    CHECK(error.empty());
    CHECK(device->connected());
    CHECK(device->info().profile == apollo::input::Profile::xbox_360);
    CHECK(device->info().name == "Test Pad");

    apollo::input::GamepadState state;
    state.set(apollo::input::Button::a, true);
    CHECK(device->update(state));
    CHECK(!device->update_imu({}));
    CHECK(!device->update_touchpad({}));

    device->disconnect();
    CHECK(!device->connected());
    CHECK(!device->update(state));

    auto dualsense = backend->create(apollo::input::Profile::dualsense, "DualSense Pad", &error);
    CHECK(dualsense != nullptr);
    CHECK(dualsense->info().capabilities.touchpad);
    CHECK(dualsense->info().capabilities.imu);
    CHECK(dualsense->update_touchpad({}));
    CHECK(dualsense->update_imu({}));
  }

  void test_unknown_backend_falls_back_to_null() {
    auto backend = apollo::input::create_backend(static_cast<apollo::input::BackendKind>(999));
    CHECK(backend != nullptr);
    CHECK(std::string {backend->name()} == "null");
  }
}  // namespace

int main() {
  test_profiles();
  test_controller_families();
  test_buttons();
  test_profile_capabilities();
  test_profile_identities();
  test_precision_filtering();
  test_recommended_profile();
  test_compatibility_chains();
  test_create_compatible();
  test_null_backend();
  test_unknown_backend_falls_back_to_null();

  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }

  std::cout << "apollo-input core tests passed\n";
  return 0;
}
