/// @file
/// @brief SCALE2118 (#2118, boss 24 Nov; Alex's rule: a cost that grows with the world is
///        solved at once): what a village's day costs as the village grows. Per village-year,
///        this thread's CPU seconds a day in the simulation's steps and in the run's chairman,
///        beside the residents, units and fields at the year's end; at the village's end the
///        log-log slope of the steps' seconds a day against the residents over its years - the
///        EXPONENT - and «SCALING: exponent X on <phase>» when it passes kShoutExponent.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that walks the village (one village a thread, and
/// population_curve starts each with worker_count 1, so every step runs on this thread and
/// the thread's CPU clock is the village's whole cost; a run with more workers would MISS the
/// parallel phases' work and must not read this).
///
/// GROUND, printed with every line: the tree and the arm as the job's environment names them
/// (KOLKHOZ_TREE, WP_ROWS; «unnamed» when unset), the village-years that went into the fit,
/// and which years were left out (the first two: genesis and the first buildings are not the
/// village's growth). NOT MEASURED here: which phase inside the steps grows - TIME2072-style
/// timers on the worst year answer that; this names WHETHER and HOW FAST.

#ifndef TESTS_RUN_COMMON_SCALE_TALLY_H_
#define TESTS_RUN_COMMON_SCALE_TALLY_H_

#include <time.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/world_state.h"

namespace run {

class ScaleTally {
 public:
  /// The exponent above which the village's growth is called out, not printed quietly.
  static constexpr double kShoutExponent = 1.3;
  /// The first years left out of the fit (genesis and the first buildings).
  static constexpr std::uint32_t kSkipYears = 2;

  /// @brief This thread's CPU seconds now (CLOCK_THREAD_CPUTIME_ID).
  static double ThreadSeconds() {
    timespec now{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return static_cast<double>(now.tv_sec) + (static_cast<double>(now.tv_nsec) * 1e-9);
  }

  void AddSimulation(double seconds) { year_.simulation += seconds; }

  void AddChairman(double seconds) { year_.chairman += seconds; }

  /// @brief Closes a year: the world at its end, the year's days.
  void CloseYear(const core::WorldState& world, std::uint32_t year) {
    year_.year = year;
    year_.residents = world.residents.rows.size();
    year_.units = world.units.rows.size();
    year_.fields = world.fields.rows.size();
    years_.push_back(year_);
    year_ = Year{};
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    const std::string ground = Ground();
    for (const Year& year : years_) {
      out << "population_curve: SCALE2118 seed " << seed << " year " << year.year << ' ' << ground
          << " residents " << year.residents << " units " << year.units << " fields " << year.fields
          << " sim_cpu_s_per_day " << year.simulation / core::kDaysPerYear
          << " chairman_cpu_s_per_day " << year.chairman / core::kDaysPerYear << '\n';
    }
    PrintFit(out, seed, ground, "simulation", [](const Year& year) { return year.simulation; });
    PrintFit(out, seed, ground, "chairman", [](const Year& year) { return year.chairman; });
  }

 private:
  struct Year {
    std::uint32_t year = 0;
    double simulation = 0.0;
    double chairman = 0.0;
    std::size_t residents = 0;
    std::size_t units = 0;
    std::size_t fields = 0;
  };

  static std::string Ground() {
    const char* const tree = std::getenv("KOLKHOZ_TREE");
    const char* const arm = std::getenv("WP_ROWS");
    return std::string("ground=tree:") + (tree != nullptr && *tree != '\0' ? tree : "unnamed") +
           ";arm:" + (arm != nullptr && *arm != '\0' ? arm : "canon");
  }

  template <class Seconds>
  void PrintFit(std::ostream& out,
                std::uint64_t seed,
                const std::string& ground,
                const char* phase,
                Seconds seconds) const {
    // Least squares of log(seconds a day) on log(residents), years after kSkipYears.
    double sum_x = 0.0;
    double sum_y = 0.0;
    double sum_xx = 0.0;
    double sum_xy = 0.0;
    std::uint32_t count = 0;
    std::size_t low = 0;
    std::size_t high = 0;
    for (const Year& year : years_) {
      const double value = seconds(year);
      if (year.year <= kSkipYears || year.residents == 0 || !(value > 0.0)) {
        continue;
      }
      const double x = std::log(static_cast<double>(year.residents));
      const double y = std::log(value);
      sum_x += x;
      sum_y += y;
      sum_xx += x * x;
      sum_xy += x * y;
      ++count;
      low = low == 0 || year.residents < low ? year.residents : low;
      high = year.residents > high ? year.residents : high;
    }
    out << "population_curve: SCALE2118 seed " << seed << " fit " << phase << ' ' << ground
        << " village_years " << count << " (years 1-" << kSkipYears << " left out) residents "
        << low << ".." << high;
    const double spread = (count * sum_xx) - (sum_x * sum_x);
    // A fit over too few years or too narrow a range of residents says nothing: said so.
    if (count < 5 || high < low * 3 / 2 || !(spread > 0.0)) {
      out << " exponent NOT MEASURED (too few years or residents grew under 1.5x)\n";
      return;
    }
    const double exponent = ((count * sum_xy) - (sum_x * sum_y)) / spread;
    out << " exponent " << exponent;
    if (exponent > kShoutExponent) {
      out << " SCALING: exponent " << exponent << " on " << phase;
    }
    out << '\n';
  }

  Year year_;
  std::vector<Year> years_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SCALE_TALLY_H_
