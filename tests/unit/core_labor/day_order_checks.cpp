// The checks of #1778's day orders (day_order_checks.h).

#include "day_order_checks.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "core_common/calendar.h"
#include "core_common/day_orders.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_labor/labor_system.h"
#include "core_tables/tables.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

// Day 30 is a Wednesday (four days a month, day 0 a Monday); day 34 a Sunday.
constexpr std::uint32_t kWednesday = 30;
constexpr std::uint32_t kSunday = 34;

/// One household of `adults` at the map's origin, twelve hours of light, and two reaping
/// fields: a near one the accountant fills first, and a far one only an order sends anybody to.
struct Village {
  explicit Village(std::uint32_t adults) {
    world.calendar.day_zero_weekday = core::Weekday::kMonday;
    world.weather.daylight_hours = 12.0F;
    world.chairman.harvest_without_days_off = 0;
    core::UnitRow house;
    const core::UnitId house_id = core::AppendRow(world.units, house);
    core::FamilyRow household;
    household.house = house_id;
    const core::FamilyId family = core::AppendRow(world.families, household);
    world.units.rows[0].household = family;
    for (std::uint32_t index = 0; index < adults; ++index) {
      core::ResidentRow resident;
      resident.family = family;
      resident.birth_day = -300;  // ~25 biological years at day 0
      resident.health = 70.0F;
      resident.rest = 70.0F;
      resident.mood = 60.0F;
      resident.stamina = 50.0F;
      resident.education_stage = core::EducationStage::kPrimary;
      core::AppendRow(world.residents, resident);
    }
    near_field = AddField(20.0F);
    far_field = AddField(300.0F);
  }

  core::FieldId AddField(float metres) {
    core::FieldRow field;
    field.center = core::Vec2{.x = metres, .y = 0.0F};
    field.area_ga = 10.0F;
    field.phase = core::FieldPhase::kHarvest;
    field.work_days_remaining = 400.0F;  // more than anybody drains in these days
    field.crop = core::CropId{0};        // the oat of the tables below
    return core::AppendRow(world.fields, field);
  }

  void RunHours(core::ILaborSystem& labor,
                std::uint32_t day,
                std::uint32_t from,
                std::uint32_t to) {
    for (std::uint32_t hour = from; hour < to; ++hour) {
      world.calendar.tick = (static_cast<core::Tick>(day) * core::kTicksPerDay) + hour;
      core::RefreshCalendarCaches(world.calendar);
      const core::WorldState previous = world;
      labor.RunAssignmentDecisions(previous, world);
    }
  }

  void RunDay(core::ILaborSystem& labor, std::uint32_t day) {
    RunHours(labor, day, 0, core::kTicksPerDay);
  }

  core::OrderId Issue(const core::OrderRow& order) {
    core::OrderRow row = order;
    row.status = core::OrderStatus::kPending;
    return core::AppendRow(world.orders, row);
  }

  core::OrderRow Crew(core::FieldId field, std::uint16_t size) const {
    core::OrderRow order;
    order.kind = core::OrderKind::kAssignCrew;
    order.work = core::WorkKind::kHarvest;
    order.field = field;
    order.crew_size = size;
    return order;
  }

  const core::OrderRow& Order(core::OrderId id) const {
    return world.orders.rows[core::FindRow(world.orders, id)];
  }

  /// How many residents work `field` now.
  std::uint32_t On(core::FieldId field) const {
    std::uint32_t count = 0;
    for (const core::ResidentRow& resident : world.residents.rows) {
      count += resident.work.field.value == field.value ? 1U : 0U;
    }
    return count;
  }

  bool Works(core::ResidentId id, core::FieldId field) const {
    const std::uint32_t row = core::FindRow(world.residents, id);
    return row != core::kNoRow && world.residents.rows[row].work.field.value == field.value;
  }

  core::WorldState world;
  core::FieldId near_field;
  core::FieldId far_field;
};

