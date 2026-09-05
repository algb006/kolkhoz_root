// Unit test of core_common: calendar arithmetic, StateTable operations, RNG.
// Plain executable, exit code = number of failed expectations (framework not
// chosen yet — tests/CMakeLists.txt).

#include <cstdint>
#include <iostream>
#include <limits>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/plot.h"
#include "core_common/quantities.h"
#include "core_common/random.h"
#include "core_common/resident_activity.h"
#include "core_common/state_table.h"
#include "core_common/state_table_ops.h"
#include "core_common/version_pin.h"

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
  rules.travel_hours = 1.0F;

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

  const auto at = [&world, &rules](std::uint32_t hour, float age) {
    world.calendar.tick = hour;
    core::RefreshCalendarCaches(world.calendar);
    return core::ActivityOfResident(world, 0, age, rules);
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
  return failures;
}

int main() {
  int failures = 0;
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
