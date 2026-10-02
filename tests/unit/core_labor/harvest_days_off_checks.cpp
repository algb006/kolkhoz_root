// The checks of the harvest rule 1 (harvest_days_off_checks.h).

#include "harvest_days_off_checks.h"

#include <cstdint>
#include <iostream>

#include "core_common/calendar.h"
#include "core_common/day_off.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "rush.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// Day zero is a Monday, so days 6, 13, 55 are Sundays; `today` is the
/// world's day. One field of the arable in its reaping with five days left,
/// and THE GATHERING COUNT'S WORD STANDING TODAY — said at today's close
/// (WorldState::gather_short_said is the day plus one): the rule's condition
/// since 0.37.147. A check that wants the count silent says so.
core::WorldState HarvestWorld(core::SimDay today) {
  core::WorldState world;
  world.calendar.day_zero_weekday = core::Weekday::kMonday;
  world.calendar.day = today;
  world.gather_short_said = today + 1;
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.phase = core::FieldPhase::kHarvest;
  field.work_days_remaining = 5.0F;
  core::AppendRow(world.fields, field);
  return world;
}

int TestTheSundayIsWorkedWhileTheHarvestStands() {
  int failures = 0;
  core::WorldState world = HarvestWorld(6);
  failures += Expect(core::IsRestDay(6, world.calendar.day_zero_weekday, world.epoch),
                     "days off: day 6 of a week beginning on Monday is the calendar's day off");
  failures += Expect(core::HarvestStands(world) && !core::IsDayOffIn(world, 6),
                     "days off: the Sunday is a working day while a field of the arable stands "
                     "in its reaping — the order is on at genesis");
  world.chairman.harvest_without_days_off = 0;
  failures += Expect(core::IsDayOffIn(world, 6),
                     "days off: with the order switched off the same Sunday is a day off");
  return failures;
}

/// THE COUNT'S WORD IS THE RULE'S CONDITION (0.37.147; boss, core-boss-potato-
/// crew-trace-2026-10-01 [27]; host-boss-pin-0-37-133-2026-10-02 [36]): a
/// ripe field alone lifts no Sunday — the gathering count must have said
/// «not in time with the days off kept», last night or at today's own close.
/// THIS CHECK TURNED WITH THE READER: from 0.37.91 to 0.37.145 it held that
/// the door answered the same whatever the count said, and said of itself
/// that it was the one that must turn.
int TestTheCountsWordIsTheCondition() {
  int failures = 0;
  core::WorldState silent = HarvestWorld(6);
  silent.gather_short_said = 0;  // never said
  core::WorldState last_night = HarvestWorld(6);
  last_night.gather_short_said = 6;  // the close of day 5
  core::WorldState this_evening = HarvestWorld(6);
  this_evening.gather_short_said = 7;  // the close of day 6 itself
  core::WorldState stale = HarvestWorld(6);
  stale.gather_short_said = 2;
  failures +=
      Expect(core::GatherShortStands(last_night) && !core::IsDayOffIn(last_night, 6) &&
                 core::GatherShortStands(this_evening) && !core::IsDayOffIn(this_evening, 6),
             "days off: the count said «not in time» last night, or at today's own "
             "close — the Sunday is worked while the harvest stands");
  failures += Expect(!core::GatherShortStands(silent) && core::IsDayOffIn(silent, 6) &&
                         !core::GatherShortStands(stale) && core::IsDayOffIn(stale, 6),
                     "days off: the count never spoke, or spoke four days ago — the Sunday is a "
                     "day off though a ripe field stands and the order is on");
  // The calendar's door does not ask the harvest at all.
  failures +=
      Expect(core::IsCalendarDayOffIn(last_night, 6) && !core::IsCalendarDayOffIn(last_night, 5),
             "days off: the calendar's door answers the calendar, whatever stands and whatever "
             "the count said");
  return failures;
}

