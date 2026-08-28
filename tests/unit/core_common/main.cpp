// Unit test of core_common: calendar arithmetic, StateTable operations, RNG.
// Plain executable, exit code = number of failed expectations (framework not
// chosen yet — tests/CMakeLists.txt).

#include <cstdint>
#include <iostream>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/random.h"
#include "core_common/state_table.h"
#include "core_common/state_table_ops.h"

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

int TestCalendar() {
  int failures = 0;

  // Day 0 = year 1, January, first day, winter.
  const core::Date day_zero = core::DateFromDay(0);
  failures += Expect(day_zero.year == 1, "day 0 is year 1");
  failures += Expect(day_zero.month == core::Month::kJanuary, "day 0 is January");
  failures += Expect(day_zero.day_in_month == 0, "day 0 is the first day of the month");

  // 4-day months: day 4 already flips to February; day 47 ends the year.
  failures += Expect(core::DateFromDay(4).month == core::Month::kFebruary, "day 4 is February");
  const core::Date last_day = core::DateFromDay(core::kDaysPerYear - 1);
  failures += Expect(last_day.month == core::Month::kDecember, "day 47 is December");
  failures += Expect(last_day.day_in_month == 3, "day 47 is the month's last day");
  failures += Expect(core::DateFromDay(core::kDaysPerYear).year == 2, "day 48 is year 2");

  // The week is independent of months: pure modulo 7.
  failures += Expect(core::WeekdayFromDay(0, core::Weekday::kMonday) == core::Weekday::kMonday,
                     "day 0 keeps the campaign weekday");
  failures += Expect(core::WeekdayFromDay(7, core::Weekday::kMonday) == core::Weekday::kMonday,
                     "a week later is the same weekday");
  failures += Expect(core::WeekdayFromDay(48, core::Weekday::kMonday) == core::Weekday::kSunday,
                     "year length 48 shifts the weekday by 48 mod 7 = 6");

  // Seasons by threes, December-February is winter.
  failures += Expect(core::SeasonOfMonth(core::Month::kFebruary) == core::Season::kWinter,
                     "February is winter");
  failures +=
      Expect(core::SeasonOfMonth(core::Month::kMarch) == core::Season::kSpring, "March is spring");
  failures += Expect(core::SeasonOfMonth(core::Month::kAugust) == core::Season::kSummer,
                     "August is summer");
  failures += Expect(core::SeasonOfMonth(core::Month::kNovember) == core::Season::kAutumn,
                     "November is autumn");
  failures += Expect(core::SeasonOfMonth(core::Month::kDecember) == core::Season::kWinter,
                     "December is winter");

  // Ticks: 24 game hours per day.
  failures += Expect(core::SimDayFromTick(23) == 0, "tick 23 is still day 0");
  failures += Expect(core::SimDayFromTick(24) == 1, "tick 24 opens day 1");
  failures += Expect(core::HourFromTick(24) == 0, "tick 24 is hour 0");
  failures += Expect(core::HourFromTick(47) == 23, "tick 47 is hour 23");

  // The refresh helper fills every cache from the tick.
  core::CalendarState calendar;
  calendar.tick = static_cast<core::Tick>(core::kTicksPerYear) + 5 * core::kTicksPerDay + 7;
  calendar.day_zero_weekday = core::Weekday::kThursday;
  core::RefreshCalendarCaches(calendar);
  failures += Expect(calendar.day == core::kDaysPerYear + 5, "refresh derives the day");
  failures += Expect(calendar.date.year == 2, "refresh derives the year");
  failures += Expect(calendar.date.month == core::Month::kFebruary, "refresh derives the month");
  failures += Expect(calendar.season == core::Season::kWinter, "refresh derives the season");
  failures +=
      Expect(calendar.weekday == core::WeekdayFromDay(calendar.day, core::Weekday::kThursday),
             "refresh derives the weekday");
  return failures;
}

struct TestRow {
  std::int32_t payload = 0;
};

