// The checks of the salt loan, the lamp and the letter (salt_loan_checks.h; #1630 (C)).
//
// THE KNOWN ANSWERS from the shipped tables (salt_need_on 1, salt_lot 10 points / 1 000 kg,
// goods_loan_markup 0.2, norm 125 g): the loan costs 12 points; 10 eaters with 6 kg of salt hold
// 4.8 days of table - the lamp lit (under 8) and red (N <= 4 is false at 4.8: floor 4 + 4 = 8 <= 8,
// red); the letter goes once a year when the points (5) do not buy the lot; the loan once a year,
// its 12 points off the next grant.

#include "salt_loan_checks.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/event_state.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "production_config.h"
#include "salt_loan.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: salt loan: " << label << '\n';
    return 1;
  }
  return 0;
}

/// Ten residents in one family, one church store (salt's house in
/// resource_stores.csv) holding `salt_g` of salt.
core::WorldState Village(const core::ProductionConfig& config,
                         core::UnitTypeId church,
                         core::Grams salt_g) {
  core::WorldState world;
  world.calendar.day = 60;
  core::FamilyRow family;
  core::AppendRow(world.families, family);
  for (int index = 0; index < 10; ++index) {
    core::ResidentRow person;
    person.family = world.families.row_ids[0];
    core::AppendRow(world.residents, person);
  }
  core::UnitRow store;
  store.type = church;
  store.level = 1;
  store.stock.assign(config.salt_resource.value + 1U, 0);
  store.stock[config.salt_resource.value] = salt_g;
  core::AppendRow(world.units, store);
  return world;
}

}  // namespace

int CheckTheSaltLoan() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  failures += Expect(config.farming.salt_need_on && core::SaltLoanPoints(config) == 12,
                     "the shipped tables turn the need on; the loan costs 10 x 1.2 = 12 points");

  // THE LAMP: 6 kg for 10 eaters = 4.8 days - lit, days to «пресно» 8 (red).
  const core::ITable* const types = tables->FindTable("unit_types");
  const core::UnitTypeId church{static_cast<std::uint16_t>(
      types != nullptr ? types->FindRowByKey("church_store") : core::kNoTableRow)};
  core::WorldState low = Village(config, church, 6'000);
  low.limit.points = 5;
  std::vector<core::Alarm> alarms;
  core::CollectSaltAlarms(config, low, alarms);
  bool lamp = false;
  bool advice = false;
  for (const core::Alarm& alarm : alarms) {
    lamp = lamp || (alarm.kind == core::AlarmKind::kSaltTableShort && alarm.days_to_loss == 8);
    advice = advice || alarm.kind == core::AlarmKind::kSaltLoanAdvice;
  }
  failures += Expect(lamp && advice,
                     "4.8 days of table: the lamp at 8 days to «пресно», the elder's loan line");
  // A full store: no lamp.
  core::WorldState full = Village(config, church, 100'000);
  std::vector<core::Alarm> none;
  core::CollectSaltAlarms(config, full, none);
  failures += Expect(none.empty(), "80 days of table: no lamp");

  // THE LETTER: once a year, the points short.
  core::RunSaltLetterDay(config, low);
  std::size_t letters = 0;
  for (const core::SimEvent& event : low.step_events) {
    letters += event.kind == core::EventKind::kSaltLoanOffered ? 1U : 0U;
  }
  core::RunSaltLetterDay(config, low);
  std::size_t again = 0;
  for (const core::SimEvent& event : low.step_events) {
    again += event.kind == core::EventKind::kSaltLoanOffered ? 1U : 0U;
  }
  failures += Expect(letters == 1 && again == 1 && low.salt.letter_year == 2,
                     "the letter goes once a year while the points do not buy the lot");

  // THE LOAN: a cart with the lot, 12 points owed, once a year.
  core::OrderRow loan;
  loan.kind = core::OrderKind::kTakeSaltLoan;
  const std::size_t carts = low.limit_deliveries.rows.size();
  const core::OrderRefusal first = core::TakeSaltLoan(config, low, loan);
  const core::OrderRefusal second = core::TakeSaltLoan(config, low, loan);
  failures +=
      Expect(first == core::OrderRefusal::kNone && low.limit_deliveries.rows.size() == carts + 1U &&
                 low.salt.loan_points_owed == 12 && low.ledger.current.salt_loans == 1 &&
                 second == core::OrderRefusal::kRuleForbids,
             "the loan brings the lot on the district's cart, owes 12, once a year");
  // THE TURN: the 12 come off the new grant.
  low.limit.points = 300;
  core::TakeSaltLoanPointsAtTurn(low);
  failures += Expect(low.limit.points == 288 && low.salt.loan_points_owed == 0,
                     "the turn takes the loan's 12 points off the grant (300 -> 288)");
  return failures;
}
