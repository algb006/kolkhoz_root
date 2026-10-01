/// @file
/// @brief The exchange's first print (boss-all-barter-counter-go-2026-10-01
/// [3], [7]; econ's barter-counter-2026-10-01 §1.4): the dry count day by
/// day, the day the fact «жителям есть что менять» rose, and the yards'
/// pantries on a few days — the figures econ's STUB thresholds are measured
/// by.
/// @threading SINGLE_THREADED
/// Test-side code: a probe a run calls after every AdvanceStep on the thread
/// that owns the simulation. It reads the completed state and writes lines
/// to stdout; it changes nothing.
///
/// At every day's first tick, for the first kBarterProbeDays days:
///   YB,day=,givers=,takers=,eq_g=<the dry count's grain equivalent, grams>,
///      in_row=,raised=<0|1>,families=,residents=
/// On the day the fact rose, once:
///   YF,day=,eq_g=
/// On days kPantryDays apart, and on the fact's day, a line per yard:
///   YP,day=,family=<row>,eaters=<residents of the yard>,
///      trudodni=<the yard's account this year, hundredths>,
///      pantry=<resource index:grams;...  or "-">
/// The lines are printed at the day's first tick: the dry count is the one
/// of the evening before (the counter's hour), the pantries are what the
/// dinner left and the day's turn added.
/// A run that ends with the fact not risen prints nothing more — the reader
/// (given the days it saw) says «НЕ ПОДНЯЛСЯ» with the highest counters; a
/// probe that printed a polite nought would be read as a measure.
///
/// HOW A RUN OUTSIDE THE TREE HOOKS IT (host's plan700, on the Xeon): the
/// header copied beside the run's source, included after its last include,
/// and `barter_probe::Step(world.State());` after each AdvanceStep.

#ifndef TESTS_RUN_PROBES_BARTER_PROBE_H_
#define TESTS_RUN_PROBES_BARTER_PROBE_H_

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core_common/barter_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace barter_probe {

/// Two game years: the fact is expected inside the first.
inline constexpr std::int64_t kBarterProbeDays = 96;

/// A month of four days between the pantries' prints.
inline constexpr std::int64_t kPantryDays = 4;

inline void PrintPantries(const core::WorldState& world, std::int64_t day) {
  std::vector<unsigned> eaters(world.families.rows.size(), 0U);
  for (const core::ResidentRow& person : world.residents.rows) {
    const std::uint32_t row = core::FindRow(world.families, person.family);
    if (row != core::kNoRow) {
      ++eaters[row];
    }
  }
  for (std::uint32_t row = 0; row < world.families.rows.size(); ++row) {
    std::string pantry;
    const core::ResourceAmounts& held = world.families.rows[row].pantry;
    for (std::size_t index = 0; index < held.size(); ++index) {
      if (held[index] > 0) {
        pantry += std::to_string(index) + ":" + std::to_string(held[index]) + ";";
      }
    }
    std::printf("YP,day=%lld,family=%u,eaters=%u,trudodni=%lld,pantry=%s\n",
                static_cast<long long>(day),
                row,
                eaters[row],
                static_cast<long long>(world.families.rows[row].trudodni_account),
                pantry.empty() ? "-" : pantry.c_str());
  }
}

inline void Step(const core::WorldState& world) {
  static std::int64_t last_day = -1;
  static bool raised_seen = false;
  const auto day = static_cast<std::int64_t>(world.calendar.day);
  if (day == last_day || day >= kBarterProbeDays) {
    return;
  }
  last_day = day;
  const core::BarterWatch& watch = world.barter;
  std::printf(
      "YB,day=%lld,givers=%u,takers=%u,eq_g=%lld,in_row=%u,raised=%u,families=%zu,"
      "residents=%zu\n",
      static_cast<long long>(day),
      static_cast<unsigned>(watch.dry_givers),
      static_cast<unsigned>(watch.dry_takers),
      static_cast<long long>(watch.dry_equivalent),
      static_cast<unsigned>(watch.dry_days_in_row),
      static_cast<unsigned>(watch.worth_starting_raised),
      world.families.rows.size(),
      world.residents.rows.size());
  const bool rose = watch.worth_starting_raised != 0 && !raised_seen;
  if (rose) {
    raised_seen = true;
    std::printf("YF,day=%lld,eq_g=%lld\n",
                static_cast<long long>(day),
                static_cast<long long>(watch.dry_equivalent));
  }
  if (rose || day % kPantryDays == 0) {
    PrintPantries(world, day);
  }
}

}  // namespace barter_probe

#endif  // TESTS_RUN_PROBES_BARTER_PROBE_H_
