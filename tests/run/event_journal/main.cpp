// Simulation run: does the journal carry what the year actually did?
//
// The structural half of this question is scripts/event_sites.py, which
// walks the enum and asks which kinds have an emitter. It cannot ask whether
// the emitter FIRES, or whether it fires as often as the thing happens —
// that is this run.
//
// Why both halves exist. On 2026-09-05 the host counted: of twenty-nine
// event kinds, eleven were written and eighteen were silent. Over four
// hundred days the village grew from 80 to 253 residents and the journal
// carried not one birth; the herd went from 60 to 230 head and carried not
// one calf. A check that only counted call sites would have been satisfied
// by an emitter behind an `if (false)`.
//
// THE EVENTS ARE PER STEP AND THE OUTBOX IS CLEARED EVERY STEP, so the tally
// has to be taken tick by tick. A run that advances whole days and reads the
// outbox afterwards sees the last tick of each day and calls the rest
// silence — which is its own way of measuring nothing.

#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

#include "../common/run_harness.h"
#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/world_state.h"

namespace {

/// The host's own window, kept deliberately: its numbers (80 -> 253
/// residents, 173 births) are the ones this run has to answer.
constexpr std::uint32_t kDays = 400;
constexpr std::uint64_t kSeed = 1929;

const char* KindName(core::EventKind kind) {
  switch (kind) {
    case core::EventKind::kNone:
      return "none";
    case core::EventKind::kResidentBorn:
      return "resident_born";
    case core::EventKind::kResidentDied:
      return "resident_died";
    case core::EventKind::kResidentArrived:
      return "resident_arrived";
    case core::EventKind::kResidentLeft:
      return "resident_left";
    case core::EventKind::kWedding:
      return "wedding";
    case core::EventKind::kFieldPhaseChanged:
      return "field_phase_changed";
    case core::EventKind::kFieldHarvested:
      return "field_harvested";
    case core::EventKind::kFieldLost:
      return "field_lost";
    case core::EventKind::kHerdBorn:
      return "herd_born";
    case core::EventKind::kHerdDied:
      return "herd_died";
    case core::EventKind::kWalkOff:
      return "walk_off";
    case core::EventKind::kDistributionIssued:
      return "distribution_issued";
    case core::EventKind::kRationIssued:
      return "ration_issued";
    case core::EventKind::kYearClosed:
      return "year_closed";
    case core::EventKind::kOrderAccepted:
      return "order_accepted";
    case core::EventKind::kOrderStarted:
      return "order_started";
    case core::EventKind::kOrderDone:
      return "order_done";
    case core::EventKind::kOrderRefused:
      return "order_refused";
    case core::EventKind::kOrderCancelled:
      return "order_cancelled";
    case core::EventKind::kUnitPaused:
      return "unit_paused";
    case core::EventKind::kUnitResumed:
      return "unit_resumed";
    case core::EventKind::kUnitBuilt:
      return "unit_built";
    case core::EventKind::kUnitDemolished:
      return "unit_demolished";
    case core::EventKind::kUnitRepaired:
      return "unit_repaired";
    case core::EventKind::kUnitCollapsed:
      return "unit_collapsed";
    case core::EventKind::kAppointed:
      return "appointed";
    case core::EventKind::kDismissed:
      return "dismissed";
    case core::EventKind::kPostVacated:
      return "post_vacated";
    case core::EventKind::kHorsesStabled:
      return "horses_stabled";
    case core::EventKind::kEventKindCount:
      return "COUNT";
  }
  return "?";
}

}  // namespace

