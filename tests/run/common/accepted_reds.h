/// @file
/// @brief The suite's accepted reds: a cell the human accepted below its gate,
/// read from tests/baselines/accepted_reds.txt.
/// @threading SINGLE_THREADED
/// Test-side code, called from main() of a run application before or after
/// its walks; no simulation state is touched.
///
/// WHY A FILE AND NOT A NOTE (boss, core-boss-oats-b-pair [26]): a suite
/// cell that is red on every run teaches everyone to step over it, and a real
/// slip then hides behind it. The human's accepted price is therefore a line
/// the check reads: within the accepted value less one SE the cell passes and
/// says so; below it the cell is red as before and names the line it broke;
/// back inside the gate the cell asks for the line to be dropped. The gate's
/// own constants are not touched.
///
/// The format, one cell a line, `|`-separated, `#` a comment:
///   run|cell|accepted value|SE|date|tree commit|the human's word and its question
///
/// Include it as "../common/accepted_reds.h" (tests/run has no include path).

#ifndef TESTS_RUN_COMMON_ACCEPTED_REDS_H_
#define TESTS_RUN_COMMON_ACCEPTED_REDS_H_

#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace run {

/// One accepted cell of tests/baselines/accepted_reds.txt.
struct AcceptedRed {
  std::string run;
  std::string cell;
  double value = 0.0;
  double se = 0.0;
  std::string date;
  std::string tree;
  std::string word;
};

/// The file's path from the source root, the WORKING_DIRECTORY ctest gives the
/// run applications.
inline constexpr std::string_view kAcceptedRedsPath = "tests/baselines/accepted_reds.txt";

/// @brief The accepted line of `run`'s `cell`, if the file holds one.
/// @return nothing when the file or the line is absent, or the line is
///         malformed (fewer than seven fields): an absent acceptance is a red
///         gate, never a pass.
inline std::optional<AcceptedRed> FindAcceptedRed(std::string_view run_name,
                                                  std::string_view cell) {
  std::ifstream file{std::string(kAcceptedRedsPath)};
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (std::size_t bar = line.find('|'); bar != std::string::npos; bar = line.find('|', start)) {
      fields.push_back(line.substr(start, bar - start));
      start = bar + 1;
    }
    fields.push_back(line.substr(start));
    if (fields.size() < 7 || fields[0] != run_name || fields[1] != cell) {
      continue;
    }
    return AcceptedRed{.run = fields[0],
                       .cell = fields[1],
                       .value = std::strtod(fields[2].c_str(), nullptr),
                       .se = std::strtod(fields[3].c_str(), nullptr),
                       .date = fields[4],
                       .tree = fields[5],
                       .word = fields[6]};
  }
  return std::nullopt;
}

}  // namespace run

#endif  // TESTS_RUN_COMMON_ACCEPTED_REDS_H_
