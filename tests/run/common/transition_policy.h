/// @file
/// @brief The run standing in for the chairman at the era transition: the
/// office is put in order when everything else is, the order is given the
/// first day the door is open, and the day it took is kept for the run.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN HAS TO PLAY HIM HERE. Until 2026-09-18 the era moved by
/// itself at 500 people (core_residents, UpdateEpoch). It moves by the
/// chairman's order now (order_state.h, kAdvanceEra; epochs §8, «выбор
/// игрока, когда готовность выполнена»), and a headless run has nobody to
/// give it — a village that never goes on would read as a village that
/// never could.
///
/// IT ASKS THE CORE'S OWN QUESTION, TransitionRefusal over the standing
/// blocks of the world now, and orders only on kNone. A second copy of the
/// six blocks here would be one rule with two homes; and asking on a closed
/// door would put a refused row in the book every day for thirty years.
///
/// AND THE OFFICE IS ASKED THE SAME QUESTION WITH ITSELF SET ASIDE. «Every
/// condition but the office» is TransitionRefusal with the office's standing
/// byte forced open — not a list of the other five written again here.

#ifndef TESTS_RUN_COMMON_TRANSITION_POLICY_H_
#define TESTS_RUN_COMMON_TRANSITION_POLICY_H_

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>

#include "core_boundary/session.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/era_readiness.h"

namespace run {

class TransitionPolicy {
 public:
  explicit TransitionPolicy(const core::ITableSet& tables)
      : catalog_(core::ReadReadinessCatalog(tables, core::Epoch::kOne)) {}

  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    if (world.epoch != core::Epoch::kOne) {
      if (year_taken_ == 0) {
        year_taken_ = static_cast<std::uint16_t>(world.calendar.date.year + 1U);
      }
      return;
    }
    if (Refusal(world) != core::OrderRefusal::kNone) {
      return;
    }
    core::OrderRow order;
    order.kind = core::OrderKind::kAdvanceEra;
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
  }

  /// @brief What the core would answer the order in `world` now.
  [[nodiscard]] core::OrderRefusal Refusal(const core::WorldState& world) const {
    return core::TransitionRefusal(
        world.readiness, core::StandingBlocks(catalog_, world), world.epoch);
  }

  /// @brief Every condition of the transition is met but, perhaps, the
  /// office's — the moment a chairman puts his office in order before he
  /// asks (office_policy.h, RunMeetingUpkeep).
  [[nodiscard]] bool AllButOfficeReady(const core::WorldState& world) const {
    core::TransitionBlocks standing = core::StandingBlocks(catalog_, world);
    standing.office_repaired = 1;
    return core::TransitionRefusal(world.readiness, standing, world.epoch) ==
           core::OrderRefusal::kNone;
  }

  /// @brief The campaign year the village was first seen in Epoch II,
  /// counted from 1 as the runs count them ("year 7"), or 0 when it never
  /// went. The calendar counts from 0, hence the +1.
  [[nodiscard]] std::uint16_t YearTaken() const { return year_taken_; }

  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman repairs the office "
                 "once every other condition of the transition is met, and orders the village "
                 "into Epoch II the first day the core says the door is open (both indices "
                 "three years, the six blocks; epochs §6). The era no longer moves by "
                 "population (2026-09-18)\n";
  }

 private:
  core::ReadinessCatalog catalog_;
  std::uint16_t year_taken_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_TRANSITION_POLICY_H_
