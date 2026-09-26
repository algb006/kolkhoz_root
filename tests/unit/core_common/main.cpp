// Unit test of core_common: calendar arithmetic, StateTable operations, RNG.
// Plain executable, exit code = number of failed expectations (framework not
// chosen yet — tests/CMakeLists.txt).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

#include "core_common/alarm_state.h"
#include "core_common/body.h"
#include "core_common/calendar.h"
#include "core_common/day_window.h"
#include "core_common/daylight.h"
#include "core_common/deadline.h"
#include "core_common/district_visit_state.h"
#include "core_common/fund_ladder.h"
#include "core_common/herd_age_band.h"
#include "core_common/herd_state.h"
#include "core_common/ids.h"
#include "core_common/map_obstacles.h"
#include "core_common/obstacle_raster.h"
#include "core_common/plot.h"
#include "core_common/post_shift.h"
#include "core_common/quantities.h"
#include "core_common/rain_stops_work.h"
#include "core_common/random.h"
#include "core_common/resident_activity.h"
#include "core_common/road_graph.h"
#include "core_common/road_pieces.h"
#include "core_common/road_route.h"
#include "core_common/road_rules.h"
#include "core_common/road_trace.h"
#include "core_common/road_view.h"
#include "core_common/state_table.h"
#include "core_common/state_table_ops.h"
#include "core_common/version_pin.h"
#include "core_common/world_state.h"

namespace core_test {
/// Defined in version_linkage_probe.cpp — a second translation unit, so that
/// the linkage of the version constants is measurable from inside one build.
const char* const* VersionStringAddressFromOtherTu();
}  // namespace core_test

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

/// The one conversion from a computed float to a stored mass (quantities.h;
/// the named cast pass of task A7a). It exists because the cast it replaces
/// is UNDEFINED for nan, for an infinity and for anything whose truncation
/// does not fit — and the floats that reach it come off hand-editable tables
/// and off save files a reader bit_casts without inspecting.
/// THE SOLAR DOOR (boss, thread boss-core-sunrise-for-month-2026-09-23): the
/// presentation asks when the sun rises in a given month WITHOUT running the
/// simulation to that day. What is checked here is the door's own shape; that
/// its answer equals the one the simulation writes is checked over all 48
/// days in tests/unit/core_time, where the weather of a day is built.
/// Rain stops the sowing and the reaping and nothing else, and the two
/// conversions between dry days and calendar days are each other's inverse.
int TestRainStopsWork() {
  int failures = 0;
  using core::Precipitation;
  using core::WorkKind;
  failures += Expect(core::RainStopsWork(Precipitation::kRain, WorkKind::kSowing) &&
                         core::RainStopsWork(Precipitation::kRain, WorkKind::kHarvest),
                     "rain: stops the sowing and the reaping");
  failures += Expect(!core::RainStopsWork(Precipitation::kRain, WorkKind::kPlowing) &&
                         !core::RainStopsWork(Precipitation::kRain, WorkKind::kHarrowing) &&
                         !core::RainStopsWork(Precipitation::kRain, WorkKind::kHauling) &&
                         !core::RainStopsWork(Precipitation::kRain, WorkKind::kHerdCare),
                     "rain: and not the plough, the harrow, the cart or the barn");
  failures += Expect(!core::RainStopsWork(Precipitation::kSnow, WorkKind::kHarvest) &&
                         !core::RainStopsWork(Precipitation::kNone, WorkKind::kHarvest),
                     "rain: snow and a dry day stop nothing here (the snow has its own rule)");

  core::RainDayShares dry{};
  failures += Expect(std::fabs(core::DryDaysBetween(dry, 3.5, 7.25) - 3.75) < 1e-9,
                     "dry days: with no rain a span is its own length, parts of days included");
  failures += Expect(
      core::DryDaysBetween(dry, 7.0, 7.0) == 0.0 && core::DryDaysBetween(dry, 8.0, 7.0) == 0.0,
      "dry days: an empty or backward span holds none");
  core::RainDayShares half{};
  half.fill(0.5F);
  failures += Expect(std::fabs(core::DryDaysBetween(half, 10.0, 14.0) - 2.0) < 1e-9,
                     "dry days: at half the days rained out, four days hold two");
  // THE YEAR WRAPS: day 47 always rains, and days 48 and 49 are days 0 and 1.
  core::RainDayShares last_wet{};
  last_wet[core::kDaysPerYear - 1] = 1.0F;
  failures += Expect(std::fabs(core::DryDaysBetween(last_wet, 46.0, 50.0) - 3.0) < 1e-9,
                     "dry days: the span wraps into next year's shares");

  failures += Expect(core::CalendarPointAfterDryDays(half, 10.0, 0.0) == 10.0,
                     "calendar point: no dry days owed, no time passes");
  failures += Expect(std::fabs(core::CalendarPointAfterDryDays(dry, 10.25, 3.0) - 13.25) < 1e-9,
                     "calendar point: with no rain a dry day is a calendar day");
  failures += Expect(std::fabs(core::CalendarPointAfterDryDays(half, 10.0, 2.0) - 14.0) < 1e-9,
                     "calendar point: at half rain two dry days take four");
  const double point = core::CalendarPointAfterDryDays(last_wet, 45.5, 2.0);
  failures += Expect(std::fabs(point - 48.5) < 1e-9 &&
                         std::fabs(core::DryDaysBetween(last_wet, 45.5, point) - 2.0) < 1e-9,
                     "calendar point: walks over the rained-out day and is the inverse");
  core::RainDayShares always_wet{};
  always_wet.fill(1.0F);
  failures += Expect(std::isinf(core::CalendarPointAfterDryDays(always_wet, 0.0, 1.0)) &&
                         std::isinf(core::CalendarPointAfterDryDays(
                             half, 0.0, std::numeric_limits<double>::infinity())),
                     "calendar point: never, when it always rains or nothing is ever done");
  // UB-001/002 OF THE 0.34.16 CYCLE: a tiny pace makes the walk long. A
  // million dry days at half rain is two million calendar days, reached by
  // skipping whole years and not a day at a time — and the answer must be
  // the one the day-by-day walk would give. Past any campaign it is never,
  // and an infinite or NaN start is never too; a span with such an end holds
  // no countable day.
  const double far = core::CalendarPointAfterDryDays(half, 10.0, 1.0e6);
  failures += Expect(std::fabs(far - (10.0 + 2.0e6)) < 1.0e-6 * 2.0e6,
                     "calendar point: a million dry days at half rain, two million days on");
  failures += Expect(std::fabs(core::DryDaysBetween(half, 10.0, far) - 1.0e6) < 1.0,
                     "calendar point: and the skip agrees with the span it skipped");
  failures += Expect(std::isinf(core::CalendarPointAfterDryDays(half, 0.0, 1.0e12)),
                     "calendar point: a point past any campaign is never");
  failures += Expect(std::isinf(core::CalendarPointAfterDryDays(
                         half, std::numeric_limits<double>::infinity(), 1.0)) &&
                         std::isinf(core::CalendarPointAfterDryDays(
                             half, std::numeric_limits<double>::quiet_NaN(), 1.0)),
                     "calendar point: from infinity or NaN, never");
  failures +=
      Expect(core::DryDaysBetween(half, std::numeric_limits<double>::infinity(), 5.0) == 0.0 &&
                 core::DryDaysBetween(half, 0.0, std::numeric_limits<double>::quiet_NaN()) == 0.0,
             "dry days: a span with an end at infinity or NaN holds none");
  return failures;
}

int TestDaylightCurve() {
  int failures = 0;

  failures += Expect(core::kDaylightGameHours.size() == core::kDaysPerYear,
                     "daylight: the curve has a value for every day of the year");

  // Every day of the year answers, and the window is a window: the sun rises
  // before it sets, both inside the day, and the two are symmetric about
  // noon — which is the whole of SolarWindow and the reason a caller can
  // trust either end alone.
  bool windows_sane = true;
  bool symmetric_about_noon = true;
  float shortest = core::kDaylightGameHours[0];
  float longest = core::kDaylightGameHours[0];
  for (std::uint32_t day = 0; day < core::kDaysPerYear; ++day) {
    const float hours = core::DaylightHoursOfDay(day);
    const core::DayWindow window = core::SolarWindowOfDay(day);
    windows_sane = windows_sane && hours > 0.0F && hours < 24.0F && window.sunrise > 0.0F &&
                   window.sunset < 24.0F && window.sunrise < window.sunset;
    symmetric_about_noon =
        symmetric_about_noon && std::fabs((window.sunrise + window.sunset) - 24.0F) < 1e-4F;
    shortest = std::min(shortest, hours);
    longest = std::max(longest, hours);
  }
  failures += Expect(windows_sane, "daylight: every day of the year has the sun up inside the day");
  failures +=
      Expect(symmetric_about_noon, "daylight: sunrise and sunset stand either side of noon");
  // The setting's own two ends (time design §3): about 7 hours in December,
  // about 17.5 at midsummer. A curve that lost its amplitude would still pass
  // every check above.
  failures += Expect(shortest > 6.9F && shortest < 7.1F, "daylight: the shortest day is ~7 hours");
  failures +=
      Expect(longest > 17.5F && longest < 17.6F, "daylight: the longest day is ~17.5 hours");

  // ANY day number answers, and that is what lets the presentation step a
  // month at a time past the year's end without guarding the boundary.
  failures += Expect(core::DaylightHoursOfDay(core::kDaysPerYear) == core::DaylightHoursOfDay(0) &&
                         core::DaylightHoursOfDay((core::kDaysPerYear * 7U) + 45U) ==
                             core::DaylightHoursOfDay(45U),
                     "daylight: the year is a cycle, so any day number lands on the curve");

  // The month form answers for the SECOND of four days, and says so by name.
  failures += Expect(core::SecondDayOfMonth(core::Month::kJanuary) == 1U &&
                         core::SecondDayOfMonth(core::Month::kDecember) == 45U,
                     "daylight: a month's second day is its day of the year");
  failures += Expect(core::DaylightHoursOfMonthSecondDay(core::Month::kDecember) ==
                             core::DaylightHoursOfDay(45U) &&
                         core::SolarWindowOfMonthSecondDay(core::Month::kDecember).sunrise ==
                             core::SolarWindowOfDay(45U).sunrise,
                     "daylight: the month form is the second day's answer and nothing else");

  // Every month answers, December included, AND no two months answer the
  // same: a month form that quietly returned one month's light for another
  // would pass every check above.
  bool all_months_differ = true;
  for (std::uint32_t first = 0; first < core::kMonthsPerYear; ++first) {
    const float light = core::DaylightHoursOfMonthSecondDay(static_cast<core::Month>(first));
    for (std::uint32_t second = first + 1; second < core::kMonthsPerYear; ++second) {
      all_months_differ = all_months_differ && light != core::DaylightHoursOfMonthSecondDay(
                                                            static_cast<core::Month>(second));
    }
  }
  failures += Expect(all_months_differ, "daylight: each of the twelve months has its own light");

  // THE SIGN OF THE ROUNDING, measured rather than promised. The middle of
  // four days lies between the second and the third; the door takes the
  // EARLIER. So in a month of growing light it answers SHORT of the month's
  // mean, and in a month of shrinking light it answers LONG of it.
  const auto mean_of_month = [](core::Month month) {
    float sum = 0.0F;
    for (std::uint32_t day = 0; day < core::kDaysPerMonth; ++day) {
      sum +=
          core::DaylightHoursOfDay((static_cast<std::uint32_t>(month) * core::kDaysPerMonth) + day);
    }
    return sum / static_cast<float>(core::kDaysPerMonth);
  };
  failures += Expect(core::DaylightHoursOfMonthSecondDay(core::Month::kMarch) <
                             mean_of_month(core::Month::kMarch) &&
                         core::DaylightHoursOfMonthSecondDay(core::Month::kSeptember) >
                             mean_of_month(core::Month::kSeptember),
                     "daylight: the month form takes the earlier of the two middle days");

  // The tolerance the header states out loud, checked instead of promised:
  // inside a month the light moves by at most ~1.2 game hours (September).
  float worst_gap = 0.0F;
  for (std::uint32_t month = 0; month < core::kMonthsPerYear; ++month) {
    const float picked = core::DaylightHoursOfMonthSecondDay(static_cast<core::Month>(month));
    for (std::uint32_t day = 0; day < core::kDaysPerMonth; ++day) {
      worst_gap = std::max(
          worst_gap,
          std::fabs(core::DaylightHoursOfDay((month * core::kDaysPerMonth) + day) - picked));
    }
  }
  std::cout << "daylight: a month's answer is off by at most " << worst_gap
            << " game hours inside that month\n";
  failures += Expect(worst_gap < 1.25F, "daylight: the month form's error stays under 1.25 hours");
  return failures;
}

int TestGramsFromFloat() {
  int failures = 0;
  failures += Expect(core::GramsFromKilograms(2.5F) == 2500, "two and a half kilos are 2500 g");
  failures += Expect(core::GramsFromTonnes(1.0F) == core::kGramsPerTonne, "a tonne is a million g");
  failures += Expect(core::GramsFromFloat(1500.7F) == 1500, "a fraction of a gram truncates");
  failures += Expect(core::GramsFromFloat(0.0F) == 0, "nothing is nothing");

  // The refusals — the whole reason the function exists. Each is a value
  // that would make the bare cast undefined.
  const float nan_value = std::numeric_limits<float>::quiet_NaN();
  const float infinity = std::numeric_limits<float>::infinity();
  failures += Expect(core::GramsFromFloat(nan_value) == 0, "nan is not a mass");
  failures += Expect(core::GramsFromKilograms(nan_value) == 0, "and not a mass in kilograms");
  failures += Expect(core::GramsFromFloat(infinity) == 0, "an infinity is not a mass");
  failures += Expect(core::GramsFromFloat(-infinity) == 0, "nor is a negative one");
  failures += Expect(core::GramsFromFloat(-1.0F) == 0, "a negative mass is refused, not stored");
  failures += Expect(core::GramsFromTonnes(1.0e30F) == 0,
                     "and so is a number too big to be a mass, before it overflows");

  // The boundary, because a test that only measures the middle measures
  // nothing: just under the ceiling still converts.
  failures += Expect(core::GramsFromFloat(8.0e15F) > 0, "a huge but usable mass still converts");
  return failures;
}

/// The version pin (version_pin.h). What CANNOT be tested here is the case
/// the pin exists for — headers of one publish against a library of another —
/// because a test links the pair it was built with, and they always agree.
/// What IS tested is the machinery that would notice: the library answers
/// with its own number rather than the caller's, and the comparison says no
/// when the numbers differ.
int TestVersionPin() {
  int failures = 0;
  // THE LINKAGE, FIRST, because everything below rests on it. The pin holds
  // the header's number beside the library's and looks for a difference; if
  // the constant has EXTERNAL linkage there is only one of it in the whole
  // program, and there is nothing left to differ. MSVC folds them and the
  // pin passes on a mismatched pair — measured on the host at /Od and at
  // /O2 /GL /LTCG alike. Two translation units, two addresses: that is what
  // internal linkage looks like from inside a single build, and it is the
  // only part of the mechanism a one-publish test can actually see.
  failures += Expect(&core::kCoreVersionString != core_test::VersionStringAddressFromOtherTu(),
                     "the version constant is per-translation-unit, not one per program");
  failures += Expect(core::CoreLibraryVersion() != nullptr, "the library states a version");
  failures += Expect(core::CoreVersionPinHolds(core::kCoreVersionString),
                     "a library and headers from one build are one publish");
  failures += Expect(!core::CoreVersionPinHolds("0.0.1"),
                     "and a caller compiled against another number is told so");
  failures += Expect(!core::CoreVersionPinHolds(nullptr),
                     "a caller that cannot say what it built against has no pin at all");
  // The number is a real one, not an empty string that would compare equal to
  // another empty string and pass this whole test for nothing.
  failures += Expect(core::CoreLibraryVersion()[0] != '\0', "and the version is not empty");
  return failures;
}