/// @param no_auto_placement Writes world_params `no_auto_placement` 1 (#1812).
std::unique_ptr<core::ILaborSystem> Labor(bool no_auto_placement = false) {
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "day_order_checks";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "crops.csv") << "key,sow_to_month,harvest_to_month,is_winter\n"
                                       "oat,5,9,0\n";
  // A cow kind with a care norm for the override check (~0.9 game man-days a head a day).
  std::ofstream(root / "livestock.csv") << "key,care_days_per_year\nhorse,0\ncow,300\n";
  if (no_auto_placement) {
    std::ofstream(root / "world_params.csv") << "key,value,reader\nno_auto_placement,1,core\n";
  }
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  if (tables == nullptr) {
    std::cout << error << '\n';
    return nullptr;
  }
  auto labor = core::CreateLaborSystem(*tables, core::StubTables::kAllowed);
  std::filesystem::remove_all(root);
  return labor;
}

/// The unit test #1778 is measured by: N filled by the level, the read returns those names,
/// and they are on the work in the morning; then yesterday's read, re-issued, does it again.
int CheckCrewFillsNAndRepeats(core::ILaborSystem& labor) {
  int failures = 0;
  Village village(6);
  const core::OrderId crew = village.Issue(village.Crew(village.far_field, 3));
  village.RunDay(labor, kWednesday);
  const core::CrewView names = labor.CrewNames(village.world, crew);
  failures += Expect(names.measured && names.crew.value == crew.value,
                     "day orders: the crew is read back by its id");
  failures +=
      Expect(names.asked == 3 && names.names.size() == 3, "day orders: three asked, three named");
  failures += Expect(names.day == kWednesday + 1U, "day orders: for the next working day");
  // Level 0 «по наличию»: the accountant's rotation, rows from (day mod roster) = 31 mod 6 = 1.
  bool rotation = names.names.size() == 3;
  for (std::uint32_t index = 0; rotation && index < 3; ++index) {
    rotation = names.names[index].value == village.world.residents.row_ids[1 + index].value;
  }
  failures += Expect(rotation, "day orders: level 0 picks in the accountant's rotation, rows 1-3");
  failures += Expect(village.On(village.far_field) == 0,
                     "day orders: nobody is on the far field the day the order is given");

  // Thursday at a working hour: the three named are on the far field, nobody else is.
  village.RunHours(labor, kWednesday + 1U, 0, 9);
  bool all_there = true;
  for (const core::ResidentId id : names.names) {
    all_there = all_there && village.Works(id, village.far_field);
  }
  failures += Expect(all_there && village.On(village.far_field) == 3,
                     "day orders: the crew's three are on its work in the morning, and only they");
  village.RunHours(labor, kWednesday + 1U, 9, core::kTicksPerDay);
  failures += Expect(village.Order(crew).status == core::OrderStatus::kDone,
                     "day orders: the crew is done at its day's close - one day long");
  failures += Expect(labor.CrewNames(village.world, crew).crew.value == core::kInvalidEntityIdValue,
                     "day orders: a closed crew has no names to read");

  // Yesterday's read and the repeat the layer makes of it.
  const core::YesterdayDayOrders yesterday = labor.YesterdayDayOrdersOf(village.world);
  failures +=
      Expect(yesterday.measured && yesterday.day == kWednesday + 1U && yesterday.lines.size() == 1,
             "day orders: yesterday holds the one order that ran");
  if (yesterday.lines.size() == 1) {
    const core::DayOrderLine& line = yesterday.lines[0];
    failures += Expect(line.form == core::DayOrderForm::kCrew && line.count == 3 &&
                           line.work == core::WorkKind::kHarvest &&
                           line.field.value == village.far_field.value,
                       "day orders: in the shape it is issued again");
    core::OrderRow again = village.Crew(line.field, line.count);
    const core::OrderId repeat = village.Issue(again);
    village.RunDay(labor, kWednesday + 2U);
    village.RunHours(labor, kWednesday + 3U, 0, 9);
    failures += Expect(village.On(village.far_field) == 3 &&
                           village.Order(repeat).status == core::OrderStatus::kAccepted,
                       "day orders: repeated - the same three hands on the same work next day");
  }
  return failures;
}

