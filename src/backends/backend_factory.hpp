/**
 * @file src/backends/backend_factory.hpp
 * @brief Internal backend factory declarations.
 */
#pragma once

#include "apollo/input/backend.hpp"

namespace apollo::input::detail {
  /**
   * @brief Create the null backend.
   *
   * @return Null backend instance.
   */
  std::unique_ptr<Backend> create_null_backend();

#if defined(_WIN32) && defined(APOLLO_INPUT_WITH_VIGEM)
  /**
   * @brief Create the ViGEmBus backend.
   *
   * @return ViGEmBus backend instance.
   */
  std::unique_ptr<Backend> create_vigem_backend();
#endif

#if defined(__linux__)
  /**
   * @brief Create the Linux uinput backend.
   *
   * @return uinput backend instance.
   */
  std::unique_ptr<Backend> create_uinput_backend();
#endif
}  // namespace apollo::input::detail
