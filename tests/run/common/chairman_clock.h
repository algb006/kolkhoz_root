// The run chairman's own time, policy by policy (#700, boss 12 Oct 2026). thirty_years' speed gate
// timed each game day WHOLE - the core's 24 steps AND the run's daily policies after them - and on
// 0.37.234 the policies carried most of the rise (seed 1929: 128 -> 176 s over thirty years, year
// 29 14 -> 42 s; the core's steps +8 %). The gate measures the GAME, so it now holds the steps
// alone; the chairman is the test side, printed beside it with each policy's time and NO gate, so
// the test side can no longer redden the engine's.
#ifndef KOLKHOZ_TESTS_RUN_COMMON_CHAIRMAN_CLOCK_H_
#define KOLKHOZ_TESTS_RUN_COMMON_CHAIRMAN_CLOCK_H_

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace run {

class ChairmanClock {
 public:
  /// @brief Runs `call` and books its wall time under `policy` in the current year.
  template <typename Call>
  void Time(const char* policy, Call&& call) {
    const auto began = std::chrono::steady_clock::now();
    std::forward<Call>(call)();
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    Slot(policy) += seconds;
  }

  /// @brief Closes year `year` (from 1): its total and its policies kept if it is the costliest so
  /// far.
  void CloseYear(std::uint32_t year) {
    double total = 0.0;
    for (const auto& [name, seconds] : this_year_) {
      total += seconds;
      bool found = false;
      for (auto& [run_name, run_seconds] : run_) {
        if (run_name == name) {
          run_seconds += seconds;
          found = true;
        }
      }
      if (!found) {
        run_.emplace_back(name, seconds);
      }
    }
    run_total_ += total;
    if (total > costliest_total_) {
      costliest_total_ = total;
      costliest_year_ = year;
      costliest_ = this_year_;
    }
    this_year_.clear();
  }

  /// @brief Prints the run's chairman time: the total, each policy's, and the costliest year's by
  /// policy.
  void Report(const char* run_name) const {
    std::cout << run_name << ": the run's chairman (test side, no gate) took " << run_total_
              << " s over the run;";
    for (const auto& [name, seconds] : run_) {
      std::cout << ' ' << name << ' ' << seconds;
    }
    std::cout << '\n'
              << run_name << ": its costliest year was year " << costliest_year_ << ", "
              << costliest_total_ << " s;";
    for (const auto& [name, seconds] : costliest_) {
      std::cout << ' ' << name << ' ' << seconds;
    }
    std::cout << '\n';
  }

 private:
  double& Slot(const char* policy) {
    for (auto& [name, seconds] : this_year_) {
      if (name == policy) {
        return seconds;
      }
    }
    this_year_.emplace_back(policy, 0.0);
    return this_year_.back().second;
  }

  std::vector<std::pair<std::string, double>> this_year_;
  std::vector<std::pair<std::string, double>> run_;
  std::vector<std::pair<std::string, double>> costliest_;
  double run_total_ = 0.0;
  double costliest_total_ = 0.0;
  std::uint32_t costliest_year_ = 0;
};

}  // namespace run

#endif  // KOLKHOZ_TESTS_RUN_COMMON_CHAIRMAN_CLOCK_H_
