/// @file
/// @brief The samogon's print (econ-boss-moonshine-sink-2026-10-01 [4]-[7];
/// 0.37.72): the year's litres brewed and sold, what the distillers hold,
/// what the yards paid for it and what left the stores — the figures econ's
/// prediction of 60–100 litres in year 1 is measured by.
/// @threading SINGLE_THREADED
/// Test-side code: a probe a run calls after every AdvanceStep on the thread
/// that owns the simulation. It reads the completed state and writes lines
/// to stdout; it changes nothing.
///
/// Once a year, at the first step seen of the year's LAST day:
///   YS,year=<1-based>,brewed_ml=,sold_ml=,held_ml=<all distillers, now>,
///      distillers=,paid_g=<the yards' payment, all resources>,
///      stolen=<resource index:grams;... off the stores, all thieves, or "-">
/// The book's figures are the year's so far: the last day's own night is
/// not in them, and the month's sale lands on the NEXT month's first day,
/// so December's is the next year's first.
/// A tree older than save 122 has no litres in the book and does not build
/// with this probe — which is the answer the reader wants from it.
///
/// HOW A RUN OUTSIDE THE TREE HOOKS IT (host's plan700, on the Xeon): the
/// header copied beside the run's source, included after its last include,
/// and `samogon_probe::Step(world.State());` after each AdvanceStep.

#ifndef TESTS_RUN_PROBES_SAMOGON_PROBE_H_
#define TESTS_RUN_PROBES_SAMOGON_PROBE_H_

#include <cstdint>
#include <cstdio>
#include <string>

#include "core_common/calendar.h"
#include "core_common/world_state.h"

namespace samogon_probe {

inline void Step(const core::WorldState& world) {
  static std::int64_t printed_year = -1;
  const auto day = static_cast<std::int64_t>(world.calendar.day);
  const std::int64_t year = day / static_cast<std::int64_t>(core::kDaysPerYear);
  if (day % static_cast<std::int64_t>(core::kDaysPerYear) !=
          static_cast<std::int64_t>(core::kDaysPerYear) - 1 ||
      year == printed_year) {
    return;
  }
  printed_year = year;
  std::uint64_t held = 0;
  unsigned distillers = 0;
  for (const core::ResidentRow& person : world.residents.rows) {
    if (person.night_trade == core::NightTrade::kDistiller) {
      ++distillers;
      held += person.samogon_ml;
    }
  }
  const core::YearLedger& book = world.ledger.current;
  long long paid = 0;
  for (const core::Grams grams : book.samogon_paid) {
    paid += static_cast<long long>(grams);
  }
  std::string stolen;
  for (std::size_t index = 0; index < book.stolen.size(); ++index) {
    if (book.stolen[index] > 0) {
      stolen += std::to_string(index) + ":" + std::to_string(book.stolen[index]) + ";";
    }
  }
  std::printf(
      "YS,year=%lld,brewed_ml=%lld,sold_ml=%lld,held_ml=%llu,distillers=%u,paid_g=%lld,"
      "stolen=%s\n",
      static_cast<long long>(year + 1),
      static_cast<long long>(book.samogon_brewed_ml),
      static_cast<long long>(book.samogon_sold_ml),
      static_cast<unsigned long long>(held),
      distillers,
      paid,
      stolen.empty() ? "-" : stolen.c_str());
}

}  // namespace samogon_probe

#endif  // TESTS_RUN_PROBES_SAMOGON_PROBE_H_