/// The plot rule (core_common/plot.h). It is checked here and not in
/// core_construction because it is no longer that module's: the wedding
/// stub in core_residents places a house through the same call, and a rule
/// with two callers and one home needs a test that belongs to neither.
int TestPlot() {
  int failures = 0;

  // Radii by type: type 0 has a 25-metre plot, type 1 none at all.
  const float radii[] = {25.0F, 0.0F};
  const core::PlotRules rules{.radius_by_type = radii, .map_side_m = 0.0F};
  core::UnitTable units;
  core::UnitRow standing;
  standing.type = core::UnitTypeId{0};  // the default id is INVALID, not row zero
  standing.position = core::Vec2{.x = 100.0F, .y = 100.0F};
  const core::UnitId first = core::AppendRow(units, standing);

  failures += Expect(
      core::PlotOverlaps(units, rules, core::Vec2{.x = 130.0F, .y = 100.0F}, 25.0F, core::UnitId{}),
      "two 25-metre plots 30 metres apart overlap");
  failures += Expect(!core::PlotOverlaps(
                         units, rules, core::Vec2{.x = 151.0F, .y = 100.0F}, 25.0F, core::UnitId{}),
                     "and 51 metres apart they do not");
  failures +=
      Expect(!core::PlotOverlaps(units, rules, core::Vec2{.x = 100.0F, .y = 100.0F}, 25.0F, first),
             "a unit does not overlap itself when it is the one being moved");

  // BOSS'S PORCH TEST (2026-09-04): two families given the SAME parental
  // house must not end up on the same plot. That is the whole defect —
  // SettleHouse took the parents' coordinates as the answer.
  const core::Vec2 parents{.x = 100.0F, .y = 100.0F};
  const core::Vec2 first_child = core::FreePlot(units, rules, parents, 25.0F);
  core::UnitRow child_house;
  child_house.type = core::UnitTypeId{0};
  child_house.position = first_child;
  core::AppendRow(units, child_house);
  const core::Vec2 second_child = core::FreePlot(units, rules, parents, 25.0F);
  failures += Expect(!(first_child.x == parents.x && first_child.y == parents.y),
                     "a house wanted on its parents' plot is moved off it");
  failures += Expect(!(second_child.x == first_child.x && second_child.y == first_child.y),
                     "and the second child does not land on the first");
  failures += Expect(!core::PlotOverlaps(units, rules, second_child, 25.0F, core::UnitId{}),
                     "and neither of them overlaps anything standing");

  // A free spot is TAKEN AS IT IS: the search must not push a house that
  // fits, or every village would drift outward one wedding at a time.
  const core::Vec2 empty{.x = 5000.0F, .y = 5000.0F};
  const core::Vec2 kept = core::FreePlot(units, rules, empty, 25.0F);
  failures +=
      Expect(kept.x == empty.x && kept.y == empty.y, "a spot that is already free is left alone");

  // A type with no plot at all takes no part, in either direction.
  failures += Expect(!core::PlotOverlaps(units, rules, parents, 0.0F, core::UnitId{}),
                     "a thing with no plot overlaps nothing");
  const core::Vec2 no_plot = core::FreePlot(units, rules, parents, 0.0F);
  failures += Expect(no_plot.x == parents.x && no_plot.y == parents.y,
                     "and is not moved off the spot it was given");

  // Empty radii: a table-less world has no plots, so nothing may be refused
  // and nothing may be moved.
  failures += Expect(!core::PlotOverlaps(units, core::PlotRules{}, parents, 25.0F, core::UnitId{}),
                     "a world whose tables name no radii has no plot rule");

  // THE EDGE OF THE MAP IS THE OTHER HALF OF THE INVARIANT. A player's
  // order is REFUSED outside the map; the stub cannot refuse, so it must
  // not go there instead — found by the delivery cycle's own analysis on
  // 2026-09-04, in the very change that was closing the same class of hole.
  const core::PlotRules bounded{.radius_by_type = radii, .map_side_m = 300.0F};
  core::UnitTable edge_units;
  core::UnitRow corner;
  corner.type = core::UnitTypeId{0};
  corner.position = core::Vec2{.x = 40.0F, .y = 40.0F};
  core::AppendRow(edge_units, corner);
  const core::Vec2 pushed = core::FreePlot(edge_units, bounded, corner.position, 25.0F);
  failures += Expect(pushed.x - 25.0F >= 0.0F && pushed.y - 25.0F >= 0.0F &&
                         pushed.x + 25.0F <= 300.0F && pushed.y + 25.0F <= 300.0F,
                     "a house pushed off a crowded corner stays on the map");
  failures += Expect(!core::PlotOverlaps(edge_units, bounded, pushed, 25.0F, core::UnitId{}),
                     "and is still clear of what pushed it");
  // A map side of zero means the table set declares no map, and then there
  // is no edge to fall off — not an edge at the origin.
  const core::Vec2 unbounded = core::FreePlot(units, rules, parents, 25.0F);
  failures += Expect(unbounded.x != 0.0F || unbounded.y != 0.0F,
                     "a table set with no map declared has no edge, not one at zero");

  // DETERMINISM. The search is a fixed walk over integer offsets, so the
  // same question asked twice gives the same answer bit for bit — the half
  // of the phase gate the runs measure, asked of the one new loop.
  const core::Vec2 again = core::FreePlot(units, rules, parents, 25.0F);
  failures += Expect(again.x == second_child.x && again.y == second_child.y,
                     "and the same crowded spot always yields the same free one");
  return failures;
}

/// One resident, one hour, one answer — and the answer is chosen by
/// priority rather than by the order the predicates happen to be written
/// in (resident_activity.h).
///
/// WHAT THIS CHECKS THAT A CHAIN OF IFS WOULD NOT. The states are not
/// mutually exclusive as facts: a sick man can also have no work order, a
/// child can also be at home. The table gives them a total order so that
/// the answer does not depend on which case a reader put first, and the
/// only way to test that is to build a resident several of them are true
/// of and see which one comes out.
int TestResidentActivity() {
  int failures = 0;
  core::ActivityRules rules;
  // The field below is two kilometres from the house, and the test wants an
  // hour of road each way: half an hour per kilometre. The rate is what the
  // rules carry now — the road itself is computed from the two places, so a
  // test can no longer state a travel that the geometry does not support.
  rules.walk_hours_per_km = 0.5F;
  rules.harness_hours_per_km = 0.5F;

  core::WorldState world;
  world.weather.daylight_hours = 12.0F;  // sunrise 6, sunset 18
  core::RefreshCalendarCaches(world.calendar);

  // A house for the family, so that home is a real place.
  core::UnitRow house;
  house.level = 1;
  house.position = core::Vec2{.x = 100.0F, .y = 100.0F};
  const core::UnitId home_id = core::AppendRow(world.units, house);
  core::FamilyRow family;
  family.house = home_id;
  const core::FamilyId family_id = core::AppendRow(world.families, family);

  // A field with work still on it, two kilometres out.
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.phase = core::FieldPhase::kPlowing;
  field.center = core::Vec2{.x = 2100.0F, .y = 100.0F};
  field.work_days_remaining = 5.0F;
  const core::FieldId field_id = core::AppendRow(world.fields, field);

  core::ResidentRow man;
  man.family = family_id;
  man.birth_day = -30 * static_cast<std::int32_t>(core::kDaysPerYear);  // thirty years old
  man.health = 70.0F;
  man.work.kind = core::WorkKind::kPlowing;
  man.work.field = field_id;
  core::AppendRow(world.residents, man);

  // The age is no longer passed in — it is computed from the birth day and
  // the speed-up, which is the whole point of the fix. So the test sets the
  // birth day to make the man the age it wants, and does it through the
  // same arithmetic the answer uses.
  const auto at = [&world, &rules](std::uint32_t hour, float age) {
    world.calendar.tick = hour;
    core::RefreshCalendarCaches(world.calendar);
    world.residents.rows[0].birth_day =
        static_cast<std::int32_t>(world.calendar.day) -
        static_cast<std::int32_t>(age / rules.life_speedup *
                                  static_cast<float>(core::kDaysPerYear));
    return core::ActivityOfResident(world, 0, rules);
  };

  // Sunrise to sunrise+travel is the road out; then the work; then the road
  // home; then the plot until sleep; then the night.
  failures += Expect(at(6, 30.0F).activity == core::ResidentActivity::kWalking,
                     "the hour after sunrise is the road to work");
  failures += Expect(at(6, 30.0F).detail == 0, "and its detail says which way he is going");
  failures += Expect(at(10, 30.0F).activity == core::ResidentActivity::kWorking,
                     "the middle of the day is work");
  failures += Expect(at(17, 30.0F).activity == core::ResidentActivity::kWalking,
                     "the hour before sunset is the road home");
  failures += Expect(at(17, 30.0F).detail == 1, "and it says so");
  failures += Expect(at(19, 30.0F).activity == core::ResidentActivity::kLph,
                     "the evening is the household plot — an INVENTED schedule, marked as one");
  failures += Expect(at(2, 30.0F).activity == core::ResidentActivity::kAtHome,
                     "the small hours are at home");
  failures += Expect(at(2, 30.0F).detail == 0, "asleep");

  // THE WORK PLACE IS THE FIELD, and it is a point rather than an id: a man
  // in a field stands at no unit at all.
  const core::ResidentActivityState working = at(10, 30.0F);
  failures += Expect(working.place.point.x == 2100.0F && working.place.unit.value == 0,
                     "a man at work is at a point, and at no unit when the work is a field");

  // NOTHING TO WORK WITH is not the same as no work: the seam empties and
  // the same man in the same hour is blocked rather than working.
  world.fields.rows[0].work_days_remaining = 0.0F;
  failures += Expect(at(10, 30.0F).activity == core::ResidentActivity::kBlocked,
                     "an order with an empty seam is BLOCKED, and that is not idleness");
  failures += Expect(at(10, 30.0F).detail == 3, "waiting his turn, for want of a nearer cause");
  world.fields.rows[0].work_days_remaining = 5.0F;

  // AND A HAULER STOPS FOR A REASON OF HIS OWN. "Waiting his turn" is a
  // true sentence about the wrong thing: a man carrying a load off a field
  // stops because there is nowhere to put it, and the cause is the only
  // part of a stoppage the player can act on.
  world.residents.rows[0].work.kind = core::WorkKind::kHauling;
  world.fields.rows[0].reaped_grams = 1000;
  world.fields.rows[0].haul_days_remaining = 0.0F;
  failures += Expect(
      at(10, 30.0F).activity == core::ResidentActivity::kBlocked && at(10, 30.0F).detail == 2,
      "a load standing with no demand against it means the doors are shut, and "
      "the stoppage says so: nowhere to put it");
  // And with a demand against it he is not blocked at all — otherwise the
  // check above would only be proving that a hauler is always stopped.
  world.fields.rows[0].haul_days_remaining = 3.0F;
  failures += Expect(at(10, 30.0F).activity == core::ResidentActivity::kWorking,
                     "and while there is room to carry it into, he is carrying");
  world.fields.rows[0].reaped_grams = 0;
  world.fields.rows[0].haul_days_remaining = 0.0F;
  world.residents.rows[0].work.kind = core::WorkKind::kPlowing;

  // NO ORDER AT ALL is idleness — the other half, and the one the player
  // fixes differently.
  world.residents.rows[0].work.kind = core::WorkKind::kNone;
  failures += Expect(at(10, 30.0F).activity == core::ResidentActivity::kIdle,
                     "no order and a fit man of working age is IDLE");

  // DINNER IS ONE HOUR IN THE MIDDLE OF THE LIGHT DAY — boss's number, not
  // a convention of the core's. It is visible for anybody the day has not
  // claimed for something the table ranks higher.
  world.residents.rows[0].work.kind = core::WorkKind::kNone;
  failures += Expect(at(12, 10.0F).activity == core::ResidentActivity::kEating,
                     "at midday a schoolchild is at dinner, not at his lesson");
  failures += Expect(at(13, 10.0F).activity == core::ResidentActivity::kStudying,
                     "and an hour later he is back: dinner is one hour, not an afternoon");
  failures += Expect(at(12, 10.0F).detail == 0xFF,
                     "where he eats has no source, and an unnamed place beats a guessed one");
  // BUT NEITHER A WORKER NOR AN IDLE MAN EVER EATS, and that is the table's
  // priority talking rather than this code: eating is 8, working 6, idle 5.
  // Everything a person of working age can be doing outranks his dinner, so
  // the state is reachable only for children and for those otherwise at
  // home. Reported to boss on 2026-09-05 as a question about the ROSTER —
  // his own reason for the meal hour is the FIELD CANTEEN, bought so that
  // the hour is not spent walking home, which is a statement about workers
  // and cannot be expressed while working outranks eating. Asserted here as
  // the table has it, so the day the priority moves these checks move too.
  failures += Expect(at(12, 30.0F).activity == core::ResidentActivity::kIdle,
                     "as the table ranks it today, an unassigned man idles through dinner");
  world.residents.rows[0].work.kind = core::WorkKind::kPlowing;
  failures += Expect(at(12, 30.0F).activity == core::ResidentActivity::kWorking,
                     "and a man at work stays at work through it");

  // A MAN WHO WALKED OFF IS NOT IDLE, AND THE DIFFERENCE IS AN ACCUSATION
  // IN BOTH DIRECTIONS. He has no order — PayDay cleared it when he broke
  // off — but he has been out today, which nobody unassigned has. Calling
  // that idleness would blame the chairman for a decision the man made
  // himself.
  world.residents.rows[0].work.kind = core::WorkKind::kNone;
  world.residents.rows[0].work.hours_away_today = 6.0F;
  world.residents.rows[0].rest = 8.0F;  // under the walk-off line he broke at
  failures +=
      Expect(at(10, 30.0F).activity == core::ResidentActivity::kTruant && at(10, 30.0F).detail == 1,
             "unassigned, already out today and under the rest he broke off at: he "
             "walked off, and that is not idleness");

  // AND THE OTHER MAN IS NOT ACCUSED. The crew finished the phase under him
  // and production moved the field on: PayDay settled him too, and he is
  // just as unassigned with just as many hours away — but he is rested, and
  // none of it was his doing.
  world.residents.rows[0].rest = 70.0F;
  failures += Expect(at(10, 30.0F).activity != core::ResidentActivity::kTruant,
                     "a man whose job vanished under him is not a truant: he did not choose it");
  world.residents.rows[0].work.hours_away_today = 0.0F;

  // AND IDLENESS IS NEVER CHARGED FOR AN AGE. The toddler and the old man
  // are at home, and nothing is charged to the player — not because either
  // has a state of his own, but because there is nothing to charge: kIdle
  // holds for a worker in working hours and for nobody else.
  //
  // The roster HAD two states for this, "too young" and "not a worker", and
  // the census found them unreachable on its first run. Boss did not
  // reorder the priorities: he took both out, because they answered "why is
  // the signal not charged to him" inside a list that answers "what is he
  // doing" — one word for two questions.
  failures += Expect(at(10, 4.0F).activity != core::ResidentActivity::kIdle,
                     "a toddler with no order is not the chairman's failure");
  failures +=
      Expect(at(10, 80.0F).activity != core::ResidentActivity::kIdle, "and neither is an old man");
  failures += Expect(at(10, 4.0F).activity == core::ResidentActivity::kAtHome,
                     "a toddler with nothing else true of him is at home, and that is the truth");
  world.residents.rows[0].health = 5.0F;
  failures += Expect(at(10, 30.0F).activity == core::ResidentActivity::kTreated,
                     "and a man too ill to stand is being treated, whatever else is true of him");
  world.residents.rows[0].health = 70.0F;

  // A SCHOOLCHILD IS STUDYING AND NOT IDLE, and the priority is what says
  // so: both predicates hold of a ten-year-old in daylight.
  failures += Expect(at(10, 10.0F).activity == core::ResidentActivity::kStudying,
                     "a child in the daytime is at school, not idling");
  failures += Expect(at(2, 10.0F).activity == core::ResidentActivity::kAtHome,
                     "and at night he is at home: school does not run round the clock");
  // A GRAVE CHILD WAITING FOR THE DISTRICT'S CAR goes to no school (boss,
  // boss-core-epoch1-2 seq 3, 1): the same ten-year-old in daylight.
  world.residents.rows[0].away_reason =
      static_cast<std::uint8_t>(core::AwayReason::kAwaitingAmbulance);
  failures += Expect(at(10, 10.0F).activity != core::ResidentActivity::kStudying,
                     "a child waiting for the district's car lies at home, not at school");
  world.residents.rows[0].away_reason = static_cast<std::uint8_t>(core::AwayReason::kNone);

  // THE WATCHMAN (boss, parcel 360): a night post holds him at his unit from
  // sunset to sunrise; by day he is at home, as his day's sleep is a STUB.
  core::UnitRow yard;
  yard.level = 1;
  yard.position = core::Vec2{.x = 900.0F, .y = 900.0F};
  const core::UnitId yard_id = core::AppendRow(world.units, yard);
  rules.post_shift = {core::PostShift::kWorkday, core::PostShift::kNight};
  world.residents.rows[0].work = core::WorkAssignment{};
  world.residents.rows[0].post =
      core::PostAssignment{.profession = core::ProfessionId{1}, .unit = yard_id};
  const core::ResidentActivityState midnight = at(0, 60.0F);
  failures +=
      Expect(midnight.activity == core::ResidentActivity::kWorking &&
                 midnight.place.unit.value == yard_id.value && midnight.place.point.x == 900.0F,
             "the watchman at midnight is working at the yard he keeps");
  failures += Expect(at(12, 60.0F).activity != core::ResidentActivity::kWorking,
                     "and at noon he is not at his post");
  // The same watchman waiting for the district's car keeps no watch — the
  // answer the post signal and the shift announcement give (0.34.19).
  world.residents.rows[0].away_reason =
      static_cast<std::uint8_t>(core::AwayReason::kAwaitingAmbulance);
  failures += Expect(at(0, 60.0F).activity != core::ResidentActivity::kWorking,
                     "a watchman waiting for the district's car is not at his post at midnight");
  return failures;
}

