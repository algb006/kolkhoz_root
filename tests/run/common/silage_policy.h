/// @file silage_policy.h
/// @brief The run chairman's silage laying (livestock design §8 «Закладка:
/// приказ, окно, работа»; contract core-boss-silage-contract-2026-10-13;
/// 0.37.248): HE LAYS ON THE HAY LAMP'S ADVICE AND ON NOTHING ELSE. The
/// morning the hay's yellow (kHerdHayShortAhead) names AlarmAdvice::
/// kLaySilage, he gives kLaySilage on the trench the lamp names, with the
/// lamp's own tonnes.
///
/// ONE TRIGGER, NOT TWO (boss and econ, 13 October 2026, four-levers §5c):
/// the first draft ordered «to 150 t less what it holds» on the window's
/// first day by the calendar and read the lamp beside it. Silage is the
/// year's INSURANCE, not a yearly reserve - grass stands uncut in September
/// exactly in the years the hay fails - so a year with no advice lays
/// nothing, and that is expected, not a fault.
/// @threading SINGLE_THREADED

#ifndef TESTS_RUN_COMMON_SILAGE_POLICY_H_
#define TESTS_RUN_COMMON_SILAGE_POLICY_H_

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

class SilagePolicy {
 public:
  explicit SilagePolicy(const core::ITableSet& /*tables*/) {}

  void RunDay(core::ISimulation& simulation) {
    alarms_.clear();
    simulation.CollectAlarms(alarms_);
    for (const core::Alarm& alarm : alarms_) {
      if (alarm.kind != core::AlarmKind::kHerdHayShortAhead ||
          alarm.advice != core::AlarmAdvice::kLaySilage) {
        continue;
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kLaySilage;
      order.unit = alarm.advice_unit;
      order.amount = alarm.advice_amount;
      simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
      ++ordered_;
      ordered_grams_ += alarm.advice_amount;
    }
  }

  /// @brief The fixture difference, in words, BEFORE the run measures.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — the morning the hay's lamp advises a "
                 "silage laying, the run's chairman orders it of the trench the lamp names, with "
                 "the lamp's own tonnes (kLaySilage); a year with no such advice lays nothing\n";
  }

  std::uint32_t Ordered() const { return ordered_; }
  core::Grams OrderedGrams() const { return ordered_grams_; }

 private:
  std::uint32_t ordered_ = 0;
  core::Grams ordered_grams_ = 0;
  std::vector<core::Alarm> alarms_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SILAGE_POLICY_H_