/// A name written over a pick, a released pick, and fewer free than asked.
int CheckNamesAndShortCrew(core::ILaborSystem& labor) {
  int failures = 0;
  Village village(4);
  const core::OrderId crew = village.Issue(village.Crew(village.far_field, 2));
  village.RunHours(labor, kWednesday, 0, 2);
  const core::CrewView picked = labor.CrewNames(village.world, crew);
  failures += Expect(picked.names.size() == 2, "day orders: two picked");
  if (picked.names.size() == 2) {
    // The player writes the first pick by name onto the near field: he leaves the crew.
    core::OrderRow named;
    named.kind = core::OrderKind::kAssignDayWork;
    named.work = core::WorkKind::kHarvest;
    named.field = village.near_field;
    named.resident = picked.names[0];
    const core::OrderId day_work = village.Issue(named);
    // And releases the second.
    core::OrderRow release;
    release.kind = core::OrderKind::kReleaseWork;
    release.resident = picked.names[1];
    const core::OrderId released = village.Issue(release);
    village.RunHours(labor, kWednesday, 2, core::kTicksPerDay);
    failures += Expect(village.Order(day_work).status == core::OrderStatus::kAccepted,
                       "day orders: a day work by name is accepted");
    failures += Expect(village.Order(released).status == core::OrderStatus::kDone,
                       "day orders: releasing a pick is done, not refused");
    failures += Expect(labor.CrewNames(village.world, crew).names.empty(),
                       "day orders: the written name and the released one both left the crew");
    village.RunHours(labor, kWednesday + 1U, 0, 9);
    failures += Expect(village.Works(picked.names[0], village.near_field),
                       "day orders: the name written by hand works where he was written");
  }
  // Ten asked of four: accepted, four named.
  Village small(4);
  const core::OrderId big = small.Issue(small.Crew(small.far_field, 10));
  small.RunHours(labor, kWednesday, 0, 2);
  const core::CrewView short_crew = labor.CrewNames(small.world, big);
  failures += Expect(small.Order(big).status == core::OrderStatus::kAccepted &&
                         short_crew.asked == 10 && short_crew.names.size() == 4,
                     "day orders: fewer free than asked - accepted, filled with what there is");
  // A crew with nothing on it is refused at the shape: crew_size 0 never reaches the book,
  // and a crew on a field that is gone is refused kNoSuchSubject.
  core::OrderRow nowhere = small.Crew(core::FieldId{77}, 2);
  const core::OrderId lost = small.Issue(nowhere);
  small.RunHours(labor, kWednesday, 2, 3);
  failures += Expect(small.Order(lost).refusal == core::OrderRefusal::kNoSuchSubject,
                     "day orders: a crew on no such field is refused kNoSuchSubject");
  return failures;
}

/// Given on Saturday, due on Sunday: the reaping rests on Sunday, the order runs on Monday.
int CheckDayOffMovesTheDay(core::ILaborSystem& labor) {
  int failures = 0;
  Village village(4);
  const core::OrderId crew = village.Issue(village.Crew(village.far_field, 2));
  village.RunDay(labor, kSunday - 1U);
  village.RunDay(labor, kSunday);
  failures += Expect(village.Order(crew).status == core::OrderStatus::kAccepted,
                     "day orders: a day off does not spend the order");
  village.RunHours(labor, kSunday + 1U, 0, 9);
  failures +=
      Expect(village.On(village.far_field) == 2, "day orders: it runs on the next working day");
  return failures;
}

