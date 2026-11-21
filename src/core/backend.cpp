/**
 * @file src/core/backend.cpp
 * @brief Backend selection, compatibility fallback, and name helpers.
 */
#include "apollo/input/backend.hpp"

#include "backends/backend_factory.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <utility>
#include <vector>

namespace apollo::input {
  namespace {
    struct backend_name_t {
      BackendKind kind;
      std::string_view name;
    };

    constexpr std::array BACKEND_NAMES {
      backend_name_t {BackendKind::automatic, "automatic"},
      backend_name_t {BackendKind::vigem, "vigem"},
      backend_name_t {BackendKind::uinput, "uinput"},
      backend_name_t {BackendKind::null_backend, "null"},
    };

    struct backend_alias_t {
      std::string_view alias;
      BackendKind kind;
    };

    constexpr std::array BACKEND_ALIASES {
      backend_alias_t {"automatic", BackendKind::automatic},
      backend_alias_t {"auto", BackendKind::automatic},
      backend_alias_t {"vigem", BackendKind::vigem},
      backend_alias_t {"vigembus", BackendKind::vigem},
      backend_alias_t {"viGEmBus", BackendKind::vigem},
      backend_alias_t {"uinput", BackendKind::uinput},
      backend_alias_t {"linux_uinput", BackendKind::uinput},
      backend_alias_t {"null", BackendKind::null_backend},
      backend_alias_t {"none", BackendKind::null_backend},
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
  }  // namespace

  const char *to_string(BackendKind kind) {
    for (const auto &entry : BACKEND_NAMES) {
      if (entry.kind == kind) {
        return entry.name.data();
      }
    }
    return "unknown";
  }

  bool backend_from_string(const std::string &name, BackendKind &kind) {
    for (const auto &entry : BACKEND_ALIASES) {
      if (equals_ignore_case(entry.alias, name)) {
        kind = entry.kind;
        return true;
      }
    }
    return false;
  }

  std::unique_ptr<Device> Backend::create_compatible(Profile profile, const std::string &name, std::string *error) {
    std::string last_error;
    for (const auto candidate : profile_compatibility_chain(profile)) {
      std::string candidate_error;
      if (auto device = create(candidate, name, &candidate_error)) {
        if (error) {
          error->clear();
        }
        return device;
      }
      if (!candidate_error.empty()) {
        last_error = std::move(candidate_error);
      }
    }

    if (error) {
      *error = last_error.empty() ? std::string {"No compatible gamepad profile is available"} : std::move(last_error);
    }
    return nullptr;
  }

  std::unique_ptr<Backend> create_backend(BackendKind kind) {
    if (kind == BackendKind::null_backend) {
      return detail::create_null_backend();
    }

#if defined(_WIN32) && defined(APOLLO_INPUT_WITH_VIGEM)
    if (kind == BackendKind::automatic || kind == BackendKind::vigem) {
      return detail::create_vigem_backend();
    }
#elif defined(__linux__)
    if (kind == BackendKind::automatic || kind == BackendKind::uinput) {
      return detail::create_uinput_backend();
    }
#endif

    return detail::create_null_backend();
  }
}  // namespace apollo::input
