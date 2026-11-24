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

#ifndef _MSC_VER
#include <time.h>
#endif

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/time_probe.h"
#include "core_common/world_state.h"
#include "env_value.h"

namespace run {

class ScaleTally {
 public:
  /// The exponent above which the village's growth is called out, not printed quietly.
  static constexpr double kShoutExponent = 1.3;
  /// The first years left out of the fit (genesis and the first buildings).
  static constexpr std::uint32_t kSkipYears = 2;

  /// @brief This thread's CPU seconds now (CLOCK_THREAD_CPUTIME_ID). Under MSVC there is no
  ///        POSIX thread clock (the host's save-230 build, C3861): 0, so every year's seconds
  ///        are 0 and the fit prints NOT MEASURED - SCALE2118 is measured on the Linux nodes.
  static double ThreadSeconds() {
#ifdef _MSC_VER
    return 0.0;
#else
    timespec now{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return static_cast<double>(now.tv_sec) + (static_cast<double>(now.tv_nsec) * 1e-9);
#endif
  }

  void AddSimulation(double seconds) {
    if (!probe_started_) {  // PROBE #2118: the village's baseline on its first day
      probe_last_ = core::probe::Thread();
      probe_started_ = true;
    }
    year_.simulation += seconds;
  }

  void AddChairman(double seconds) { year_.chairman += seconds; }

  /// @brief Closes a year: the world at its end, the year's days.
  void CloseYear(const core::WorldState& world, std::uint32_t year) {
    year_.year = year;
    year_.residents = world.residents.rows.size();
    year_.units = world.units.rows.size();
    year_.fields = world.fields.rows.size();
    if (probe_started_) {  // PROBE #2118: this thread's slots since the last close
      const core::probe::Counters& now = core::probe::Thread();
      for (std::size_t slot = 0; slot < core::probe::kSlots; ++slot) {
        year_.slot_s[slot] = static_cast<double>(now.ns[slot] - probe_last_.ns[slot]) * 1e-9;
      }
      year_.pairs = static_cast<double>(now.measure_pairs - probe_last_.measure_pairs);
      probe_last_ = now;
    }
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
    // PROBE #2118: every slot's year lines and fit, and MeasureRoads' pairs.
    static constexpr std::array<const char*, core::probe::kSlots> kNames = {"prologue",
                                                                            "time_weather",
                                                                            "needs",
                                                                            "decisions",
                                                                            "production",
                                                                            "metrics",
                                                                            "events",
                                                                            "measure_roads",
                                                                            "plan_assignments",
                                                                            "collect",
                                                                            "make_day",
                                                                            "with_trails",
                                                                            "tread",
                                                                            "decay"};
    for (const Year& year : years_) {
      out << "population_curve: SCALE2118P seed " << seed << " year " << year.year << " residents "
          << year.residents << " units " << year.units << " pairs_per_day "
          << year.pairs / core::kDaysPerYear;
      for (std::size_t slot = 0; slot < core::probe::kSlots; ++slot) {
        out << ' ' << kNames[slot] << ' ' << year.slot_s[slot] / core::kDaysPerYear;
      }
      out << '\n';
    }
    for (std::size_t slot = 0; slot < core::probe::kSlots; ++slot) {
      PrintFit(
          out, seed, ground, kNames[slot], [slot](const Year& year) { return year.slot_s[slot]; });
    }
    PrintFit(out, seed, ground, "measure_pairs", [](const Year& year) { return year.pairs; });
  }

 private:
  struct Year {
    std::uint32_t year = 0;
    double simulation = 0.0;
    double chairman = 0.0;
    std::array<double, core::probe::kSlots> slot_s{};  // PROBE #2118
    double pairs = 0.0;                                // PROBE #2118
    std::size_t residents = 0;
    std::size_t units = 0;
    std::size_t fields = 0;
  };

  // Through run::EnvValue: MSVC refuses std::getenv (C4996 under /WX; the save-230 build).
  static std::string Ground() {
    const std::optional<std::string> tree = run::EnvValue("KOLKHOZ_TREE");
    const std::optional<std::string> arm = run::EnvValue("WP_ROWS");
    return std::string("ground=tree:") + (tree && !tree->empty() ? *tree : "unnamed") +
           ";arm:" + (arm && !arm->empty() ? *arm : "canon");
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
  bool probe_started_ = false;          // PROBE #2118
  core::probe::Counters probe_last_{};  // PROBE #2118
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SCALE_TALLY_H_
