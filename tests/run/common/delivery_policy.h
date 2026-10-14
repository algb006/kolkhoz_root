/// @file delivery_policy.h
/// @brief The run chairman's plan deliveries and whose transport carries them
/// (routing stage В5; district §4 «Логистика с районом»; econ's rule (c),
/// econ-boss-v5-district-lines-2026-10-14 [1], taken by boss in
/// core-boss-v5-scope-2026-10-14 [3]).
///
/// THE CANON (kCanon): once a day, while a position is owed beyond what the
/// district is already asked for and an own delivery already carries, he
/// orders kDeliverPlan for every position - by OWN CARTS only on a day with no
/// arable field in its harvest and no grain lying reaped on a field, by the
/// DISTRICT's cart otherwise. The straw lying at a field is not counted as a
/// heap (a named reading of econ's «heap»: the straw is feed, carted after
/// the grain).
///
/// THE PAIR's ARMS (a run patch flips kRunDeliveryMode; never two worlds in one
/// tree): kDistrictOnly - every request to the district's calendar (arm D);
/// kOwnOnly - every delivery by own carts (arm O). Arm I is the tree before
/// В5, where no run ordered a delivery and the deadline carried it all.
///
/// Until В5 NO RUN ORDERED kDeliverPlan: the deadline's delivery carried the
/// whole plan at the year's turn. This policy is the first that does.
/// @threading SINGLE_THREADED

#ifndef TESTS_RUN_COMMON_DELIVERY_POLICY_H_
#define TESTS_RUN_COMMON_DELIVERY_POLICY_H_

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>

#include "core_common/land_state.h"
#include "core_common/limit_state.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "core_world/world.h"

namespace run {

/// Whose transport the run chairman sends with the plan's delivery.
enum class RunDeliveryMode : std::uint8_t {
  kCanon,         ///< own carts outside the harvest and its heaps, else the district
  kDistrictOnly,  ///< always the district's calendar (arm D)
  kOwnOnly,       ///< always own carts (arm O)
};

/// The mode every run plays; the pair's arms change this one line.
inline constexpr RunDeliveryMode kRunDeliveryMode = RunDeliveryMode::kCanon;

class DeliveryPolicy {
 public:
  /// @brief One day of the chairman's attention to the plan. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    if (world.plan.announced == 0 || !SomethingUnasked(world)) {
      return;
    }
    const bool own = kRunDeliveryMode == RunDeliveryMode::kOwnOnly ||
                     (kRunDeliveryMode == RunDeliveryMode::kCanon && !HarvestOrHeap(world));
    core::OrderRow order;
    order.kind = core::OrderKind::kDeliverPlan;
    order.enable = own ? 1 : 0;
    order.amount = 0;  // what is owed
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    ++(own ? own_orders_ : district_orders_);
  }

  /// @brief The fixture difference, in words, before anything is measured.
  static void Declare(std::string_view run) {
    std::cout << run
              << ": FIXTURE DIFFERS FROM 0.37.257 - the run's chairman ORDERS THE PLAN'S "
                 "DELIVERY every day a position is owed beyond what is asked (stage В5), "
              << (kRunDeliveryMode == RunDeliveryMode::kDistrictOnly
                      ? "always by the district's monthly cart (arm D)"
                  : kRunDeliveryMode == RunDeliveryMode::kOwnOnly
                      ? "always by own carts (arm O)"
                      : "by own carts on a day with no field in its harvest and no grain "
                        "reaped on a field, by the district's monthly cart otherwise "
                        "(econ's rule (c))")
              << '\n';
  }

  /// @brief How many orders went by each transport.
  void Report(std::string_view run) const {
    std::cout << run << ": the run's chairman ordered " << own_orders_
              << " plan deliveries by own carts and " << district_orders_
              << " by the district's cart\n";
  }

 private:
  /// A position the district is not asked for and no own delivery carries.
  static bool SomethingUnasked(const core::WorldState& world) {
    for (std::size_t index = 0; index < world.plan.due.size(); ++index) {
      core::Grams covered = index < world.plan.delivered.size() ? world.plan.delivered[index] : 0;
      covered += index < world.plan.requested.size() ? world.plan.requested[index] : 0;
      for (const core::LimitDeliveryRow& row : world.limit_deliveries.rows) {
        covered += row.outbound != 0 && index < row.goods.size() ? row.goods[index] : 0;
      }
      if (world.plan.due[index] > covered) {
        return true;
      }
    }
    return false;
  }

  /// An arable field in its harvest, or grain lying reaped on a field.
  static bool HarvestOrHeap(const core::WorldState& world) {
    for (const core::FieldRow& field : world.fields.rows) {
      if (field.reaped_grams > 0 ||
          (field.kind == core::LandKind::kArable && field.phase == core::FieldPhase::kHarvest)) {
        return true;
      }
    }
    return false;
  }

  std::uint32_t own_orders_ = 0;
  std::uint32_t district_orders_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_DELIVERY_POLICY_H_