/// THE FIGURE RULE, measured on the rule and not on a village.
///
/// A first version of this guard walked the eighty people of the start and
/// asserted nobody stood outside the cut. It passed with the cut REMOVED —
/// eighty draws of a 3.7 % sigma simply never reach 2.5 sigma, so the
/// assertion was about the sample and not about the rule. Mutation found it;
/// reading it could not have.
int CheckTheFigureRule() {
  int failures = 0;
  constexpr float kSigma = 0.037F;
  constexpr float kCut = 2.5F;
  constexpr std::uint64_t kPeople = 200000;

  float widest = 0.0F;
  double sum = 0.0;
  std::uint32_t at_the_cut = 0;
  bool identical = true;
  const float first = core::DrawBodyDeviation(1930, 0, 0, kSigma, kCut);
  for (std::uint64_t person = 0; person < kPeople; ++person) {
    const float deviation = core::DrawBodyDeviation(1930, person, 0, kSigma, kCut);
    widest = std::max(widest, std::abs(deviation));
    sum += static_cast<double>(deviation);
    at_the_cut += std::abs(deviation) > (kCut * kSigma) - 1.0e-6F ? 1U : 0U;
    identical = identical && deviation == first;
  }
  failures += Expect(!identical, "two hundred thousand people are not all the same height");
  // THE CUT IS THE SUBJECT. At this many draws a normal distribution reaches
  // past 2.5 sigma about one time in eighty, so an untrimmed tail would show
  // here in thousands of people.
  failures += Expect(widest <= (kCut * kSigma) + 1.0e-6F,
                     "and not one of them stands outside the cut of 2.5 sigma");
  failures += Expect(at_the_cut > 0,
                     "while some of them stand exactly ON it — otherwise the check above passes on "
                     "a spread too narrow to ever reach the cut");
  const double mean = sum / static_cast<double>(kPeople);
  failures += Expect(mean > -0.002 && mean < 0.002,
                     "the spread is centred: the village is not quietly taller or shorter than "
                     "its own base height");

  // THE SAME PERSON IS THE SAME HEIGHT, however often asked, and a different
  // world gives a different one. This is what makes the figure storable
  // without being stored twice.
  failures += Expect(core::DrawBodyDeviation(1930, 42, 0, kSigma, kCut) ==
                         core::DrawBodyDeviation(1930, 42, 0, kSigma, kCut),
                     "the same person in the same world is the same height every time");
  failures += Expect(core::DrawBodyDeviation(1930, 42, 0, kSigma, kCut) !=
                         core::DrawBodyDeviation(1931, 42, 0, kSigma, kCut),
                     "and the same person in another world is not");
  // Height and build must not be tied to each other: one hash position for
  // both would make every tall person broad.
  failures += Expect(core::DrawBodyDeviation(1930, 42, 0, kSigma, kCut) !=
                         core::DrawBodyDeviation(1930, 42, 0x100, kSigma, kCut),
                     "height and build are drawn apart, so a tall man is not broad by arithmetic");
  // AND THE ROLL ACTUALLY USES TWO POSITIONS, which the two calls above do
  // not prove: they test the function, and the defect would live in its
  // CALLER. Measured with the two spreads made equal, so that a shared
  // position shows as equal values rather than merely proportional ones —
  // with the shipped sigmas the two would differ anyway and the guard would
  // pass on arithmetic instead of on the rule.
  {
    core::BodyKnobs even;
    even.height_sigma_frac = kSigma;
    even.build_sigma_frac = kSigma;
    core::ResidentRow person;
    core::RollBody(1930, 42, even, person);
    failures += Expect(person.height_deviation != person.build_deviation,
                       "a person's height and build come from two different draws, not from one");
  }

  // THE GOLDEN VALUE, and it is a CROSS-COMPILER tripwire rather than a
  // regression test of arithmetic.
  //
  // These two floats are the first things in the SAVED state that are not a
  // scaled integer behind an integer gate, so the argument that holds
  // Clang and MSVC together elsewhere does not cover them. The draw was
  // rewritten to use addition alone for that reason — IEEE-754 addition is
  // correctly rounded by the standard, and `std::log` and `std::cos` are
  // not — and this line is what says so out loud on the day somebody
  // "simplifies" it back to Box-Muller, or the day the two compilers
  // disagree.
  //
  // Compared BY BITS: an equality on floats would pass on two numbers that
  // print the same and are not.
  {
    const float golden = core::DrawBodyDeviation(1930, 42, 0, kSigma, kCut);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &golden, sizeof(bits));
    failures += Expect(bits == 0x3D8E5241U,
                       "the figure draw is bit-for-bit what it was: the same person in the same "
                       "world is the same height under any compiler that follows IEEE-754");
  }

  // A NONSENSE KNOB GIVES NO DEVIATION rather than a NaN village: written
  // positively so anything unexpected falls out here and not three systems
  // away.
  failures += Expect(core::DrawBodyDeviation(1930, 7, 0, -1.0F, kCut) == 0.0F,
                     "a negative spread is refused into a flat zero");
  failures += Expect(core::DrawBodyDeviation(1930, 7, 0, kSigma, 0.0F) == 0.0F,
                     "and so is a cut of nothing");

  // TWO LEGAL KNOBS WHOSE PRODUCT IS NOT LEGAL. Each band is read and
  // accepted on its own — 0..0.5 for the spread, 0.5..6 for the cut — and
  // together they multiply out to three, which would make `base * (1 +
  // deviation)` a NEGATIVE height. The guard measures the pair, because
  // that is the thing neither band can see.
  {
    // A CROWD AND NOT ONE MAN. The first version of this guard asked one
    // person, and that person's draw happened to be moderate: with the cap
    // removed he still landed inside the band, and the check passed its own
    // mutation. The subject of a bound is the WIDEST of many, never a
    // sample of one.
    float widest_absurd = 0.0F;
    for (std::uint64_t person = 0; person < 5000; ++person) {
      widest_absurd =
          std::max(widest_absurd, std::abs(core::DrawBodyDeviation(1930, person, 0, 0.5F, 6.0F)));
    }
    failures += Expect(widest_absurd < 1.0F,
                       "a legal-but-absurd pair of knobs still cannot make a person shorter than "
                       "nothing: the product is bounded where neither factor is");
    failures += Expect(widest_absurd > 0.5F,
                       "and the pair really does push against that bound — otherwise the check "
                       "above passes on knobs too tame to test it");
    core::ResidentRow tall;
    tall.sex = core::Sex::kMale;
    tall.height_deviation = -widest_absurd;
    core::BodyKnobs wild;
    wild.height_sigma_frac = 0.5F;
    wild.clamp_sigma = 6.0F;
    // THE AGE IS SPELLED OUT because the parameter stopped being a boolean on
    // 2026-09-13: a surviving `true` still compiles and now means "aged one
    // year", which is a toddler and not the grown man this line is about. The
    // analysis caught it here, where the assertion — "greater than zero" —
    // would have passed on either reading.
    failures += Expect(core::HeightMeters(tall, wild, 30.0F) > 0.0F,
                       "and the metres that come out of it are a height and not a hole");
  }

  // AND INHERITANCE STAYS INSIDE THE BAND, generation after generation. Half
  // a spread on top of an already-clamped parental mean walks outward: one
  // generation reached 13.9 % against a promised 9 %, and the bound crept on
  // from there. Walked twenty generations, because the first version of this
  // rule was wrong in a way that ONE generation barely showed.
  {
    core::BodyKnobs knobs;
    const float band = knobs.height_sigma_frac * knobs.clamp_sigma;
    core::ResidentRow mother;
    core::ResidentRow father;
    mother.height_deviation = band;
    father.height_deviation = band;
    bool inside = true;
    for (std::uint64_t generation = 0; generation < 20; ++generation) {
      core::ResidentRow child;
      core::RollBodyFromParents(1930, generation + 1, knobs, mother, father, child);
      inside = inside && std::abs(child.height_deviation) <= band + 1.0e-6F;
      mother = child;
      father = child;
    }
    failures += Expect(inside,
                       "twenty generations of the tallest possible parents stay inside the band "
                       "the knobs describe");
  }

  // THE METRES, AND THE FOUR STEPS OF CHILDHOOD (2026-09-13). The answer for
  // a child was 0 until the fractions and their bands arrived together; the
  // hour between the two exports is the lesson, and this block is what makes
  // it hard to lose again.
  core::ResidentRow man;
  man.sex = core::Sex::kMale;
  man.height_deviation = 0.1F;
  core::BodyKnobs knobs;
  const float grown = core::HeightMeters(man, knobs, 30.0F);
  failures += Expect(grown > 1.82F && grown < 1.83F,
                     "an adult's height is his base raised by his own fraction");
  // THE LADDER, STEP BY STEP, and each rung asserted against the NEXT rather
  // than against a number of its own: the fractions are balance data and the
  // ORDER is the fact. A test written against 0.45 and 0.65 would go red on a
  // balance edit that changed nothing about the rule.
  const float infant = core::HeightMeters(man, knobs, 1.0F);
  const float preschool = core::HeightMeters(man, knobs, 4.0F);
  const float junior = core::HeightMeters(man, knobs, 8.0F);
  const float senior = core::HeightMeters(man, knobs, 13.0F);
  failures += Expect(infant > 0.0F,
                     "a child has a height now: the fractions of childhood arrived with the bands "
                     "they apply on, and a fraction without a band could do nothing at all");
  failures += Expect(infant < preschool && preschool < junior && junior < senior && senior < grown,
                     "and the four steps climb in order, each shorter than the next and all "
                     "shorter than the grown man");
  // THE EDGES BELONG TO THE STEP THEY OPEN, and that is asserted rather than
  // assumed: a band read as "up to and including" would put a seven-year-old
  // in the preschool step and nothing else in the suite would notice.
  failures += Expect(core::HeightMeters(man, knobs, knobs.age_school_junior_from_years) == junior,
                     "a child on the day a step opens is already in it");
  failures += Expect(core::HeightMeters(man, knobs, knobs.age_adult_from_years) == grown,
                     "and the day adulthood opens he is grown");
  core::ResidentRow woman = man;
  woman.sex = core::Sex::kFemale;
  failures += Expect(core::HeightMeters(woman, knobs, 30.0F) < grown,
                     "the two bases are told apart — the same fraction of a smaller base is less");
  failures += Expect(core::HeightMeters(woman, knobs, 4.0F) < preschool,
                     "and so are the children's: ONE fraction per step for both sexes, because "
                     "the difference already sits in the base it multiplies");
  return failures;
}

/// ONE DOOR KEPT THE LIMIT AND TWENTY PLACES TRUSTED IT.
///
/// A definition id is a 16-bit dense row index with 0xFFFF reserved, and the
/// only thing that kept a row index inside it was csv_table_set.cpp refusing
/// a file of more than 65535 data rows. Every conversion in the tree spelled
/// `Id{static_cast<std::uint16_t>(row)}` by hand and relied on that. The
/// cost of the arrangement is not that the sentinel gets reached — it is
/// that a row above it WRAPS, silently, onto another row's meaning.
///
/// The conversion is total: out of range has no id, and it says so.
/// A REFUSAL MUST NOT BE ABLE TO WEAR THE FACE OF A MEASUREMENT.
///
/// deadline.h says a `kDays` answer of 0 means "the limit is reached now" —
/// the most alarming reading the type has — and it warns in its own header
/// that the counterfeit to guard against is exactly a refusal dressed as a
/// zero. Until 0.17.81 the one function that could produce that counterfeit
/// was `NoDeadline(DeadlineKind)`, which accepted `kDays` without a word.
///
/// The three refusals are now three names with no argument, so the forgery
/// cannot be written at all. What is left to check is that none of them
/// carries `kDays`, and that a real zero-day forecast stays distinguishable
/// from all three — which is the confusion the defect would have caused.
int TestDeadlineRefusals() {
  int failures = 0;

  failures += Expect(core::DeadlineNever().kind == core::DeadlineKind::kNever &&
                         core::DeadlineNotApplicable().kind == core::DeadlineKind::kNotApplicable &&
                         core::DeadlineNoData().kind == core::DeadlineKind::kNoData,
                     "each refusal answers with its own kind and no other");

  // THE ASSERTION THE DEFECT WOULD HAVE FAILED. Not "days is 0" — days is 0
  // in all three by construction and would be 0 in the forgery too; the
  // question is the KIND, because that is what a reader switches on.
  failures += Expect(core::DeadlineNever().kind != core::DeadlineKind::kDays &&
                         core::DeadlineNotApplicable().kind != core::DeadlineKind::kDays &&
                         core::DeadlineNoData().kind != core::DeadlineKind::kDays,
                     "and no refusal is a forecast: none of the three answers kDays");

  // The real thing it must not be confused with. A limit reached TODAY is a
  // legitimate answer and looks identical in the `days` field alone.
  const core::Deadline now = core::DeadlineInDays(0);
  failures += Expect(now.kind == core::DeadlineKind::kDays && now.days == 0,
                     "a limit reached today is a forecast of zero days, and stays one");
  failures += Expect(now.kind != core::DeadlineNoData().kind,
                     "and it is a different answer from 'nothing to answer with'");

  // Negative days would be a limit already passed, which the type does not
  // express: it clamps rather than carrying a number no reader expects.
  failures += Expect(core::DeadlineInDays(-5).days == 0,
                     "a limit already passed is reported as today, not as a negative count");

  // AND THE FIFTH ANSWER, WHICH IS A MEASUREMENT AND NOT A REFUSAL
  // (2026-09-12). It carries a number like kDays and means the opposite: not
  // "so long left" but "so long since the window shut". The whole reason it
  // exists is that one integer was answering both questions with the same
  // zero, and a work queue ranking the smallest first therefore put hopeless
  // work above work that still mattered.
  const core::Deadline late = core::DeadlineOverdue(30);
  failures += Expect(late.kind == core::DeadlineKind::kOverdue && late.days == 30,
                     "an overdue answer carries its own kind and the days since");
  failures += Expect(late.kind != core::DeadlineKind::kDays,
                     "and it is NOT a forecast: a reader switching on the kind cannot mistake "
                     "'thirty days late' for 'thirty days left'");
  failures += Expect(core::DeadlineOverdue(-5).days == 0,
                     "and it clamps like the forecast does, for the same reason");

  // The default is the refusal that shows nothing, deliberately: an unfilled
  // field must not read as well-being.
  failures += Expect(core::Deadline{}.kind == core::DeadlineKind::kNoData,
                     "an unfilled deadline defaults to 'no data', never to a comfortable zero");

  static_assert(core::DeadlineNever().kind != core::DeadlineKind::kDays,
                "the refusals are constexpr, so this holds before the test even runs");
  return failures;
}

int TestDefIdFromRow() {
  int failures = 0;

  failures += Expect(core::DefIdFromRow<core::ResourceIdTag>(0).value == 0,
                     "row zero is a real id, not mistaken for absence");
  failures += Expect(core::DefIdFromRow<core::ResourceIdTag>(65534).value == 65534,
                     "and the last row a table may have is still a real id");

  // ROW 65535 PROVES NOTHING, AND THE LINE SAYING SO IS THE POINT OF IT.
  // The truncation of 65535 to sixteen bits IS the sentinel, so the guarded
  // and the unguarded conversion agree there exactly — damaging the bound
  // reddens nothing on this row, measured. It is asserted anyway, as the
  // statement that absence is what a caller gets; it is not evidence that
  // the bound exists.
  failures +=
      Expect(core::DefIdFromRow<core::ResourceIdTag>(65535).value == core::kInvalidDefIdValue,
             "the row that lands on the sentinel is refused an id (agrees either way)");

  // THESE ARE THE EVIDENCE, and they are about WRAPPING, not about reaching
  // the sentinel. 65536 truncated to 0 — the first row of the table, a
  // perfectly valid id belonging to something else. Nothing downstream could
  // have noticed: not a sentinel, not a failure, just the wrong resource.
  failures +=
      Expect(core::DefIdFromRow<core::ResourceIdTag>(65536).value == core::kInvalidDefIdValue,
             "a row past the sentinel does not WRAP onto row zero's meaning");
  failures +=
      Expect(core::DefIdFromRow<core::ResourceIdTag>(70000).value == core::kInvalidDefIdValue,
             "nor onto any other row's — 70000 would have become row 4464");

  // ITable's own "no such row" is 0xFFFFFFFF, and it must keep arriving as
  // absence: every `row == kNoTableRow ? Id{} : ...` in the tree collapsed
  // into this call, so if it answered anything else those all changed
  // meaning at once.
  failures +=
      Expect(core::DefIdFromRow<core::ResourceIdTag>(0xFFFFFFFFU).value == core::kInvalidDefIdValue,
             "and kNoTableRow still arrives as absence, as the ternaries it replaced did");

  // The tag is part of the type, so a crop id cannot be handed to something
  // expecting a resource. That is the whole reason DefId is a template.
  const core::CropId crop = core::DefIdFromRow<core::CropIdTag>(3);
  failures += Expect(crop.value == 3, "the conversion is per tag, and keeps the tag");

  // The index-shaped call: same rule, different reason to be in range, and a
  // std::size_t on the way in rather than a table's uint32.
  failures += Expect(core::DefIdFromIndex<core::ResourceIdTag>(std::size_t{5}).value == 5,
                     "a vector index becomes the same id a row would");
  failures += Expect(core::DefIdFromIndex<core::ResourceIdTag>(std::size_t{65536}).value ==
                         core::kInvalidDefIdValue,
                     "and it does not wrap either — 65536 would have become index zero");
  failures += Expect(core::DefIdFromIndex<core::ResourceIdTag>(std::size_t{100000}).value ==
                         core::kInvalidDefIdValue,
                     "nor at any distance past it");

  // constexpr, so a wrong answer here would not even compile through.
  static_assert(core::DefIdFromRow<core::ResourceIdTag>(65536).value == core::kInvalidDefIdValue,
                "the wrap must be closed at compile time too");
  return failures;
}

