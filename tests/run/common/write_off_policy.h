/// @file
/// @brief The run chairman writes a store's shortage off after the audit's
/// first remark (#1568 link 2d's lever; boss, the #1503 thread [41]: «a good
/// chairman pulls this lever, and the bots are meant to play well» - the
/// human's 2 Oct rule).
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
///
/// The rule: once the district's audit has given its remark
/// (WorldState::audit.remarked), every store with a gap in its book gets a
/// kWriteOffShortage the same evening; the core books the grams, takes the
/// reputation and withdraws the remark. Nothing happens while the remark
/// stands withdrawn, so a store is written off once a remark. With the crime
/// links off there is never a remark, and the rule never acts - the canon.
/// `Enable(false)` is the arm without it (--no-write-off).

#ifndef TESTS_RUN_COMMON_WRITE_OFF_POLICY_H_
#define TESTS_RUN_COMMON_WRITE_OFF_POLICY_H_

#include <cstdint>
#include <span>
#include <vector>

#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"

namespace run {

class WriteOffPolicy {
 public:
  /// @brief On by default; off is the arm that never writes off.
  void Enable(bool on) { on_ = on; }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    if (!on_ || world.audit.remarked == 0) {
      return;
    }
    std::vector<core::OrderRow> orders;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      core::Grams gap = 0;
      for (const core::Grams grams : world.units.rows[row].book_gap) {
        gap += grams;
      }
      if (gap > 0) {
        core::OrderRow order;
        order.kind = core::OrderKind::kWriteOffShortage;
        order.unit = world.units.row_ids[row];
        orders.push_back(order);
      }
    }
    if (!orders.empty()) {
      written_off_orders_ += static_cast<std::uint32_t>(orders.size());
      simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
    }
  }

  /// @brief The write-off orders the rule staged, for a run to print.
  std::uint32_t Orders() const { return written_off_orders_; }

 private:
  bool on_ = true;
  std::uint32_t written_off_orders_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_WRITE_OFF_POLICY_H_
