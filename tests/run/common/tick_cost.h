/// @file
/// @brief What a tick costs at 80, 500 and 1500 residents, split by the parts
/// of the step: the instrument for "does the tick grow faster than the
/// village".
/// @threading SINGLE_THREADED
/// Test-side instrument, read from the thread that owns the simulation.
///
/// WHY (boss, thread boss-core-tick-growth-2026-09-20; the human's question
/// about a CPU core for the simulation). The question is not what a tick
/// costs today — it is WHETHER THE COST GROWS LINEARLY with the village. A
/// bottleneck is almost never "much work": it is quadratic, invisible at 80
/// and fatal at 1500, and the three populations are our own milestones
/// towards Epochs II and III.
///
/// THE THREE POPULATIONS ARE MOMENTS OF ONE RUN and not three worlds. The
/// thirty-year run starts at 80 residents and ends between 1400 and 1500, so
/// 500 and 1500 are ticks of that same journey. A hand-made world at 500
/// would have measured a world nobody plays.
///
/// WHAT IT PRINTS, and why each line is there:
///   * the population of the band beside every number, so a band that never
///     filled is visible rather than absent;
///   * how many ticks landed in the band — a mean over three ticks is not a
///     mean, and the count is what says so;
///   * the WORST single tick beside the mean (root rules §6): a frame is lost
///     by the worst tick, not by the average one;
///   * the six phases AND the prologue separately. The prologue is the buffer
///     copy (step.h, buffer-law rule 2): its cost follows the SIZE OF THE
///     WORLD, not the work anyone asked for, and a summary millisecond hides
///     exactly that.
///
/// It is not a device: nothing here asserts, nothing fails a run. A threshold
/// and a guard are earned by a measurement that comes close to one.

#ifndef TESTS_RUN_COMMON_TICK_COST_H_
#define TESTS_RUN_COMMON_TICK_COST_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

#include "core_common/world_state.h"
#include "core_sim/step.h"

namespace run {

/// @brief The populations the measurement asks about, and how wide a band
/// around each one counts as "at that population".
///
/// The window is a tenth on purpose. Too narrow and the band fills with a
/// handful of ticks on the way past; too wide and two bands meet and the
/// measurement compares itself with itself.
struct TickCostBand {
  std::uint32_t residents = 0;

  double window_fraction = 0.1;
};

class TickCost {
 public:
  static constexpr std::size_t kBandCount = 3;

  TickCost()
      : bands_{TickCostBand{.residents = 80, .window_fraction = 0.1},
               TickCostBand{.residents = 500, .window_fraction = 0.1},
               TickCostBand{.residents = 1500, .window_fraction = 0.1}} {}

  /// @brief One step's reading. Call right after AdvanceStep, before
  /// anything else steps the world: the timing read here is the last step's.
  void CountStep(const core::WorldState& state) {
    ++steps_seen_;
    const auto population = static_cast<std::uint32_t>(state.residents.rows.size());
    const core::StepTiming& timing = core::LastStepTiming();
    for (std::size_t index = 0; index < kBandCount; ++index) {
      if (!InBand(bands_[index], population)) {
        continue;
      }
      Band& band = readings_[index];
      ++band.ticks;
      band.step_ns_sum += timing.step_ns;
      band.prologue_ns_sum += timing.prologue_ns;
      for (std::size_t phase = 0; phase < core::kStepPhaseCount; ++phase) {
        band.phase_ns_sum[phase] += timing.phase_ns[phase];
      }
      if (timing.step_ns > band.worst_step_ns) {
        band.worst_step_ns = timing.step_ns;
        band.worst_at_residents = population;
      }
      band.lowest_residents =
          band.lowest_residents == 0 ? population : std::min(band.lowest_residents, population);
      band.highest_residents = std::max(band.highest_residents, population);
      return;
    }
    ++steps_outside_;
  }

  /// @brief Prints every band, filled or empty, and the growth between them.
  void Report(std::string_view run_name) const {
    std::cout << run_name << ": tick cost by population — " << steps_seen_ << " ticks timed, "
              << steps_outside_ << " of them outside every band\n";
    if (!core::StepTimingEnabled()) {
      // A STOPPED CLOCK IS UNDER EVERY BUDGET. Said first, because every
      // number below would otherwise read as a fast simulation.
      std::cout << run_name << ": THE STEP CLOCK WAS NEVER ON — every figure below is a zero "
                << "that means \"not measured\", not \"free\"\n";
    }
    for (std::size_t index = 0; index < kBandCount; ++index) {
      ReportBand(run_name, bands_[index], readings_[index]);
    }
    ReportGrowth(run_name);
  }