int TestStateTable() {
  int failures = 0;
  core::StateTable<core::ResidentId, TestRow> table;

  // Ids are issued from 1 and rows are appended densely.
  const core::ResidentId first = core::AppendRow(table, TestRow{.payload = 10});
  const core::ResidentId second = core::AppendRow(table, TestRow{.payload = 20});
  const core::ResidentId third = core::AppendRow(table, TestRow{.payload = 30});
  failures += Expect(first.value == 1, "the first issued id is 1");
  failures += Expect(third.value == 3, "ids grow by one");
  failures += Expect(table.rows.size() == 3, "three rows after three appends");
  failures += Expect(core::FindRow(table, second) == 1, "the second row sits at index 1");

  // The invalid id and never-issued ids read as "gone".
  failures +=
      Expect(core::FindRow(table, core::ResidentId{}) == core::kNoRow, "the invalid id has no row");
  failures += Expect(core::FindRow(table, core::ResidentId{99}) == core::kNoRow,
                     "an unissued id has no row");

  // Removal swaps the last row in; ids stay valid, indices do not.
  failures += Expect(core::RemoveRow(table, first), "removing a live entity succeeds");
  failures += Expect(!core::RemoveRow(table, first), "removing it again fails");
  failures += Expect(core::FindRow(table, first) == core::kNoRow, "the removed id is gone");
  const std::uint32_t moved_row = core::FindRow(table, third);
  failures += Expect(moved_row == 0, "the last row moved into the vacated slot");
  failures += Expect(table.rows[moved_row].payload == 30, "the moved row kept its data");
  failures += Expect(table.row_ids[moved_row].value == third.value, "row_ids moved in step");

  // Ids are never reused, even after removals.
  const core::ResidentId fourth = core::AppendRow(table, TestRow{.payload = 40});
  failures += Expect(fourth.value == 4, "a freed id value is not reissued");

  // A save stores rows + row_ids + next_id_value; only the lookup is rebuilt.
  core::StateTable<core::ResidentId, TestRow> loaded;
  loaded.rows = table.rows;
  loaded.row_ids = table.row_ids;
  loaded.next_id_value = table.next_id_value;
  core::RebuildLookup(loaded);
  failures += Expect(loaded.next_id_value == 5, "rebuild keeps the stored next_id_value");
  failures += Expect(core::FindRow(loaded, third) == core::FindRow(table, third),
                     "rebuild recovers the lookup");
  failures += Expect(core::FindRow(loaded, first) == core::kNoRow, "rebuild keeps dead ids dead");

  // MEM-001 regression: the entity with the highest id dies before the save.
  // The saved counter must survive the rebuild — recomputing it from the
  // maximum live id would re-issue the dead id to the next newborn.
  failures += Expect(core::RemoveRow(loaded, fourth), "the highest id dies before the save");
  core::StateTable<core::ResidentId, TestRow> reloaded;
  reloaded.rows = loaded.rows;
  reloaded.row_ids = loaded.row_ids;
  reloaded.next_id_value = loaded.next_id_value;
  core::RebuildLookup(reloaded);
  const core::ResidentId newborn = core::AppendRow(reloaded, TestRow{.payload = 50});
  failures += Expect(newborn.value == 5, "a dead id is not re-issued after load");
  return failures;
}

int TestRandom() {
  int failures = 0;

  // Same seed and stream — the same sequence, forever (save-format contract).
  core::RngState rng_a = core::SeedRngState(42, 54);
  core::RngState rng_b = core::SeedRngState(42, 54);
  bool sequences_match = true;
  for (int i = 0; i < 1000; ++i) {
    sequences_match =
        sequences_match && (core::NextRandomBits(rng_a) == core::NextRandomBits(rng_b));
  }
  failures += Expect(sequences_match, "equal seeds give equal sequences");

  // Different streams on the same seed diverge.
  core::RngState stream_one = core::SeedRngState(42, 1);
  core::RngState stream_two = core::SeedRngState(42, 2);
  int equal_draws = 0;
  for (int i = 0; i < 64; ++i) {
    if (core::NextRandomBits(stream_one) == core::NextRandomBits(stream_two)) {
      ++equal_draws;
    }
  }
  failures += Expect(equal_draws < 4, "different streams diverge");

  // Bounds hold and every residue is reachable.
  core::RngState rng = core::SeedRngState(7, 0);
  bool in_bounds = true;
  std::uint32_t bucket_mask = 0;
  for (int i = 0; i < 1000; ++i) {
    const std::uint32_t draw = core::NextRandomBelow(rng, 10);
    in_bounds = in_bounds && draw < 10;
    bucket_mask |= 1U << draw;
  }
  failures += Expect(in_bounds, "NextRandomBelow stays below the bound");
  failures += Expect(bucket_mask == 0x3FFU, "all ten residues appear in 1000 draws");

  bool floats_in_range = true;
  for (int i = 0; i < 1000; ++i) {
    const float draw = core::NextRandomUnitFloat(rng);
    floats_in_range = floats_in_range && draw >= 0.0F && draw < 1.0F;
  }
  failures += Expect(floats_in_range, "unit floats stay in [0, 1)");

  // Counter hash: pure, and every argument matters.
  const std::uint64_t base = core::CounterHashBits(1, 2, 3, 4);
  failures += Expect(base == core::CounterHashBits(1, 2, 3, 4), "counter hash is pure");
  failures += Expect(base != core::CounterHashBits(9, 2, 3, 4), "seed changes the hash");
  failures += Expect(base != core::CounterHashBits(1, 9, 3, 4), "tick changes the hash");
  failures += Expect(base != core::CounterHashBits(1, 2, 9, 4), "entity changes the hash");
  failures += Expect(base != core::CounterHashBits(1, 2, 3, 9), "salt changes the hash");
  const float hash_float = core::CounterHashUnitFloat(1, 2, 3, 4);
  failures += Expect(hash_float >= 0.0F && hash_float < 1.0F, "hash unit float in [0, 1)");

  // Pinned first draws of PCG32(42, 54): any change here is an algorithm
  // change, i.e. a VERSION_SAVE event (random.h). Values must match on Clang
  // and MSVC bit for bit.
  core::RngState pinned = core::SeedRngState(42, 54);
  const std::uint32_t draw0 = core::NextRandomBits(pinned);
  const std::uint32_t draw1 = core::NextRandomBits(pinned);
  std::cout << "PCG32(42, 54) first draws: " << draw0 << ' ' << draw1 << '\n';
  failures += Expect(draw0 == 0xA15C02B7U, "pinned draw 0 of PCG32(42, 54)");
  failures += Expect(draw1 == 0x7B47F409U, "pinned draw 1 of PCG32(42, 54)");
  return failures;
}

}  // namespace

int main() {
  int failures = 0;
  failures += TestCalendar();
  failures += TestStateTable();
  failures += TestRandom();
  if (failures == 0) {
    std::cout << "unit_core_common: all checks passed\n";
  }
  return failures;
}