/// SeasonWorks reads the job's own window; CrewGapsToday's source names a job left short.
int CheckSeasonWorksAndGaps(core::ILaborSystem& labor) {
  int failures = 0;
  Village village(2);
  village.RunHours(labor, kWednesday, 0, 2);
  const core::SeasonWorks works = labor.SeasonWorksOf(village.world);
  failures += Expect(works.measured && works.lines.size() == 2,
                     "season works: both reaping fields are listed");
  if (!works.lines.empty()) {
    // The oat's reaping closes with month 9 (crops.csv, 1-based: September is month index 8,
    // labor_config.cpp reads `harvest_to - 1`): the window ends at day (8 + 1) x 4 = 36 of the
    // year, so the last day is 35 - five days from Wednesday 30. (The first draft of this check
    // took the month 0-based and expected 39; the rule printed 35 and the arithmetic was mine.)
    const core::SeasonWorkLine& line = works.lines[0];
    const bool as_ruled = line.work == core::WorkKind::kHarvest && !line.overdue &&
                          line.deadline == 35U && line.area_ha == 10.0F &&
                          line.work_days_left == 400.0F;
    if (!as_ruled) {
      std::cout << "  season works line 0: work " << static_cast<int>(line.work) << " deadline "
                << line.deadline << " overdue " << (line.overdue ? 1 : 0) << " area "
                << line.area_ha << " days left " << line.work_days_left << '\n';
    }
    failures += Expect(as_ruled, "season works: the window's last day from the accountant's rule");
  }
  // #1815: today's stop, by the placement's own predicates - none on a dry Wednesday, the rain
  // on a wet one (the reaping stands), the day off on Sunday.
  if (!works.lines.empty()) {
    failures += Expect(works.lines[0].stopped_by == core::WorkStop::kNone,
                       "season works: a dry working day stops nothing");
  }
  core::WorldState wet = village.world;
  wet.weather.precipitation = core::Precipitation::kRain;
  const core::SeasonWorks rained = labor.SeasonWorksOf(wet);
  failures += Expect(!rained.lines.empty() && rained.lines[0].stopped_by == core::WorkStop::kRain,
                     "season works: the rain stands the reaping, and the line says so");
  core::WorldState sunday = village.world;
  sunday.calendar.tick = static_cast<core::Tick>(kSunday) * core::kTicksPerDay;
  core::RefreshCalendarCaches(sunday.calendar);
  const core::SeasonWorks rested = labor.SeasonWorksOf(sunday);
  failures += Expect(!rested.lines.empty() && rested.lines[0].stopped_by == core::WorkStop::kDayOff,
                     "season works: on the day off the line names it");
  // Two hands and 800 man-days of reaping: the morning leaves a field short.
  bool named_short = false;
  for (const core::CrewGapLine& gap : village.world.crew_gaps) {
    named_short = named_short || gap.work == core::WorkKind::kHarvest;
  }
  failures += Expect(named_short, "crew gaps: the morning names a reaping left short of hands");
  return failures;
}

/// The chain's last date (boss 17 Nov, for game's «пахота, затем боронование и сев — всё до»):
/// a field in its ploughing carries its sowing's deadline - the same date while the window is
/// open (day 10: the oat sows to May, days 16-19), and on day 30 the plough is overdue while the
/// sowing's own rule is asked (never earlier than the plough's end).
int CheckSowingDeadline(core::ILaborSystem& labor) {
  int failures = 0;
  const auto plough_line = [&](std::uint32_t day) {
    Village village(2);
    village.world.fields.rows[0].phase = core::FieldPhase::kPlowing;
    village.world.fields.rows[1].phase = core::FieldPhase::kGrowing;  // off the list
    village.RunHours(labor, day, 0, 1);
    const core::SeasonWorks works = labor.SeasonWorksOf(village.world);
    return works.lines.empty() ? core::SeasonWorkLine{} : works.lines[0];
  };
  const core::SeasonWorkLine open = plough_line(10);
  failures += Expect(open.work == core::WorkKind::kPlowing && !open.overdue &&
                         open.sowing_deadline == open.deadline && !open.sowing_overdue &&
                         open.deadline == 19U,
                     "sowing deadline: while the window is open the chain ends with the plough");
  const core::SeasonWorkLine late = plough_line(30);
  std::cout << "  sowing deadline, day 30: plough " << late.deadline << " overdue "
            << (late.overdue ? 1 : 0) << ", sowing " << late.sowing_deadline << " overdue "
            << (late.sowing_overdue ? 1 : 0) << '\n';
  failures +=
      Expect(late.work == core::WorkKind::kPlowing && late.overdue && late.sowing_deadline != 0U &&
                 (!late.sowing_overdue || late.sowing_deadline >= late.deadline),
             "sowing deadline: past the window the plough is overdue and the line still "
             "carries the sowing's own date, never earlier than the plough's end");
  // A reaping line carries none.
  Village reaping(2);
  reaping.RunHours(labor, kWednesday, 0, 1);
  const core::SeasonWorks reaped = labor.SeasonWorksOf(reaping.world);
  failures += Expect(!reaped.lines.empty() && reaped.lines[0].sowing_deadline == 0U &&
                         !reaped.lines[0].sowing_overdue,
                     "sowing deadline: a reaping line carries no sowing date");
  return failures;
}