/// The field's next sowing (fund_ladder.h, NextSowingCrop): by the field's
/// state, never by the year's number. Oats 0, rye 1, clover 2 in the slots.
int CheckNextSowingCrop() {
  int failures = 0;
  constexpr core::SimDay kToday = core::kDaysPerYear + 30;
  core::FieldRow field;
  field.rotation_year0 = core::CropId{0};
  field.rotation_year1 = core::CropId{1};
  field.rotation_year2 = core::CropId{2};
  field.phase = core::FieldPhase::kIdle;
  // Crop 1 is the winter one (the ladder test's norms below say the same).
  const auto next = [&field]() {
    return core::NextSowingCrop(field, kToday, field.rotation_year0.value == 1).value;
  };
  failures += Expect(next() == 0, "next sowing: an idle field waits for its first slot");
  field.phase = core::FieldPhase::kGrowing;
  field.crop = core::CropId{0};
  failures += Expect(next() == 1, "next sowing: the first slot standing, the second is next");
  field.phase = core::FieldPhase::kIdle;
  field.crop = core::CropId{};
  field.reaped_day = kToday - 5;
  failures += Expect(next() == 1, "next sowing: reaped this year, the second slot is next");
  field.reaped_day = 5;  // last year: this year's first slot is owed again
  failures += Expect(next() == 0, "next sowing: reaped LAST year, the first slot again");
  field.phase = core::FieldPhase::kHarrowing;
  field.crop = core::CropId{1};
  failures += Expect(next() == 1, "next sowing: a missed first slot worked for the rye");
  field.phase = core::FieldPhase::kGrowing;
  field.reaped_day = kToday - 5;
  failures += Expect(next() == 2, "next sowing: the autumn's rye in, the third slot is next");
  field.rotation_year0 = core::CropId{};
  field.phase = core::FieldPhase::kIdle;
  field.crop = core::CropId{};
  field.reaped_day = core::kNeverReapedDay;
  failures += Expect(next() == 1, "next sowing: a fallow first slot passes to the second");
  // Rye in both slots: this year's rye standing, unreaped, is the FIRST slot's.
  field.rotation_year0 = core::CropId{1};
  field.phase = core::FieldPhase::kGrowing;
  field.crop = core::CropId{1};
  failures += Expect(next() == 1, "next sowing: rye in both slots, this year's standing");
  field.reaped_day = kToday - 5;
  failures += Expect(next() == 2, "next sowing: rye in both slots, next year's sown after");
  // A CHAIN NAMED AFTER THIS YEAR'S SOWING (rotation_skips_turn): the turn
  // holds it still, and its first named crop is sown next — not the second
  // slot a reaped field would otherwise name (static review of 0.34.38).
  field.rotation_year0 = core::CropId{0};
  field.phase = core::FieldPhase::kIdle;
  field.crop = core::CropId{};
  field.rotation_skips_turn = 1;
  failures += Expect(next() == 0, "next sowing: a chain the turn holds still sows its first");
  // A RUNNING chain whose winter first slot missed its autumn (question 278,
  // 0.36.13): idle, the rye neither standing nor reaped this year — the slot
  // lies fallow, and the next sowing is the second slot's, not the lost rye.
  field.rotation_skips_turn = 0;
  field.rotation_assigned = 1;
  field.rotation_year0 = core::CropId{1};
  field.rotation_year1 = core::CropId{0};
  field.rotation_year2 = core::CropId{2};
  field.phase = core::FieldPhase::kIdle;
  field.crop = core::CropId{};
  field.reaped_day = 5;  // last year's
  failures += Expect(next() == 0,
                     "next sowing: a winter slot lost to its window passes to the "
                     "second slot, like a fallow");
  field.reaped_day = kToday - 5;  // reaped this year: not lost, and the second is next anyway
  failures += Expect(next() == 0, "next sowing: the winter slot reaped this year, the second next");
  return failures;
}

