/// @file
/// @brief The run standing in for the chairman at one decision: renewing a
/// sown grass stand that has lived its life.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN HAS TO PLAY HIM HERE (0.37.212; boss's ruling of 10 October
/// 2026, on the human's word of 2 October that a run's bot is given the sound
/// move and not a gross mistake). A sown grass stand has an age since
/// 0.37.212 (grass_stand.csv): its last described summer and every later one
/// give 0.4 of the table's yield. The start's layout keeps ten hectares under
/// timothy for ever, and a headless run has nobody to plough them up — the
/// first canon under the rule measured a chairman who let his only hay field
/// stand at 0.4 for fifteen years: cows −14 %, milk −15 % over twenty years.
///
/// ONE ANSWER A YEAR, AND NOTHING SMARTER: on the year's first day he re-gives
/// its own chain to every field whose stand ENTERS THE SUMMER OF THE TABLE's
/// LAST ROW (ISimulation::GrassStandOf: summer >= summers described). Nothing
/// standing is lost in winter; the layout's rotation stays as it was given;
/// «plough up for grain» stays the player's decision. A stand being cut is
/// left for the next year (the core would refuse the order).
///
/// IT SAYS WHAT IT DID: a line a year with the fields renewed, and the count
/// of stand-summers entered at each summer of the life — so «the chairman
/// renews» is a number, and a field left at the last row names itself.
///
/// THE FIXTURE DIFFERS FROM THE START CANON, AND IT SAYS SO (Declare).

#ifndef TESTS_RUN_COMMON_STAND_RENEWAL_POLICY_H_
#define TESTS_RUN_COMMON_STAND_RENEWAL_POLICY_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/grass_stand_view.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"

namespace run {

class StandRenewalPolicy {
 public:
  static void Declare(const std::string& run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — on each year's first day the run's "
                 "chairman re-gives its own rotation to every field whose sown grass stand enters "
                 "the last described summer of its life (grass_stand.csv), so the stand is "
                 "ploughed up and sown anew that spring; a canon player would decide it himself\n";
  }

  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t year = world.calendar.day / core::kDaysPerYear;
    if (world.calendar.day % core::kDaysPerYear != 0 || year == last_year_asked_) {
      return;
    }
    last_year_asked_ = year;
    std::vector<core::OrderRow> orders;
    std::string renewed;
    for (std::size_t row = 0; row < world.fields.rows.size(); ++row) {
      const core::FieldId id = world.fields.row_ids[row];
      const core::GrassStandView stand = simulation.GrassStandOf(id);
      if (!stand.stands) {
        continue;
      }
      // The summer the stand ENTERS this year is the one its next cut is
      // taken in: counted here, by the summer of the life (the last slot
      // holds the last described summer and every later one).
      const std::size_t slot =
          std::min<std::size_t>(stand.summer == 0 ? 1U : stand.summer, summers_.size()) - 1U;
      const bool lived = stand.summers_described > 0 && stand.summer >= stand.summers_described;
      if (!lived || stand.being_cut) {
        ++summers_[slot];
        continue;
      }
      const core::FieldRow& field = world.fields.rows[row];
      core::OrderRow order;
      order.kind = core::OrderKind::kSetRotation;
      order.field = id;
      order.rotation_year0 = field.rotation_year0;
      order.rotation_year1 = field.rotation_year1;
      order.rotation_year2 = field.rotation_year2;
      orders.push_back(order);
      // Renewed: the stand it becomes is in its first summer this year.
      ++summers_[0];
      ++renewed_total_;
      renewed += (renewed.empty() ? "" : "; ") + std::string("field ") + std::to_string(id.value) +
                 " at (" + std::to_string(static_cast<int>(field.center.x)) + ", " +
                 std::to_string(static_cast<int>(field.center.y)) + "), " +
                 std::to_string(static_cast<int>(field.area_ga)) + " ha, in summer " +
                 std::to_string(static_cast<int>(stand.summer));
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
    }
    std::cout << "stands renewed, year " << (year + 1U) << ": " << orders.size() << " fields"
              << (renewed.empty() ? "" : " — " + renewed) << "; stand-summers entered so far by "
              << "summer of the life 1.." << summers_.size() << "+:";
    for (const std::uint32_t count : summers_) {
      std::cout << ' ' << count;
    }
    std::cout << "; renewals so far " << renewed_total_ << '\n';
  }

 private:
  std::uint32_t last_year_asked_ = 0xFFFFFFFFU;
  std::uint32_t renewed_total_ = 0;
  /// Stand-summers entered, by the summer of the life; the last slot holds
  /// the fifth and every later one — the summers a stand should not reach.
  std::array<std::uint32_t, 5> summers_{};
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_STAND_RENEWAL_POLICY_H_