/// #1811's first-build finding: a crew's picks are out of the accountant's plan, so the job he
/// fills keeps its hand. Three people, one cow's care (one hand) and a crew of two on the far
/// field: the rotation of day 31 puts row 1 first, the very man the crew picked. Planned with
/// him and then taken off, the barn stood empty; planned without him, row 0 tends the cow.
int CheckOverrideKeepsTheClerksJobs(core::ILaborSystem& labor) {
  int failures = 0;
  Village village(3);
  core::UnitRow barn;
  barn.position = core::Vec2{.x = 5.0F, .y = 0.0F};
  core::HerdRow cow;
  cow.kind = core::LivestockKindId{1};  // the tables' cow, care 300 a year
  cow.unit = core::AppendRow(village.world.units, barn);
  cow.adult_count = 1;
  const core::HerdId herd = core::AppendRow(village.world.herds, cow);
  const core::OrderId crew = village.Issue(village.Crew(village.far_field, 2));
  village.RunDay(labor, kWednesday);
  village.RunHours(labor, kWednesday + 1U, 0, 2);
  std::uint32_t tending = 0;
  for (const core::ResidentRow& resident : village.world.residents.rows) {
    tending += resident.work.herd.value == herd.value ? 1U : 0U;
  }
  const core::CrewView names = labor.CrewNames(village.world, crew);
  std::cout << "  override: crew names " << names.names.size() << ", on the far field "
            << village.On(village.far_field) << ", tending the cow " << tending << '\n';
  failures += Expect(village.On(village.far_field) == 2 && tending == 1,
                     "override: the crew of two works, and the barn keeps the accountant's hand");
  return failures;
}

/// #1826 (the human 17 Nov, «Ваша рекомендация»): a STANDING order's man is out of the plan too.
/// Three people, one cow's care and a standing kAssignWork sending row 1 - the rotation's first
/// on day 31 - to the far field: planned without him, the barn keeps a hand.
int CheckStandingKeepsTheClerksJobs(core::ILaborSystem& labor) {
  int failures = 0;
  Village village(3);
  core::UnitRow barn;
  barn.position = core::Vec2{.x = 5.0F, .y = 0.0F};
  core::HerdRow cow;
  cow.kind = core::LivestockKindId{1};
  cow.unit = core::AppendRow(village.world.units, barn);
  cow.adult_count = 1;
  const core::HerdId herd = core::AppendRow(village.world.herds, cow);
  core::OrderRow standing;
  standing.kind = core::OrderKind::kAssignWork;
  standing.work = core::WorkKind::kHarvest;
  standing.field = village.far_field;
  standing.resident = village.world.residents.row_ids[1];
  village.Issue(standing);
  village.RunDay(labor, kWednesday);
  village.RunHours(labor, kWednesday + 1U, 0, 2);
  std::uint32_t tending = 0;
  for (const core::ResidentRow& resident : village.world.residents.rows) {
    tending += resident.work.herd.value == herd.value ? 1U : 0U;
  }
  failures +=
      Expect(village.Works(village.world.residents.row_ids[1], village.far_field) && tending == 1,
             "standing order: its man works where ordered, and the barn keeps a hand");
  return failures;
}

