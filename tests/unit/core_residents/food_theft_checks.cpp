// The checks of #1568 link 2 (food_theft_checks.h).

#include "food_theft_checks.h"

#include <array>
#include <cstdint>
#include <iostream>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/order_state.h"
#include "core_common/post_shift.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "food_theft.h"
#include "night_trade.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr core::Grams kTonne = 1000 * core::kGramsPerKilogram;
constexpr core::ResourceId kRye{0};

/// A store of 50 t of rye at the origin, a house 100 m off it, and in the
/// house a man of 30 at `alcoholism`. With `watched`, a sober night watchman
/// is posted at the store (profession 0, the night's shift).
core::WorldState Village(float alcoholism, bool watched) {
  core::WorldState world;
  world.world_seed = 1931;
  world.audit.books_accepted = 1;  // after the act; the act's own check sets 0
  core::UnitRow store;
  store.level = 1;
  store.stock.assign(1, 50 * kTonne);
  const core::UnitId store_id = core::AppendRow(world.units, store);
  core::UnitRow house;
  house.level = 1;
  house.position = core::Vec2{.x = 100.0F, .y = 0.0F};
  const core::UnitId house_id = core::AppendRow(world.units, house);
  core::FamilyRow family;
  family.house = house_id;
  const core::FamilyId family_id = core::AppendRow(world.families, family);
  core::ResidentRow man;
  man.sex = core::Sex::kMale;
  man.alcoholism = alcoholism;
  man.family = family_id;
  man.birth_day = -static_cast<std::int32_t>(30 * core::kDaysPerYear / 4);
  core::AppendRow(world.residents, man);
  if (watched) {
    core::ResidentRow watchman;
    watchman.sex = core::Sex::kMale;
    watchman.family = family_id;
    watchman.post.unit = store_id;
    watchman.post.profession = core::ProfessionId{0};
    core::AppendRow(world.residents, watchman);
  }
  return world;
}

core::NightTradeConfig Night() {
  core::NightTradeConfig night;
  night.post_shift = {core::PostShift::kNight};
  return night;
}

core::FoodTheftConfig On() {
  core::FoodTheftConfig config;
  config.on = true;
  config.food = {kRye};
  return config;
}

/// The moonlit night's hour out of the month's first month.
void ToTheftHour(const core::NightTradeConfig& night, core::WorldState& world) {
  world.calendar.tick =
      (static_cast<core::Tick>(night.moon_day_in_month) * core::kTicksPerDay) + night.hour_out;
  core::RefreshCalendarCaches(world.calendar);
}

std::uint32_t Count(const core::WorldState& world, core::EventKind kind) {
  std::uint32_t count = 0;
  for (const core::SimEvent& event : world.step_events) {
    count += event.kind == kind ? 1U : 0U;
  }
  return count;
}

}  // namespace

