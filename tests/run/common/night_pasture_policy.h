/// @file
/// @brief The run standing in for the chairman at one decision: taking the
/// team to night pasture once it is gathered and the children are on holiday.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN HAS TO PLAY HIM HERE. The night pasture is a standing ORDER
/// (livestock design, «Ночное»), and a headless run has nobody to give it. Up
/// to 2026-09-17 that did not matter, because the team's summer feed discount
/// was applied unconditionally and the runs got the gain for nothing. Behind
/// its three conditions the gain is now EARNED, and a run with no chairman
/// measures a village that simply never takes it — true, and not the question
/// the fodder balance is asked.
///
/// So this asks once, on the first day all three conditions hold, and never
/// again: the order is standing, and re-issuing it would be the run inventing
/// a chore the design does not have.

#ifndef TESTS_RUN_COMMON_NIGHT_PASTURE_POLICY_H_
#define TESTS_RUN_COMMON_NIGHT_PASTURE_POLICY_H_

#include <span>

#include "core_boundary/session.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"

namespace run {

class NightPasturePolicy {
 public:
  void RunDay(core::ISimulation& simulation) const {
    const core::WorldState& world = simulation.CompletedState();
    // The two conditions the run can see without the config: the team is
    // gathered, and no order stands yet. The holidays and the children are
    // the core's to judge — asking on a day it refuses costs one refused row
    // and teaches the run nothing, so the ask is repeated until it takes.
    if (world.chairman.horses_stabled == 0 || world.chairman.night_pasture_ordered != 0) {
      return;
    }
    core::OrderRow order;
    order.kind = core::OrderKind::kGrazeAtNight;
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
  }

  /// @brief Whether the order has taken, for a run that wants to say so.
  static bool Standing(const core::WorldState& world) {
    return world.chairman.night_pasture_ordered != 0;
  }
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_NIGHT_PASTURE_POLICY_H_
