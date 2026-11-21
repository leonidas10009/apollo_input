/**
 * @file tests/windows/xinput_loopback.cpp
 * @brief End-to-end ViGEmBus test: create a virtual pad and read it via XInput.
 */
#include "apollo/input/input.hpp"

#ifdef _WIN32

  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <xinput.h>

  #include <array>
  #include <chrono>
  #include <cstdio>
  #include <thread>

namespace {
  using xinput_get_state_t = DWORD(WINAPI *)(DWORD, XINPUT_STATE *);

  /**
   * @brief Load XInput dynamically so the test works with any Windows 10 build.
   *
   * @return XInputGetState function, or null when XInput is unavailable.
   */
  xinput_get_state_t load_xinput() {
    for (const auto *name : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
      if (HMODULE module = LoadLibraryW(name)) {
        if (auto function = reinterpret_cast<xinput_get_state_t>(GetProcAddress(module, "XInputGetState"))) {
          return function;
        }
        FreeLibrary(module);
      }
    }
    return nullptr;
  }

  /**
   * @brief Query one XInput slot.
   *
   * @param get_state XInputGetState function.
   * @param index Controller slot.
   * @param state Receives the current state.
   * @return True when the slot is connected.
   */
  bool connected(xinput_get_state_t get_state, DWORD index, XINPUT_STATE *state = nullptr) {
    XINPUT_STATE local {};
    return get_state(index, state ? state : &local) == ERROR_SUCCESS;
  }

  /**
   * @brief Wait until a predicate is true or the timeout expires.
   *
   * @param predicate Condition to poll.
   * @param timeout_ms Maximum wait in milliseconds.
   * @return True when the predicate became true.
   */
  template<typename Predicate>
  bool wait_for(Predicate predicate, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      if (predicate()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return predicate();
  }
}  // namespace

int main() {
  const auto get_state = load_xinput();
  if (!get_state) {
    std::fprintf(stderr, "XInput is not available\n");
    return 77;
  }

  auto backend = apollo::input::create_backend(apollo::input::BackendKind::vigem);
  std::string error;
  if (!backend || !backend->available(&error)) {
    std::fprintf(stderr, "ViGEmBus backend unavailable: %s\n", error.c_str());
    return 77;
  }

  std::array<bool, XUSER_MAX_COUNT> before {};
  for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
    before[index] = connected(get_state, index);
  }

  auto device = backend->create(apollo::input::Profile::xbox_360, "Apollo Input Test Pad", &error);
  if (!device) {
    std::fprintf(stderr, "Unable to create the virtual controller: %s\n", error.c_str());
    return 1;
  }

  DWORD slot = XUSER_MAX_COUNT;
  const bool appeared = wait_for([&] {
    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
      if (!before[index] && connected(get_state, index)) {
        slot = index;
        return true;
      }
    }
    return false;
  }, 5000);

  if (!appeared) {
    std::fprintf(stderr, "The virtual controller did not appear in XInput\n");
    device->disconnect();
    return 1;
  }

  std::printf("Virtual controller appeared in XInput slot %lu\n", static_cast<unsigned long>(slot));

  apollo::input::GamepadState state;
  state.set(apollo::input::Button::a, true);
  state.left_x = 32767;
  if (!device->update(state)) {
    std::fprintf(stderr, "The backend rejected the button report\n");
    device->disconnect();
    return 1;
  }

  XINPUT_STATE xinput_state {};
  const bool button_seen = wait_for([&] {
    if (!connected(get_state, slot, &xinput_state)) {
      return false;
    }
    return (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0 &&
           xinput_state.Gamepad.sThumbLX > 30000;
  }, 2000);

  if (!button_seen) {
    std::fprintf(stderr, "XInput did not report the expected A button and left stick state\n");
    device->disconnect();
    return 1;
  }

  state.clear();
  if (!device->update(state)) {
    std::fprintf(stderr, "The backend rejected the release report\n");
    device->disconnect();
    return 1;
  }

  const bool released = wait_for([&] {
    if (!connected(get_state, slot, &xinput_state)) {
      return false;
    }
    return (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_A) == 0;
  }, 2000);

  device->disconnect();

  if (!released) {
    std::fprintf(stderr, "XInput did not observe the A button release\n");
    return 1;
  }

  auto compatible = backend->create_compatible(apollo::input::Profile::dualsense, "Apollo Input Compatible", &error);
  if (!compatible) {
    std::fprintf(stderr, "Unable to create a compatible fallback controller: %s\n", error.c_str());
    return 1;
  }
  if (compatible->info().profile != apollo::input::Profile::dualshock4) {
    std::fprintf(stderr, "DualSense fallback did not use the DualShock 4 profile\n");
    compatible->disconnect();
    return 1;
  }
  compatible->disconnect();

  std::puts("XInput loopback passed");
  return 0;
}

#else

int main() {
  return 77;
}

#endif
