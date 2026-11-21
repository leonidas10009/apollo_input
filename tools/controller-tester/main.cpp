/**
 * @file tools/controller-tester/main.cpp
 * @brief Interactive Windows diagnostic tool for ApolloInput Nova controllers.
 */

#include "apollo/input/input.hpp"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#define DIRECTINPUT_VERSION 0x0800

#include <windows.h>

#include <dinput.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
#include <xinput.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <conio.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace {
  using namespace std::chrono_literals;

  /**
   * @brief Convert a wide Windows string to UTF-8 for console output.
   *
   * @param text Wide string.
   * @return UTF-8 string.
   */
  std::string utf8(const std::wstring &text) {
    if (text.empty()) {
      return {};
    }

    const auto size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
      return {};
    }

    std::string result(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
  }

  /**
   * @brief Load XInput dynamically so the tool starts on any Windows 10 build.
   */
  struct xinput_api_t {
    HMODULE module = nullptr;
    decltype(&XInputGetState) get_state = nullptr;
    decltype(&XInputGetCapabilities) get_capabilities = nullptr;
    decltype(&XInputSetState) set_state = nullptr;

    bool load() {
      if (module) {
        return true;
      }

      for (const auto *name : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
        module = LoadLibraryW(name);
        if (!module) {
          continue;
        }

        get_state = reinterpret_cast<decltype(get_state)>(GetProcAddress(module, "XInputGetState"));
        get_capabilities = reinterpret_cast<decltype(get_capabilities)>(GetProcAddress(module, "XInputGetCapabilities"));
        set_state = reinterpret_cast<decltype(set_state)>(GetProcAddress(module, "XInputSetState"));
        if (get_state && get_capabilities && set_state) {
          return true;
        }

        FreeLibrary(module);
        module = nullptr;
        get_state = nullptr;
        get_capabilities = nullptr;
        set_state = nullptr;
      }

      return false;
    }

    ~xinput_api_t() {
      if (module) {
        FreeLibrary(module);
      }
    }
  } xinput;

  /**
   * @brief One HID game-controller interface found through SetupAPI.
   */
  struct hid_device_info_t {
    std::wstring path;
    std::wstring product;
    std::wstring manufacturer;
    USHORT vendor_id = 0;
    USHORT product_id = 0;
    USAGE usage_page = 0;
    USAGE usage = 0;
    USHORT input_report_bytes = 0;
  };

  /**
   * @brief One DirectInput game controller.
   */
  struct dinput_device_info_t {
    GUID instance_guid {};
    std::wstring instance_name;
    std::wstring product_name;
    DWORD vendor_id = 0;
    DWORD product_id = 0;
    int axes = 0;
    int buttons = 0;
    int povs = 0;
  };

  /**
   * @brief Inclusive range reported for a DirectInput axis.
   */
  struct axis_range_t {
    LONG minimum = 0;
    LONG maximum = 65535;
    bool valid = false;
    DWORD offset = 0;
  };

  /**
   * @brief DirectInput axis mapping for one physical controller.
   */
  struct dinput_mapping_t {
    axis_range_t left_x;
    axis_range_t left_y;
    axis_range_t right_x;
    axis_range_t right_y;
    axis_range_t left_trigger;
    axis_range_t right_trigger;
  };

  /**
   * @brief Check whether a HID top-level collection looks like a game controller.
   *
   * @param device HID interface.
   * @return True when the interface should be shown as a controller.
   */
  bool is_game_controller(const hid_device_info_t &device) {
    if (device.usage_page == 0x01 && (device.usage == 0x04 || device.usage == 0x05 || device.usage == 0x08)) {
      return true;
    }

    // Some controllers expose a vendor-defined top-level collection. Known
    // vendors are still useful in a diagnostic listing.
    switch (device.vendor_id) {
      case 0x045E:  // Microsoft
      case 0x054C:  // Sony
      case 0x057E:  // Nintendo
      case 0x28DE:  // Valve
      case 0x2DC8:  // 8BitDo
        return true;
      default:
        return false;
    }
  }

  /**
   * @brief Enumerate HID game-controller interfaces.
   *
   * @return HID interfaces sorted by vendor, product, and name.
   */
  std::vector<hid_device_info_t> enumerate_hid_devices() {
    std::vector<hid_device_info_t> devices;
    GUID hid_guid {};
    HidD_GetHidGuid(&hid_guid);

    const auto devices_info = SetupDiGetClassDevsW(&hid_guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices_info == INVALID_HANDLE_VALUE) {
      return devices;
    }

    for (DWORD index = 0;; ++index) {
      SP_DEVICE_INTERFACE_DATA interface_data {};
      interface_data.cbSize = sizeof(interface_data);
      if (!SetupDiEnumDeviceInterfaces(devices_info, nullptr, &hid_guid, index, &interface_data)) {
        break;
      }

      DWORD required = 0;
      SetupDiGetDeviceInterfaceDetailW(devices_info, &interface_data, nullptr, 0, &required, nullptr);
      if (required == 0) {
        continue;
      }

      std::vector<std::byte> buffer(required);
      auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
      detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
      if (!SetupDiGetDeviceInterfaceDetailW(devices_info, &interface_data, detail, required, nullptr, nullptr)) {
        continue;
      }

      hid_device_info_t info;
      info.path = detail->DevicePath;

      const auto handle = CreateFileW(
        info.path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
      );
      if (handle == INVALID_HANDLE_VALUE) {
        continue;
      }

      HIDD_ATTRIBUTES attributes {};
      attributes.Size = sizeof(attributes);
      if (HidD_GetAttributes(handle, &attributes)) {
        info.vendor_id = attributes.VendorID;
        info.product_id = attributes.ProductID;
      }

      wchar_t text[256] {};
      if (HidD_GetProductString(handle, text, sizeof(text))) {
        info.product = text;
      }
      std::memset(text, 0, sizeof(text));
      if (HidD_GetManufacturerString(handle, text, sizeof(text))) {
        info.manufacturer = text;
      }

      PHIDP_PREPARSED_DATA preparsed = nullptr;
      if (HidD_GetPreparsedData(handle, &preparsed)) {
        HIDP_CAPS caps {};
        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
          info.usage_page = caps.UsagePage;
          info.usage = caps.Usage;
          info.input_report_bytes = caps.InputReportByteLength;
        }
        HidD_FreePreparsedData(preparsed);
      }

      CloseHandle(handle);

      if (is_game_controller(info)) {
        devices.push_back(std::move(info));
      }
    }

    SetupDiDestroyDeviceInfoList(devices_info);

    std::ranges::sort(devices, [](const auto &left, const auto &right) {
      return std::tie(left.vendor_id, left.product_id, left.product) < std::tie(right.vendor_id, right.product_id, right.product);
    });
    return devices;
  }

  /**
   * @brief Shared state for DirectInput enumeration.
   */
  struct dinput_enum_context_t {
    IDirectInput8W *dinput = nullptr;
    std::vector<dinput_device_info_t> *devices = nullptr;
  };

  /**
   * @brief Count axes, buttons, and POV hats on a DirectInput device.
   */
  BOOL CALLBACK count_objects_callback(LPCDIDEVICEOBJECTINSTANCEW object, LPVOID user_data) {
    auto *info = static_cast<dinput_device_info_t *>(user_data);
    if (object->dwType & DIDFT_AXIS) {
      ++info->axes;
    } else if (object->dwType & DIDFT_BUTTON) {
      ++info->buttons;
    } else if (object->dwType & DIDFT_POV) {
      ++info->povs;
    }
    return DIENUM_CONTINUE;
  }

  /**
   * @brief DirectInput device enumeration callback.
   */
  BOOL CALLBACK enumerate_dinput_callback(LPCDIDEVICEINSTANCEW instance, LPVOID user_data) {
    auto *context = static_cast<dinput_enum_context_t *>(user_data);
    dinput_device_info_t info;
    info.instance_guid = instance->guidInstance;
    info.instance_name = instance->tszInstanceName;
    info.product_name = instance->tszProductName;

    IDirectInputDevice8W *device = nullptr;
    if (SUCCEEDED(context->dinput->CreateDevice(instance->guidInstance, &device, nullptr))) {
      if (SUCCEEDED(device->SetDataFormat(&c_dfDIJoystick2))) {
        DIPROPDWORD property {};
        property.diph.dwSize = sizeof(property);
        property.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        property.diph.dwObj = 0;
        property.diph.dwHow = DIPH_DEVICE;
        if (SUCCEEDED(device->GetProperty(DIPROP_VIDPID, &property.diph))) {
          // DIPROP_VIDPID packs the vendor ID in the low word and the product
          // ID in the high word.
          info.vendor_id = LOWORD(property.dwData);
          info.product_id = HIWORD(property.dwData);
        }
      }
      device->EnumObjects(count_objects_callback, &info, DIDFT_ALL);
      device->Release();
    }

    context->devices->push_back(std::move(info));
    return DIENUM_CONTINUE;
  }

  /**
   * @brief DirectInput singleton used for enumeration and device creation.
   */
  struct dinput_api_t {
    IDirectInput8W *dinput = nullptr;

    bool init() {
      if (dinput) {
        return true;
      }
      return SUCCEEDED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void **>(&dinput), nullptr));
    }

    ~dinput_api_t() {
      if (dinput) {
        dinput->Release();
      }
    }
  } dinput_api;

  /**
   * @brief Enumerate DirectInput game controllers.
   *
   * @return DirectInput devices.
   */
  std::vector<dinput_device_info_t> enumerate_dinput_devices() {
    std::vector<dinput_device_info_t> devices;
    if (!dinput_api.init()) {
      return devices;
    }

    dinput_enum_context_t context {dinput_api.dinput, &devices};
    dinput_api.dinput->EnumDevices(DI8DEVCLASS_GAMECTRL, enumerate_dinput_callback, &context, DIEDFL_ATTACHEDONLY);

    std::ranges::sort(devices, [](const auto &left, const auto &right) {
      return std::tie(left.vendor_id, left.product_id, left.product_name) < std::tie(right.vendor_id, right.product_id, right.product_name);
    });
    return devices;
  }

  /**
   * @brief Owned DirectInput gamepad used for monitoring and bridging.
   */
  struct open_dinput_device_t {
    IDirectInputDevice8W *device = nullptr;

    ~open_dinput_device_t() {
      if (device) {
        device->Unacquire();
        device->Release();
      }
    }

    bool open(const GUID &guid) {
      if (!dinput_api.init()) {
        return false;
      }
      if (FAILED(dinput_api.dinput->CreateDevice(guid, &device, nullptr))) {
        return false;
      }
      if (FAILED(device->SetDataFormat(&c_dfDIJoystick2))) {
        return false;
      }
      if (FAILED(device->SetCooperativeLevel(nullptr, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) {
        device->SetCooperativeLevel(nullptr, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
      }
      device->Acquire();
      return true;
    }

    bool poll(DIJOYSTATE2 &state) {
      if (!device) {
        return false;
      }
      if (FAILED(device->Poll())) {
        device->Acquire();
      }
      auto result = device->GetDeviceState(sizeof(state), &state);
      if (FAILED(result)) {
        device->Acquire();
        result = device->GetDeviceState(sizeof(state), &state);
      }
      return SUCCEEDED(result);
    }
  };

  /**
   * @brief Query the inclusive range for a DirectInput axis offset.
   *
   * @param device Open DirectInput device.
   * @param offset DIJOFS_* axis offset.
   * @return Axis range, or an invalid range when the axis does not exist.
   */
  axis_range_t query_axis_range(IDirectInputDevice8W *device, DWORD offset) {
    DIPROPRANGE range {};
    range.diph.dwSize = sizeof(range);
    range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    range.diph.dwObj = offset;
    range.diph.dwHow = DIPH_BYOFFSET;
    if (SUCCEEDED(device->GetProperty(DIPROP_RANGE, &range.diph))) {
      return {range.lMin, range.lMax, true, offset};
    }
    return {};
  }

  /**
   * @brief Read one DirectInput axis by its known offset.
   *
   * @param state DirectInput state.
   * @param range Axis metadata.
   * @return Raw axis value.
   */
  LONG axis_value(const DIJOYSTATE2 &state, const axis_range_t &range) {
    switch (range.offset) {
      case DIJOFS_X:
        return state.lX;
      case DIJOFS_Y:
        return state.lY;
      case DIJOFS_Z:
        return state.lZ;
      case DIJOFS_RX:
        return state.lRx;
      case DIJOFS_RY:
        return state.lRy;
      case DIJOFS_RZ:
        return state.lRz;
      case DIJOFS_SLIDER(0):
        return state.rglSlider[0];
      case DIJOFS_SLIDER(1):
        return state.rglSlider[1];
      default:
        return 0;
    }
  }

  /**
   * @brief Normalize an axis to -1..1.
   *
   * @param value Raw axis value.
   * @param range Axis range.
   * @return Normalized value.
   */
  float normalize_axis(LONG value, const axis_range_t &range) {
    if (range.valid && range.maximum != range.minimum) {
      return std::clamp(2.0F * static_cast<float>(value - range.minimum) / static_cast<float>(range.maximum - range.minimum) - 1.0F, -1.0F, 1.0F);
    }
    if (value >= -2048 && value <= 2048) {
      return std::clamp(static_cast<float>(value) / 1000.0F, -1.0F, 1.0F);
    }
    return std::clamp((static_cast<float>(value) - 32767.5F) / 32767.5F, -1.0F, 1.0F);
  }

  /**
   * @brief Normalize an axis to 0..1.
   *
   * @param value Raw axis value.
   * @param range Axis range.
   * @return Normalized value.
   */
  float normalize_unipolar(LONG value, const axis_range_t &range) {
    if (!range.valid || range.maximum == range.minimum) {
      return 0.0F;
    }
    return std::clamp(static_cast<float>(value - range.minimum) / static_cast<float>(range.maximum - range.minimum), 0.0F, 1.0F);
  }

  /**
   * @brief Convert a normalized axis to the Apollo input range.
   *
   * @param value Normalized axis.
   * @return Signed 16-bit axis.
   */
  std::int16_t to_int16(float value) {
    const auto scaled = value < 0 ? value * 32768.0F : value * 32767.0F;
    return static_cast<std::int16_t>(std::clamp<std::int32_t>(static_cast<std::int32_t>(std::lround(scaled)), -32768, 32767));
  }

  /**
   * @brief Convert a normalized trigger to the Apollo input range.
   *
   * @param value Normalized trigger.
   * @return Unsigned 8-bit trigger.
   */
  std::uint8_t to_trigger(float value) {
    return static_cast<std::uint8_t>(std::clamp<std::int32_t>(static_cast<std::int32_t>(std::lround(value * 255.0F)), 0, 255));
  }

  /**
   * @brief Decide whether an axis is an unipolar trigger at rest.
   *
   * @param range Axis range.
   * @param rest Raw rest value.
   * @return True when the axis looks like a trigger.
   */
  bool looks_like_trigger(const axis_range_t &range, LONG rest) {
    if (!range.valid) {
      return false;
    }
    const auto span = static_cast<double>(range.maximum - range.minimum);
    const auto distance = std::min(std::abs(static_cast<double>(rest - range.minimum)), std::abs(static_cast<double>(rest - range.maximum)));
    return span > 1.0 && distance <= span * 0.1;
  }

  /**
   * @brief Detect the useful axes for a DirectInput controller.
   *
   * @param device Open device.
   * @param rest Resting state used to distinguish sticks from triggers.
   * @return Axis mapping.
   */
  dinput_mapping_t detect_mapping(IDirectInputDevice8W *device, const DIJOYSTATE2 &rest) {
    dinput_mapping_t mapping;
    mapping.left_x = query_axis_range(device, DIJOFS_X);
    mapping.left_y = query_axis_range(device, DIJOFS_Y);
    mapping.right_x = query_axis_range(device, DIJOFS_RX);
    mapping.right_y = query_axis_range(device, DIJOFS_RY);

    const auto z = query_axis_range(device, DIJOFS_Z);
    const auto rz = query_axis_range(device, DIJOFS_RZ);
    if (looks_like_trigger(z, rest.lZ)) {
      mapping.left_trigger = z;
    } else if (z.valid && !mapping.right_x.valid) {
      mapping.right_x = z;
    }
    if (looks_like_trigger(rz, rest.lRz)) {
      mapping.right_trigger = rz;
    } else if (rz.valid && !mapping.right_y.valid) {
      mapping.right_y = rz;
    }

    if (!mapping.left_trigger.valid) {
      const auto slider = query_axis_range(device, DIJOFS_SLIDER(0));
      if (looks_like_trigger(slider, rest.rglSlider[0])) {
        mapping.left_trigger = slider;
      }
    }
    if (!mapping.right_trigger.valid) {
      const auto slider = query_axis_range(device, DIJOFS_SLIDER(1));
      if (looks_like_trigger(slider, rest.rglSlider[1])) {
        mapping.right_trigger = slider;
      }
    }

    return mapping;
  }

  /**
   * @brief Map DirectInput buttons for the controller family.
   *
   * @param state DirectInput state.
   * @param vendor_id USB vendor ID.
   * @param out Apollo gamepad state.
   */
  void apply_dinput_buttons(const DIJOYSTATE2 &state, DWORD vendor_id, apollo::input::GamepadState &out) {
    using apollo::input::Button;
    auto pressed = [&](int index) {
      return index >= 0 && index < 128 && (state.rgbButtons[index] & 0x80) != 0;
    };
    auto set = [&](int index, Button button) {
      out.set(button, pressed(index));
    };

    if (vendor_id == 0x054C) {
      // DualShock 4 / DualSense DirectInput order.
      set(0, Button::x);
      set(1, Button::a);
      set(2, Button::b);
      set(3, Button::y);
      set(4, Button::left_shoulder);
      set(5, Button::right_shoulder);
      if (pressed(6)) {
        out.left_trigger = 255;
      }
      if (pressed(7)) {
        out.right_trigger = 255;
      }
      set(8, Button::back);
      set(9, Button::start);
      set(10, Button::left_thumb);
      set(11, Button::right_thumb);
      set(12, Button::guide);
      set(13, Button::touchpad_click);
    } else if (vendor_id == 0x057E) {
      // Nintendo Switch Pro DirectInput order.
      set(0, Button::b);
      set(1, Button::a);
      set(2, Button::y);
      set(3, Button::x);
      set(4, Button::left_shoulder);
      set(5, Button::right_shoulder);
      if (pressed(6)) {
        out.left_trigger = 255;
      }
      if (pressed(7)) {
        out.right_trigger = 255;
      }
      set(8, Button::back);
      set(9, Button::start);
      set(10, Button::left_thumb);
      set(11, Button::right_thumb);
      set(12, Button::guide);
      set(13, Button::share);
    } else {
      // Xbox and generic DirectInput order.
      set(0, Button::a);
      set(1, Button::b);
      set(2, Button::x);
      set(3, Button::y);
      set(4, Button::left_shoulder);
      set(5, Button::right_shoulder);
      if (pressed(6)) {
        out.left_trigger = 255;
      }
      if (pressed(7)) {
        out.right_trigger = 255;
      }
      set(8, Button::back);
      set(9, Button::start);
      set(10, Button::left_thumb);
      set(11, Button::right_thumb);
      set(12, Button::guide);
    }

    // Extra buttons used by Elite/Edge-style controllers and some third-party pads.
    set(14, Button::paddle1);
    set(15, Button::paddle2);
    set(16, Button::paddle3);
    set(17, Button::paddle4);
    set(18, Button::share);
    set(19, Button::misc2);
  }

  /**
   * @brief Apply the DirectInput D-pad hat.
   *
   * @param state DirectInput state.
   * @param out Apollo gamepad state.
   */
  void apply_dinput_dpad(const DIJOYSTATE2 &state, apollo::input::GamepadState &out) {
    const auto pov = state.rgdwPOV[0];
    if (pov == 0xFFFFFFFF) {
      return;
    }

    const auto degrees = static_cast<int>(pov / 100);
    out.set(apollo::input::Button::dpad_up, degrees >= 315 || degrees < 45);
    out.set(apollo::input::Button::dpad_right, degrees >= 45 && degrees < 135);
    out.set(apollo::input::Button::dpad_down, degrees >= 135 && degrees < 225);
    out.set(apollo::input::Button::dpad_left, degrees >= 225 && degrees < 315);
  }

  /**
   * @brief Convert a DirectInput state into the Apollo input model.
   *
   * @param state DirectInput state.
   * @param vendor_id USB vendor ID.
   * @param mapping Axis mapping.
   * @return Apollo gamepad state.
   */
  apollo::input::GamepadState map_dinput_state(const DIJOYSTATE2 &state, DWORD vendor_id, const dinput_mapping_t &mapping) {
    apollo::input::GamepadState out;
    apply_dinput_buttons(state, vendor_id, out);
    apply_dinput_dpad(state, out);

    if (mapping.left_x.valid) {
      out.left_x = to_int16(normalize_axis(state.lX, mapping.left_x));
    }
    if (mapping.left_y.valid) {
      out.left_y = to_int16(-normalize_axis(state.lY, mapping.left_y));
    }
    if (mapping.right_x.valid) {
      out.right_x = to_int16(normalize_axis(state.lRx, mapping.right_x));
    }
    if (mapping.right_y.valid) {
      out.right_y = to_int16(-normalize_axis(state.lRy, mapping.right_y));
    }
    if (mapping.left_trigger.valid) {
      out.left_trigger = to_trigger(normalize_unipolar(axis_value(state, mapping.left_trigger), mapping.left_trigger));
    }
    if (mapping.right_trigger.valid) {
      out.right_trigger = to_trigger(normalize_unipolar(axis_value(state, mapping.right_trigger), mapping.right_trigger));
    }
    return out;
  }

  /**
   * @brief Convert an XInput state into the Apollo input model.
   *
   * @param state XInput state.
   * @return Apollo gamepad state.
   */
  apollo::input::GamepadState map_xinput_state(const XINPUT_STATE &state) {
    using apollo::input::Button;
    const auto &pad = state.Gamepad;
    apollo::input::GamepadState out;
    out.set(Button::a, pad.wButtons & XINPUT_GAMEPAD_A);
    out.set(Button::b, pad.wButtons & XINPUT_GAMEPAD_B);
    out.set(Button::x, pad.wButtons & XINPUT_GAMEPAD_X);
    out.set(Button::y, pad.wButtons & XINPUT_GAMEPAD_Y);
    out.set(Button::dpad_up, pad.wButtons & XINPUT_GAMEPAD_DPAD_UP);
    out.set(Button::dpad_down, pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
    out.set(Button::dpad_left, pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
    out.set(Button::dpad_right, pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
    out.set(Button::start, pad.wButtons & XINPUT_GAMEPAD_START);
    out.set(Button::back, pad.wButtons & XINPUT_GAMEPAD_BACK);
    out.set(Button::left_shoulder, pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
    out.set(Button::right_shoulder, pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
    out.set(Button::left_thumb, pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB);
    out.set(Button::right_thumb, pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB);
    out.left_trigger = pad.bLeftTrigger;
    out.right_trigger = pad.bRightTrigger;
    out.left_x = pad.sThumbLX;
    out.left_y = pad.sThumbLY;
    out.right_x = pad.sThumbRX;
    out.right_y = pad.sThumbRY;
    return out;
  }

  /**
   * @brief Print all physical controllers and HID interfaces.
   */
  void list_devices() {
    std::puts("== XInput ==");
    if (!xinput.load()) {
      std::puts("XInput is not available.");
    } else {
      for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
        XINPUT_STATE state {};
        if (xinput.get_state(index, &state) != ERROR_SUCCESS) {
          continue;
        }
        XINPUT_CAPABILITIES caps {};
        if (xinput.get_capabilities(index, 0, &caps) == ERROR_SUCCESS) {
          const char *subtype = "unknown";
          switch (caps.SubType) {
            case XINPUT_DEVSUBTYPE_GAMEPAD:
              subtype = "gamepad";
              break;
            case XINPUT_DEVSUBTYPE_WHEEL:
              subtype = "wheel";
              break;
            case XINPUT_DEVSUBTYPE_ARCADE_STICK:
              subtype = "arcade stick";
              break;
            case XINPUT_DEVSUBTYPE_FLIGHT_STICK:
              subtype = "flight stick";
              break;
            case XINPUT_DEVSUBTYPE_DANCE_PAD:
              subtype = "dance pad";
              break;
            case XINPUT_DEVSUBTYPE_GUITAR:
              subtype = "guitar";
              break;
            case XINPUT_DEVSUBTYPE_DRUM_KIT:
              subtype = "drum kit";
              break;
            default:
              break;
          }
          std::printf("  [xinput %lu] %s (battery-free diagnostic view)\n", static_cast<unsigned long>(index), subtype);
        } else {
          std::printf("  [xinput %lu] connected\n", static_cast<unsigned long>(index));
        }
      }
    }

    std::puts("== DirectInput ==");
    const auto dinput_devices = enumerate_dinput_devices();
    if (dinput_devices.empty()) {
      std::puts("  No DirectInput controllers found.");
    }
    for (std::size_t index = 0; index < dinput_devices.size(); ++index) {
      const auto &device = dinput_devices[index];
      std::printf(
        "  [dinput %zu] %s | VID:PID %04lX:%04lX | axes=%d buttons=%d povs=%d\n",
        index,
        utf8(device.product_name.empty() ? device.instance_name : device.product_name).c_str(),
        static_cast<unsigned long>(device.vendor_id),
        static_cast<unsigned long>(device.product_id),
        device.axes,
        device.buttons,
        device.povs
      );
    }

    std::puts("== HID interfaces ==");
    const auto hid_devices = enumerate_hid_devices();
    if (hid_devices.empty()) {
      std::puts("  No HID game-controller interfaces found.");
    }
    for (const auto &device : hid_devices) {
      std::printf(
        "  %s | VID:PID %04X:%04X | usage %04X:%04X | input=%u bytes\n",
        utf8(device.product.empty() ? device.manufacturer : device.product).c_str(),
        device.vendor_id,
        device.product_id,
        device.usage_page,
        device.usage,
        static_cast<unsigned>(device.input_report_bytes)
      );
    }
  }

  /**
   * @brief Print a concise XInput state.
   *
   * @param state XInput state.
   */
  void print_xinput_state(const XINPUT_STATE &state) {
    const auto &pad = state.Gamepad;
    std::printf(
      "\rA=%d B=%d X=%d Y=%d LB=%d RB=%d Back=%d Start=%d LS=%d RS=%d Dpad=%d%d%d%d LT=%3u RT=%3u LX=%6d LY=%6d RX=%6d RY=%6d   ",
      (pad.wButtons & XINPUT_GAMEPAD_A) != 0,
      (pad.wButtons & XINPUT_GAMEPAD_B) != 0,
      (pad.wButtons & XINPUT_GAMEPAD_X) != 0,
      (pad.wButtons & XINPUT_GAMEPAD_Y) != 0,
      (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0,
      (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0,
      (pad.wButtons & XINPUT_GAMEPAD_BACK) != 0,
      (pad.wButtons & XINPUT_GAMEPAD_START) != 0,
      (pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0,
      (pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0,
      (pad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0,
      (pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0,
      (pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0,
      (pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0,
      pad.bLeftTrigger,
      pad.bRightTrigger,
      pad.sThumbLX,
      pad.sThumbLY,
      pad.sThumbRX,
      pad.sThumbRY
    );
    std::fflush(stdout);
  }

  /**
   * @brief Monitor an XInput controller.
   *
   * @param index XInput slot.
   * @param seconds Duration in seconds.
   */
  void monitor_xinput(DWORD index, int seconds) {
    if (!xinput.load()) {
      std::fprintf(stderr, "XInput is not available.\n");
      return;
    }

    std::puts("Press any key to stop early.");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
      XINPUT_STATE state {};
      if (xinput.get_state(index, &state) != ERROR_SUCCESS) {
        std::fprintf(stderr, "\nXInput slot %lu is not connected.\n", static_cast<unsigned long>(index));
        return;
      }
      print_xinput_state(state);
      if (_kbhit()) {
        _getch();
        break;
      }
      std::this_thread::sleep_for(16ms);
    }
    std::puts("");
  }

  /**
   * @brief Monitor a DirectInput controller.
   *
   * @param index DirectInput device index.
   * @param seconds Duration in seconds.
   */
  void monitor_dinput(std::size_t index, int seconds) {
    const auto devices = enumerate_dinput_devices();
    if (index >= devices.size()) {
      std::fprintf(stderr, "DirectInput device %zu does not exist.\n", index);
      return;
    }

    open_dinput_device_t device;
    if (!device.open(devices[index].instance_guid)) {
      std::fprintf(stderr, "Unable to open DirectInput device %zu.\n", index);
      return;
    }

    DIJOYSTATE2 state {};
    if (!device.poll(state)) {
      std::fprintf(stderr, "Unable to read DirectInput device %zu.\n", index);
      return;
    }
    const auto mapping = detect_mapping(device.device, state);

    std::puts("Press any key to stop early.");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
      if (!device.poll(state)) {
        continue;
      }
      std::printf(
        "\rLX=%6ld LY=%6ld RX=%6ld RY=%6ld Z=%6ld RZ=%6ld B0-7=%d%d%d%d%d%d%d%d   ",
        state.lX,
        state.lY,
        state.lRx,
        state.lRy,
        state.lZ,
        state.lRz,
        (state.rgbButtons[0] & 0x80) != 0,
        (state.rgbButtons[1] & 0x80) != 0,
        (state.rgbButtons[2] & 0x80) != 0,
        (state.rgbButtons[3] & 0x80) != 0,
        (state.rgbButtons[4] & 0x80) != 0,
        (state.rgbButtons[5] & 0x80) != 0,
        (state.rgbButtons[6] & 0x80) != 0,
        (state.rgbButtons[7] & 0x80) != 0
      );
      static_cast<void>(mapping);
      std::fflush(stdout);
      if (_kbhit()) {
        _getch();
        break;
      }
      std::this_thread::sleep_for(16ms);
    }
    std::puts("");
  }

  /**
   * @brief Run a drift test for an XInput controller.
   *
   * @param index XInput slot.
   * @param seconds Duration in seconds.
   * @return True when the sanitized rest state stays centered.
   */
  bool drift_xinput(DWORD index, int seconds) {
    if (!xinput.load()) {
      std::fprintf(stderr, "XInput is not available.\n");
      return false;
    }

    std::puts("Do not touch the controller. Measuring rest drift...");
    int samples = 0;
    int raw_nonzero = 0;
    int sanitized_nonzero = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
      XINPUT_STATE state {};
      if (xinput.get_state(index, &state) != ERROR_SUCCESS) {
        std::fprintf(stderr, "XInput slot %lu is not connected.\n", static_cast<unsigned long>(index));
        return false;
      }

      auto mapped = map_xinput_state(state);
      const auto raw = mapped;
      apollo::input::sanitize_gamepad_state(mapped, {3, 2});
      ++samples;
      if (raw.left_x != 0 || raw.left_y != 0 || raw.right_x != 0 || raw.right_y != 0 || raw.left_trigger != 0 || raw.right_trigger != 0) {
        ++raw_nonzero;
      }
      if (mapped.left_x != 0 || mapped.left_y != 0 || mapped.right_x != 0 || mapped.right_y != 0 || mapped.left_trigger != 0 || mapped.right_trigger != 0) {
        ++sanitized_nonzero;
      }
      std::this_thread::sleep_for(16ms);
    }

    std::printf("Samples: %d | raw non-zero: %d | sanitized non-zero: %d\n", samples, raw_nonzero, sanitized_nonzero);
    if (sanitized_nonzero == 0) {
      std::puts("PASS: no ghost drift after filtering.");
      return true;
    }
    std::puts("FAIL: residual movement remains after filtering. Increase the deadzone or check the physical controller.");
    return false;
  }

  /**
   * @brief Run a drift test for a DirectInput controller.
   *
   * @param index DirectInput device index.
   * @param seconds Duration in seconds.
   * @return True when the sanitized rest state stays centered.
   */
  bool drift_dinput(std::size_t index, int seconds) {
    const auto devices = enumerate_dinput_devices();
    if (index >= devices.size()) {
      std::fprintf(stderr, "DirectInput device %zu does not exist.\n", index);
      return false;
    }

    open_dinput_device_t device;
    if (!device.open(devices[index].instance_guid)) {
      std::fprintf(stderr, "Unable to open DirectInput device %zu.\n", index);
      return false;
    }

    DIJOYSTATE2 rest {};
    if (!device.poll(rest)) {
      std::fprintf(stderr, "Unable to read DirectInput device %zu.\n", index);
      return false;
    }
    const auto mapping = detect_mapping(device.device, rest);
    const auto vendor = devices[index].vendor_id;

    std::puts("Do not touch the controller. Measuring rest drift...");
    int samples = 0;
    int raw_nonzero = 0;
    int sanitized_nonzero = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
      DIJOYSTATE2 state {};
      if (!device.poll(state)) {
        continue;
      }
      auto raw = map_dinput_state(state, vendor, mapping);
      auto sanitized = raw;
      apollo::input::sanitize_gamepad_state(sanitized, {3, 2});
      ++samples;
      if (raw.left_x != 0 || raw.left_y != 0 || raw.right_x != 0 || raw.right_y != 0 || raw.left_trigger != 0 || raw.right_trigger != 0) {
        ++raw_nonzero;
      }
      if (sanitized.left_x != 0 || sanitized.left_y != 0 || sanitized.right_x != 0 || sanitized.right_y != 0 || sanitized.left_trigger != 0 || sanitized.right_trigger != 0) {
        ++sanitized_nonzero;
      }
      std::this_thread::sleep_for(16ms);
    }

    std::printf("Samples: %d | raw non-zero: %d | sanitized non-zero: %d\n", samples, raw_nonzero, sanitized_nonzero);
    if (sanitized_nonzero == 0) {
      std::puts("PASS: no ghost drift after filtering.");
      return true;
    }
    std::puts("FAIL: residual movement remains after filtering. Increase the deadzone or check the physical controller.");
    return false;
  }

  /**
   * @brief Forward an XInput controller to a virtual ApolloInput device.
   *
   * @param index XInput slot.
   * @param profile Preferred virtual profile.
   * @param seconds Duration in seconds.
   */
  void bridge_xinput(DWORD index, apollo::input::Profile profile, int seconds) {
    if (!xinput.load()) {
      std::fprintf(stderr, "XInput is not available.\n");
      return;
    }

    auto backend = apollo::input::create_backend(apollo::input::BackendKind::automatic);
    std::string error;
    auto pad = backend->create_compatible(profile, "Apollo Input Tester", &error);
    if (!pad) {
      std::fprintf(stderr, "Unable to create the virtual controller: %s\n", error.c_str());
      return;
    }

    std::printf(
      "Physical: XInput %lu -> Virtual: %s (%s)\n",
      static_cast<unsigned long>(index),
      apollo::input::to_string(pad->info().profile),
      pad->info().name.c_str()
    );
    std::puts("Open joy.cpl or Steam > Controller Tester. Press any key to stop.");

    std::atomic<std::uint16_t> rumble_low {0};
    std::atomic<std::uint16_t> rumble_high {0};
    pad->set_feedback_callback([&](const apollo::input::Feedback &feedback) {
      if (feedback.type == apollo::input::FeedbackType::rumble) {
        rumble_low = feedback.low_frequency;
        rumble_high = feedback.high_frequency;
      } else if (feedback.type == apollo::input::FeedbackType::led) {
        std::printf("\nLED: rgb(%u,%u,%u)\n", feedback.led_red, feedback.led_green, feedback.led_blue);
      }
    });
    pad->set_deadzone({3, 2});

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
      XINPUT_STATE state {};
      if (xinput.get_state(index, &state) != ERROR_SUCCESS) {
        std::fprintf(stderr, "\nXInput slot %lu disconnected.\n", static_cast<unsigned long>(index));
        break;
      }

      auto mapped = map_xinput_state(state);
      if (!pad->update(mapped)) {
        std::fprintf(stderr, "\nThe virtual device rejected the report.\n");
        break;
      }

      const auto low = rumble_low.load();
      const auto high = rumble_high.load();
      if (low || high) {
        XINPUT_VIBRATION vibration {
          static_cast<WORD>(low / 257),
          static_cast<WORD>(high / 257),
        };
        xinput.set_state(index, &vibration);
      } else {
        XINPUT_VIBRATION vibration {};
        xinput.set_state(index, &vibration);
      }

      if (_kbhit()) {
        _getch();
        break;
      }
      std::this_thread::sleep_for(8ms);
    }

    pad->disconnect();
  }

  /**
   * @brief Forward a DirectInput controller to a virtual ApolloInput device.
   *
   * @param index DirectInput device index.
   * @param profile Preferred virtual profile.
   * @param seconds Duration in seconds.
   */
  void bridge_dinput(std::size_t index, apollo::input::Profile profile, int seconds) {
    const auto devices = enumerate_dinput_devices();
    if (index >= devices.size()) {
      std::fprintf(stderr, "DirectInput device %zu does not exist.\n", index);
      return;
    }

    open_dinput_device_t physical;
    if (!physical.open(devices[index].instance_guid)) {
      std::fprintf(stderr, "Unable to open DirectInput device %zu.\n", index);
      return;
    }

    DIJOYSTATE2 rest {};
    if (!physical.poll(rest)) {
      std::fprintf(stderr, "Unable to read DirectInput device %zu.\n", index);
      return;
    }
    const auto mapping = detect_mapping(physical.device, rest);
    const auto vendor = devices[index].vendor_id;

    auto backend = apollo::input::create_backend(apollo::input::BackendKind::automatic);
    std::string error;
    auto pad = backend->create_compatible(profile, "Apollo Input Tester", &error);
    if (!pad) {
      std::fprintf(stderr, "Unable to create the virtual controller: %s\n", error.c_str());
      return;
    }

    std::printf(
      "Physical: %s (VID:PID %04lX:%04lX) -> Virtual: %s\n",
      utf8(devices[index].product_name).c_str(),
      static_cast<unsigned long>(vendor),
      static_cast<unsigned long>(devices[index].product_id),
      apollo::input::to_string(pad->info().profile)
    );
    std::puts("Open joy.cpl or Steam > Controller Tester. Press any key to stop.");
    pad->set_deadzone({3, 2});

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
      DIJOYSTATE2 state {};
      if (!physical.poll(state)) {
        std::this_thread::sleep_for(8ms);
        continue;
      }

      const auto mapped = map_dinput_state(state, vendor, mapping);
      if (!pad->update(mapped)) {
        std::fprintf(stderr, "\nThe virtual device rejected the report.\n");
        break;
      }
      if (_kbhit()) {
        _getch();
        break;
      }
      std::this_thread::sleep_for(8ms);
    }

    pad->disconnect();
  }

  /**
   * @brief Short label for a portable button.
   *
   * @param button Button to describe.
   * @return Static label.
   */
  const char *button_name(apollo::input::Button button) {
    using apollo::input::Button;
    switch (button) {
      case Button::a:
        return "A";
      case Button::b:
        return "B";
      case Button::x:
        return "X";
      case Button::y:
        return "Y";
      case Button::dpad_up:
        return "D-pad up";
      case Button::dpad_down:
        return "D-pad down";
      case Button::dpad_left:
        return "D-pad left";
      case Button::dpad_right:
        return "D-pad right";
      case Button::start:
        return "Start";
      case Button::back:
        return "Back";
      case Button::guide:
        return "Guide";
      case Button::left_thumb:
        return "Left stick click";
      case Button::right_thumb:
        return "Right stick click";
      case Button::left_shoulder:
        return "Left shoulder";
      case Button::right_shoulder:
        return "Right shoulder";
      case Button::touchpad:
        return "Touchpad";
      case Button::touchpad_click:
        return "Touchpad click";
      case Button::share:
        return "Share";
      case Button::options:
        return "Options";
      case Button::paddle1:
        return "Paddle 1";
      case Button::paddle2:
        return "Paddle 2";
      case Button::paddle3:
        return "Paddle 3";
      case Button::paddle4:
        return "Paddle 4";
      case Button::misc2:
        return "Misc 2";
    }
    return "Button";
  }

  /**
   * @brief Apply a button state to a gamepad.
   *
   * @param state State to update.
   * @param button Button to change.
   * @param pressed New state.
   */
  void set_button(apollo::input::GamepadState &state, apollo::input::Button button, bool pressed) {
    state.set(button, pressed);
  }

  /**
   * @brief Run a visible sequence on a virtual controller.
   *
   * @param profile Preferred virtual profile.
   * @param seconds Minimum duration in seconds.
   */
  void self_test(apollo::input::Profile profile, int seconds) {
    auto backend = apollo::input::create_backend(apollo::input::BackendKind::automatic);
    std::string error;
    auto pad = backend->create_compatible(profile, "Apollo Input Self Test", &error);
    if (!pad) {
      std::fprintf(stderr, "Unable to create the virtual controller: %s\n", error.c_str());
      return;
    }

    const auto &info = pad->info();
    std::printf("Virtual profile: %s\n", apollo::input::to_string(info.profile));
    std::printf(
      "Capabilities: rumble=%d trigger_rumble=%d leds=%d player_leds=%d imu=%d touchpad=%d adaptive=%d battery=%d paddles=%d misc=%u\n",
      info.capabilities.rumble,
      info.capabilities.trigger_rumble,
      info.capabilities.leds,
      info.capabilities.player_leds,
      info.capabilities.imu,
      info.capabilities.touchpad,
      info.capabilities.adaptive_triggers,
      info.capabilities.battery,
      info.capabilities.paddles,
      static_cast<unsigned>(info.capabilities.misc_buttons)
    );
    std::puts("Open joy.cpl or Steam > Controller Tester now.");
    pad->set_deadzone({3, 2});

    struct step_t {
      const char *label;
      apollo::input::GamepadState state;
      int milliseconds;
    };

    std::vector<step_t> steps;
    steps.push_back({"neutral", {}, 700});
    for (const auto button : {
           apollo::input::Button::a,
           apollo::input::Button::b,
           apollo::input::Button::x,
           apollo::input::Button::y,
         }) {
      apollo::input::GamepadState state;
      set_button(state, button, true);
      steps.push_back({button_name(button), state, 500});
    }
    for (const auto button : {
           apollo::input::Button::dpad_up,
           apollo::input::Button::dpad_down,
           apollo::input::Button::dpad_left,
           apollo::input::Button::dpad_right,
         }) {
      apollo::input::GamepadState state;
      set_button(state, button, true);
      steps.push_back({"d-pad", state, 400});
    }
    {
      apollo::input::GamepadState state;
      state.set(apollo::input::Button::left_shoulder, true);
      state.set(apollo::input::Button::right_shoulder, true);
      steps.push_back({"shoulders", state, 500});
    }
    {
      apollo::input::GamepadState state;
      state.left_trigger = 255;
      state.right_trigger = 255;
      steps.push_back({"triggers", state, 600});
    }
    {
      apollo::input::GamepadState state;
      state.left_x = 32767;
      state.right_x = -32768;
      steps.push_back({"sticks X", state, 600});
    }
    {
      apollo::input::GamepadState state;
      state.left_y = 32767;
      state.right_y = -32768;
      steps.push_back({"sticks Y", state, 600});
    }
    {
      apollo::input::GamepadState state;
      state.set(apollo::input::Button::paddle1, true);
      state.set(apollo::input::Button::share, true);
      steps.push_back({"paddles / share", state, 600});
    }
    steps.push_back({"neutral", {}, 700});

    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    std::size_t index = 0;
    while (std::chrono::steady_clock::now() < end) {
      const auto &step = steps[index % steps.size()];
      std::printf("\rAction: %-20s", step.label);
      std::fflush(stdout);
      pad->update(step.state);
      const auto step_end = std::chrono::steady_clock::now() + std::chrono::milliseconds(step.milliseconds);
      while (std::chrono::steady_clock::now() < step_end && std::chrono::steady_clock::now() < end) {
        if (_kbhit()) {
          _getch();
          pad->update({});
          pad->disconnect();
          std::puts("\nStopped.");
          return;
        }
        std::this_thread::sleep_for(16ms);
      }
      ++index;
    }

    pad->update({});
    pad->disconnect();
    std::puts("\nSelf-test finished.");
  }

  /**
   * @brief Verify the Xbox 360 profile through XInput loopback.
   */
  void verify_x360() {
    if (!xinput.load()) {
      std::fprintf(stderr, "XInput is not available.\n");
      return;
    }

    auto backend = apollo::input::create_backend(apollo::input::BackendKind::automatic);
    std::string error;
    auto pad = backend->create(apollo::input::Profile::xbox_360, "Apollo Input Verify", &error);
    if (!pad) {
      std::fprintf(stderr, "Unable to create the virtual controller: %s\n", error.c_str());
      return;
    }

    std::array<bool, XUSER_MAX_COUNT> before {};
    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
      XINPUT_STATE state {};
      before[index] = xinput.get_state(index, &state) == ERROR_SUCCESS;
    }

    DWORD slot = XUSER_MAX_COUNT;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline && slot == XUSER_MAX_COUNT) {
      for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
        XINPUT_STATE state {};
        if (!before[index] && xinput.get_state(index, &state) == ERROR_SUCCESS) {
          slot = index;
          break;
        }
      }
      std::this_thread::sleep_for(25ms);
    }

    if (slot == XUSER_MAX_COUNT) {
      std::puts("FAIL: the virtual controller did not appear in XInput.");
      pad->disconnect();
      return;
    }

    apollo::input::GamepadState state;
    state.set(apollo::input::Button::a, true);
    state.left_x = 32767;
    pad->update(state);

    bool seen = false;
    const auto button_deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < button_deadline && !seen) {
      XINPUT_STATE xstate {};
      if (xinput.get_state(slot, &xstate) == ERROR_SUCCESS) {
        seen = (xstate.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0 && xstate.Gamepad.sThumbLX > 30000;
      }
      std::this_thread::sleep_for(25ms);
    }

    pad->update({});
    pad->disconnect();

    if (seen) {
      std::printf("PASS: virtual Xbox 360 pad appeared in XInput slot %lu and reported A + left stick.\n", static_cast<unsigned long>(slot));
    } else {
      std::puts("FAIL: XInput did not report the expected A + left stick state.");
    }
  }

  /**
   * @brief Print command-line usage.
   *
   * @param program Executable name.
   */
  void usage(const char *program) {
    std::printf(
      "ApolloInput Nova controller tester\n"
      "\n"
      "Usage:\n"
      "  %s list\n"
      "  %s monitor xinput <slot> [--seconds <n>]\n"
      "  %s monitor dinput <index> [--seconds <n>]\n"
      "  %s drift xinput <slot> [--seconds <n>]\n"
      "  %s drift dinput <index> [--seconds <n>]\n"
      "  %s bridge xinput <slot> [--profile <name>] [--seconds <n>]\n"
      "  %s bridge dinput <index> [--profile <name>] [--seconds <n>]\n"
      "  %s selftest [--profile <name>] [--seconds <n>]\n"
      "  %s verify\n"
      "\n"
      "Profiles: xbox_360, xbox_one, xbox_series, xbox_elite, dualshock4,\n"
      "          dualsense, dualsense_edge, switch_pro, steam_controller,\n"
      "          steam_deck, generic\n"
      "Aliases:  x360, xone, xseries, elite, ds4, ds5, ds5_edge, switch,\n"
      "          steam, deck, generic\n"
      "\n"
      "Examples:\n"
      "  %s list\n"
      "  %s drift xinput 0 --seconds 10\n"
      "  %s bridge xinput 0 --profile ds5 --seconds 120\n"
      "  %s bridge dinput 0 --profile switch --seconds 120\n"
      "  %s selftest --profile xseries --seconds 20\n",
      program,
      program,
      program,
      program,
      program,
      program,
      program,
      program,
      program,
      program,
      program,
      program,
      program,
      program
    );
  }

  /**
   * @brief Parse an integer argument.
   *
   * @param text Numeric text.
   * @return Parsed value, or null when invalid.
   */
  std::optional<int> parse_int(const char *text) {
    char *end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || value < 0) {
      return std::nullopt;
    }
    return static_cast<int>(value);
  }
}  // namespace

int main(int argc, char **argv) {
  SetConsoleOutputCP(CP_UTF8);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  if (argc < 2) {
    usage(argv[0]);
    CoUninitialize();
    return 0;
  }

  const std::string command = argv[1];
  if (command == "list") {
    list_devices();
    CoUninitialize();
    return 0;
  }

  if (command == "verify") {
    verify_x360();
    CoUninitialize();
    return 0;
  }

  if (command == "selftest") {
    apollo::input::Profile profile = apollo::input::Profile::xbox_series;
    int seconds = 20;
    for (int index = 2; index < argc; ++index) {
      if (std::strcmp(argv[index], "--profile") == 0 && index + 1 < argc) {
        if (!apollo::input::profile_from_string(argv[++index], profile)) {
          std::fprintf(stderr, "Unknown profile: %s\n", argv[index]);
          CoUninitialize();
          return 2;
        }
      } else if (std::strcmp(argv[index], "--seconds") == 0 && index + 1 < argc) {
        const auto value = parse_int(argv[++index]);
        if (!value) {
          std::fprintf(stderr, "Invalid duration: %s\n", argv[index]);
          CoUninitialize();
          return 2;
        }
        seconds = *value;
      }
    }
    self_test(profile, seconds);
    CoUninitialize();
    return 0;
  }

  if (command == "monitor" || command == "drift" || command == "bridge") {
    if (argc < 4) {
      usage(argv[0]);
      CoUninitialize();
      return 2;
    }

    const std::string source = argv[2];
    const auto index = parse_int(argv[3]);
    if (!index) {
      std::fprintf(stderr, "Invalid device index: %s\n", argv[3]);
      CoUninitialize();
      return 2;
    }

    apollo::input::Profile profile = apollo::input::Profile::xbox_series;
    int seconds = command == "bridge" ? 120 : 30;
    for (int arg = 4; arg < argc; ++arg) {
      if (std::strcmp(argv[arg], "--profile") == 0 && arg + 1 < argc) {
        if (!apollo::input::profile_from_string(argv[++arg], profile)) {
          std::fprintf(stderr, "Unknown profile: %s\n", argv[arg]);
          CoUninitialize();
          return 2;
        }
      } else if (std::strcmp(argv[arg], "--seconds") == 0 && arg + 1 < argc) {
        const auto value = parse_int(argv[++arg]);
        if (!value) {
          std::fprintf(stderr, "Invalid duration: %s\n", argv[arg]);
          CoUninitialize();
          return 2;
        }
        seconds = *value;
      }
    }

    if (source == "xinput") {
      if (command == "monitor") {
        monitor_xinput(static_cast<DWORD>(*index), seconds);
      } else if (command == "drift") {
        drift_xinput(static_cast<DWORD>(*index), seconds);
      } else {
        bridge_xinput(static_cast<DWORD>(*index), profile, seconds);
      }
    } else if (source == "dinput") {
      if (command == "monitor") {
        monitor_dinput(static_cast<std::size_t>(*index), seconds);
      } else if (command == "drift") {
        drift_dinput(static_cast<std::size_t>(*index), seconds);
      } else {
        bridge_dinput(static_cast<std::size_t>(*index), profile, seconds);
      }
    } else {
      std::fprintf(stderr, "Unknown source: %s (use xinput or dinput)\n", source.c_str());
      CoUninitialize();
      return 2;
    }

    CoUninitialize();
    return 0;
  }

  usage(argv[0]);
  CoUninitialize();
  return 2;
}