int TestWhatIsNotTheHarvest() {
  int failures = 0;
  core::WorldState done = HarvestWorld(6);
  done.fields.rows[0].work_days_remaining = 0.0F;
  failures += Expect(!core::HarvestStands(done) && core::IsDayOffIn(done, 6),
                     "days off: a reaping with no work left is no harvest — the Sunday stands");
  core::WorldState growing = HarvestWorld(6);
  growing.fields.rows[0].phase = core::FieldPhase::kGrowing;
  failures += Expect(!core::HarvestStands(growing) && core::IsDayOffIn(growing, 6),
                     "days off: a growing field is no harvest — the Sunday stands");
  core::WorldState meadow = HarvestWorld(6);
  meadow.fields.rows[0].kind = core::LandKind::kMeadow;
  failures += Expect(!core::HarvestStands(meadow) && core::IsDayOffIn(meadow, 6),
                     "days off: the meadow's cut is not the harvest — the Sunday stands");
  return failures;
}

int TestAHolidayIsNeverWorked() {
  int failures = 0;
  core::WorldState world = HarvestWorld(0);
  core::SimDay holiday = 0;
  bool found = false;
  for (core::SimDay day = 0; day < core::kDaysPerYear && !found; ++day) {
    if (core::HolidayOn(day, world.calendar.day_zero_weekday, world.epoch) !=
        core::Holiday::kNone) {
      holiday = day;
      found = true;
    }
  }
  if (Expect(found, "days off: the year of Epoch I has a holiday to test by") != 0) {
    return 1;
  }
  world.calendar.day = holiday;
  world.gather_short_said = holiday + 1;
  failures += Expect(core::HarvestStands(world) && core::GatherShortStands(world) &&
                         core::IsDayOffIn(world, holiday),
                     "days off: a holiday is a day off whatever stands in the field and whatever "
                     "the count said");
  return failures;
}

int TestADayAheadIsJudgedByToday() {
  int failures = 0;
  // Saturday, day 5: tomorrow's Sunday is worked; next year's Sunday (day 55)
  // is the calendar's.
  const core::WorldState saturday = HarvestWorld(5);
  failures += Expect(!core::IsDayOffIn(saturday, 6),
                     "days off: the Sunday to come is a working day while the harvest stands "
                     "today");
  failures += Expect(core::IsRestDay(55, saturday.calendar.day_zero_weekday, saturday.epoch) &&
                         core::IsDayOffIn(saturday, 55),
                     "days off: a Sunday of another year is the calendar's");
  // Day 13, the next Sunday: the one that passed is the calendar's.
  const core::WorldState later = HarvestWorld(13);
  failures += Expect(core::IsDayOffIn(later, 6) && !core::IsDayOffIn(later, 13),
                     "days off: a Sunday that passed is the calendar's, today's is worked");
  return failures;
}

core::OrderRefusal Switch(core::WorldState& world, std::uint8_t enable) {
  core::OrderRow order;
  order.kind = core::OrderKind::kHarvestWithoutDaysOff;
  order.status = core::OrderStatus::kPending;
  order.enable = enable;
  const core::OrderId id = core::AppendRow(world.orders, order);
  core::ReadRushOrders(world);
  const core::OrderRow& read = world.orders.rows[core::FindRow(world.orders, id)];
  return read.status == core::OrderStatus::kDone ? core::OrderRefusal::kNone : read.refusal;
}

int TestTheChairmanSwitchesIt() {
  int failures = 0;
  core::WorldState world = HarvestWorld(6);
  failures += Expect(Switch(world, 1) == core::OrderRefusal::kRuleForbids &&
                         world.chairman.harvest_without_days_off == 1,
                     "days off: switching on what stands on is refused — it is on at genesis");
  failures += Expect(Switch(world, 0) == core::OrderRefusal::kNone &&
                         world.chairman.harvest_without_days_off == 0 && core::IsDayOffIn(world, 6),
                     "days off: the order switches it off, and the Sunday is a day off again");
  failures += Expect(Switch(world, 0) == core::OrderRefusal::kRuleForbids,
                     "days off: switching off what stands off is refused");
  failures +=
      Expect(Switch(world, 1) == core::OrderRefusal::kNone &&
                 world.chairman.harvest_without_days_off == 1 && !core::IsDayOffIn(world, 6),
             "days off: and the order switches it back on");
  return failures;
}

}  // namespace

int CheckHarvestDaysOff() {
  int failures = 0;
  failures += TestTheSundayIsWorkedWhileTheHarvestStands();
  failures += TestTheCountsWordIsTheCondition();
  failures += TestWhatIsNotTheHarvest();
  failures += TestAHolidayIsNeverWorked();
  failures += TestADayAheadIsJudgedByToday();
  failures += TestTheChairmanSwitchesIt();
  return failures;
}
