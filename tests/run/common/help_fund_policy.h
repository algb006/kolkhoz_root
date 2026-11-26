/// @file
/// @brief The run's chairman and the help fund (#2199; econ's help-fund §3, «good play»): when
///        the core says the fund is empty with a family still short (kHelpFundEmpty), he raises
///        its share by 10 ‰, to the ceiling of 30 - the fund then draws from the free stock
///        above the other funds, and the core gives only what lies there, so «free stock
///        covers it» is the core's own check. The novice leaves the default (host's).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
#ifndef TESTS_RUN_COMMON_HELP_FUND_POLICY_H_
#define TESTS_RUN_COMMON_HELP_FUND_POLICY_H_

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include "core_common/event_state.h"
#include "core_common/help_fund_state.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"

namespace run {

class HelpFundPolicy {
 public:
  void Enable(bool on) { on_ = on; }

  /// @brief After every step: hears «фонд помощи исчерпан».
  void WatchStep(const core::WorldState& world) {
    for (const core::SimEvent& event : world.step_events) {
      empty_heard_ = empty_heard_ || event.kind == core::EventKind::kHelpFundEmpty;
    }
  }

  /// @brief Once a day: raises the share after an «empty».
  void RunDay(core::ISimulation& simulation) {
    if (!on_ || !empty_heard_) {
      return;
    }
    empty_heard_ = false;
    const std::uint16_t share = simulation.CompletedState().help_fund.share_permille;
    if (share >= core::kHelpFundRules.max_permille) {
      return;
    }
    core::OrderRow raise;
    raise.kind = core::OrderKind::kSetHelpFundShare;
    raise.amount = std::min<core::Grams>(share + kStep, core::kHelpFundRules.max_permille);
    std::vector<core::OrderRow> orders = {raise};
    simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
    ++raises_;
  }

  std::uint32_t Raises() const { return raises_; }

 private:
  static constexpr core::Grams kStep = 10;
  bool on_ = true;
  bool empty_heard_ = false;
  std::uint32_t raises_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HELP_FUND_POLICY_H_
