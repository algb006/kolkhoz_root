/// @file
/// @brief The longest run of working days a resident fit for work stood idle
/// for want of a horse, or with the brigade he could join full - «passed
/// over» by the morning plan - per resident, for #1355's return condition.
/// @threading SINGLE_THREADED
/// Test-side code, read from the thread that owns the simulation.
///
/// WHY IT EXISTS (#1355; boss, boss-core-1205-birth-prints [53] and his
/// answer; econ's gates, econ 399d8d1 §1): «who is served first» is parked -
/// on 570410c3 the reversed-table arm reads −2.4, inside 2 SE, where #1347
/// left −11.3 - and its save field (`last_served_day`) is bought only if a
/// resident is passed over more than 8 working days in a row at the P99. This
/// tally is that instrument, standing in a canon run, so the condition needs
/// nobody to remember to look.
///
/// WHAT «PASSED OVER» IS HERE: the morning plan's own word, the idle reason it
/// booked (labor_state.h) - kNoHorse (a horse work or a ride stood for want of
/// a horse), kHorseLock (hosting a horse, no horse work had room) for «the
/// horse»; kCrewCap (a job he could take was capped by its brigade) for «the
/// contested job». econ's third kind, «the haul», is NOT split from them: a
/// carter passed over for a cart reads kNoHorse. A day off, the rain and a
/// day he worked end nothing and add nothing for the day off and the rain;
/// a placed day ends the run.

#ifndef TESTS_RUN_COMMON_PASSED_OVER_TALLY_H_
#define TESTS_RUN_COMMON_PASSED_OVER_TALLY_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core_common/labor_state.h"
#include "core_common/world_state.h"

namespace run {

class PassedOverTally {
 public:
  /// The kinds of choice the tally keeps apart.
  enum Kind : std::uint8_t { kHorse = 0, kCrew = 1, kKinds = 2 };

  /// @brief Reads the day's morning plan off every resident. Call once a day,
  /// after the day.
  void CountDay(const core::WorldState& world) {
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const core::ResidentRow& resident = world.residents.rows[row];
      Runs& runs = runs_[world.residents.row_ids[row].value];
      const core::IdleReason reason = resident.idle_reason;
      if (reason == core::IdleReason::kDayOff || reason == core::IdleReason::kRain) {
        continue;  // neither served nor passed over
      }
      const bool working = resident.work.kind != core::WorkKind::kNone;
      const std::array<bool, kKinds> passed = {!working && (reason == core::IdleReason::kNoHorse ||
                                                            reason == core::IdleReason::kHorseLock),
                                               !working && reason == core::IdleReason::kCrewCap};
      for (std::uint8_t kind = 0; kind < kKinds; ++kind) {
        runs.now[kind] = passed[kind] ? runs.now[kind] + 1U : 0U;
        runs.longest[kind] = std::max(runs.longest[kind], runs.now[kind]);
      }
    }
  }

  /// @brief Each resident's longest run of `kind`, the residents ever passed
  /// over only (the rest are not «served by rotation», they never waited).
  std::vector<std::uint32_t> Longest(Kind kind) const {
    std::vector<std::uint32_t> out;
    for (const auto& [id, runs] : runs_) {
      if (runs.longest[kind] > 0) {
        out.push_back(runs.longest[kind]);
      }
    }
    return out;
  }

 private:
  struct Runs {
    std::array<std::uint32_t, kKinds> now = {};
    std::array<std::uint32_t, kKinds> longest = {};
  };

  std::unordered_map<std::uint32_t, Runs> runs_;  // by resident id
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_PASSED_OVER_TALLY_H_