/// The top two rungs of the ladder of funds (fund_ladder.h): seed for a field
/// not yet sown, the plan reserve — what is owed, as far as the crop lies
/// below the seed (0.34.42) — and each unsealing off its own rung (0.34.17).
int CheckTheTopOfTheLadder() {
  int failures = 0;
  core::WorldState world;
  const std::vector<core::SeedNorm> norms = {
      {.resource = core::ResourceId{2}, .sowing_norm_kg_per_ha = 100.0F},
      {.resource = core::ResourceId{1}, .sowing_norm_kg_per_ha = 50.0F, .is_winter = true},
      {.resource = core::ResourceId{0}, .sowing_norm_kg_per_ha = 80.0F}};
  core::FieldRow waiting;
  waiting.area_ga = 2.0F;
  waiting.rotation_year0 = core::CropId{0};
  waiting.phase = core::FieldPhase::kHarrowing;
  core::AppendRow(world.fields, waiting);
  core::FieldRow sown = waiting;
  sown.phase = core::FieldPhase::kGrowing;
  core::AppendRow(world.fields, sown);

  // 300 000 owed; the barn holds 500 000 — the 200 000 of seed and the debt.
  // Since 0.34.42 the plan rung holds what is owed as far as the crop LIES
  // below the seed (fund_ladder.h, PlanRungGrams), not as far as the book
  // says it was reaped.
  world.plan.due = {0, 0, 300'000};
  core::UnitRow barn;
  barn.level = 1;
  barn.stock = {0, 0, 500'000};
  core::AppendRow(world.units, barn);

  const core::ResourceAmounts held = core::HeldAboveFodder(world, norms, 3, true);
  failures += Expect(held.size() == 3 && held[2] == 200'000 + 300'000,
                     "ladder: seed for the unsown field only, and the plan's debt");
  failures += Expect(core::HeldAboveFodder(world, norms, 3, false)[2] == 300'000,
                     "ladder: the seed rung can be switched off, the plan rung cannot");
  // QUESTION 278 (0.36.13): a running chain's winter first slot LOST to its
  // window owes no seed this year — the fund held its rye from January to the
  // fallow's harrow (static review). And the next slot's winter crop is not
  // owed past its own window when no work on it began.
  {
    core::WorldState lost = world;
    lost.fields.rows.clear();
    lost.fields.row_ids.clear();
    lost.fields.row_by_id.clear();
    lost.fields.next_id_value = 1;
    core::FieldRow rye_first;
    rye_first.area_ga = 2.0F;
    rye_first.rotation_assigned = 1;
    rye_first.rotation_year0 = core::CropId{1};  // the winter crop, lost
    rye_first.rotation_year1 = core::CropId{2};
    rye_first.rotation_year2 = core::CropId{0};
    rye_first.phase = core::FieldPhase::kIdle;
    rye_first.reaped_day = 5;  // last year's reaping
    core::AppendRow(lost.fields, rye_first);
    lost.calendar.tick = (core::kDaysPerYear + 4U) * core::kTicksPerDay;  // February, year 2
    core::RefreshCalendarCaches(lost.calendar);
    failures += Expect(core::SeedRungLeft(lost, norms, 3)[1] == 0,
                       "ladder: a winter slot lost to its window owes no rye seed this year");
    core::WorldState autumn = lost;
    core::FieldRow& field = autumn.fields.rows[0];
    field.rotation_year0 = core::CropId{2};
    field.rotation_year1 = core::CropId{1};  // the autumn's rye
    field.rotation_year2 = core::CropId{0};
    field.reaped_day = core::kDaysPerYear + 30;  // year0 reaped this summer
    std::vector<core::SeedNorm> windowed = norms;
    windowed[1].sow_to_month = 8;                                            // September, 0-based
    autumn.calendar.tick = (core::kDaysPerYear + 32U) * core::kTicksPerDay;  // September
    core::RefreshCalendarCaches(autumn.calendar);
    failures += Expect(core::SeedRungLeft(autumn, windowed, 3)[1] == 100'000,
                       "ladder: in its window the autumn's rye is owed (2 ha x 50 kg)");
    autumn.calendar.tick = (core::kDaysPerYear + 40U) * core::kTicksPerDay;  // November
    core::RefreshCalendarCaches(autumn.calendar);
    failures += Expect(core::SeedRungLeft(autumn, windowed, 3)[1] == 0,
                       "ladder: past its window, unsown and unbegun, it is owed nobody");
    field.crop = core::CropId{1};
    field.phase = core::FieldPhase::kSowing;
    failures += Expect(core::SeedRungLeft(autumn, windowed, 3)[1] == 100'000,
                       "ladder: but a sowing begun in its window still takes its seed after it");
  }
  // WHAT WENT TO THE DISTRICT EARLY IS NOT HELD (boss seq 25, item 3): the
  // rung is what is STILL OWED.
  {
    core::WorldState shipped = world;
    shipped.plan.delivered = {0, 0, 100'000};
    failures += Expect(core::HeldAboveFodder(shipped, norms, 3, false)[2] == 200'000,
                       "ladder: 100 000 shipped, 200 000 still owed and held");
    shipped.plan.delivered = {0, 0, 250'000};
    failures += Expect(core::HeldAboveFodder(shipped, norms, 3, false)[2] == 50'000,
                       "ladder: 250 000 shipped, 50 000 still owed and held");
    shipped.plan.delivered = {0, 0, 400'000};
    failures += Expect(core::HeldAboveFodder(shipped, norms, 3, false)[2] == 0,
                       "ladder: shipped past the due, the plan rung is empty");
  }
  // THE SEED FIRST, THE PLAN BELOW IT (resources design §6; static review of
  // 0.34.42): 400 000 lie, 200 000 of seed, 300 000 owed. Capped at all that
  // lies, the two rungs held 500 000 of 400 000 — and unsealing 100 000 of
  // the plan freed nothing. Now the plan holds the 200 000 below the seed,
  // and the unsealing frees its 100 000.
  {
    core::WorldState thin = world;
    thin.units.rows[0].stock = {0, 0, 400'000};
    failures += Expect(core::HeldAboveFodder(thin, norms, 3, true)[2] == 400'000,
                       "ladder: seed and plan never hold more than lies — the plan below the seed");
    thin.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kPlanReserve)] = {0, 0, 100'000};
    failures += Expect(core::HeldAboveFodder(thin, norms, 3, true)[2] == 300'000,
                       "ladder: and unsealing 100 000 of the plan frees 100 000");
  }

  // A FIELD REAPED THIS YEAR OWES THIS YEAR NO SEED (boss, parcel 421): idle
  // again and still naming its crop until the turn, it held seed for a crop
  // already in the stores. Reaped LAST year, it is waiting for this year's
  // sowing, and holds it.
  {
    core::WorldState autumn = world;
    autumn.unsealed = {};
    autumn.calendar.day = core::kDaysPerYear + 40;  // October of year 2
    core::FieldRow reaped = waiting;
    reaped.phase = core::FieldPhase::kIdle;
    reaped.reaped_day = core::kDaysPerYear + 38;
    autumn.fields.rows[0] = reaped;
    failures += Expect(core::HeldAboveFodder(autumn, norms, 3, true)[2] == 300'000,
                       "ladder: a field reaped this year holds no seed for the crop it gave");
    autumn.fields.rows[0].reaped_day = 38;  // reaped in the October of year 1
    failures += Expect(core::HeldAboveFodder(autumn, norms, 3, true)[2] == 200'000 + 300'000,
                       "ladder: a field reaped last year holds this year's seed again");
  }

  // THE AUTUMN'S WINTER CROP IS NEXT YEAR'S SOWING (boss seq 18, econ
  // plan-700): from the reaping, or through a fallow year, until the rye is
  // in the ground. 2 ha x 50 kg = 100 000 g of resource 1.
  {
    core::WorldState autumn;
    autumn.calendar.day = core::kDaysPerYear + 40;
    core::FieldRow field;
    field.area_ga = 2.0F;
    field.rotation_year0 = core::CropId{0};
    field.rotation_year1 = core::CropId{1};
    field.phase = core::FieldPhase::kIdle;
    field.reaped_day = core::kDaysPerYear + 30;
    core::AppendRow(autumn.fields, field);
    const auto winter_seed = [&norms](const core::WorldState& state) {
      return core::HeldAboveFodder(state, norms, 3, true)[1];
    };
    failures += Expect(winter_seed(autumn) == 100'000,
                       "ladder: a reaped field holds the seed of the winter crop it sows next");
    autumn.fields.rows[0].rotation_year0 = core::CropId{};
    autumn.fields.rows[0].reaped_day = core::kNeverReapedDay;
    failures += Expect(winter_seed(autumn) == 100'000,
                       "ladder: a fallow year holds the seed of the winter crop after it");
    autumn.fields.rows[0].rotation_year0 = core::CropId{0};
    autumn.fields.rows[0].phase = core::FieldPhase::kHarrowing;
    failures += Expect(winter_seed(autumn) == 0,
                       "ladder: a field still owing its first slot holds no winter seed yet");
    autumn.fields.rows[0].phase = core::FieldPhase::kGrowing;
    autumn.fields.rows[0].crop = core::CropId{1};
    autumn.fields.rows[0].reaped_day = core::kDaysPerYear + 30;
    failures += Expect(winter_seed(autumn) == 0,
                       "ladder: the winter crop in the ground holds its seed no longer");
    // Being worked for the rye is not being sown with it: on the reaped field
    // under the plough, and on the black fallow standing "growing" with no
    // crop (static review, 2026-09-24: either half of winter_sown alone
    // passed every line above).
    autumn.fields.rows[0].phase = core::FieldPhase::kPlowing;
    failures += Expect(winter_seed(autumn) == 100'000,
                       "ladder: ploughing for the winter crop still holds its seed");
    autumn.fields.rows[0].phase = core::FieldPhase::kGrowing;
    autumn.fields.rows[0].crop = core::CropId{};
    autumn.fields.rows[0].rotation_year0 = core::CropId{};
    failures += Expect(winter_seed(autumn) == 100'000,
                       "ladder: a black fallow with nothing in it still holds the rye's seed");
    // A FIRST SLOT THAT MISSED ITS WINDOW (TrySow -> TrySowWinter): the oats
    // will not be sown, the rye will — hold the rye and not the oats.
    autumn.fields.rows[0].rotation_year0 = core::CropId{0};
    autumn.fields.rows[0].reaped_day = core::kNeverReapedDay;
    autumn.fields.rows[0].phase = core::FieldPhase::kHarrowing;
    autumn.fields.rows[0].crop = core::CropId{1};
    const core::ResourceAmounts missed = core::HeldAboveFodder(autumn, norms, 3, true);
    failures += Expect(missed[1] == 100'000 && missed[2] == 0,
                       "ladder: a missed first slot worked for rye holds the rye, not the oats");
    // One crop in both slots: the work is the first slot's, held once.
    autumn.fields.rows[0].rotation_year0 = core::CropId{1};
    failures += Expect(winter_seed(autumn) == 100'000,
                       "ladder: rye in both slots, the rye's seed held once and not twice");
    // And a SPRING crop in both slots under its own plough: its seed is the
    // first slot's and held (0.34.36 held none — static review of 0.34.38).
    {
      core::WorldState oats = autumn;
      oats.fields.rows[0].rotation_year0 = core::CropId{0};
      oats.fields.rows[0].rotation_year1 = core::CropId{0};
      oats.fields.rows[0].crop = core::CropId{0};
      failures += Expect(core::HeldAboveFodder(oats, norms, 3, true)[2] == 200'000,
                         "ladder: oats in both slots, ploughed for, hold their seed");
    }
    autumn.fields.rows[0].phase = core::FieldPhase::kIdle;
    autumn.fields.rows[0].crop = core::CropId{};
    autumn.fields.rows[0].rotation_year0 = core::CropId{0};
    autumn.fields.rows[0].reaped_day = core::kDaysPerYear + 30;
    autumn.fields.rows[0].rotation_year1 = core::CropId{2};
    failures += Expect(core::HeldAboveFodder(autumn, norms, 3, true)[0] == 0,
                       "ladder: a SPRING crop of the second slot is not owed in the autumn");
  }

  // EACH FUND OPENS ITS OWN RUNG (boss, boss-core-epoch1-resume seq 14,
  // answer 3). Until 0.34.17 every release came off one total, and that is
  // how unsealing the FODDER fund opened the plan's oats. Rungs here: seed
  // 200 000, plan 300 000.
  const auto slot = [](core::FundKind fund) { return static_cast<std::size_t>(fund); };
  world.unsealed.by_fund[slot(core::FundKind::kSeed)] = {0, 0, 150'000};
  failures += Expect(core::HeldAboveFodder(world, norms, 3, true)[2] == 50'000 + 300'000,
                     "ladder: the seed fund's release comes off the seed rung only");
  world.unsealed.by_fund[slot(core::FundKind::kPlanReserve)] = {0, 0, 100'000};
  failures += Expect(core::HeldAboveFodder(world, norms, 3, true)[2] == 50'000 + 200'000,
                     "ladder: the plan reserve's release comes off the plan rung only");
  world.unsealed.by_fund[slot(core::FundKind::kFodder)] = {0, 0, 1'000'000};
  failures += Expect(core::HeldAboveFodder(world, norms, 3, true)[2] == 50'000 + 200'000,
                     "ladder: the fodder fund's release opens neither the seed nor the plan");
  world.unsealed.by_fund[slot(core::FundKind::kSeed)] = {0, 0, 900'000};
  failures += Expect(core::HeldAboveFodder(world, norms, 3, true)[2] == 200'000,
                     "ladder: a release past its own rung clamps at zero and reaches no other");
  // RUNG 3: the claim (last year's feed) and inside it the fund — the larger
  // is held, and the fodder release comes off THAT (boss seq 17). Checked
  // with the claim larger, with the fund larger, and past both.
  world.unsealed.by_fund[slot(core::FundKind::kFodder)] = {0, 0, 100'000};
  failures += Expect(core::FodderRungLeft(world, {0, 0, 500'000}, {0, 0, 400'000})[2] == 400'000,
                     "ladder: last year's feed the larger, the release comes off it");
  failures += Expect(core::FodderRungLeft(world, {0, 0, 0}, {0, 0, 400'000})[2] == 300'000,
                     "ladder: no book (the first year), the fund held and the release off it");
  failures += Expect(core::FodderRungLeft(world, {7'000, 0, 0}, {0, 0, 0})[0] == 7'000,
                     "ladder: a feed with no fund — the cow's — is held whole");
  world.unsealed.by_fund[slot(core::FundKind::kFodder)] = {0, 0, 1'000'000};
  failures += Expect(core::FodderRungLeft(world, {0, 0, 500'000}, {0, 0, 400'000})[2] == 0,
                     "ladder: a fodder release past rung 3 empties it and no more");
  return failures;
}

/// A closed square area of `kind` from (x0, y0) to (x1, y1).
core::MapAreaDef SquareArea(core::MapAreaKind kind, float x0, float y0, float x1, float y1) {
  return core::MapAreaDef{
      .key = "square",
      .kind = kind,
      .outline = {{.x = x0, .y = y0}, {.x = x1, .y = y0}, {.x = x1, .y = y1}, {.x = x0, .y = y1}}};
}

/// THE OBSTACLE RASTER (obstacle_raster.h; delivery 7b), counted by hand: a
/// forest square 100 m on a 10 m grid is a hundred cells; a river along
/// y = 300 with a half-width of 10 m flags the two rows whose centres (295,
/// 305) lie within it — eighty cells on a 400 m map. A village contour flags
/// nothing; a brook is not drawn at all.
int TestObstacleRaster() {
  int failures = 0;
  core::MapObstacles obstacles;
  obstacles.areas.push_back(SquareArea(core::MapAreaKind::kForest, 100.0F, 100.0F, 200.0F, 200.0F));
  obstacles.areas.push_back(
      SquareArea(core::MapAreaKind::kVillageZone, 0.0F, 0.0F, 400.0F, 400.0F));
  obstacles.lines.push_back(core::MapLineDef{
      .key = "river",
      .kind = core::MapLineKind::kRiver,
      .points = {{.position = {.x = 0.0F, .y = 300.0F}, .half_width_m = 10.0F},
                 {.position = {.x = 400.0F, .y = 300.0F}, .half_width_m = 10.0F}}});
  obstacles.lines.push_back(
      core::MapLineDef{.key = "brook",
                       .kind = core::MapLineKind::kBrook,
                       .points = {{.position = {.x = 0.0F, .y = 50.0F}, .half_width_m = 5.0F},
                                  {.position = {.x = 400.0F, .y = 50.0F}, .half_width_m = 5.0F}}});
  const core::ObstacleRaster raster(obstacles, 400.0F, 10.0F);
  failures += Expect(raster.CountCells(core::kObstacleForest) == 100,
                     "raster: a 100 m forest square is a hundred 10 m cells");
  failures += Expect(raster.CountCells(core::kObstacleRiver) == 80,
                     "raster: the river's two rows of forty cells");
  failures += Expect(raster.CountCells(core::kObstacleWater) == 0 && raster.Bytes() == 1600,
                     "raster: the village and the brook flag nothing; 40 x 40 cells, a byte each");
  failures += Expect(raster.FlagsAt({.x = 150.0F, .y = 150.0F}) == core::kObstacleForest &&
                         raster.FlagsAt({.x = 50.0F, .y = 150.0F}) == 0 &&
                         raster.FlagsAt({.x = -1.0F, .y = 150.0F}) == 0,
                     "raster: forest inside, clear outside, nothing off the map");
  return failures;
}

/// The tracer's test map: 1000 m square, 2.5 m cells, and what each case
/// adds to it.
struct TraceBench {
  core::MapObstacles obstacles;
  std::vector<core::RoadFord> fords;
  std::vector<std::vector<core::Vec2>> village;
  std::vector<core::RoadUnitDisc> units;
  core::RoadTable roads;

  core::RoadDraftResult Trace(const core::RoadDraft& draft, bool asphalt_open = false) const {
    const core::ObstacleRaster raster(obstacles, 1000.0F, 2.5F);
    core::RoadTraceSite site;
    site.raster = &raster;
    site.fords = fords;
    site.village = village;
    site.units = units;
    site.roads = &roads;
    site.map_side_m = 1000.0F;
    site.timber_m3_per_ha = 30.0F;
    for (std::size_t surface = 0; surface < core::kRoadSurfaceSlots; ++surface) {
      site.costs[surface].open = surface <= 2 || asphalt_open;
    }
    site.costs[2].man_days_per_100m = 60.0F;
    site.costs[2].materials_per_100m = {0, 2000};
    return core::TraceRoad(site, draft);
  }
};

core::RoadDraft Draft(core::RoadSurface surface, std::initializer_list<core::Vec2> points) {
  core::RoadDraft draft;
  draft.kind = surface == core::RoadSurface::kNone ? core::RoadKind::kPath : core::RoadKind::kRoad;
  draft.surface = surface;
  for (const core::Vec2& point : points) {
    draft.points[draft.point_count++] = point;
  }
  return draft;
}

bool HasBlock(const core::RoadDraftResult& result, core::RoadDraftRefusal refusal) {
  return std::ranges::any_of(result.blocks, [refusal](const core::RoadDraftBlock& block) {
    return block.refusal == refusal;
  });
}

/// THE SELECTION OF PIECES (road_pieces.h; delivery 7d). The network: a
/// trunk the map keeps (removable 0) along y = 0 with its way out at x = 0;
/// a branch north from the trunk at x = 400 up to y = 600; a spur east from
/// the branch at y = 300 to x = 700, a unit at its end; a unit at the
/// branch's top; a map road east from x = 1000 to a way out at x = 1500.
int TestRoadPieces() {
  int failures = 0;
  core::RoadTable roads;
  const auto road = [&roads](std::vector<core::RoadPoint> axis,
                             std::uint8_t removable,
                             core::RoadSurface surface = core::RoadSurface::kDirt,
                             core::RoadKind kind = core::RoadKind::kRoad) {
    core::RoadRow row;
    row.kind = kind;
    row.surface = surface;
    row.removable = removable;
    row.axis = std::move(axis);
    return core::AppendRow(roads, row);
  };
  const core::RoadId trunk =
      road({{.position = {.x = 0.0F, .y = 0.0F}, .mark = core::RoadMark::kBorder},
            {.position = {.x = 1000.0F, .y = 0.0F}}},
           0);
  const core::RoadId branch =
      road({{.position = {.x = 400.0F, .y = 0.0F}}, {.position = {.x = 400.0F, .y = 600.0F}}}, 1);
  const core::RoadId spur =
      road({{.position = {.x = 400.0F, .y = 300.0F}}, {.position = {.x = 700.0F, .y = 300.0F}}}, 1);
  core::RoadRow east_row;
  east_row.removable = 1;
  east_row.map_road = core::MapRoadId{3};
  east_row.axis = {{.position = {.x = 1000.0F, .y = 0.0F}},
                   {.position = {.x = 1500.0F, .y = 0.0F}, .mark = core::RoadMark::kBorder}};
  const core::RoadId east = core::AppendRow(roads, east_row);
  const std::vector<core::RoadAnchorUnit> units = {
      {.unit = core::UnitId{5}, .position = {.x = 705.0F, .y = 305.0F}},
      {.unit = core::UnitId{6}, .position = {.x = 400.0F, .y = 610.0F}}};
  core::RoadPieceSite site;
  site.roads = &roads;
  site.units = units;
  site.costs[static_cast<std::size_t>(core::RoadSurface::kGravel)].open = true;
  site.costs[static_cast<std::size_t>(core::RoadSurface::kGravel)].man_days_per_100m = 60.0F;
  const auto select =
      [&site](core::RoadId on, core::Vec2 from, core::Vec2 to, core::RoadOperation operation) {
        return core::SelectRoadPiecesOn(
            site, core::RoadSelection{.road = on, .from = from, .to = to}, operation);
      };
  const core::RoadOperation demolish = core::RoadOperation::kDemolish;

  // Snapping: a drag from 20 m to 200 m up the branch's first piece (0-300)
  // runs back to the joint at 0 (a 20 m remnant) and is cut at 200 (100 m).
  const core::RoadPieces snapped = select(branch,
                                          {.x = 400.0F, .y = 20.0F},
                                          {.x = 400.0F, .y = 200.0F},
                                          core::RoadOperation::kUpgradeToGravel);
  failures += Expect(snapped.pieces.size() == 1 && snapped.pieces[0].s_from_m == 0.0F &&
                         std::abs(snapped.pieces[0].s_to_m - 200.0F) < 0.01F &&
                         snapped.pieces[0].refusal == core::RoadPieceRefusal::kNone &&
                         std::abs(snapped.estimate.man_days - 120.0F) < 0.01F,
                     "pieces: a 20 m remnant runs to the joint, a 100 m one is cut; gravel on "
                     "200 m is 120 man-days");
  // The branch's lower piece is the only way for both units: refused, the
  // unit named. The spur alone strands the unit at its end.
  const core::RoadPieces lower =
      select(branch, {.x = 400.0F, .y = 60.0F}, {.x = 400.0F, .y = 240.0F}, demolish);
  const core::RoadPieces spur_only =
      select(spur, {.x = 400.0F, .y = 300.0F}, {.x = 700.0F, .y = 300.0F}, demolish);
  failures += Expect(lower.pieces.size() == 1 &&
                         lower.pieces[0].refusal == core::RoadPieceRefusal::kOnlyRoad &&
                         lower.pieces[0].stranded_unit.value != 0 && spur_only.pieces.size() == 1 &&
                         spur_only.pieces[0].refusal == core::RoadPieceRefusal::kOnlyRoad &&
                         spur_only.pieces[0].stranded_unit.value == 5,
                     "pieces: the only road to a unit is not taken, and the unit is named");
  failures += Expect(select(trunk, {.x = 100.0F, .y = 0.0F}, {.x = 300.0F, .y = 0.0F}, demolish)
                             .pieces[0]
                             .refusal == core::RoadPieceRefusal::kStartRoad,
                     "pieces: a road the map keeps is not taken");
  const core::RoadPieces way_out =
      select(east, {.x = 1000.0F, .y = 0.0F}, {.x = 1500.0F, .y = 0.0F}, demolish);
  failures += Expect(way_out.pieces.size() == 1 &&
                         way_out.pieces[0].refusal == core::RoadPieceRefusal::kOnlyRoad &&
                         way_out.pieces[0].stranded_map_road.value == 3,
                     "pieces: the only road to a way out is not taken, and its map road named");
  // The pair: a second road to the spur's unit, down to the trunk — now the
  // spur duplicates it and goes.
  road({{.position = {.x = 700.0F, .y = 300.0F}}, {.position = {.x = 700.0F, .y = 0.0F}}}, 1);
  const core::RoadPieces duplicated =
      select(spur, {.x = 400.0F, .y = 300.0F}, {.x = 700.0F, .y = 300.0F}, demolish);
  failures += Expect(duplicated.pieces.size() == 1 &&
                         duplicated.pieces[0].refusal == core::RoadPieceRefusal::kNone,
                     "pieces: a road another duplicates is taken");
  // A map road's dead end leads somewhere (STUB until the places are
  // exported): its last way is kept. The same dead end laid by the player is
  // not a place, and goes.
  core::RoadRow map_dead_end;
  map_dead_end.removable = 1;
  map_dead_end.map_road = core::MapRoadId{8};
  map_dead_end.axis = {{.position = {.x = 200.0F, .y = 0.0F}},
                       {.position = {.x = 200.0F, .y = -300.0F}}};
  const core::RoadId map_end = core::AppendRow(roads, map_dead_end);
  core::RoadRow player_dead_end = map_dead_end;
  player_dead_end.origin = core::RoadOrigin::kPlayer;
  player_dead_end.map_road = core::MapRoadId{};
  player_dead_end.axis = {{.position = {.x = 900.0F, .y = 0.0F}},
                          {.position = {.x = 900.0F, .y = -300.0F}}};
  const core::RoadId player_end = core::AppendRow(roads, player_dead_end);
  const core::RoadPieces to_place =
      select(map_end, {.x = 200.0F, .y = 0.0F}, {.x = 200.0F, .y = -300.0F}, demolish);
  const core::RoadPieces to_nothing =
      select(player_end, {.x = 900.0F, .y = 0.0F}, {.x = 900.0F, .y = -300.0F}, demolish);
  failures +=
      Expect(to_place.pieces.size() == 1 &&
                 to_place.pieces[0].refusal == core::RoadPieceRefusal::kOnlyRoad &&
                 to_place.pieces[0].stranded_map_road.value == 8 && to_nothing.pieces.size() == 1 &&
                 to_nothing.pieces[0].refusal == core::RoadPieceRefusal::kNone,
             "pieces: a map road's dead end is a place and kept; a player's goes");
  // Upgrades: asphalt before its epoch; a path is no road's step.
  const core::RoadId path =
      road({{.position = {.x = 100.0F, .y = 0.0F}}, {.position = {.x = 100.0F, .y = 200.0F}}},
           1,
           core::RoadSurface::kNone,
           core::RoadKind::kPath);
  failures += Expect(select(branch,
                            {.x = 400.0F, .y = 0.0F},
                            {.x = 400.0F, .y = 300.0F},
                            core::RoadOperation::kUpgradeToAsphalt)
                                 .pieces[0]
                                 .refusal == core::RoadPieceRefusal::kClosedByEpoch &&
                         select(path,
                                {.x = 100.0F, .y = 0.0F},
                                {.x = 100.0F, .y = 200.0F},
                                core::RoadOperation::kUpgradeToGravel)
                                 .pieces[0]
                                 .refusal == core::RoadPieceRefusal::kNotThisStep,
                     "pieces: asphalt before its epoch, and a path upgraded, are refused");
  return failures;
}

/// THE TRACER (road_trace.h; delivery 7b), one rule a case, each with its
/// neighbour that the rule lets through.
int TestRoadTrace() {
  int failures = 0;
  const core::RoadSurface dirt = core::RoadSurface::kDirt;
  const core::RoadSurface gravel = core::RoadSurface::kGravel;
  {
    // Clear ground: a straight dirt road, waved within 5 m, the same twice.
    const TraceBench bench;
    const core::RoadDraft draft =
        Draft(dirt, {{.x = 100.0F, .y = 500.0F}, {.x = 900.0F, .y = 500.0F}});
    const core::RoadDraftResult first = bench.Trace(draft);
    const core::RoadDraftResult again = bench.Trace(draft);
    float stray = 0.0F;
    for (const core::RoadAxisPoint& point : first.axis) {
      stray = std::max(stray, std::abs(point.position.y - 500.0F));
    }
    failures += Expect(first.blocks.empty() && !first.gaps.obstacles_unread &&
                           first.length_m >= 800.0F && first.length_m < 808.0F,
                       "trace: clear ground, no block, 800 m and a little wave");
    failures += Expect(stray > 0.5F && stray <= 5.01F,
                       "trace: the dirt road's wave strays more than 0.5 m and at most 5 m");
    bool same = first.axis.size() == again.axis.size();
    for (std::size_t index = 0; same && index < first.axis.size(); ++index) {
      same = first.axis[index].position.x == again.axis[index].position.x &&
             first.axis[index].position.y == again.axis[index].position.y;
    }
    failures += Expect(same, "trace: the same draft traces to the bit");
    failures += Expect(
        first.carriageway_m == 8.0F && first.clearing_m == 0.0F && first.estimate.man_days == 0.0F,
        "trace: an 8 m bed, and dirt costs nothing");
  }
  {
    // Forest across the line: a red span where it is, from ~300 to ~400 m.
    TraceBench bench;
    bench.obstacles.areas.push_back(
        SquareArea(core::MapAreaKind::kForest, 400.0F, 0.0F, 500.0F, 1000.0F));
    const core::RoadDraftResult result =
        bench.Trace(Draft(dirt, {{.x = 100.0F, .y = 500.0F}, {.x = 900.0F, .y = 500.0F}}));
    const bool one_span =
        result.blocks.size() == 1 && result.blocks[0].refusal == core::RoadDraftRefusal::kForest &&
        result.blocks[0].s_from_m > 290.0F && result.blocks[0].s_from_m < 305.0F &&
        result.blocks[0].s_to_m > 395.0F && result.blocks[0].s_to_m < 410.0F;
    failures += Expect(one_span, "trace: two points go round nothing — the forest is one red span");
    // Three points round a wall that has a way past: the curve finds it.
    TraceBench pond;
    pond.obstacles.areas.push_back(
        SquareArea(core::MapAreaKind::kPond, 280.0F, 540.0F, 320.0F, 640.0F));
    const core::RoadDraftResult round = pond.Trace(Draft(
        dirt,
        {{.x = 100.0F, .y = 500.0F}, {.x = 500.0F, .y = 700.0F}, {.x = 900.0F, .y = 500.0F}}));
    bool through_middle = false;
    for (const core::RoadAxisPoint& point : round.axis) {
      through_middle = through_middle || (point.position.x == 500.0F && point.position.y == 700.0F);
    }
    failures += Expect(round.blocks.empty() && through_middle,
                       "trace: three points go round the pond and still through the middle point");
    // And a wall with no way past inside the band: kNoWayRound, never a
    // road quietly led somewhere else.
    TraceBench wall;
    wall.obstacles.areas.push_back(
        SquareArea(core::MapAreaKind::kForest, 280.0F, 0.0F, 320.0F, 1000.0F));
    const core::RoadDraftResult stuck = wall.Trace(Draft(
        dirt,
        {{.x = 100.0F, .y = 500.0F}, {.x = 500.0F, .y = 700.0F}, {.x = 900.0F, .y = 500.0F}}));
    failures += Expect(HasBlock(stuck, core::RoadDraftRefusal::kNoWayRound) &&
                           HasBlock(stuck, core::RoadDraftRefusal::kForest),
                       "trace: a wall across the band — no way round, and the forest said");
  }
  {
    // The river: crossed at the ford, refused away from it.
    TraceBench bench;
    bench.obstacles.lines.push_back(core::MapLineDef{
        .key = "river",
        .kind = core::MapLineKind::kRiver,
        .points = {{.position = {.x = 0.0F, .y = 300.0F}, .half_width_m = 10.0F},
                   {.position = {.x = 1000.0F, .y = 300.0F}, .half_width_m = 10.0F}}});
    bench.fords.push_back(core::RoadFord{.position = {.x = 500.0F, .y = 300.0F}, .reach_m = 20.0F});
    failures += Expect(
        bench.Trace(Draft(dirt, {{.x = 500.0F, .y = 100.0F}, {.x = 500.0F, .y = 500.0F}}))
                .blocks.empty() &&
            HasBlock(
                bench.Trace(Draft(dirt, {{.x = 200.0F, .y = 100.0F}, {.x = 200.0F, .y = 500.0F}})),
                core::RoadDraftRefusal::kRiverNoCrossing),
        "trace: the river crossed at the ford, refused 300 m from it");
  }
  {
    // The floodplain: dirt crosses it, gravel does not.
    TraceBench bench;
    bench.obstacles.areas.push_back(
        SquareArea(core::MapAreaKind::kFloodplain, 400.0F, 0.0F, 600.0F, 1000.0F));
    const core::RoadDraft line =
        Draft(dirt, {{.x = 100.0F, .y = 500.0F}, {.x = 900.0F, .y = 500.0F}});
    core::RoadDraft paved = line;
    paved.surface = gravel;
    failures += Expect(bench.Trace(line).blocks.empty() &&
                           HasBlock(bench.Trace(paved), core::RoadDraftRefusal::kFloodplain),
                       "trace: the floodplain takes dirt and refuses gravel");
  }
  {
    // A grove: in dirt's way, cleared by gravel — and the estimate counts it.
    TraceBench bench;
    bench.obstacles.areas.push_back(
        SquareArea(core::MapAreaKind::kGrove, 400.0F, 0.0F, 500.0F, 1000.0F));
    const core::RoadDraft line =
        Draft(dirt, {{.x = 100.0F, .y = 500.0F}, {.x = 900.0F, .y = 500.0F}});
    core::RoadDraft paved = line;
    paved.surface = gravel;
    const core::RoadDraftResult cleared = bench.Trace(paved);
    // ~108 m of trees across the bed's width (100 m plus half a bed each
    // side) times 8 m: ~0.086 ha, 2.6 m³ at 30 a hectare; 800 m of gravel is
    // eight hundreds: 480 man-days and 16000 g of the second material; and
    // the clearing's 150 man-days a hectare on top (boss [66]), ~13 more.
    failures += Expect(
        HasBlock(bench.Trace(line), core::RoadDraftRefusal::kTrees) && cleared.blocks.empty() &&
            cleared.clearing_m == 8.0F && cleared.estimate.clearing_ha > 0.080F &&
            cleared.estimate.clearing_ha < 0.092F &&
            std::abs(cleared.estimate.timber_m3 - (cleared.estimate.clearing_ha * 30.0F)) < 1e-4F &&
            std::abs(cleared.estimate.man_days - (60.0F * cleared.length_m / 100.0F) -
                     (cleared.estimate.clearing_ha * 150.0F)) < 0.01F &&
            cleared.estimate.man_days > 490.0F && cleared.estimate.man_days < 500.0F &&
            cleared.estimate.materials.size() == 2 && cleared.estimate.materials[1] > 15990 &&
            cleared.estimate.materials[1] < 16200,
        "trace: the grove stops dirt; gravel clears it, and the estimate counts it");
  }
  {
    // The epoch, a unit's circle, bad points, off the map.
    TraceBench bench;
    bench.units.push_back(
        core::RoadUnitDisc{.centre = {.x = 500.0F, .y = 520.0F}, .radius_m = 25.0F});
    const core::RoadDraftResult asphalt = bench.Trace(Draft(
        core::RoadSurface::kAsphalt, {{.x = 100.0F, .y = 100.0F}, {.x = 900.0F, .y = 100.0F}}));
    failures += Expect(asphalt.blocks.size() == 1 &&
                           asphalt.blocks[0].refusal == core::RoadDraftRefusal::kClosedByEpoch &&
                           asphalt.blocks[0].s_to_m == asphalt.length_m,
                       "trace: asphalt before its epoch is refused along the whole axis");
    failures += Expect(
        HasBlock(bench.Trace(Draft(dirt, {{.x = 100.0F, .y = 500.0F}, {.x = 900.0F, .y = 500.0F}})),
                 core::RoadDraftRefusal::kUnit) &&
            bench.Trace(Draft(dirt, {{.x = 100.0F, .y = 440.0F}, {.x = 900.0F, .y = 440.0F}}))
                .blocks.empty(),
        "trace: a unit's circle refuses the road through it, not the one 80 m off");
    core::RoadDraft one_point = Draft(dirt, {{.x = 100.0F, .y = 100.0F}});
    core::RoadDraft path_with_bed =
        Draft(dirt, {{.x = 100.0F, .y = 100.0F}, {.x = 200.0F, .y = 100.0F}});
    path_with_bed.kind = core::RoadKind::kPath;
    const core::RoadDraftResult bad = bench.Trace(one_point);
    failures += Expect(
        bad.blocks.size() == 1 && bad.blocks[0].refusal == core::RoadDraftRefusal::kBadPoints &&
            bad.axis.empty() &&
            bench.Trace(path_with_bed).blocks[0].refusal == core::RoadDraftRefusal::kBadPoints,
        "trace: one point, or a path with a bed, is no draft");
    failures += Expect(
        HasBlock(bench.Trace(Draft(dirt, {{.x = -50.0F, .y = 100.0F}, {.x = 300.0F, .y = 100.0F}})),
                 core::RoadDraftRefusal::kOutsideMap),
        "trace: off the map is refused");
  }
  {
    // The ends: onto a junction within 20 m, onto a road's axis within 12 m.
    TraceBench bench;
    core::RoadRow road;
    road.axis = {
        core::RoadPoint{.position = {.x = 0.0F, .y = 700.0F}},
        core::RoadPoint{.position = {.x = 600.0F, .y = 700.0F}, .mark = core::RoadMark::kJunction},
        core::RoadPoint{.position = {.x = 1000.0F, .y = 700.0F}}};
    const core::RoadId road_id = core::AppendRow(bench.roads, road);
    const core::RoadDraftResult snapped =
        bench.Trace(Draft(dirt, {{.x = 590.0F, .y = 705.0F}, {.x = 300.0F, .y = 708.0F}}));
    failures +=
        Expect(snapped.start.snap == core::RoadEndSnap::kJunction &&
                   snapped.start.road.value == road_id.value && snapped.start.road_s_m == 600.0F &&
                   snapped.end.snap == core::RoadEndSnap::kRoad &&
                   std::abs(snapped.end.road_s_m - 300.0F) < 0.01F &&
                   std::abs(snapped.end.point.y - 700.0F) < 0.01F &&
                   snapped.axis.front().mark == core::RoadMark::kJunction,
               "trace: an end takes the junction 10 m off, the other the axis 8 m off");
    const core::RoadDraftResult free =
        bench.Trace(Draft(dirt, {{.x = 300.0F, .y = 100.0F}, {.x = 300.0F, .y = 400.0F}}));
    failures += Expect(
        free.start.snap == core::RoadEndSnap::kFree && free.end.snap == core::RoadEndSnap::kFree,
        "trace: ends far from any road stay free");
    // Both ends snapped onto one junction: no road of nought metres passed
    // clean (static review of 0.36.26), but the draft's own refusal.
    const core::RoadDraftResult collapsed =
        bench.Trace(Draft(dirt, {{.x = 595.0F, .y = 705.0F}, {.x = 606.0F, .y = 703.0F}}));
    failures += Expect(collapsed.blocks.size() == 1 &&
                           collapsed.blocks[0].refusal == core::RoadDraftRefusal::kBadPoints &&
                           collapsed.axis.empty(),
                       "trace: two ends snapped onto one junction are no draft");
    // Along the laid road (5 m off its axis, beds overlapping for 800 m):
    // kAlongRoad. Across it, square: no. Into it, an end joining: no.
    const auto along_of = [&bench](core::Vec2 from, core::Vec2 to) {
      return HasBlock(bench.Trace(Draft(dirt, {from, to})), core::RoadDraftRefusal::kAlongRoad);
    };
    failures += Expect(along_of({.x = 100.0F, .y = 705.0F}, {.x = 900.0F, .y = 705.0F}) &&
                           !along_of({.x = 300.0F, .y = 500.0F}, {.x = 300.0F, .y = 900.0F}) &&
                           !along_of({.x = 450.0F, .y = 500.0F}, {.x = 450.0F, .y = 699.0F}),
                       "trace: along a laid road is refused; across it or into it is a junction");
  }
  {
    // Asphalt with walks: in the village and nowhere else.
    TraceBench bench;
    bench.village.push_back({{.x = 0.0F, .y = 0.0F},
                             {.x = 300.0F, .y = 0.0F},
                             {.x = 300.0F, .y = 300.0F},
                             {.x = 0.0F, .y = 300.0F}});
    const core::RoadSurface walks = core::RoadSurface::kAsphaltWalks;
    failures += Expect(
        bench.Trace(Draft(walks, {{.x = 50.0F, .y = 100.0F}, {.x = 250.0F, .y = 100.0F}}), true)
                .blocks.empty() &&
            HasBlock(
                bench.Trace(Draft(walks, {{.x = 50.0F, .y = 100.0F}, {.x = 600.0F, .y = 100.0F}}),
                            true),
                core::RoadDraftRefusal::kOutsideVillage),
        "trace: walks inside the village, refused past its edge");
  }
  return failures;
}

/// THE ROADS AS THE LAYER DRAWS THEM (road_view.h; delivery 7a): a map road
/// of three points with a bridge mark and worn stretches, and a player's
/// path. Each view carries the row's id and fields, `s` running from nought
/// and ending at the very length the graph measures, the marks, the wear —
/// and no work, which comes with 7e.
int TestRoadViews() {
  int failures = 0;
  core::RoadTable roads;
  core::RoadRow trunk;
  trunk.surface = core::RoadSurface::kDirt;
  trunk.map_road = core::MapRoadId{2};
  trunk.removable = 0;
  trunk.traffic_word = core::RoadTrafficWord::kAlmostNone;
  trunk.axis = {
      core::RoadPoint{.position = {.x = 0.0F, .y = 0.0F}},
      core::RoadPoint{.position = {.x = 30.0F, .y = 40.0F}, .mark = core::RoadMark::kBridge},
      core::RoadPoint{.position = {.x = 30.0F, .y = 100.0F}}};
  trunk.stretches = {core::RoadStretch{.wear_pct = 35.0F},
                     core::RoadStretch{.wear_pct = 60.0F},
                     core::RoadStretch{.wear_pct = 10.0F},
                     core::RoadStretch{.wear_pct = 0.0F},
                     core::RoadStretch{.wear_pct = 5.0F}};
  const core::RoadId trunk_id = core::AppendRow(roads, trunk);
  core::RoadRow path;
  path.kind = core::RoadKind::kPath;
  path.surface = core::RoadSurface::kNone;
  path.origin = core::RoadOrigin::kPlayer;
  path.axis = {core::RoadPoint{.position = {.x = 200.0F, .y = 0.0F}},
               core::RoadPoint{.position = {.x = 200.0F, .y = 20.0F}}};
  path.stretches = {core::RoadStretch{}};
  core::AppendRow(roads, path);

  const std::vector<core::RoadView> views = core::RoadViews(roads);
  failures += Expect(views.size() == 2, "road views: one a road");
  if (views.size() != 2) {
    return failures;
  }
  const core::RoadView& first = views[0];
  failures +=
      Expect(first.road.value == trunk_id.value && first.kind == core::RoadKind::kRoad &&
                 first.surface == core::RoadSurface::kDirt &&
                 first.origin == core::RoadOrigin::kMap && first.map_road.value == 2 &&
                 first.removable == 0 && first.traffic_word == core::RoadTrafficWord::kAlmostNone,
             "road views: the row's id, kind, surface, origin, map road, removability "
             "and traffic word");
  failures += Expect(first.axis.size() == 3 && first.axis[0].s_m == 0.0F &&
                         first.axis[1].s_m == 50.0F && first.axis[2].s_m == 110.0F &&
                         first.axis[2].s_m == core::RoadAxisLength(trunk.axis),
                     "road views: `s` runs from nought to the graph's own length (50, 110)");
  failures += Expect(first.axis[1].mark == core::RoadMark::kBridge &&
                         first.axis[0].mark == core::RoadMark::kNone &&
                         first.axis[1].position.x == 30.0F && first.axis[1].position.y == 40.0F,
                     "road views: each point keeps its position and its mark");
  failures += Expect(first.wear_pct == std::vector<float>{35.0F, 60.0F, 10.0F, 0.0F, 5.0F},
                     "road views: the stretches' wear, in order");
  failures += Expect(first.works.empty() && views[1].works.empty(),
                     "road views: no work on any road until 7e");
  failures += Expect(views[1].kind == core::RoadKind::kPath &&
                         views[1].surface == core::RoadSurface::kNone &&
                         views[1].origin == core::RoadOrigin::kPlayer &&
                         views[1].axis[1].s_m == 20.0F && views[1].wear_pct.size() == 1,
                     "road views: and the player's path as it is");
  return failures;
}

/// THE ROAD GRAPH (road_graph.h; 0.36.0): a trunk from (0,0) to (1000,0), a
/// spur leaving it at x = 400 — its first point ON the trunk's axis, not at
/// a vertex, as the map draws junctions — and a second spur from the
/// trunk's end over a bridge. And one road far away, alone.
int TestRoadGraph() {
  int failures = 0;
  const auto road = [](std::vector<core::RoadPoint> axis) {
    core::RoadRow row;
    row.axis = std::move(axis);
    return row;
  };
  core::RoadTable roads;
  core::AppendRow(
      roads,
      road({core::RoadPoint{.position = {.x = 0.0F, .y = 0.0F}, .mark = core::RoadMark::kBorder},
            core::RoadPoint{.position = {.x = 500.0F, .y = 0.0F}},
            core::RoadPoint{.position = {.x = 1000.0F, .y = 0.0F}}}));
  core::AppendRow(roads,
                  road({core::RoadPoint{.position = {.x = 400.0F, .y = 0.5F}},
                        core::RoadPoint{.position = {.x = 400.0F, .y = 300.0F}}}));
  core::AppendRow(
      roads,
      road({core::RoadPoint{.position = {.x = 1000.0F, .y = 0.0F}},
            core::RoadPoint{.position = {.x = 1100.0F, .y = 0.0F}, .mark = core::RoadMark::kBridge},
            core::RoadPoint{.position = {.x = 1200.0F, .y = 0.0F}}}));
  core::AppendRow(roads,
                  road({core::RoadPoint{.position = {.x = 5000.0F, .y = 5000.0F}},
                        core::RoadPoint{.position = {.x = 5100.0F, .y = 5000.0F}}}));
  const core::RoadGraph graph = core::BuildRoadGraph(roads);
  // Nodes: trunk ends 2, the mouth of the first spur 1 (its first point),
  // its far end 1, the second spur's far end 1 (its first point IS the
  // trunk's end), the lone road's two ends 2: seven. Edges: the trunk cut
  // in two at x = 400, the first spur, the second spur, the lone road: five.
  failures += Expect(graph.nodes.size() == 7 && graph.edges.size() == 5,
                     "road graph: seven nodes and five edges, the trunk cut at the mouth");
  float trunk_pieces = 0.0F;
  bool cut_at_400 = false;
  for (const core::RoadEdge& edge : graph.edges) {
    if (edge.road.value == roads.row_ids[0].value) {
      trunk_pieces += edge.length_m;
      cut_at_400 = cut_at_400 || std::abs(edge.to_chainage_m - 400.0F) < 0.01F;
    }
  }
  failures += Expect(std::abs(trunk_pieces - 1000.0F) < 0.01F && cut_at_400,
                     "road graph: the trunk's two pieces are its 1000 m, cut at 400");
  const std::vector<std::uint32_t> components = core::RoadComponents(graph);
  const std::uint32_t networks = *std::ranges::max_element(components) + 1U;
  failures += Expect(networks == 2,
                     "road graph: the trunk and its spurs are one network, the "
                     "lone road another");
  std::uint32_t borders = 0;
  for (const core::RoadNode& node : graph.nodes) {
    borders += node.border ? 1U : 0U;
  }
  std::uint32_t bridges = 0;
  for (const core::RoadEdge& edge : graph.edges) {
    bridges += edge.bridge ? 1U : 0U;
  }
  failures += Expect(borders == 1 && bridges == 1,
                     "road graph: the border end is a way out, the bridge piece is marked");
  // The pair to the join: a spur whose first point is 3 m off the trunk does
  // not join it at the default tolerance of 2 m — and does at 5.
  core::RoadTable apart;
  core::AppendRow(apart, roads.rows[0]);
  core::AppendRow(apart,
                  road({core::RoadPoint{.position = {.x = 400.0F, .y = 3.0F}},
                        core::RoadPoint{.position = {.x = 400.0F, .y = 300.0F}}}));
  const core::RoadGraph loose = core::BuildRoadGraph(apart);
  const core::RoadGraph joined = core::BuildRoadGraph(apart, 5.0F);
  failures += Expect(loose.edges.size() == 2 && joined.edges.size() == 3,
                     "road graph: an end 3 m off the axis joins at 5 m of tolerance, not at 2");
  // THE CROSS (0.36.0: the village lanes cross the street in the middle):
  // a road across the trunk at x = 700 is cut there, and cuts the trunk. The
  // pair: the same road moved to stop 10 m short of the trunk joins nothing.
  core::RoadTable crossing;
  core::AppendRow(crossing, roads.rows[0]);
  core::AppendRow(crossing,
                  road({core::RoadPoint{.position = {.x = 700.0F, .y = -100.0F}},
                        core::RoadPoint{.position = {.x = 700.0F, .y = 100.0F}}}));
  const core::RoadGraph cross = core::BuildRoadGraph(crossing);
  core::RoadTable short_of_it;
  core::AppendRow(short_of_it, roads.rows[0]);
  core::AppendRow(short_of_it,
                  road({core::RoadPoint{.position = {.x = 700.0F, .y = 10.0F}},
                        core::RoadPoint{.position = {.x = 700.0F, .y = 100.0F}}}));
  const core::RoadGraph apart_road = core::BuildRoadGraph(short_of_it);
  failures += Expect(cross.nodes.size() == 5 && cross.edges.size() == 4 &&
                         apart_road.nodes.size() == 4 && apart_road.edges.size() == 2,
                     "road graph: a road across the trunk is a crossing cut into both; one that "
                     "stops short of it joins nothing");
  // THREE CASES OF THE STATIC REVIEW OF 0.36.0, each with its pair.
  // (a) A LOOP — a square lane leaving (0,0) and coming back to it — is one
  //     node and one edge, not nothing.
  core::RoadTable loop;
  core::AppendRow(loop,
                  road({core::RoadPoint{.position = {.x = 0.0F, .y = 0.0F}},
                        core::RoadPoint{.position = {.x = 100.0F, .y = 0.0F}},
                        core::RoadPoint{.position = {.x = 100.0F, .y = 100.0F}},
                        core::RoadPoint{.position = {.x = 0.0F, .y = 100.0F}},
                        core::RoadPoint{.position = {.x = 0.0F, .y = 0.0F}}}));
  const core::RoadGraph ring = core::BuildRoadGraph(loop);
  failures += Expect(ring.nodes.size() == 1 && ring.edges.size() == 1 &&
                         std::abs(ring.edges[0].length_m - 400.0F) < 0.01F,
                     "road graph: a loop is one node and its 400 m edge");
  // (b) A SPUR ENDING 1.5 m off the trunk and 1.5 m short of its end — 2.1 m
  //     from the trunk's end, too far for the ends to merge — still joins.
  core::RoadTable near_end;
  core::AppendRow(near_end, roads.rows[0]);
  core::AppendRow(near_end,
                  road({core::RoadPoint{.position = {.x = 998.5F, .y = 1.5F}},
                        core::RoadPoint{.position = {.x = 998.5F, .y = 200.0F}}}));
  const core::RoadGraph spur_near_end = core::BuildRoadGraph(near_end);
  const std::vector<std::uint32_t> near_components = core::RoadComponents(spur_near_end);
  failures +=
      Expect(*std::ranges::max_element(near_components) == 0,
             "road graph: a spur ending 2.1 m from the trunk's end is joined, not an island");
  // (c) A DECK from x = 100 to 300, the road cut at x = 50, 150 and 250 by
  //     crossings: 50-150, 150-250 and 250-400 overlap the deck and are
  //     bridges, 0-50 is not. 150-250 lies WHOLLY inside the deck with no
  //     bridge vertex in it — the one the old vertex rule missed (its first
  //     draft cut at 50 and 200, where the old rule gave the same answer and
  //     the fault did not redden: a miss, named).
  core::RoadTable deck;
  core::AppendRow(
      deck,
      road({core::RoadPoint{.position = {.x = 0.0F, .y = 0.0F}},
            core::RoadPoint{.position = {.x = 100.0F, .y = 0.0F}, .mark = core::RoadMark::kBridge},
            core::RoadPoint{.position = {.x = 300.0F, .y = 0.0F}, .mark = core::RoadMark::kBridge},
            core::RoadPoint{.position = {.x = 400.0F, .y = 0.0F}}}));
  for (const float x : {50.0F, 150.0F, 250.0F}) {
    core::AppendRow(deck,
                    road({core::RoadPoint{.position = {.x = x, .y = -50.0F}},
                          core::RoadPoint{.position = {.x = x, .y = 50.0F}}}));
  }
  const core::RoadGraph decked = core::BuildRoadGraph(deck);
  std::uint32_t deck_pieces = 0;
  std::uint32_t road_pieces = 0;
  for (const core::RoadEdge& edge : decked.edges) {
    if (edge.road.value == deck.row_ids[0].value) {
      ++road_pieces;
      deck_pieces += edge.bridge ? 1U : 0U;
    }
  }
  failures += Expect(road_pieces == 4 && deck_pieces == 3,
                     "road graph: of four pieces, the three overlapping the deck are bridges");
  // The helpers the graph stands on.
  const core::AxisProjection foot =
      core::ProjectOntoAxis(roads.rows[0].axis, core::Vec2{.x = 700.0F, .y = 40.0F});
  failures += Expect(
      std::abs(foot.chainage_m - 700.0F) < 0.01F && std::abs(foot.distance_m - 40.0F) < 0.01F,
      "road graph: a point 40 m off the trunk at x = 700 projects there");
  const core::Vec2 at = core::PointAtChainage(roads.rows[2].axis, 150.0F);
  failures += Expect(std::abs(at.x - 1150.0F) < 0.01F && std::abs(at.y) < 0.01F,
                     "road graph: 150 m along the second spur is x = 1150");
  failures += Expect(core::StretchCountForLength(1000.0F) == 40 &&
                         core::StretchCountForLength(1001.0F) == 41 &&
                         core::StretchCountForLength(0.0F) == 1,
                     "road graph: 25 m stretches, the last one shorter, at least one");
  return failures;
}

/// THE ROAD INDEX (road_route.h; 0.36.1): who goes where, and what the way
/// weighs. A trunk (0,0)-(1000,0), a spur from its x = 400 up to (400,300), a
/// PATH from its end (1000,0) up to (1000,500). Off-road weights: the default
/// rules (walk 1.2, cart 2.5).
/// The beds' condition (roads delivery 3; road_rules.h): wet on the rain
/// day, dirt one day more and gravel none, longer in the cold; the mud on
/// dirt and gravel, wet on asphalt; snow over all; frozen without snow.
int TestRoadBeds() {
  int failures = 0;
  const core::RoadRules rules;
  constexpr auto kDirt = static_cast<std::size_t>(core::RoadBed::kDirt);
  constexpr auto kGravel = static_cast<std::size_t>(core::RoadBed::kGravel);
  constexpr auto kAsphalt = static_cast<std::size_t>(core::RoadBed::kAsphalt);
  using core::RoadCondition;
  const core::RoadBeds dry{};
  const core::RoadBeds rain = core::RoadBedsAfter(rules, dry, true, 12.0F, false, false);
  failures += Expect(rain.condition[kDirt] == RoadCondition::kWet &&
                         rain.condition[kGravel] == RoadCondition::kWet &&
                         rain.condition[kAsphalt] == RoadCondition::kWet,
                     "road beds: the rainy day wets every bed");
  const core::RoadBeds after = core::RoadBedsAfter(rules, rain, false, 12.0F, false, false);
  failures += Expect(after.condition[kDirt] == RoadCondition::kWet &&
                         after.condition[kGravel] == RoadCondition::kDry &&
                         after.condition[kAsphalt] == RoadCondition::kDry,
                     "road beds: the day after, dirt is still wet and gravel has dried");
  const core::RoadBeds second = core::RoadBedsAfter(rules, after, false, 12.0F, false, false);
  failures += Expect(second.condition[kDirt] == RoadCondition::kDry,
                     "road beds: and the second day dirt is dry again (road_dry_days_dirt 1)");
  const core::RoadBeds cold_rain = core::RoadBedsAfter(rules, dry, true, 3.0F, false, false);
  const core::RoadBeds cold_1 = core::RoadBedsAfter(rules, cold_rain, false, 3.0F, false, false);
  const core::RoadBeds cold_2 = core::RoadBedsAfter(rules, cold_1, false, 3.0F, false, false);
  const core::RoadBeds cold_3 = core::RoadBedsAfter(rules, cold_2, false, 3.0F, false, false);
  failures += Expect(cold_2.condition[kDirt] == RoadCondition::kWet &&
                         cold_2.condition[kGravel] == RoadCondition::kDry &&
                         cold_3.condition[kDirt] == RoadCondition::kDry,
                     "road beds: below +5 dirt stays wet one day longer, gravel too dries a day "
                     "later — it is wet the day after, not the second");
  const core::RoadBeds mud = core::RoadBedsAfter(rules, dry, false, 8.0F, true, false);
  failures += Expect(mud.condition[kDirt] == RoadCondition::kMud &&
                         mud.condition[kGravel] == RoadCondition::kMud &&
                         mud.condition[kAsphalt] == RoadCondition::kWet,
                     "road beds: the mud season muds dirt and gravel; asphalt only gets wet");
  const core::RoadBeds snow = core::RoadBedsAfter(rules, rain, false, -5.0F, true, true);
  const core::RoadBeds frost = core::RoadBedsAfter(rules, dry, false, -5.0F, false, false);
  failures += Expect(snow.condition[kDirt] == RoadCondition::kSnow &&
                         snow.condition[kAsphalt] == RoadCondition::kSnow &&
                         frost.condition[kDirt] == RoadCondition::kFrozen,
                     "road beds: snow lying covers every bed; frost without snow is the winter "
                     "road");
  failures += Expect(
      core::RoadBedFactor(rules, 0.5F, RoadCondition::kMud, core::RoadBed::kDirt, true) == 0.5F &&
          core::RoadBedFactor(rules, 0.5F, RoadCondition::kMud, core::RoadBed::kGravel, true) ==
              0.95F &&
          core::RoadBedFactor(rules, 0.5F, RoadCondition::kSnow, core::RoadBed::kDirt, false) ==
              0.3F &&
          core::RoadBedFactor(rules, 0.5F, RoadCondition::kSnow, core::RoadBed::kDirt, true) ==
              1.0F,
      "road beds: the mud's factor is mud_speed_factor on dirt and its own on gravel; the "
      "snow slows a wheel and not a sleigh");
  return failures;
}

int TestRoadIndex() {
  int failures = 0;
  const auto line = [](core::RoadKind kind, std::vector<core::Vec2> points) {
    core::RoadRow row;
    row.kind = kind;
    for (const core::Vec2 point : points) {
      row.axis.push_back(core::RoadPoint{.position = point});
    }
    return row;
  };
  core::RoadTable roads;
  core::AppendRow(roads, line(core::RoadKind::kRoad, {{0.0F, 0.0F}, {1000.0F, 0.0F}}));
  core::AppendRow(roads, line(core::RoadKind::kRoad, {{400.0F, 0.0F}, {400.0F, 300.0F}}));
  core::AppendRow(roads, line(core::RoadKind::kPath, {{1000.0F, 0.0F}, {1000.0F, 500.0F}}));
  const auto index = core::BuildRoadIndex(roads);
  const auto near = [](float value, float expected) { return std::abs(value - expected) < 0.002F; };
  // A walker 50 m off the trunk at each end (the far end BELOW the trunk,
  // off the path — the test's first draft put it on the path, and the walker
  // rightly took it for 1.11): the road (0.05 x 1.2 twice + 1.0 = 1.12)
  // beats the straight line across (1.0 x 1.2 = 1.2).
  const float walk = index->EffectiveKm(core::TravelMode::kWalk, {0.0F, 50.0F}, {1000.0F, -50.0F});
  failures += Expect(near(walk, 1.12F),
                     "road index: a walker takes the road when open ground "
                     "weighs more (1.12 against 1.2)");
  // A cart from beside the trunk's start to the spur's end: the trunk to 400,
  // the spur up — 0.05 x 2.5 + 0.4 + 0.3.
  const float cart = index->EffectiveKm(core::TravelMode::kCart, {0.0F, 50.0F}, {400.0F, 300.0F});
  failures += Expect(near(cart, 0.825F), "road index: a cart goes by the roads (0.825)");
  // The PATH is the walker's and not the cart's: to (1000,500) the walker
  // walks the path (0.5), the cart comes the 500 m off the road's end at its
  // off-road weight (1.25).
  const float walk_path =
      index->EffectiveKm(core::TravelMode::kWalk, {1000.0F, 0.0F}, {1000.0F, 500.0F});
  const float cart_path =
      index->EffectiveKm(core::TravelMode::kCart, {1000.0F, 0.0F}, {1000.0F, 500.0F});
  failures += Expect(near(walk_path, 0.5F) && near(cart_path, 1.25F),
                     "road index: a path carries a walker, not a cart (0.5 against 1.25)");
  // A cart is never sent across the whole way: between two points 50 m off
  // the trunk's two ends it goes by the road even when a log cart would not.
  const core::Route cart_way =
      index->Way(core::TravelMode::kCart, {0.0F, 300.0F}, {1000.0F, 300.0F});
  bool legs_add_up = !cart_way.legs.empty();
  float summed = 0.0F;
  for (std::size_t leg = 0; leg < cart_way.legs.size(); ++leg) {
    summed += cart_way.legs[leg].effective_km;
  }
  legs_add_up = legs_add_up && near(summed, cart_way.effective_km) && !cart_way.cart_without_road;
  const float log_cart =
      index->EffectiveKm(core::TravelMode::kLogCart, {0.0F, 300.0F}, {1000.0F, 300.0F});
  failures += Expect(legs_add_up && log_cart <= cart_way.effective_km + 0.001F,
                     "road index: a way's legs add up to its weight, and a log cart may go "
                     "across where the grain cart may not");
  // NO NETWORK AT ALL (a hand-built world): every mode goes the straight line
  // at its own pace, weight 1 — no road for open ground to be slower than —
  // and the cart says it had no road.
  const auto bare = core::BuildRoadIndex(core::RoadTable{});
  const core::Route lost = bare->Way(core::TravelMode::kCart, {0.0F, 0.0F}, {1000.0F, 0.0F});
  failures += Expect(
      lost.cart_without_road && near(lost.effective_km, 1.0F) &&
          near(bare->EffectiveKm(core::TravelMode::kWalk, {0.0F, 0.0F}, {1000.0F, 0.0F}), 1.0F),
      "road index: with no network every mode walks the line at its pace, and a cart says it "
      "had no road");
  // THE WAY'S METRES OFF THE ROAD (0.36.9, the produce cart's book): the
  // same choice EffectiveKm makes, and the pieces that join the network.
  const core::RouteMeasure cart_measure =
      index->Measure(core::TravelMode::kCart, {0.0F, 50.0F}, {400.0F, 300.0F});
  const core::RouteMeasure cart_path_measure =
      index->Measure(core::TravelMode::kCart, {1000.0F, 0.0F}, {1000.0F, 500.0F});
  const core::RouteMeasure walk_measure =
      index->Measure(core::TravelMode::kWalk, {0.0F, 50.0F}, {1000.0F, -50.0F});
  const core::RouteMeasure lost_measure =
      bare->Measure(core::TravelMode::kCart, {0.0F, 0.0F}, {1000.0F, 0.0F});
  std::cout << "road index, metres off the road: cart " << cart_measure.off_road_m
            << ", cart past the path " << cart_path_measure.off_road_m << ", walker "
            << walk_measure.off_road_m << ", cart with no network " << lost_measure.off_road_m
            << '\n';
  failures += Expect(near(cart_measure.effective_km, cart) &&
                         near(cart_path_measure.effective_km, cart_path) &&
                         near(walk_measure.effective_km, walk),
                     "road index: Measure answers EffectiveKm's kilometres for the same way");
  failures += Expect(std::abs(cart_measure.off_road_m - 50.0F) < 0.5F &&
                         std::abs(cart_path_measure.off_road_m - 500.0F) < 0.5F &&
                         std::abs(walk_measure.off_road_m - 100.0F) < 0.5F &&
                         std::abs(lost_measure.off_road_m - 1000.0F) < 0.5F,
                     "road index: the metres off the road are the pieces that join it — 50 to "
                     "the trunk, the 500 past the path's end a cart may not use, 50 + 50 for the "
                     "walker; with no network, the whole line");
  // BY END (0.36.15): the start's piece and the end's, and a way with no road
  // is the start's whole.
  failures +=
      Expect(std::abs(cart_measure.off_road_start_m - 50.0F) < 0.5F &&
                 cart_measure.off_road_end_m < 0.5F && cart_path_measure.off_road_start_m < 0.5F &&
                 std::abs(cart_path_measure.off_road_end_m - 500.0F) < 0.5F &&
                 std::abs(lost_measure.off_road_start_m - 1000.0F) < 0.5F &&
                 lost_measure.off_road_end_m == 0.0F,
             "road index: by end — 50 at the start and none at the spur's end; none at "
             "the road's end and 500 to the place; with no road, all at the start");
  return failures;
}

/// THE HERD'S AGE BAND (herd_age_band.h; 0.35.16): set by the first heads,
/// widened by more, folded when herds are gathered, aged, and cut from the
/// top when the oldest go — the top of a uniform band, by the share gone.
int TestHerdAgeBand() {
  int failures = 0;
  core::HerdRow herd;
  core::WidenAdultAgeBand(herd, 0, 2.0F, 2.0F);
  failures += Expect(herd.adult_age_min_game_years == 2.0F && herd.adult_age_max_game_years == 2.0F,
                     "age band: the first head sets it, stale values or not");
  core::WidenAdultAgeBand(herd, 1, 1.0F, 1.0F);
  core::WidenAdultAgeBand(herd, 2, 4.0F, 4.0F);
  failures += Expect(herd.adult_age_min_game_years == 1.0F && herd.adult_age_max_game_years == 4.0F,
                     "age band: more heads widen it at both ends");
  herd.adult_count = 4;
  core::HerdRow other;
  other.adult_count = 2;
  other.adult_age_min_game_years = 0.5F;
  other.adult_age_max_game_years = 3.0F;
  core::HerdRow stale = herd;
  stale.adult_age_min_game_years = 9.0F;  // a herd with no adults keeps stale numbers
  stale.adult_age_max_game_years = 9.0F;
  core::MergeAdultAgeBand(stale, 0, other);
  failures +=
      Expect(stale.adult_age_min_game_years == 0.5F && stale.adult_age_max_game_years == 3.0F,
             "age band: gathered into a herd with no adults, the band is the newcomers'");
  core::MergeAdultAgeBand(herd, 4, other);
  failures += Expect(herd.adult_age_min_game_years == 0.5F && herd.adult_age_max_game_years == 4.0F,
                     "age band: two herds gathered hold both bands");
  core::AgeAdultAgeBand(herd, 0.5F);
  failures += Expect(herd.adult_age_min_game_years == 1.0F && herd.adult_age_max_game_years == 4.5F,
                     "age band: ageing moves both ends");
  // Four of eight go from the top of 1.0..4.5: the band keeps 1.0..2.75, and
  // the four were 2.75..4.5, a mean of 3.625.
  const float taken = core::CutOldestFromAdultAgeBand(herd, 8, 4);
  failures += Expect(std::abs(taken - 3.625F) < 1.0e-4F && herd.adult_age_min_game_years == 1.0F &&
                         std::abs(herd.adult_age_max_game_years - 2.75F) < 1.0e-4F,
                     "age band: the oldest half goes off the top, and the top comes down");
  core::CutOldestFromAdultAgeBand(herd, 4, 4);
  failures += Expect(herd.adult_age_min_game_years == 0.0F && herd.adult_age_max_game_years == 0.0F,
                     "age band: with every adult gone it is empty");
  return failures;
}

/// The visit's outcome crosses the seam packed into one amount: every field
/// comes back, and an amount that is not a visit's is refused rather than read
/// as a face that does not exist.
int TestDistrictVisitPacking() {
  int failures = 0;
  // Every field off its default, and each a different value, so a byte
  // written into the wrong place or not at all comes back different.
  const core::DistrictVisitOutcome sent{.face = core::DistrictFace::kZhernova,
                                        .kind = core::DistrictVisitKind::kGift,
                                        .found = core::DistrictVisitFinding::kJuniorMiss,
                                        .miss_by = core::DistrictFace::kPolushkina,
                                        .has_miss_by = true,
                                        .gift = core::DistrictGiftOutcome::kReturned};
  core::DistrictVisitOutcome read;
  failures +=
      Expect(core::UnpackDistrictVisit(core::PackDistrictVisit(sent), read) &&
                 read.face == sent.face && read.kind == sent.kind && read.found == sent.found &&
                 read.miss_by == sent.miss_by && read.has_miss_by && read.gift == sent.gift,
             "district visit: every field survives the packing");
  // Korenev is face 0, so an empty miss_by must not read back as "Korenev's".
  core::DistrictVisitOutcome nobody;
  failures += Expect(core::UnpackDistrictVisit(core::PackDistrictVisit(core::DistrictVisitOutcome{
                                                   .face = core::DistrictFace::kKarasev,
                                                   .kind = core::DistrictVisitKind::kRegular}),
                                               nobody) &&
                         !nobody.has_miss_by,
                     "district visit: an empty miss_by is empty, not Korenev");
  core::DistrictVisitOutcome untouched;
  constexpr std::int64_t kSixthFace = 5;
  constexpr std::int64_t kMissBySeventh = std::int64_t{6} << 24U;
  constexpr std::int64_t kSixthByte = std::int64_t{1} << 40U;
  failures += Expect(!core::UnpackDistrictVisit(-1, untouched) &&
                         !core::UnpackDistrictVisit(kSixthFace, untouched) &&
                         !core::UnpackDistrictVisit(kMissBySeventh, untouched) &&
                         !core::UnpackDistrictVisit(kSixthByte, untouched),
                     "district visit: a negative amount, a face past the five, a miss_by past "
                     "them and a byte past the gift are not visits");
  return failures;
}

/// WHICH ID NAMES AN ALARM'S SUBJECT (alarm_state.cpp, AlarmSubjectValue).
/// The boundary sorts the day's alarms by it (session.cpp), so a kind that
/// answers with the wrong field sorts by a number that belongs to somebody
/// else — and the layer, which reads the sorted list, cannot tell.
///
/// UNTESTED UNTIL 2026-09-16, and four of its arms had never been executed at
/// all: the herd's two, the family's and the felling stand's. Found by
/// coverage that day (boss, standstill parcels 11 and 14). Every kind is
/// asked here, each with its own id in its own field and nothing else set, so
/// an arm that reads the wrong field answers zero.
int CheckAlarmSubjectValue() {
  int failures = 0;
  const auto subject = [](core::AlarmKind kind, auto fill) {
    core::Alarm alarm;
    alarm.kind = kind;
    fill(alarm);
    return core::AlarmSubjectValue(alarm);
  };
  const auto unit_id = [](core::Alarm& alarm) { alarm.unit = core::UnitId{11}; };
  const auto field_id = [](core::Alarm& alarm) { alarm.field = core::FieldId{22}; };
  const auto herd_id = [](core::Alarm& alarm) { alarm.herd = core::HerdId{33}; };
  const auto family_id = [](core::Alarm& alarm) { alarm.family = core::FamilyId{44}; };
  const auto stand_id = [](core::Alarm& alarm) { alarm.stand = core::TimberStandId{55}; };

  failures += Expect(subject(core::AlarmKind::kStoreFull, unit_id) == 11 &&
                         subject(core::AlarmKind::kSiteWithoutMaterials, unit_id) == 11 &&
                         subject(core::AlarmKind::kSiteWithoutCrew, unit_id) == 11 &&
                         subject(core::AlarmKind::kSiteUnreachable, unit_id) == 11 &&
                         subject(core::AlarmKind::kYardWithoutGroom, unit_id) == 11,
                     "the alarms of a unit answer with the unit");
  failures += Expect(subject(core::AlarmKind::kHarvestWillNotFit, field_id) == 22 &&
                         subject(core::AlarmKind::kHarvestWaitingOnField, field_id) == 22 &&
                         subject(core::AlarmKind::kSeedShort, field_id) == 22 &&
                         subject(core::AlarmKind::kSowingWillNotFit, field_id) == 22 &&
                         subject(core::AlarmKind::kHarvestWillNotBeGathered, field_id) == 22,
                     "the alarms of a field answer with the field");
  failures += Expect(subject(core::AlarmKind::kHerdStarving, herd_id) == 33 &&
                         subject(core::AlarmKind::kHerdWithoutStable, herd_id) == 33,
                     "the herd's two answer with the herd");
  failures += Expect(subject(core::AlarmKind::kFamilyGoingHungry, family_id) == 44,
                     "a hungry family answers with the family");
  failures += Expect(subject(core::AlarmKind::kFellingUnreachable, stand_id) == 55,
                     "an unreachable felling answers with the stand");
  // THE FOUR ARMS ABOVE MUST NOT READ THE UNIT, which is the mistake this
  // switch is shaped to prevent: a herd alarm carrying a unit id and no herd
  // answers zero, not the unit's number.
  failures += Expect(subject(core::AlarmKind::kHerdStarving, unit_id) == 0 &&
                         subject(core::AlarmKind::kFamilyGoingHungry, unit_id) == 0 &&
                         subject(core::AlarmKind::kFellingUnreachable, unit_id) == 0,
                     "and none of them answers with a unit that is not their subject");

  core::Alarm position;
  position.kind = core::AlarmKind::kPlanPositionUncovered;
  position.resource = core::ResourceId{4};
  position.amount = 2;  // the second year of the chain
  core::Alarm next_year = position;
  next_year.amount = 3;
  failures +=
      Expect(core::AlarmSubjectValue(position) == 14 && core::AlarmSubjectValue(next_year) == 15 &&
                 core::AlarmSubjectValue(position) < core::AlarmSubjectValue(next_year),
             "an uncovered plan position sorts by its resource and then by its year");

  core::Alarm short_position;
  short_position.kind = core::AlarmKind::kPlanPositionShort;
  short_position.resource = core::ResourceId{4};
  short_position.unit = core::UnitId{11};
  short_position.amount = 900'000;  // grams, not a year: must not enter the subject
  failures += Expect(core::AlarmSubjectValue(short_position) == 4,
                     "a short plan position answers with its resource alone");

  core::Alarm nothing;
  failures += Expect(core::AlarmSubjectValue(nothing) == 0, "kNone names no subject");
  return failures;
}

int main() {
  int failures = 0;
  failures += TestDistrictVisitPacking();
  failures += TestHerdAgeBand();
  failures += TestRoadBeds();
  failures += TestRoadGraph();
  failures += TestRoadViews();
  failures += TestObstacleRaster();
  failures += TestRoadTrace();
  failures += TestRoadPieces();
  failures += TestRoadIndex();
  {
    // The night shift (boss, parcel 360): read as itself now, sunset to
    // sunrise, off the day's list — and a word the base never wrote is still
    // refused.
    core::PostShift night = core::PostShift::kBathDay;
    core::PostShift nonsense = core::PostShift::kBathDay;
    failures += Expect(core::ParsePostShift("night", night) && night == core::PostShift::kNight,
                       "post shift: `night` reads as the night shift");
    failures += Expect(!core::ParsePostShift("midnight", nonsense),
                       "post shift: a word the base never wrote is refused");
    // A day of 12 hours of light: sunrise 6, sunset 18.
    const core::DayWindow twelve = core::SolarWindow(12.0F);
    const auto on = [&twelve](std::uint32_t hour) {
      return core::InPostShift(core::PostShift::kNight, core::Weekday::kSunday, hour, twelve);
    };
    failures += Expect(on(18) && on(23) && on(0) && on(5) && !on(6) && !on(12) && !on(17),
                       "post shift: the night runs from sunset to sunrise across midnight, "
                       "Sunday too, and not an hour of the day");
    failures += Expect(core::PostHoldsTheDay(core::PostShift::kNight) &&
                           !core::PostHoldsTheDay(core::PostShift::kEvening),
                       "post shift: the night post takes its holder off the day's list");
  }
  {
    // The day off (time design §12), public in calendar.h since 2026-09-15.
    failures += Expect(core::IsDayOff(core::Weekday::kSunday, core::Epoch::kOne) &&
                           core::IsDayOff(core::Weekday::kSunday, core::Epoch::kThree),
                       "day off: Sunday is off in every epoch");
    failures += Expect(!core::IsDayOff(core::Weekday::kSaturday, core::Epoch::kOne) &&
                           !core::IsDayOff(core::Weekday::kSaturday, core::Epoch::kTwo) &&
                           core::IsDayOff(core::Weekday::kSaturday, core::Epoch::kThree),
                       "day off: Saturday joins it in Epoch III and not before");
    failures += Expect(!core::IsDayOff(core::Weekday::kMonday, core::Epoch::kThree) &&
                           !core::IsDayOff(core::Weekday::kFriday, core::Epoch::kThree),
                       "day off: a weekday is a working day even in Epoch III");
  }
  {
    // THE HOLIDAYS (host door request no. 2; boss's numbers, registry "Числа
    // дверей Эпохи I"). Day 0 a Monday: 1 May is day 16, 7 November day 41 —
    // a Sunday in year 1 (41 % 7 == 6), so it moves to the Monday, day 42.
    const core::Weekday monday = core::Weekday::kMonday;
    const core::Epoch one = core::Epoch::kOne;
    failures += Expect(core::HolidayOn(16, monday, one) == core::Holiday::kMayDay &&
                           core::HolidayOn(17, monday, one) == core::Holiday::kNone,
                       "holiday: May Day is the first day of May and only that day");
    failures += Expect(core::HolidayOn(41, monday, one) == core::Holiday::kNone &&
                           core::HolidayOn(42, monday, one) == core::Holiday::kRevolutionDay,
                       "holiday: 7 November on a Sunday moves to the next working day");
    // Year 2: day 89 is a Monday already (89 % 7 == 5, a Saturday — Epoch I
    // works Saturdays), so it stays where it falls.
    failures += Expect(
        core::HolidayOn(core::kDaysPerYear + 41, monday, one) == core::Holiday::kRevolutionDay,
        "holiday: 7 November on a working day stays on the second day of November");
    failures += Expect(core::HolidayOn(core::kDaysPerYear, monday, one) == core::Holiday::kNone &&
                           core::HolidayOn(core::kDaysPerYear, monday, core::Epoch::kTwo) ==
                               core::Holiday::kNewYear,
                       "holiday: New Year is a holiday from Epoch II and not in Epoch I");
    failures += Expect(core::IsRestDay(16, monday, one) && !core::IsRestDay(17, monday, one),
                       "holiday: a holiday is a day of rest, the day after it is not");
  }
  failures += TestDaylightCurve();
  failures += TestRainStopsWork();
  failures += CheckTheTopOfTheLadder();
  failures += CheckNextSowingCrop();
  failures += CheckTheFigureRule();
  failures += CheckAlarmSubjectValue();
  failures += TestDefIdFromRow();
  failures += TestDeadlineRefusals();
  failures += TestCalendar();
  failures += TestStateTable();
  failures += TestRandom();
  failures += TestGramsFromFloat();
  failures += TestVersionPin();
  failures += TestPlot();
  failures += TestResidentActivity();
  if (failures == 0) {
    std::cout << "unit_core_common: all checks passed\n";
  }
  return failures;
}
