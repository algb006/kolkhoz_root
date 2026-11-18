// The store report's theft lines (#1881; core_common/store_theft.h and the
// pre-acceptance pair of night_trade_state.h). Called from main.cpp
// (TestTheStoreTheft).
//
// THE KNOWN ANSWERS, on a hand-made world in month 5 (day 20): store A's ring
// holds 3 kg of rye in month 4 and 1 kg in month 5; store B was last written
// in month 2 (its month 4 is untouched, so nothing); store C's window has
// rolled past month 4 by its length (nothing). The pair holds 2 kg booked in
// month 4: it reads as month 4's line from month 5, and as nothing from month
// 6, or after a roll to month 7.
#include <iostream>

#include "core_common/calendar.h"
#include "core_common/night_trade_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/store_theft.h"
#include "core_common/world_state.h"

namespace core_test {
namespace {

int ExpectTheft(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: store theft: " << label << '\n';
  return 1;
}

constexpr core::Grams kKg = 1000;

core::UnitRow StoreWithWindow(std::uint32_t last_month, std::size_t length) {
  core::UnitRow unit;
  unit.level = 1;
  unit.book_window.assign(length, core::BookMonth{});
  unit.book_window_month = last_month + 1U;
  return unit;
}

}  // namespace

int TestTheStoreTheft() {
  int failures = 0;
  core::WorldState world;
  world.calendar.day = 5U * core::kDaysPerMonth;  // month 5
  core::UnitRow a = StoreWithWindow(5, 24);
  a.book_window[4].lost = {3 * kKg};
  a.book_window[5].lost = {1 * kKg};
  core::UnitRow b = StoreWithWindow(2, 24);
  b.book_window[2].lost = {7 * kKg};
  core::UnitRow c = StoreWithWindow(5, 1);  // a one-month ring: month 4 is gone
  c.book_window[0].lost = {9 * kKg};
  core::AppendRow(world.units, a);
  core::AppendRow(world.units, b);
  core::AppendRow(world.units, c);
  core::RollBeforeAcceptance(world.audit, 4);
  world.audit.before_acceptance_this_month = {2 * kKg};

  const core::StoreTheftView view = core::StoreTheftOf(world);
  failures += ExpectTheft(view.measured && view.closed && view.month == 4,
                          "month 5 reports the closed month 4");
  failures += ExpectTheft(view.rows.size() == 1 && view.rows[0].unit == world.units.row_ids[0] &&
                              view.rows[0].resource.value == 0 && view.rows[0].grams == 3 * kKg,
                          "one row: store A's 3 kg of month 4 - not its month 5, not B's month 2, "
                          "not C's rolled-off slot");
  failures += ExpectTheft(
      view.before_acceptance_grams.size() == 1 && view.before_acceptance_grams[0] == 2 * kKg,
      "the pre-acceptance 2 kg booked in month 4 is month 4's line");

  world.calendar.day = 6U * core::kDaysPerMonth;  // month 6, no theft booked in 5
  failures += ExpectTheft(core::StoreTheftOf(world).before_acceptance_grams.empty(),
                          "from month 6 the pair's month 4 is not the closed month's line");
  core::RollBeforeAcceptance(world.audit, 5);
  failures += ExpectTheft(world.audit.before_acceptance_last_month.size() == 1 &&
                              world.audit.before_acceptance_this_month.empty(),
                          "a roll by one month moves this month's grams to last month's");
  core::RollBeforeAcceptance(world.audit, 7);
  failures += ExpectTheft(
      world.audit.before_acceptance_last_month.empty() && world.audit.before_acceptance_month == 8,
      "a roll by two months clears both");

  world.calendar.day = 2;  // the campaign's first month
  const core::StoreTheftView first = core::StoreTheftOf(world);
  failures += ExpectTheft(first.measured && !first.closed && first.rows.empty(),
                          "the first month has no closed month to report");
  if (failures == 0) {
    std::cout << "store theft: the closed month read off three rings and the pair\n";
  }
  return failures;
}

}  // namespace core_test
