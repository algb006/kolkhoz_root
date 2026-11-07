#ifndef TESTS_RUN_COMMON_ENV_VALUE_H_
#define TESTS_RUN_COMMON_ENV_VALUE_H_

/// @file env_value.h
/// @brief An environment variable's value, or none - the one door a run reads the environment by.
/// MSVC refuses
///        std::getenv as unsafe (C4996, an error under /WX: host, 0.37.296-298, and again on
///        #1573's b3dd8b24 - population_curve's KOLKHOZ_WALKERS and thirty_years'
///        KOLKHOZ_SAVE_LAST), so there the copy is _dupenv_s's. social_objects_policy.h's
///        SocialRuleOn keeps its own form of the same rule.
/// @threading SINGLE_THREADED - read at a run's start.

#include <cstdlib>
#include <optional>
#include <string>

namespace run {

/// @return The variable's text, or nullopt when it is not set.
inline std::optional<std::string> EnvValue(const char* variable) {
#ifdef _MSC_VER
  char* value = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&value, &length, variable) != 0 || value == nullptr) {
    return std::nullopt;
  }
  std::string text(value);
  std::free(value);
  return text;
#else
  const char* const value = std::getenv(variable);
  return value == nullptr ? std::nullopt : std::optional<std::string>(value);
#endif
}

}  // namespace run

#endif  // TESTS_RUN_COMMON_ENV_VALUE_H_