 private:
  struct Band {
    std::uint64_t ticks = 0;

    std::uint64_t step_ns_sum = 0;

    std::uint64_t prologue_ns_sum = 0;

    std::array<std::uint64_t, core::kStepPhaseCount> phase_ns_sum = {};

    std::uint64_t worst_step_ns = 0;

    std::uint32_t worst_at_residents = 0;

    std::uint32_t lowest_residents = 0;

    std::uint32_t highest_residents = 0;
  };

  static bool InBand(const TickCostBand& band, std::uint32_t population) {
    const double half = static_cast<double>(band.residents) * band.window_fraction;
    return static_cast<double>(population) >= static_cast<double>(band.residents) - half &&
           static_cast<double>(population) <= static_cast<double>(band.residents) + half;
  }

  static double Millis(std::uint64_t nanos, std::uint64_t over) {
    return over == 0 ? 0.0 : static_cast<double>(nanos) / static_cast<double>(over) / 1e6;
  }

  static std::string_view PhaseName(std::size_t phase) {
    switch (static_cast<core::StepPhase>(phase)) {
      case core::StepPhase::kTimeAndWeather:
        return "time+weather";
      case core::StepPhase::kNeeds:
        return "needs";
      case core::StepPhase::kDecisions:
        return "decisions";
      case core::StepPhase::kProduction:
        return "production";
      case core::StepPhase::kMetrics:
        return "metrics";
      case core::StepPhase::kEvents:
        return "events";
    }
    return "?";
  }

  static void ReportBand(std::string_view run_name, const TickCostBand& band, const Band& reading) {
    if (reading.ticks == 0) {
      // AN EMPTY BAND SHOUTS. A polite zero in a list of numbers is read by
      // nobody, and a band that never filled is the one thing that would
      // quietly turn this measurement into an opinion.
      std::cout << run_name << ": " << band.residents
                << " residents — NO TICK EVER LANDED IN THIS BAND, so nothing below was "
                   "measured at that population\n";
      return;
    }
    const std::uint64_t ticks = reading.ticks;
    std::cout << std::fixed << std::setprecision(3);
    std::cout << run_name << ": " << band.residents << " residents (" << reading.lowest_residents
              << ".." << reading.highest_residents << " seen, " << ticks << " ticks) — mean "
              << Millis(reading.step_ns_sum, ticks) << " ms/tick, worst single tick "
              << Millis(reading.worst_step_ns, 1) << " ms at " << reading.worst_at_residents
              << " residents\n";
    std::cout << run_name << ": " << band.residents << " residents — prologue (buffer copy) "
              << Millis(reading.prologue_ns_sum, ticks) << " ms";
    std::uint64_t parts = reading.prologue_ns_sum;
    for (std::size_t phase = 0; phase < core::kStepPhaseCount; ++phase) {
      parts += reading.phase_ns_sum[phase];
      std::cout << ", " << PhaseName(phase) << ' ' << Millis(reading.phase_ns_sum[phase], ticks)
                << " ms";
    }
    // The swap and the clock's own reads: printed rather than assumed small.
    const double rest = Millis(reading.step_ns_sum, ticks) - Millis(parts, ticks);
    std::cout << ", the rest of the step " << rest << " ms\n";
  }

  /// The one question the whole instrument exists for, asked in ratios: a
  /// tick that is linear in the village grows exactly as the population does.
  void ReportGrowth(std::string_view run_name) const {
    for (std::size_t index = 1; index < kBandCount; ++index) {
      const Band& from = readings_[index - 1];
      const Band& to = readings_[index];
      if (from.ticks == 0 || to.ticks == 0) {
        std::cout << run_name << ": " << bands_[index - 1].residents << " -> "
                  << bands_[index].residents
                  << " — GROWTH NOT MEASURABLE, one of the two bands is empty\n";
        continue;
      }
      const double residents_ratio = static_cast<double>(bands_[index].residents) /
                                     static_cast<double>(bands_[index - 1].residents);
      const double cost_ratio =
          Millis(to.step_ns_sum, to.ticks) / Millis(from.step_ns_sum, from.ticks);
      std::cout << run_name << ": " << bands_[index - 1].residents << " -> "
                << bands_[index].residents << " — residents x" << residents_ratio << ", tick x"
                << cost_ratio << " (linear growth would print the same two numbers)\n";
    }
  }

  std::array<TickCostBand, kBandCount> bands_;

  std::array<Band, kBandCount> readings_ = {};

  std::uint64_t steps_seen_ = 0;

  std::uint64_t steps_outside_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_TICK_COST_H_