int main() {
  int failures = 0;
  run::Simulation world = run::Start(kSeed);
  if (!world) {
    return 1;
  }

  constexpr auto kKinds = static_cast<std::size_t>(core::EventKind::kEventKindCount);
  std::array<std::uint32_t, kKinds> seen{};
  // Heads, not events: the herd speaks once per herd per day and carries the
  // calves in `amount`, so the comparable quantity is the sum.
  std::int64_t herd_born_heads = 0;
  // The ledger rotates at the year's end, so the running totals have to be
  // taken off the closed books plus the open one — otherwise a thirty-year
  // count would be the last year's.
  std::uint32_t births = 0;
  std::uint32_t deaths = 0;
  std::uint32_t arrivals = 0;
  std::uint32_t departures = 0;
  std::uint32_t weddings = 0;
  std::uint32_t herd_births = 0;
  std::uint32_t walk_offs = 0;
  std::uint16_t last_closed_year = 0;

  for (std::uint32_t tick = 0; tick < kDays * core::kTicksPerDay; ++tick) {
    world->AdvanceStep();
    const core::WorldState& state = world.State();
    for (const core::SimEvent& event : state.step_events) {
      const auto index = static_cast<std::size_t>(event.kind);
      if (index < kKinds) {
        ++seen[index];
      }
      if (event.kind == core::EventKind::kHerdBorn) {
        herd_born_heads += event.amount;
      }
    }
    // A closed book is counted once, on the tick it closed.
    if (state.ledger.closed.year != last_closed_year) {
      last_closed_year = state.ledger.closed.year;
      births += state.ledger.closed.births;
      deaths += state.ledger.closed.deaths;
      arrivals += state.ledger.closed.arrivals;
      departures += state.ledger.closed.departures;
      weddings += state.ledger.closed.weddings;
      herd_births += state.ledger.closed.herd_births;
      walk_offs += state.ledger.closed.walk_offs;
    }
  }
  const core::YearLedger& open_book = world.State().ledger.current;
  births += open_book.births;
  deaths += open_book.deaths;
  arrivals += open_book.arrivals;
  departures += open_book.departures;
  weddings += open_book.weddings;
  herd_births += open_book.herd_births;
  walk_offs += open_book.walk_offs;

  std::uint32_t kinds_seen = 0;
  std::cout << "event_journal: " << kDays << " days, seed " << kSeed << '\n';
  for (std::size_t index = 1; index < kKinds; ++index) {
    if (seen[index] == 0) {
      continue;
    }
    ++kinds_seen;
    std::cout << "  " << std::setw(20) << std::left << KindName(static_cast<core::EventKind>(index))
              << " " << seen[index] << '\n';
  }
  std::cout << "  kinds that spoke: " << kinds_seen << '\n';

  // -- THE JOURNAL AGAINST THE BOOKS --------------------------------------
  //
  // Not "did the kind appear at all" but "as often as the thing happened".
  // The ledger counts the same events for its own reasons, so the two are
  // independent tallies of one fact, and a disagreement is a defect in
  // whichever of them is wrong.
  const auto count = [&seen](core::EventKind kind) { return seen[static_cast<std::size_t>(kind)]; };
  failures += run::Expect(count(core::EventKind::kResidentBorn) == births,
                          "every birth in the books is a birth in the journal");
  failures += run::Expect(count(core::EventKind::kResidentDied) == deaths, "and every death");
  failures +=
      run::Expect(count(core::EventKind::kResidentArrived) == arrivals, "and every arrival");
  failures +=
      run::Expect(count(core::EventKind::kResidentLeft) == departures, "and every departure");
  failures += run::Expect(count(core::EventKind::kWedding) == weddings, "and every wedding");
  failures += run::Expect(count(core::EventKind::kWalkOff) == walk_offs,
                          "and every man who walked off the job");
  // The herd emits once per herd per day and carries the calves in
  // `amount`, so the counts cannot match — the SUMS must.
  failures += run::Expect(herd_born_heads == static_cast<std::int64_t>(herd_births),
                          "every calf in the books is a calf in the journal");

  // A stopped simulation passes every equality above with zeroes on both
  // sides. The measure has to prove it measured.
  failures +=
      run::Expect(births > 0 && herd_births > 0, "the run actually produced births to compare");
  std::cout << "  books: births " << births << ", deaths " << deaths << ", arrivals " << arrivals
            << ", departures " << departures << ", weddings " << weddings << ", walk-offs "
            << walk_offs << ", herd births " << herd_births << " (journal heads " << herd_born_heads
            << ")\n";

  std::cout << (failures == 0 ? "event_journal: all checks passed\n"
                              : "event_journal: FAILURES ABOVE\n");
  return failures;
}