/// #1825: the preview of a draft crew and the herd's care by the accountant's norm. Two people
/// and one cow: a draft crew of two takes both, so the barn loses the accountant's hand - the
/// preview names it before any order is issued. The cow's care: its seam, one hand's expected
/// output (under one care-day) and the hands that cover it (never fewer than the seam's ceiling).
int CheckPreviewAndHerdCare(core::ILaborSystem& labor) {
  int failures = 0;
  Village village(2);
  core::UnitRow barn;
  barn.position = core::Vec2{.x = 5.0F, .y = 0.0F};
  core::HerdRow cow;
  cow.kind = core::LivestockKindId{1};
  cow.unit = core::AppendRow(village.world.units, barn);
  cow.adult_count = 1;
  const core::HerdId herd = core::AppendRow(village.world.herds, cow);
  village.RunHours(labor, kWednesday, 0, 9);
  const core::DayOrderPreview preview =
      labor.PreviewDayOrder(village.world, village.Crew(village.far_field, 2));
  const core::DayOrderPreview idle =
      labor.PreviewDayOrder(village.world, village.Crew(village.far_field, 0));
  const core::DayOrderKindPreview* care = nullptr;
  for (const core::DayOrderKindPreview& kind : preview.kinds) {
    care = kind.work == core::WorkKind::kHerdCare ? &kind : care;
  }
  std::cout << "  preview: measured " << (preview.measured ? 1 : 0) << ", taken " << preview.taken
            << ", day " << preview.measured_on_day << ", herd care hands "
            << (care != nullptr ? care->hands_without : 99) << " -> "
            << (care != nullptr ? care->hands_with : 99) << '\n';
  failures += Expect(preview.measured && preview.taken == 2 && care != nullptr &&
                         care->hands_without == 1 && care->hands_with == 0 &&
                         preview.measured_on_day == kWednesday,
                     "preview: a crew of both people leaves the barn without its hand, said first");
  failures += Expect(idle.measured && idle.taken == 0 && idle.kinds.empty(),
                     "preview: a draft that takes nobody changes nothing (the pair)");
  // The clerk's first two for the barn, each with his expected output; a field with no open work
  // names nothing.
  core::OrderRow barn_work;
  barn_work.work = core::WorkKind::kHerdCare;
  barn_work.herd = herd;
  const core::ClerkPicks picks = labor.ClerkPicksFor(village.world, barn_work, 2);
  core::OrderRow nothing;
  nothing.work = core::WorkKind::kSowing;
  nothing.field = village.near_field;  // in its reaping: no sowing job open
  const core::ClerkPicks none = labor.ClerkPicksFor(village.world, nothing, 2);
  failures += Expect(picks.measured && picks.picks.size() == 2 && picks.seam > 0.0F &&
                         picks.picks[0].daily_norm > 0.0F && !none.measured,
                     "clerk picks: two names for the barn with their output; none for a job "
                     "that is not open");
  const core::HerdCareView view = labor.HerdCareToday(village.world, herd);
  std::cout << "  herd care: seam " << view.seam << ", per hand " << view.per_hand << ", hands "
            << view.hands << '\n';
  // NOT «one hand under a care-day»: the first draft asserted it and the run gave 1.20 on a
  // 12-hour day - a hand gives more than a standard day in summer light and less in winter's.
  // What holds is the read's own arithmetic, the seam over the accountant's per-hand norm.
  failures +=
      Expect(view.measured && view.seam > 0.0F && view.per_hand > 0.0F &&
                 view.hands == static_cast<std::uint16_t>(std::ceil(view.seam / view.per_hand)),
             "herd care: the hands are the seam over the accountant's per-hand norm");
  // THE CLERK'S AGENDA (#1811): every job in his order, ranks 0..n-1; the barn's line carries
  // the one hand the preview's plan put there; the hands are the village's two at most.
  const core::ClerkAgendaView agenda = labor.ClerkAgendaOf(village.world);
  bool ranks_run = true;
  std::uint32_t hands = 0;
  const core::ClerkAgendaLine* barn_line = nullptr;
  for (std::size_t index = 0; index < agenda.lines.size(); ++index) {
    const core::ClerkAgendaLine& line = agenda.lines[index];
    ranks_run = ranks_run && line.rank == index;
    hands += line.hands;
    if (line.work == core::WorkKind::kHerdCare && line.herd.value == herd.value) {
      barn_line = &line;
    }
  }
  std::cout << "  agenda: measured " << (agenda.measured ? 1 : 0) << ", lines "
            << agenda.lines.size() << ", hands " << hands << ", barn hands "
            << (barn_line != nullptr ? barn_line->hands : 99) << '\n';
  failures += Expect(agenda.measured && agenda.measured_on_day == kWednesday &&
                         agenda.lines.size() >= 2 && ranks_run && hands <= 2 &&
                         barn_line != nullptr && barn_line->hands == 1 && barn_line->seam > 0.0F,
                     "clerk agenda: every job in rank order, the barn's one hand as the plan "
                     "puts it, two hands at most");
  return failures;
}

