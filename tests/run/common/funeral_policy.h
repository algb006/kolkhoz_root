/// @file
/// @brief The run's chairman goes to every LOUD funeral (#1459; social.md
/// «Председатель — по приглашению»): «Пойти» on the invitation.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY. Until #1459 the canon's chairman never pressed «Пойти», so the canon
/// was econ's «goes to none» arm: every loud funeral closed missed, the kin
/// -8 and the household -3 (funeral_rules.h RunFuneralDay). A good bot (the
/// human's word of 2 October: the bots play well) goes where the design says
/// it matters - the loud ones; the quiet ones cost nothing unattended.
///
/// THE RULE. Every morning, each funeral standing with `loud`, its chairman
/// still kInvited and its burial day today or tomorrow, gets a kAttendFuneral
/// order naming it (`amount` = the funeral's id). The core refuses it while
/// the chairman is away in the district (he is then excused at the close).

#ifndef TESTS_RUN_COMMON_FUNERAL_POLICY_H_
#define TESTS_RUN_COMMON_FUNERAL_POLICY_H_

#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

#include "core_common/funeral_state.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"

namespace run {

class FuneralPolicy {
 public:
  /// @brief The difference from the start canon, in words, before the run.
  static void Declare() {
    std::cout << "FIXTURE DIFFERS FROM THE START CANON — the run's chairman presses «Пойти» on "
                 "every LOUD funeral's invitation (kAttendFuneral), the quiet ones left alone "
                 "(#1459; social.md «Председатель — по приглашению»)\n";
  }

  /// @brief #1459's base arm: the chairman goes to none.
  void SetOff() { off_ = true; }

  /// @brief The morning's «Пойти» on the loud funerals not yet answered.
  void RunDay(core::ISimulation& simulation) {
    if (off_) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    std::vector<core::OrderRow> orders;
    for (std::uint32_t row = 0; row < world.funerals.rows.size(); ++row) {
      const core::FuneralRow& funeral = world.funerals.rows[row];
      const std::uint32_t id = world.funerals.row_ids[row].value;
      if (funeral.loud == 0 || funeral.chairman != core::FuneralChairman::kInvited ||
          funeral.burial_day < world.calendar.day || AlreadyAsked(id)) {
        continue;
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kAttendFuneral;
      order.amount = static_cast<core::Grams>(id);
      orders.push_back(order);
      asked_.push_back(id);
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
    }
  }

  /// @brief The «Пойти» orders staged in all (the run's own line).
  std::uint32_t Asked() const { return static_cast<std::uint32_t>(asked_.size()); }

 private:
  bool AlreadyAsked(std::uint32_t id) const {
    for (const std::uint32_t asked : asked_) {
      if (asked == id) {
        return true;
      }
    }
    return false;
  }

  bool off_ = false;
  std::vector<std::uint32_t> asked_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FUNERAL_POLICY_H_
