// A run's positional numbers - the seed, the years - read from the command line WITH A REFUSAL
// (#700, boss 12 Oct 2026). Until then every run read argv[1] with strtoull and no check, so
// «thirty_years --tick-cost» ran seed 0 in silence (strtoull of a word is nought): the cost pair of
// #700 measured a village nobody plays, and an earlier trace (core-submit-561reds.sh, «thirty_years
// --saw-reserve») fell into the same hole. A usage line would not have stopped the next one.
//
// THE RULE: an argument that begins with «--» is a flag, wherever it stands; every other argument
// is a positional, counted from 0 in the order given; a positional that is not a whole decimal
// number is REFUSED - the run says so and stops (rc 2), it never runs a seed it was not asked for.
#ifndef KOLKHOZ_TESTS_RUN_COMMON_RUN_ARGS_H_
#define KOLKHOZ_TESTS_RUN_COMMON_RUN_ARGS_H_

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace run {

/// @brief Positional argument `position` (0-based, flags «--…» skipped) as an unsigned number.
/// @param fallback What `out` takes when the run was given fewer positionals.
/// @param run, what The run's name and the positional's meaning, for the refusal's line.
/// @return false - after printing «<run>: <what> is not a number: <arg>» - when that positional
///         is there and is not a whole decimal number; the caller returns 2.
inline bool PositionalNumber(int argc,
                             char** argv,
                             int position,
                             std::uint64_t fallback,
                             const char* run,
                             const char* what,
                             std::uint64_t& out) {
  out = fallback;
  int seen = 0;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument.starts_with("--")) {
      continue;
    }
    if (seen++ != position) {
      continue;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long value = std::strtoull(argv[index], &end, 10);
    if (argument.empty() || end == argv[index] || *end != '\0' || errno == ERANGE ||
        argument.front() == '-') {
      std::cout << run << ": " << what << " is not a number: «" << argument
                << "» (flags begin with «--» and may stand anywhere)\n";
      return false;
    }
    out = static_cast<std::uint64_t>(value);
    return true;
  }
  return true;
}

}  // namespace run

#endif  // KOLKHOZ_TESTS_RUN_COMMON_RUN_ARGS_H_
