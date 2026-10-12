// Hands still on a work whose seam is drained (#709's kin; boss and econ, 12 October 2026). The
// herd's care held 10-20 hands at the barn from noon to hour 17 after its care was done (seed 1936,
// year 1) while a ripe potato field had nobody: a day-long kind whose seam drains before evening
// may hold hands the same way elsewhere - unit work, a site between its phases. This counts, hour
// by hour of the working light, every resident still assigned to a work whose seam stands at
// nought, BY KIND, and not released for the day (WorkAssignment::released_home). The herd's care is
// econ's instrument: «hands on kHerdCare after care_days_remaining = 0, hours» - since #709 a care
// drained releases its hands, so its count must be nought, and the run SHOUTS above it.
#ifndef KOLKHOZ_TESTS_RUN_COMMON_SEAM_ZERO_TALLY_H_
#define KOLKHOZ_TESTS_RUN_COMMON_SEAM_ZERO_TALLY_H_

#include <array>
#include <cstdint>
#include <iostream>

#include "core_common/calendar.h"
#include "core_common/day_window.h"
#include "core_common/labor_state.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"

namespace run {

class SeamZeroTally {
 public:
  /// @brief One step's census: every working-light hour, each hand on a drained seam, by kind.
  void CountStep(const core::WorldState& world) {
    const auto hour = static_cast<float>(core::HourFromTick(world.calendar.tick));
    const core::DayWindow window = core::SolarWindow(world.weather.daylight_hours);
    if (hour < window.sunrise || hour >= window.sunset) {
      return;
    }
    for (const core::ResidentRow& person : world.residents.rows) {
      const core::WorkAssignment& work = person.work;
      if (work.kind == core::WorkKind::kNone || work.released_home != 0) {
        continue;
      }
      const float* seam = core::WorkSeamOf(world, work);
      if (seam == nullptr || *seam > 0.0F) {
        continue;
      }
      const auto kind = static_cast<std::size_t>(work.kind);
      if (kind < year_.size()) {
        ++year_[kind];
        ++run_[kind];
      }
    }
  }

  /// @brief Prints the year's hours by kind (only the kinds with any), and clears the year.
  void CloseYear(const char* run_name, std::uint32_t year) {
    std::cout << run_name << ": year " << year << " hand-hours on a drained seam, by kind:";
    bool any = false;
    for (std::size_t kind = 0; kind < year_.size(); ++kind) {
      if (year_[kind] > 0) {
        std::cout << " kind " << kind << " " << year_[kind];
        any = true;
      }
    }
    std::cout << (any ? "" : " none") << '\n';
    year_.fill(0);
  }

  /// @brief The run's hand-hours on a drained herd care - econ's instrument; must be nought since
  /// #709.
  std::uint64_t HerdCareHours() const {
    return run_[static_cast<std::size_t>(core::WorkKind::kHerdCare)];
  }

 private:
  std::array<std::uint64_t, core::kWorkKindCount> year_{};
  std::array<std::uint64_t, core::kWorkKindCount> run_{};
};

}  // namespace run

#endif  // KOLKHOZ_TESTS_RUN_COMMON_SEAM_ZERO_TALLY_H_
