/// @file hay_day_off_policy.h
/// @brief The run chairman's answer to the hay lamp's «объявить день рабочим»
/// (lever 1; 0.37.250; contract core-boss-lever1-hay-pace-contract-2026-10-13):
/// the MORNING the lamp names AlarmAdvice::kDeclareDayWorking he gives
/// kCancelDayOff with `enable` 1 - today's day off is worked.
///
/// WHY IT IS NOT IN HayAnswer: that answer is given in the evening, with the
/// day's alarms, and its orders settle in tomorrow's steps. This advice is the
/// day off's OWN MORNING - the door is today's (TodayMayBeDeclaredWorking), and
/// an order staged in the evening would reach a day that is no longer the day
/// off. So this policy is asked after every step and acts once a day, in the
/// first hour: the order settles the next hour and labour places the day then
/// (ChairmanState::place_after_declaring).
///
/// ONE TRIGGER: the lamp's. He cancels no day off the lamp does not name.
/// @threading SINGLE_THREADED

#ifndef TESTS_RUN_COMMON_HAY_DAY_OFF_POLICY_H_
#define TESTS_RUN_COMMON_HAY_DAY_OFF_POLICY_H_

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "core_world/world.h"

namespace run {

class HayDayOffPolicy {
 public:
  /// @brief After every step. Acts in the day's first hour, once a day.
  /// @return true when the order was staged this step.
  bool RunStep(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    if (core::HourFromTick(world.calendar.tick) != 0 || world.calendar.day == asked_day_) {
      return false;
    }
    asked_day_ = world.calendar.day;
    // ONE ANSWER A WORLD A DAY, WHOEVER HOLDS THE POLICY: the limit policy
    // carries one (limit_policy.h) and a probe or a run may carry another -
    // two would stage the same order twice.
    if (g_answered_world == &simulation && g_answered_day == world.calendar.day) {
      return false;
    }
    g_answered_world = &simulation;
    g_answered_day = world.calendar.day;
    alarms_.clear();
    simulation.CollectAlarms(alarms_);
    for (const core::Alarm& alarm : alarms_) {
      if (alarm.kind != core::AlarmKind::kHerdHayShortAhead) {
        continue;
      }
      // «СНЯТЬ АВРАЛ»: the avral on the cut has stood its days - he lifts it
      // on every meadow it stands on, this morning (kDeclareRush, 0).
      if (alarm.advice == core::AlarmAdvice::kLiftRush) {
        std::vector<core::OrderRow> orders;
        for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
          const core::FieldRow& field = world.fields.rows[row];
          if (field.kind == core::LandKind::kArable || field.phase != core::FieldPhase::kHarvest ||
              field.rush_phase != core::FieldPhase::kHarvest || field.rush_step == 0) {
            continue;
          }
          core::OrderRow order;
          order.kind = core::OrderKind::kDeclareRush;
          order.field = world.fields.row_ids[row];
          order.amount = 0;
          orders.push_back(order);
        }
        simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
        lifted_ += static_cast<std::uint32_t>(orders.size());
        return !orders.empty();
      }
      if (alarm.advice != core::AlarmAdvice::kDeclareDayWorking) {
        continue;
      }
      // A DATED move is another morning's (Alarm::advice_day): the door is
      // that day's, and he waits for it.
      if (alarm.advice_day != 0) {
        continue;
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kCancelDayOff;
      order.enable = 1;
      simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
      ++ordered_;
      return true;
    }
    return false;
  }

  /// @brief The fixture difference, in words, BEFORE the run measures.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — the morning the hay's lamp advises "
                 "«объявить день рабочим» (a dry day off inside the cut with its pace short), the "
                 "run's chairman declares that day off working (kCancelDayOff, today's)\n";
  }

  std::uint32_t Ordered() const { return ordered_; }

  /// The avrals lifted on the lamp's «снять аврал», a meadow each.
  std::uint32_t Lifted() const { return lifted_; }

 private:
  static inline const core::ISimulation* g_answered_world = nullptr;
  static inline core::SimDay g_answered_day = static_cast<core::SimDay>(-1);
  std::uint32_t lifted_ = 0;
  core::SimDay asked_day_ = static_cast<core::SimDay>(-1);
  std::uint32_t ordered_ = 0;
  std::vector<core::Alarm> alarms_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HAY_DAY_OFF_POLICY_H_