/// #1812: without the accountant nobody is placed, and the chairman's crew still works - the
/// pair, refused and allowed, on one village.
int CheckNoAutoPlacement() {
  int failures = 0;
  const auto labor = Labor(true);
  if (Expect(labor != nullptr, "no auto placement: the labor system is made") != 0) {
    return 1;
  }
  Village village(5);
  const core::OrderId crew = village.Issue(village.Crew(village.far_field, 2));
  village.RunDay(*labor, kWednesday);
  village.RunHours(*labor, kWednesday + 1U, 0, 9);
  failures += Expect(village.On(village.near_field) == 0,
                     "no auto placement: the accountant puts nobody on the near field");
  failures += Expect(village.On(village.far_field) == 2 &&
                         village.Order(crew).status == core::OrderStatus::kAccepted,
                     "no auto placement: the chairman's crew of two still works");
  bool near_short = false;
  for (const core::CrewGapLine& gap : village.world.crew_gaps) {
    near_short = near_short || (gap.field.value == village.near_field.value && gap.placed == 0);
  }
  failures += Expect(near_short, "no auto placement: the near field is named short, nobody on it");
  // The control on the same village shape: with the accountant, the near field is worked.
  const auto with_clerk = Labor(false);
  Village control(5);
  control.RunHours(*with_clerk, kWednesday, 0, 9);
  failures +=
      Expect(control.On(control.near_field) > 0,
             "no auto placement: the control - with the accountant the near field is worked");
  return failures;
}

/// #1818: an ordered carting crew's horses come out of the pool and ride - two carters, one
/// horse asked, two horses in the stable, no accountant: one rides, one walks; the same crew
/// with no horse asked: nobody rides (the pair).
int CheckOrderedHorses() {
  int failures = 0;
  const auto labor = Labor(true);
  if (Expect(labor != nullptr, "ordered horses: the labor system is made") != 0) {
    return 1;
  }
  const auto riders = [&](std::uint16_t horses) {
    Village village(4);
    village.world.fields.rows[1].haul_days_remaining = 20.0F;  // a load lies on the far field
    village.world.fields.rows[1].reaped_grams = 5'000'000;
    core::UnitRow stable;
    stable.position = core::Vec2{.x = 10.0F, .y = 0.0F};
    core::HerdRow team;
    team.kind = core::LivestockKindId{0};  // the horse of the tables above
    team.unit = core::AppendRow(village.world.units, stable);
    team.adult_count = 2;
    core::AppendRow(village.world.herds, team);
    core::OrderRow carters;
    carters.kind = core::OrderKind::kAssignCrew;
    carters.work = core::WorkKind::kHauling;
    carters.field = village.far_field;
    carters.crew_size = 2;
    carters.horses = horses;
    village.Issue(carters);
    village.RunDay(*labor, kWednesday);
    village.RunHours(*labor, kWednesday + 1U, 0, 1);
    std::uint32_t riding = 0;
    std::uint32_t carting = 0;
    for (const core::ResidentRow& resident : village.world.residents.rows) {
      if (resident.work.kind == core::WorkKind::kHauling) {
        ++carting;
        riding += resident.work.rides_horse != 0 ? 1U : 0U;
      }
    }
    return std::pair{carting, riding};
  };
  const auto [carting_one, riding_one] = riders(1);
  const auto [carting_none, riding_none] = riders(0);
  failures += Expect(carting_one == 2 && riding_one == 1,
                     "ordered horses: one horse asked for two carters - one rides, one walks");
  failures += Expect(carting_none == 2 && riding_none == 0,
                     "ordered horses: no horse asked and no accountant - both walk (the pair)");
  return failures;
}

}  // namespace

int CheckDayOrders() {
  const auto labor = Labor();
  if (Expect(labor != nullptr, "day orders: the labor system is made") != 0) {
    return 1;
  }
  int failures = 0;
  failures += CheckCrewFillsNAndRepeats(*labor);
  failures += CheckNamesAndShortCrew(*labor);
  failures += CheckDayOffMovesTheDay(*labor);
  failures += CheckSeasonWorksAndGaps(*labor);
  failures += CheckSowingDeadline(*labor);
  failures += CheckOverrideKeepsTheClerksJobs(*labor);
  failures += CheckPreviewAndHerdCare(*labor);
  failures += CheckStandingKeepsTheClerksJobs(*labor);
  failures += CheckNoAutoPlacement();
  failures += CheckOrderedHorses();
  return failures;
}