int CheckFoodTheft() {
  int failures = 0;
  const core::NightTradeConfig night = Night();
  const core::FoodTheftConfig on = On();
  constexpr float kSpeedup = 4.0F;
  {
    core::WorldState open = Village(45.0F, false);
    ToTheftHour(night, open);
    core::RunFoodTheft(on, night, kSpeedup, open);
    const core::Grams taken = 50 * kTonne - open.units.rows[0].stock[0];
    std::cout << "  food theft: open store lost " << taken << " g, book gap "
              << core::AmountOf(open.units.rows[0].book_gap, kRye) << " g\n";
    failures += Expect(taken == 25 * core::kGramsPerKilogram,
                       "food theft: a drinker of 45 takes his 25 kg from an open store");
    failures += Expect(core::AmountOf(open.units.rows[0].book_gap, kRye) == taken,
                       "food theft: and the book keeps it - recorded = real + gap");
    failures += Expect(Count(open, core::EventKind::kFoodStolen) == 1,
                       "food theft: one kFoodStolen for it");
    failures += Expect(core::AmountOf(open.ledger.current.stolen, kRye) == taken,
                       "food theft: the year's `stolen` carries it");
  }
  {
    core::WorldState guarded = Village(45.0F, true);
    ToTheftHour(night, guarded);
    core::RunFoodTheft(on, night, kSpeedup, guarded);
    failures += Expect(guarded.units.rows[0].stock[0] == 50 * kTonne,
                       "food theft: a sober night watchman closes the store - nothing taken");
  }
  {
    core::WorldState sober = Village(39.0F, false);
    ToTheftHour(night, sober);
    core::RunFoodTheft(on, night, kSpeedup, sober);
    failures += Expect(sober.units.rows[0].stock[0] == 50 * kTonne,
                       "food theft: a man under 40 steals nothing");
  }
  {
    core::FoodTheftConfig off = On();
    off.on = false;
    core::WorldState canon = Village(60.0F, false);
    ToTheftHour(night, canon);
    core::RunFoodTheft(off, night, kSpeedup, canon);
    failures +=
        Expect(canon.units.rows[0].stock[0] == 50 * kTonne && canon.units.rows[0].book_gap.empty(),
               "food theft: with crime_food_theft_on 0 nothing moves - the canon");
  }
  // THE ACT OF ACCEPTANCE (#1568 (a)): before it the theft is «до приёмки»,
  // not in the book; on its day every book is set to the real stock.
  {
    core::WorldState before = Village(45.0F, false);
    before.audit.books_accepted = 0;
    ToTheftHour(night, before);
    core::RunFoodTheft(on, night, kSpeedup, before);
    failures += Expect(before.units.rows[0].book_gap.empty() &&
                           core::AmountOf(before.ledger.current.stolen_before_acceptance, kRye) ==
                               25 * core::kGramsPerKilogram,
                       "act: a theft before the act is the former management's, not the book's");
    std::uint32_t acts = 0;
    for (std::uint32_t day = 0; day < core::kDaysPerYear; ++day) {
      before.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay;
      core::RefreshCalendarCaches(before.calendar);
      before.step_events.clear();
      core::RunStoreAudit(on, before);
      acts += Count(before, core::EventKind::kBooksAccepted);
    }
    std::cout << "  act: signed " << acts << " time(s) in y1; audits after it "
              << before.ledger.current.audits << " (the half's day may fall before it)\n";
    failures +=
        Expect(acts == 1 && before.audit.books_accepted == 1 && before.ledger.current.audits <= 1,
               "act: signed once, on its month of y1 - and no audit before it");
  }
  // THE RECEIPTS (#1568 (b)): a rise over what the book last saw, and the first
  // look is no receipt. The first form compared with the previous step's state,
  // which in the residents' slot is the same, and read every receipt as nought.
  {
    core::WorldState seen = Village(0.0F, false);
    core::RunBookReceipts(on, seen);  // the first look: 50 t seen, none received
    seen.units.rows[0].stock[0] += 5 * kTonne;
    core::RunBookReceipts(on, seen);
    core::Grams received = 0;
    for (const core::BookMonth& month : seen.units.rows[0].book_window) {
      received += core::AmountOf(month.received, kRye);
    }
    std::cout << "  receipts: " << received << " g received after a rise of 5 t\n";
    failures += Expect(received == 5 * kTonne,
                       "receipts: a rise of 5 t over the book's last look is 5 t received");
  }
  // THE AUDIT PER POSITION, over the window (#1568 (b)): 1 t lost of 10 t
  // received in y2's first month - 10 %. The year's first audit gives the
  // remark, never the trigger; the second, the trigger.
  core::WorldState audited = Village(0.0F, false);
  audited.audit.books_accepted = 1;
  audited.units.rows[0].stock.assign(1, 40 * kTonne);
  audited.units.rows[0].book_gap.assign(1, 1 * kTonne);
  audited.units.rows[0].book_window.assign(on.window_months, core::BookMonth{});
  constexpr std::uint32_t kY2FirstMonth = core::kDaysPerYear / core::kDaysPerMonth;
  audited.units.rows[0].book_window[kY2FirstMonth % on.window_months].lost.assign(1, kTonne);
  audited.units.rows[0].book_window[kY2FirstMonth % on.window_months].received.assign(1,
                                                                                      10 * kTonne);
  audited.units.rows[0].book_window_month = kY2FirstMonth + 1U;
  std::uint32_t remarks = 0;
  std::uint32_t triggers = 0;
  for (std::uint32_t day = core::kDaysPerYear; day < 2U * core::kDaysPerYear; ++day) {
    audited.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay;
    core::RefreshCalendarCaches(audited.calendar);
    audited.step_events.clear();
    core::RunStoreAudit(on, audited);
    remarks += Count(audited, core::EventKind::kAuditRemark);
    triggers += Count(audited, core::EventKind::kAuditTrialDue);
  }
  std::cout << "  audit: a year at 10 % of receipts lost - " << audited.ledger.current.audits
            << " audits, " << remarks << " remark, " << triggers << " trigger\n";
  failures += Expect(audited.ledger.current.audits == 2, "audit: two a year");
  failures += Expect(remarks == 1 && triggers == 1,
                     "audit: the first gives the remark, the second the «Под суд» condition");
  failures += Expect(audited.audit.trial_due == 1, "audit: the condition stands");
  // ROUND 2 (econ's crime-separation-2, boss 7 Nov): the same world, a year of
  // y2's audits, under R2 and R3 - each with its pair.
  {
    const auto leaking = [&on]() {
      core::WorldState world = Village(0.0F, false);
      world.audit.books_accepted = 1;
      world.units.rows[0].type = core::UnitTypeId{0};  // R2 asks the type
      world.units.rows[0].stock.assign(1, 40 * kTonne);
      world.units.rows[0].book_gap.assign(1, 1 * kTonne);
      world.units.rows[0].book_window.assign(on.window_months, core::BookMonth{});
      world.units.rows[0].book_window[kY2FirstMonth % on.window_months].lost.assign(1, kTonne);
      world.units.rows[0].book_window[kY2FirstMonth % on.window_months].received.assign(
          1, 10 * kTonne);
      world.units.rows[0].book_window_month = kY2FirstMonth + 1U;
      return world;
    };

    struct Year {
      std::uint32_t remarks = 0;
      std::uint32_t triggers = 0;
      std::uint32_t leaks = 0;
    };

    const auto y2 = [](const core::FoodTheftConfig& config, core::WorldState world) {
      Year year;
      for (std::uint32_t day = core::kDaysPerYear; day < 2U * core::kDaysPerYear; ++day) {
        world.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay;
        core::RefreshCalendarCaches(world.calendar);
        world.step_events.clear();
        core::RunStoreAudit(config, world);
        year.remarks += Count(world, core::EventKind::kAuditRemark);
        year.triggers += Count(world, core::EventKind::kAuditTrialDue);
        year.leaks += Count(world, core::EventKind::kAuditUnguardedLeak);
      }
      return year;
    };
    // R3: the act signed on y2's first day, 12 months asked - both of y2's
    // audits give the remark, neither the trigger.
    core::FoodTheftConfig r3 = On();
    r3.trial_after_act_months = 12;
    core::WorldState late_act = leaking();
    late_act.audit.accepted_day = core::kDaysPerYear;
    const Year waited = y2(r3, late_act);
    core::WorldState early_act = leaking();
    early_act.audit.accepted_day = 0;
    const Year in_time = y2(r3, early_act);
    std::cout << "  round 2, R3: act on y2's first day - " << waited.remarks << " remarks, "
              << waited.triggers << " triggers; act on y1's - " << in_time.triggers << '\n';
    failures += Expect(waited.remarks == 2 && waited.triggers == 0,
                       "R3: within 12 months of the act no trigger - the remark again");
    failures += Expect(in_time.triggers == 1,
                       "R3's pair: 12 months past the act, a remark before - the trigger");
    // R2: the store's type has no night post - reported, never audited.
    core::FoodTheftConfig r2 = On();
    r2.guarded_stores_only = true;
    r2.guardable_type = {0};
    const Year unguarded = y2(r2, leaking());
    r2.guardable_type = {1};
    const Year guarded = y2(r2, leaking());
    std::cout << "  round 2, R2: unguardable store - " << unguarded.remarks << " remarks, "
              << unguarded.leaks << " leak reports; guardable - " << guarded.remarks << " remark, "
              << guarded.triggers << " trigger\n";
    failures += Expect(unguarded.remarks == 0 && unguarded.triggers == 0 && unguarded.leaks == 2,
                       "R2: a store no watchman can be posted at - no remark, no trigger, its "
                       "leak reported at each audit");
    failures += Expect(guarded.remarks == 1 && guarded.triggers == 1 && guarded.leaks == 0,
                       "R2's pair: a guardable store - audited as before");
    // R1: the act of year 2 - none in y1, one in y2.
    core::FoodTheftConfig r1 = On();
    r1.book_opens_year = 1;
    core::WorldState taking_over = Village(0.0F, false);
    taking_over.audit.books_accepted = 0;
    std::array<std::uint32_t, 2> acts = {0, 0};
    for (std::uint32_t day = 0; day < 2U * core::kDaysPerYear; ++day) {
      taking_over.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay;
      core::RefreshCalendarCaches(taking_over.calendar);
      taking_over.step_events.clear();
      core::RunStoreAudit(r1, taking_over);
      acts[day / core::kDaysPerYear] += Count(taking_over, core::EventKind::kBooksAccepted);
    }
    failures += Expect(acts[0] == 0 && acts[1] == 1 &&
                           taking_over.audit.accepted_day ==
                               core::kDaysPerYear + (r1.book_opens_month * core::kDaysPerMonth),
                       "R1: the act in y2 - none in y1, one on its month of y2, its day kept");
  }
  // THE WRITE-OFF clears the book and withdraws the remark.
  core::OrderRow order;
  order.kind = core::OrderKind::kWriteOffShortage;
  order.unit = audited.units.row_ids[0];
  core::AppendRow(audited.orders, order);
  const float before = audited.chairman.raikom_reputation;
  core::TakeWriteOffOrders(on, audited);
  failures += Expect(audited.units.rows[0].book_gap.empty() && audited.audit.remarked == 0,
                     "write-off: the book set to the real stock, the remark withdrawn");
  failures += Expect(core::AmountOf(audited.ledger.current.written_off, kRye) == 1 * kTonne,
                     "write-off: the 1 t booked as written off");
  failures +=
      Expect(audited.chairman.raikom_reputation < before, "write-off: the district is paid");
  return failures;
}
