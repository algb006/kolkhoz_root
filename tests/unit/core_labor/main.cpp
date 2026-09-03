// Unit test of core_labor. Two halves, as the module has:
//   * the accountant's placement (task F2) — determinism against input
//     order, the surplus-idles rule, the road limit, the horse pool and the
//     horse lock, window urgency, what each placement level sees;
//   * the working day (task O1) — the solar window, the road by what the
//     order travels on, the reference worker's efficiency, and a whole day
//     driven through the subsystem interface: placement, the seam draining,
//     trudodni on the family account, the walk-off, the
//     day off. (The economic year's burn moved to core_residents with the
//     distribution it pays for — stage 6, task O1.)

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "assignment.h"
#include "core_common/alarm_state.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_labor/labor_system.h"
#include "core_tables/tables.h"
#include "labor_config.h"
#include "labor_day.h"
#include "posts.h"

static_assert(std::is_abstract_v<core::ILaborSystem>, "ILaborSystem is a contract");
static_assert(std::has_virtual_destructor_v<core::ILaborSystem>,
              "implementations are destroyed through the interface");

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

/// One in-memory table: a header row plus data rows, enough to feed the
/// labor parser without touching the disk.
class FakeTable final : public core::ITable {
 public:
  FakeTable(std::vector<std::string_view> columns, std::vector<std::vector<std::string_view>> rows)
      : columns_(std::move(columns)), rows_(std::move(rows)) {}

  std::uint32_t RowCount() const override { return static_cast<std::uint32_t>(rows_.size()); }

  std::uint32_t ColumnCount() const override { return static_cast<std::uint32_t>(columns_.size()); }

  std::uint32_t FindColumn(std::string_view name) const override {
    for (std::uint32_t index = 0; index < columns_.size(); ++index) {
      if (columns_[index] == name) {
        return index;
      }
    }
    return core::kNoTableColumn;
  }

  std::uint32_t FindRowByKey(std::string_view key) const override {
    for (std::uint32_t row = 0; row < rows_.size(); ++row) {
      if (!rows_[row].empty() && rows_[row][0] == key) {
        return row;
      }
    }
    return core::kNoTableRow;
  }

  std::string_view CellText(std::uint32_t row, std::uint32_t column) const override {
    if (row >= rows_.size() || column >= rows_[row].size()) {
      return {};
    }
    return rows_[row][column];
  }

  std::optional<std::int64_t> CellInteger(std::uint32_t row, std::uint32_t column) const override {
    const std::optional<float> value = CellReal(row, column);
    return value ? std::optional<std::int64_t>(static_cast<std::int64_t>(*value)) : std::nullopt;
  }

  std::optional<float> CellReal(std::uint32_t row, std::uint32_t column) const override {
    const std::string_view text = CellText(row, column);
    if (text.empty()) {
      return std::nullopt;
    }
    float value = 0.0F;
    const char* const begin = text.data();
    const auto result = std::from_chars(begin, begin + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != begin + text.size()) {
      return std::nullopt;
    }
    return value;
  }

 private:
  std::vector<std::string_view> columns_;

  std::vector<std::vector<std::string_view>> rows_;
};

/// A table set holding exactly one named table.
class OneTableSet final : public core::ITableSet {
 public:
  OneTableSet(std::string_view name, const core::ITable& table) : name_(name), table_(&table) {}

  const core::ITable* FindTable(std::string_view name) const override {
    return name == name_ ? table_ : nullptr;
  }

  std::uint32_t TableCount() const override { return 1; }

  std::string_view TableName(std::uint32_t index) const override {
    return index == 0 ? name_ : std::string_view{};
  }

 private:
  std::string_view name_;

  const core::ITable* table_;
};

class EmptyTableSet final : public core::ITableSet {
 public:
  const core::ITable* FindTable(std::string_view /*name*/) const override { return nullptr; }

  std::uint32_t TableCount() const override { return 0; }

  std::string_view TableName(std::uint32_t /*index*/) const override { return {}; }
};

core::AssignmentJob FieldJob(core::WorkKind kind,
                             std::uint32_t field_id,
                             core::Vec2 position,
                             float work_days,
                             std::uint8_t window_days) {
  core::AssignmentJob job;
  job.kind = kind;
  job.field = core::FieldId{field_id};
  job.position = position;
  job.work_days_remaining = work_days;
  job.window_days_left = window_days;
  return job;
}

core::AssignmentCandidate Worker(std::uint32_t row, core::Vec2 home) {
  core::AssignmentCandidate candidate;
  candidate.resident_row = row;
  candidate.home = home;
  candidate.efficiency = 1.0F;
  candidate.rest = 70.0F;
  candidate.skill = 20.0F;
  return candidate;
}

/// Params with a wide-open day so geometry tests control the outcome.
core::AssignmentParams DayParams() {
  core::AssignmentParams params;
  params.window_hours = 12.0F;
  params.walk_hours_per_km = 3.1F;
  params.travel_limit_hours = 2.0F;
  params.standard_day_hours = 10.0F;
  params.draught_horses = 0;
  params.placement_level = 2;
  return params;
}

int TestSurplusIdles() {
  int failures = 0;
  // One tiny job, five hands: the crew stops when expected output covers
  // the demand — the rest idle (a trudoden is a work norm, not attendance).
  const std::vector<core::AssignmentJob> jobs = {
      FieldJob(core::WorkKind::kSowing, 1, {100.0F, 0.0F}, 0.5F, 3),
  };
  std::vector<core::AssignmentCandidate> candidates;
  for (std::uint32_t row = 0; row < 5; ++row) {
    candidates.push_back(Worker(row, {0.0F, 0.0F}));
  }
  const auto plan = core::PlanDayAssignments(jobs, candidates, DayParams());
  std::uint32_t assigned = 0;
  for (const std::uint32_t job : plan) {
    assigned += job != core::kNoJobAssigned ? 1 : 0;
  }
  failures += Expect(assigned == 1, "a half-day job takes one worker, four idle");
  return failures;
}

int TestRoadLimit() {
  int failures = 0;
  // 1 km straight-line at 3.1 h/km is over the 2-hour limit: unreachable
  // even when he is the only hand. 500 m is within it.
  const std::vector<core::AssignmentJob> jobs = {
      FieldJob(core::WorkKind::kSowing, 1, {1000.0F, 0.0F}, 5.0F, 3),
  };
  const std::vector<core::AssignmentCandidate> far = {Worker(0, {0.0F, 0.0F})};
  const auto none = core::PlanDayAssignments(jobs, far, DayParams());
  failures += Expect(none[0] == core::kNoJobAssigned, "a job beyond the road limit takes nobody");

  const std::vector<core::AssignmentCandidate> near = {Worker(0, {500.0F, 0.0F})};
  const auto one = core::PlanDayAssignments(jobs, near, DayParams());
  failures += Expect(one[0] == 0, "the same job within the limit takes the worker");
  return failures;
}

int TestHorsePoolAndLock() {
  int failures = 0;
  // Plowing wants ~4 workers, but two horses cap the crew at two; the
  // locked resident may take only horse work, so on a sowing day he idles.
  const core::Vec2 origin{0.0F, 0.0F};
  const std::vector<core::AssignmentJob> jobs = {
      FieldJob(core::WorkKind::kPlowing, 1, origin, 4.0F, 10),
      FieldJob(core::WorkKind::kSowing, 2, origin, 4.0F, 10),
  };
  std::vector<core::AssignmentCandidate> candidates;
  for (std::uint32_t row = 0; row < 6; ++row) {
    candidates.push_back(Worker(row, origin));
  }
  candidates[0].horse_locked = true;
  auto params = DayParams();
  params.draught_horses = 2;
  const auto plan = core::PlanDayAssignments(jobs, candidates, params);

  std::uint32_t plowing = 0;
  for (const std::uint32_t job : plan) {
    plowing += job == 0 ? 1 : 0;
  }
  failures += Expect(plowing == 2, "two horses cap the plowing crew at two");
  failures += Expect(plan[0] == 0, "the horse-locked resident is spent on horse work first");

  // Without horses the plowing job takes nobody, and the locked resident
  // cannot be placed on the sowing that remains.
  params.draught_horses = 0;
  const auto horseless = core::PlanDayAssignments(jobs, candidates, params);
  failures += Expect(horseless[0] == core::kNoJobAssigned,
                     "no horses: the locked resident idles even with sowing open");
  std::uint32_t on_plowing = 0;
  for (const std::uint32_t job : horseless) {
    on_plowing += job == 0 ? 1 : 0;
  }
  failures += Expect(on_plowing == 0, "no horses: nobody plows");
  return failures;
}

int TestWindowUrgency() {
  int failures = 0;
  // One worker, two jobs: the harvest window closes in 2 days, the plowing
  // in 30 — the urgent one wins regardless of input order.
  const core::Vec2 origin{0.0F, 0.0F};
  std::vector<core::AssignmentJob> jobs = {
      FieldJob(core::WorkKind::kPlowing, 1, origin, 3.0F, 30),
      FieldJob(core::WorkKind::kHarvest, 2, origin, 3.0F, 2),
  };
  const std::vector<core::AssignmentCandidate> candidates = {Worker(0, origin)};
  auto params = DayParams();
  params.draught_horses = 1;
  const auto plan = core::PlanDayAssignments(jobs, candidates, params);
  failures += Expect(plan[0] == 1, "the closing harvest window outranks relaxed plowing");

  // The same jobs in the opposite input order give the same choice by
  // target, not by position: determinism against input order.
  std::swap(jobs[0], jobs[1]);
  const auto swapped = core::PlanDayAssignments(jobs, candidates, params);
  failures += Expect(swapped[0] == 0, "input order does not change the chosen target");
  return failures;
}

int TestPlacementLevels() {
  int failures = 0;
  // A strong worker lives 600 m away (long walk), a weak one next to the
  // field. The naive level takes the first row; the experienced level
  // weighs the road in and takes the neighbor.
  const core::Vec2 field_at{0.0F, 0.0F};
  // Demand 0.9 norm-days: either worker covers it alone, so the crew is
  // one and the choice itself is what the levels disagree on.
  const std::vector<core::AssignmentJob> jobs = {
      FieldJob(core::WorkKind::kSowing, 1, field_at, 0.9F, 3),
  };
  std::vector<core::AssignmentCandidate> candidates = {
      Worker(0, {600.0F, 0.0F}),
      Worker(1, {50.0F, 0.0F}),
  };
  candidates[0].efficiency = 1.1F;
  candidates[1].efficiency = 0.9F;

  auto params = DayParams();
  params.placement_level = 0;
  const auto naive = core::PlanDayAssignments(jobs, candidates, params);
  failures += Expect(naive[0] == 0 && naive[1] == core::kNoJobAssigned,
                     "the naive level places by stable order, blind to the road");

  params.placement_level = 2;
  const auto experienced = core::PlanDayAssignments(jobs, candidates, params);
  failures += Expect(experienced[1] == 0 && experienced[0] == core::kNoJobAssigned,
                     "the experienced level prices the road in and takes the neighbor");
  return failures;
}

int TestBoundarySystemStub() {
  int failures = 0;
  const EmptyTableSet tables;
  const auto system = core::CreateLaborSystem(tables);
  failures += Expect(system != nullptr, "factory yields a system");
  if (system != nullptr) {
    const core::WorldState previous;
    core::WorldState current = previous;
    system->RunAssignmentDecisions(previous, current);
    failures += Expect(current.residents.rows.empty() && current.fields.rows.empty(),
                       "an empty world stays empty through the labor sub-step");
  }
  return failures;
}

// ---------------------------------------------------------------------------
// The working day (task O1)
// ---------------------------------------------------------------------------

/// A hand-built world plus the ticking that drives the labor sub-step: one
/// family in one house, adults of ~25 years, and whatever jobs the test adds.
class DayWorld {
 public:
  explicit DayWorld(std::uint32_t adults) {
    world.calendar.day_zero_weekday = core::Weekday::kMonday;
    world.weather.daylight_hours = 12.0F;
    core::UnitRow house;
    house.position = core::Vec2{.x = 0.0F, .y = 0.0F};
    const core::UnitId house_id = core::AppendRow(world.units, house);
    core::FamilyRow household;
    household.house = house_id;
    family = core::AppendRow(world.families, household);
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
  }

  /// Adds a field standing in `phase` with `work` game man-days of demand.
  core::FieldId AddField(core::FieldPhase phase, float work, core::Vec2 center) {
    core::FieldRow field;
    field.center = center;
    field.area_ga = 10.0F;
    field.phase = phase;
    field.work_days_remaining = work;
    return core::AppendRow(world.fields, field);
  }

  /// Adds a herd standing at a new unit `metres` east of the village.
  core::HerdId AddUnitHerd(std::uint16_t heads, float metres) {
    core::UnitRow barn;
    barn.position = core::Vec2{.x = metres, .y = 0.0F};
    core::HerdRow herd;
    herd.unit = core::AppendRow(world.units, barn);
    herd.adult_count = heads;
    return core::AppendRow(world.herds, herd);
  }

  void RunDay(core::ILaborSystem& labor, std::uint32_t day) {
    for (std::uint32_t hour = 0; hour < core::kTicksPerDay; ++hour) {
      world.calendar.tick = (static_cast<core::Tick>(day) * core::kTicksPerDay) + hour;
      core::RefreshCalendarCaches(world.calendar);
      const core::WorldState previous = world;
      labor.RunAssignmentDecisions(previous, world);
    }
  }

  core::WorldState world;

  core::FamilyId family;
};

int TestSolarWindow() {
  int failures = 0;
  const core::DayWindow window = core::SolarWindow(12.0F);
  failures += Expect(window.sunrise == 6.0F && window.sunset == 18.0F,
                     "a 12-hour day runs from six to six");
  const core::DayWindow winter = core::SolarWindow(7.0F);
  failures += Expect(winter.sunset - winter.sunrise == 7.0F, "the window is the daylight itself");
  failures += Expect(core::HoursInside(5, 5.5F, 18.0F) == 0.5F, "a half hour of the sixth hour");
  failures += Expect(core::HoursInside(9, 5.5F, 18.0F) == 1.0F, "a whole hour inside the window");
  failures += Expect(core::HoursInside(20, 5.5F, 18.0F) == 0.0F, "nothing after sunset");
  return failures;
}

int TestRoadByWhatHeTravelsOn() {
  int failures = 0;
  const core::LaborConfig config;
  const float walk = core::HoursPerKm(config, core::WorkKind::kSowing);
  const float harness = core::HoursPerKm(config, core::WorkKind::kPlowing);
  // 5 km/h under the x12 chronometer is 0.42 km per game hour, so two hours
  // on foot are the design's ~800 m (time design §7).
  failures += Expect(walk > 2.3F && walk < 2.5F, "a kilometre on foot costs ~2.4 game hours");
  failures += Expect(harness < walk, "a harnessed order rides and so reaches farther");
  const core::Vec2 home{.x = 0.0F, .y = 0.0F};
  const core::Vec2 field{.x = 600.0F, .y = 800.0F};  // exactly 1 km away
  const float travel = core::TravelHours(home, field, walk);
  failures += Expect(travel > 2.3F && travel < 2.5F,
                     "travel is the straight line in kilometres times the rate");
  return failures;
}

int TestReferenceWorkerDeliversOneNorm() {
  int failures = 0;
  const core::LaborConfig config;
  core::ResidentRow reference;
  reference.health = 70.0F;
  reference.rest = 70.0F;
  reference.mood = 60.0F;
  reference.stamina = 50.0F;
  reference.education_stage = core::EducationStage::kPrimary;
  // The plateau ends at life expectancy less labor's margin: 60 - 20.
  constexpr float kAgingFrom = 40.0F;
  const float efficiency = core::ResidentEfficiency(config, reference, 30.0F, kAgingFrom);
  failures += Expect(efficiency > 0.97F && efficiency < 1.03F,
                     "the reference worker is worth exactly one norm day");

  core::ResidentRow illiterate = reference;
  illiterate.education_stage = core::EducationStage::kNone;
  failures +=
      Expect(core::ResidentEfficiency(config, illiterate, 30.0F, kAgingFrom) < efficiency * 0.9F,
             "illiteracy costs the canonical 15% (education design §6)");
  failures += Expect(core::ResidentEfficiency(config, reference, 70.0F, kAgingFrom) < efficiency,
                     "past the aging threshold output falls");

  core::ResidentRow tough = reference;
  tough.stamina = 100.0F;
  tough.sportiness = 100.0F;
  failures += Expect(core::RestDrain(config, tough, core::WorkKind::kHarvest, 1.0F) <
                         core::RestDrain(config, reference, core::WorkKind::kHarvest, 1.0F),
                     "stamina and sport buy endurance, not output");
  return failures;
}

int TestWholeWorkingDay() {
  int failures = 0;
  const EmptyTableSet tables;
  const auto labor = core::CreateLaborSystem(tables);
  if (labor == nullptr) {
    return Expect(false, "factory yields a system");
  }
  DayWorld day(2);
  const core::FieldId field =
      day.AddField(core::FieldPhase::kSowing, 1.0F, core::Vec2{.x = 200.0F, .y = 0.0F});
  day.RunDay(*labor, 1);

  const core::FieldRow& worked = day.world.fields.rows[core::FindRow(day.world.fields, field)];
  failures += Expect(worked.work_days_remaining <= 0.0F, "the crew drained the day's demand");
  const core::FamilyRow& household =
      day.world.families.rows[core::FindRow(day.world.families, day.family)];
  failures += Expect(household.trudodni_account >= 95 && household.trudodni_account <= 110,
                     "one norm day of sowing paid about one trudoden to the family");
  bool cleared = true;
  for (const core::ResidentRow& resident : day.world.residents.rows) {
    cleared = cleared && resident.work.kind == core::WorkKind::kNone &&
              resident.work.worked_norm_days_today == 0.0F;
  }
  failures += Expect(cleared, "the close of the day clears every assignment");
  return failures;
}

int TestWalkOffPaysAndStops() {
  int failures = 0;
  const EmptyTableSet tables;
  const auto labor = core::CreateLaborSystem(tables);
  if (labor == nullptr) {
    return Expect(false, "factory yields a system");
  }
  DayWorld day(1);
  day.world.residents.rows[0].rest = 16.0F;  // an hour of work from the limit
  day.AddField(core::FieldPhase::kHarvest, 50.0F, core::Vec2{.x = 100.0F, .y = 0.0F});
  day.RunDay(*labor, 1);
  const core::FamilyRow& household =
      day.world.families.rows[core::FindRow(day.world.families, day.family)];
  failures += Expect(day.world.residents.rows[0].rest < 40.0F,
                     "an exhausted man ends the day tired, not fresh");
  failures +=
      Expect(household.trudodni_account > 0, "he is paid for the part of the day he did work");
  failures += Expect(day.world.fields.rows[0].work_days_remaining > 40.0F,
                     "and the work he walked away from is still waiting");
  return failures;
}

int TestBarnRunsOnTheDayOff() {
  int failures = 0;
  const EmptyTableSet tables;
  const auto labor = core::CreateLaborSystem(tables);
  if (labor == nullptr) {
    return Expect(false, "factory yields a system");
  }
  DayWorld day(3);
  day.AddField(core::FieldPhase::kHarvest, 5.0F, core::Vec2{.x = 100.0F, .y = 0.0F});
  const core::HerdId herd = day.AddUnitHerd(10, 150.0F);
  // Day 6 with day zero on Monday is a Sunday: field work stops, the barn
  // does not — the cows eat anyway.
  day.RunDay(*labor, 6);
  failures += Expect(day.world.calendar.weekday == core::Weekday::kSunday, "day 6 is a Sunday");
  failures +=
      Expect(day.world.fields.rows[0].work_days_remaining == 5.0F, "nobody reaps on a Sunday");
  const core::HerdRow& cows = day.world.herds.rows[core::FindRow(day.world.herds, herd)];
  failures += Expect(cows.care_days_remaining < 0.01F, "the barn was served all the same");
  return failures;
}

}  // namespace

/// The factory's contract on tables: a missing one keeps the canonical
/// defaults, a present one is read, a present-but-broken one refuses.
int TestLaborTableParsing() {
  int failures = 0;
  const FakeTable good(
      {"key", "value", "trudodni_rate", "rest_drain_per_norm_day"},
      {{"standard_day_hours", "12"}, {"travel_limit_hours", "3"}, {"harvest", "", "1.5", "5"}});
  const OneTableSet good_set("labor", good);
  failures +=
      Expect(core::CreateLaborSystem(good_set) != nullptr, "a readable labor table is read");

  const FakeTable not_a_number({"key", "value"}, {{"standard_day_hours", "рано"}});
  const OneTableSet bad_text("labor", not_a_number);
  failures += Expect(core::CreateLaborSystem(bad_text) == nullptr,
                     "a cell that is not a number refuses the factory");

  const FakeTable out_of_range({"key", "value"}, {{"path_factor", "99"}});
  const OneTableSet bad_range("labor", out_of_range);
  failures += Expect(core::CreateLaborSystem(bad_range) == nullptr,
                     "a value outside its range refuses the factory too");

  // A table of a shape the parser knows nothing about is not an error: every
  // key is optional, and what is absent keeps the canonical default.
  const FakeTable strange({"key", "value"}, {{"nobody_reads_this", "5"}});
  const OneTableSet strange_set("labor", strange);
  failures += Expect(core::CreateLaborSystem(strange_set) != nullptr,
                     "unknown keys are the balancer's notes, not a failure");
  return failures;
}

/// The barn leads the field at equal urgency: on a day when the harvest
/// window has run out too, the reaping crew must not swallow every hand.
int TestBarnLeadsTheClosedWindow() {
  int failures = 0;
  const core::Vec2 origin{.x = 0.0F, .y = 0.0F};
  const std::vector<core::AssignmentJob> jobs = {
      FieldJob(core::WorkKind::kHarvest, 1, origin, 40.0F, 0),
      [] {
        core::AssignmentJob care;
        care.kind = core::WorkKind::kHerdCare;
        care.herd = core::HerdId{7};
        care.work_days_remaining = 1.0F;
        care.window_days_left = 0;
        return care;
      }(),
  };
  std::vector<core::AssignmentCandidate> candidates;
  for (std::uint32_t row = 0; row < 4; ++row) {
    candidates.push_back(Worker(row, origin));
  }
  const auto plan = core::PlanDayAssignments(jobs, candidates, DayParams());
  std::uint32_t on_barn = 0;
  for (const std::uint32_t job : plan) {
    on_barn += job == 1 ? 1 : 0;
  }
  failures += Expect(on_barn >= 1, "somebody feeds the cows even at the peak of the harvest");
  return failures;
}

// ---------------------------------------------------------------------------
// Posts (task A7; manual/74-posts.md)
// ---------------------------------------------------------------------------

/// A table set with a roster of posts and a staff line for each, written to
/// disk so it goes through the real loader. Four posts, chosen to exercise
/// one column each: the groom (nothing but the working age), the milkmaid (a
/// woman's post), the agronomist (a diploma), the chairman (one to a
/// village).
std::filesystem::path WritePostTables() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_labor_posts";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "unit_types.csv") << "key\nhorse_yard\ncow_barn\n";
  std::ofstream(root / "professions.csv")
      << "key,min_education,min_age,max_age,gender,single_post\n"
         "groom,any,16,,any,0\n"
         "milkmaid,any,16,,female,0\n"
         "agronomist,vocational,16,,any,0\n"
         "chairman,any,25,60,any,1\n";
  std::ofstream(root / "unit_staff.csv") << "unit,profession,level,slots\n"
                                            "horse_yard,groom,,1\n"
                                            "horse_yard,chairman,,\n"
                                            "cow_barn,milkmaid,2,3\n"
                                            "cow_barn,agronomist,,\n";
  std::ofstream(root / "livestock.csv") << "key,care_days_per_real_year\nhorse,40\ncow,32\n";
  return root;
}

/// The world the post tests share: a yard at level 2, a barn at level 1, one
/// family and the adults the test asks for.
class PostWorld {
 public:
  explicit PostWorld(std::uint32_t adults) : day_(adults) {
    core::UnitRow stable;
    stable.position = core::Vec2{.x = 10.0F, .y = 0.0F};
    // The TYPE is what carries the post: a unit without one carries nothing,
    // whatever its level. Row 0 of unit_types.csv is the horse yard, row 1
    // the cow barn.
    stable.type = core::UnitTypeId{0};
    stable.level = 2;
    yard = core::AppendRow(day_.world.units, stable);
    core::UnitRow cowshed;
    cowshed.position = core::Vec2{.x = 20.0F, .y = 0.0F};
    cowshed.type = core::UnitTypeId{1};
    cowshed.level = 1;
    barn = core::AppendRow(day_.world.units, cowshed);
  }

  /// Stages an order the way the engine would: appended, kPending.
  core::OrderId Issue(const core::OrderRow& order) {
    core::OrderRow row = order;
    row.status = core::OrderStatus::kPending;
    return core::AppendRow(day_.world.orders, row);
  }

  core::OrderRow Appoint(std::uint32_t resident_row, core::UnitId unit, std::uint16_t post) const {
    core::OrderRow order;
    order.kind = core::OrderKind::kAppoint;
    order.resident = day_.world.residents.row_ids[resident_row];
    order.unit = unit;
    order.profession = core::ProfessionId{post};
    return order;
  }

  core::OrderRow Dismiss(std::uint32_t resident_row) const {
    core::OrderRow order;
    order.kind = core::OrderKind::kDismiss;
    order.resident = day_.world.residents.row_ids[resident_row];
    return order;
  }

  const core::OrderRow& Order(core::OrderId id) const {
    return day_.world.orders.rows[core::FindRow(day_.world.orders, id)];
  }

  bool Settled(core::OrderId id) const {
    return core::FindRow(day_.world.orders, id) != core::kNoRow;
  }

  void RunDay(core::ILaborSystem& labor, std::uint32_t day) { day_.RunDay(labor, day); }

  core::WorldState& world() { return day_.world; }

  core::UnitId yard;

  core::UnitId barn;

 private:
  DayWorld day_;
};

int TestPostTablesParse() {
  int failures = 0;
  const std::filesystem::path root = WritePostTables();
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  if (Expect(tables != nullptr, "the post tables load") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  core::LaborConfig config;
  failures += Expect(core::ParseLaborConfig(*tables, config, error), "and parse");
  failures += Expect(config.professions.size() == 4, "four posts in the roster");
  failures += Expect(config.staff.size() == 4, "four staff lines");
  failures += Expect(config.groom_post.value == 0, "the groom is found by his key");
  if (config.professions.size() == 4) {
    failures += Expect(config.professions[1].sex_rule == core::PostSexRule::kFemale,
                       "the milkmaid's column is read, not guessed");
    failures += Expect(config.professions[2].min_education == core::EducationStage::kVocational,
                       "the agronomist's diploma is a threshold");
    failures += Expect(
        config.professions[3].single_post != 0 && config.professions[3].max_age_years == 60.0F,
        "one chairman to a village, and he retires at sixty");
  }
  // The staff line is what makes a post EXIST at a unit, and the level
  // column is what makes it exist at one step of the ladder and not another.
  const core::UnitTypeId yard_type{0};
  const core::UnitTypeId barn_type{1};
  failures += Expect(core::FindStaffSlot(config, yard_type, 1, core::ProfessionId{0}) != nullptr,
                     "an empty level means every step of the ladder");
  failures += Expect(core::FindStaffSlot(config, yard_type, 0, core::ProfessionId{0}) == nullptr,
                     "a marked site carries no post, whatever the table says");
  failures += Expect(core::FindStaffSlot(config, barn_type, 1, core::ProfessionId{1}) == nullptr,
                     "the milkmaid's line names level 2, so level 1 has no place for her");
  failures += Expect(core::FindStaffSlot(config, barn_type, 2, core::ProfessionId{1}) != nullptr,
                     "and level 2 does");

  // A staff line naming a unit type nobody has is a broken export, not a
  // line to skip in silence.
  std::ofstream(root / "unit_staff.csv") << "unit,profession,level,slots\nno_such_unit,groom,,1\n";
  const auto broken = core::LoadTableSet(root.string(), &error);
  core::LaborConfig ignored;
  failures += Expect(broken != nullptr && !core::ParseLaborConfig(*broken, ignored, error),
                     "a staff line for a unit type nobody has is an error");
  std::filesystem::remove_all(root);
  return failures;
}

int TestAppointmentTakesEffectAtTheDayClose() {
  int failures = 0;
  const std::filesystem::path root = WritePostTables();
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto labor = tables == nullptr ? nullptr : core::CreateLaborSystem(*tables);
  if (Expect(labor != nullptr, "the post tables build a labor system") != 0) {
    return 1;
  }
  PostWorld post(1);
  const core::OrderId order = post.Issue(post.Appoint(0, post.yard, 0));

  // Hour by hour, so the moment of the change is measured and not assumed:
  // the post must be empty for every hour of the day and filled at its close.
  bool empty_all_day = true;
  for (std::uint32_t hour = 0; hour < core::kTicksPerDay; ++hour) {
    post.world().calendar.tick = hour;
    core::RefreshCalendarCaches(post.world().calendar);
    const core::WorldState previous = post.world();
    labor->RunAssignmentDecisions(previous, post.world());
    const bool held = post.world().residents.rows[0].post.profession.value == 0;
    if (hour + 1 < core::kTicksPerDay) {
      empty_all_day = empty_all_day && !held;
      // And it is ACCEPTED all that time: visible, and cancellable.
      empty_all_day = empty_all_day && post.Settled(order) &&
                      post.Order(order).status == core::OrderStatus::kAccepted;
    }
  }
  failures += Expect(empty_all_day, "the post stays empty, and the order accepted, all day long");
  failures += Expect(post.world().residents.rows[0].post.profession.value == 0 &&
                         post.world().residents.rows[0].post.unit.value == post.yard.value,
                     "and he holds it from the day's close");
  failures += Expect(post.Order(order).status == core::OrderStatus::kDone,
                     "the order is done, not still waiting");
  bool appointed_event = false;
  for (const core::SimEvent& event : post.world().step_events) {
    appointed_event = appointed_event || event.kind == core::EventKind::kAppointed;
  }
  failures += Expect(appointed_event, "and says so with an event");

  // AN ORDER THAT ARRIVES ON THE DAY'S LAST TICK waits a whole day, and this
  // is the case that measures the rule: an order issued earlier is applied at
  // that day's close whichever way round reading and applying are done, so it
  // proves nothing about the order of the two. This one does — put the read
  // before the apply and the post changes at the very midnight the order
  // arrived, while the day was still being paid out.
  const core::OrderId midnight = post.Issue(post.Appoint(0, post.yard, 3));
  post.world().calendar.tick = (2 * core::kTicksPerDay) - 1;  // the last hour of day 1
  core::RefreshCalendarCaches(post.world().calendar);
  const core::WorldState at_midnight = post.world();
  labor->RunAssignmentDecisions(at_midnight, post.world());
  failures += Expect(post.Order(midnight).status == core::OrderStatus::kAccepted &&
                         post.world().residents.rows[0].post.profession.value == 0,
                     "an order arriving at midnight misses that close and holds nothing yet");
  post.RunDay(*labor, 2);
  failures += Expect(post.world().residents.rows[0].post.profession.value == 3,
                     "and takes effect at the FIRST close after it was accepted, not the same one");

  // Dismissal is the same kind of order, and takes the same day.
  const core::OrderId release = post.Issue(post.Dismiss(0));
  post.world().step_events.clear();
  post.RunDay(*labor, 1);
  failures +=
      Expect(post.world().residents.rows[0].post.profession.value == core::kInvalidDefIdValue,
             "a dismissal empties the post at the close of its own day");
  failures += Expect(post.Order(release).status == core::OrderStatus::kDone,
                     "and is an order like any other");
  std::filesystem::remove_all(root);
  return failures;
}

int TestAppointmentRefusals() {
  int failures = 0;
  const std::filesystem::path root = WritePostTables();
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto labor = tables == nullptr ? nullptr : core::CreateLaborSystem(*tables);
  if (Expect(labor != nullptr, "the post tables build a labor system") != 0) {
    return 1;
  }
  PostWorld post(3);
  post.world().residents.rows[0].sex = core::Sex::kMale;
  post.world().residents.rows[1].sex = core::Sex::kFemale;
  post.world().residents.rows[2].birth_day = -10;  // a child, ~0.8 years old

  const core::OrderId no_such_unit = post.Issue(post.Appoint(0, core::UnitId{999}, 0));
  // The barn stands at level 2 for this test, because the milkmaid's staff
  // line names that step: at level 1 she would be refused by RULE and the
  // sex column would never be reached — a test that passes for the wrong
  // reason proves nothing about the column it claims to measure.
  post.world().units.rows[core::FindRow(post.world().units, post.barn)].level = 2;
  const core::OrderId wrong_sex = post.Issue(post.Appoint(0, post.barn, 1));
  const core::OrderId no_diploma = post.Issue(post.Appoint(0, post.barn, 2));
  const core::OrderId too_young = post.Issue(post.Appoint(2, post.yard, 0));
  const core::OrderId nothing_to_leave = post.Issue(post.Dismiss(1));

  // One tick is enough: validation happens in the step the order is read.
  post.world().calendar.tick = 0;
  core::RefreshCalendarCaches(post.world().calendar);
  const core::WorldState previous = post.world();
  labor->RunAssignmentDecisions(previous, post.world());

  const auto refusal = [&](core::OrderId id) { return post.Order(id).refusal; };
  failures += Expect(refusal(no_such_unit) == core::OrderRefusal::kNoSuchSubject,
                     "no such unit: the named thing is gone, exactly as in construction");
  failures += Expect(refusal(wrong_sex) == core::OrderRefusal::kNotEligible,
                     "a man is not put to the milking, and the table says so, not the code");
  failures += Expect(refusal(no_diploma) == core::OrderRefusal::kNotEligible,
                     "the agronomist's post wants the diploma it names");
  failures +=
      Expect(refusal(too_young) == core::OrderRefusal::kNotEligible, "a child is nobody's groom");
  failures += Expect(refusal(nothing_to_leave) == core::OrderRefusal::kRuleForbids,
                     "there is nothing to dismiss him from");
  // The pair that matters to the presentation: "no such unit" and "that unit
  // takes no groom" are two different sentences, and it cannot build them
  // from one code (boss, 2026-09-03). The barn exists and carries no groom.
  const core::OrderId wrong_place = post.Issue(post.Appoint(0, post.barn, 0));
  post.world().calendar.tick = 0;
  core::RefreshCalendarCaches(post.world().calendar);
  const core::WorldState again = post.world();
  labor->RunAssignmentDecisions(again, post.world());
  failures += Expect(refusal(wrong_place) == core::OrderRefusal::kRuleForbids,
                     "a unit that exists but carries no such post is refused by RULE, "
                     "and the two answers are not the same word");

  // Vacancy: the yard has ONE groom's place. The first man in takes it at the
  // day's close; the second is refused for the place and not by the rule.
  const core::OrderId first = post.Issue(post.Appoint(0, post.yard, 0));
  post.RunDay(*labor, 0);
  failures += Expect(post.Order(first).status == core::OrderStatus::kDone, "the first is groom");
  const core::OrderId second = post.Issue(post.Appoint(1, post.yard, 0));
  post.RunDay(*labor, 1);
  failures += Expect(post.Order(second).refusal == core::OrderRefusal::kNoVacancy,
                     "the second finds the place taken, which is not the same as forbidden");

  // Two orders for one man, both waiting: unresolvable without an order
  // nobody has seen.
  const core::OrderId move = post.Issue(post.Appoint(0, post.yard, 0));
  const core::OrderId also = post.Issue(post.Dismiss(0));
  post.world().calendar.tick = 2 * core::kTicksPerDay;
  core::RefreshCalendarCaches(post.world().calendar);
  const core::WorldState before = post.world();
  labor->RunAssignmentDecisions(before, post.world());
  const bool one_of_each = (post.Order(move).status == core::OrderStatus::kAccepted &&
                            post.Order(also).refusal == core::OrderRefusal::kConflictsWithActive) ||
                           (post.Order(also).status == core::OrderStatus::kAccepted &&
                            post.Order(move).refusal == core::OrderRefusal::kConflictsWithActive);
  failures += Expect(one_of_each, "of two orders for one man, the second waits for nothing");
  std::filesystem::remove_all(root);
  return failures;
}

int TestHolderIsOutOfThePoolAndOnHisOwnWork() {
  int failures = 0;
  const std::filesystem::path root = WritePostTables();
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto labor = tables == nullptr ? nullptr : core::CreateLaborSystem(*tables);
  if (Expect(labor != nullptr, "the post tables build a labor system") != 0) {
    return 1;
  }

  /// Runs a morning and reports where each man ended up. Measured at a
  /// WORKING hour and not at the day's end: the close clears every
  /// assignment, so a test that looks after it sees nothing at all.
  const auto morning = [&labor](PostWorld& post) {
    for (std::uint32_t hour = 0; hour <= 8; ++hour) {
      post.world().calendar.tick = hour;
      core::RefreshCalendarCaches(post.world().calendar);
      const core::WorldState previous = post.world();
      labor->RunAssignmentDecisions(previous, post.world());
    }
  };
  const auto add_harvest = [](PostWorld& post) {
    core::FieldRow field;
    field.center = core::Vec2{.x = 40.0F, .y = 0.0F};
    field.area_ga = 10.0F;
    field.phase = core::FieldPhase::kHarvest;
    field.work_days_remaining = 40.0F;
    core::AppendRow(post.world().fields, field);
  };

  // -- with a herd at his yard: he stands on it, and on THAT one ------------
  PostWorld stabled(2);
  core::HerdRow team;
  team.unit = stabled.yard;
  team.kind = core::LivestockKindId{0};
  team.adult_count = 16;
  const core::HerdId horses = core::AppendRow(stabled.world().herds, team);
  core::HerdRow cows;
  cows.unit = stabled.barn;
  cows.kind = core::LivestockKindId{1};
  cows.adult_count = 20;
  core::AppendRow(stabled.world().herds, cows);
  add_harvest(stabled);
  stabled.world().residents.rows[0].post.profession = core::ProfessionId{0};
  stabled.world().residents.rows[0].post.unit = stabled.yard;
  morning(stabled);
  const core::WorkAssignment& groom = stabled.world().residents.rows[0].work;
  failures += Expect(groom.kind == core::WorkKind::kHerdCare && groom.herd.value == horses.value,
                     "the groom stands at his OWN yard's herd, not at the barn next door");

  // -- with no herd there: out of the pool, and idle in silence -------------
  // This is the half that proves the first: the accountant has a harvest
  // crying out for hands and does not touch him.
  PostWorld reserved(2);
  add_harvest(reserved);
  reserved.world().residents.rows[0].post.profession = core::ProfessionId{0};
  reserved.world().residents.rows[0].post.unit = reserved.yard;
  morning(reserved);
  failures += Expect(reserved.world().residents.rows[0].work.kind == core::WorkKind::kNone,
                     "a post with no work the core models keeps its holder reserved and idle");
  failures += Expect(reserved.world().residents.rows[1].work.kind == core::WorkKind::kHarvest,
                     "while the man without a post is sent to the harvest that wanted them both");
  bool silent = true;
  for (const core::SimEvent& event : reserved.world().step_events) {
    silent = silent && event.kind != core::EventKind::kAppointed;
  }
  failures += Expect(silent, "and an idle holder says nothing about it");
  std::filesystem::remove_all(root);
  return failures;
}

int TestYardWithoutGroomAlarm() {
  int failures = 0;
  const std::filesystem::path root = WritePostTables();
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto labor = tables == nullptr ? nullptr : core::CreateLaborSystem(*tables);
  if (Expect(labor != nullptr, "the post tables build a labor system") != 0) {
    return 1;
  }
  PostWorld post(1);
  core::HerdRow team;
  team.kind = core::LivestockKindId{0};  // horses
  team.household = post.world().families.row_ids[0];
  team.adult_count = 16;
  core::AppendRow(post.world().herds, team);

  std::vector<core::Alarm> alarms;
  labor->CollectAlarms(post.world(), alarms);
  failures += Expect(alarms.size() == 1 && alarms[0].kind == core::AlarmKind::kYardWithoutGroom &&
                         alarms[0].unit.value == post.yard.value && alarms[0].amount == 16,
                     "a built yard with no groom and the team still at the yards raises it");

  // Appointed, and the horses NOT yet moved: the alarm is gone the moment
  // the post is filled. The groom's one idle morning makes no sound
  // (boss's condition of 2026-09-03).
  post.world().residents.rows[0].post.profession = core::ProfessionId{0};
  post.world().residents.rows[0].post.unit = post.yard;
  alarms.clear();
  labor->CollectAlarms(post.world(), alarms);
  failures += Expect(alarms.empty(), "appointed is enough: the idle morning is silent");

  // And once the team is in, the question is closed for the campaign, even
  // if the groom is later dismissed.
  post.world().residents.rows[0].post = core::PostAssignment{};
  post.world().chairman.horses_stabled = 1;
  alarms.clear();
  labor->CollectAlarms(post.world(), alarms);
  failures += Expect(alarms.empty(), "after the horses are stabled it never comes back");
  std::filesystem::remove_all(root);
  return failures;
}

int main() {
  int failures = 0;
  failures += TestSurplusIdles();
  failures += TestRoadLimit();
  failures += TestHorsePoolAndLock();
  failures += TestWindowUrgency();
  failures += TestPlacementLevels();
  failures += TestBoundarySystemStub();
  failures += TestSolarWindow();
  failures += TestRoadByWhatHeTravelsOn();
  failures += TestReferenceWorkerDeliversOneNorm();
  failures += TestWholeWorkingDay();
  failures += TestWalkOffPaysAndStops();
  failures += TestBarnRunsOnTheDayOff();
  failures += TestLaborTableParsing();
  failures += TestBarnLeadsTheClosedWindow();
  failures += TestPostTablesParse();
  failures += TestAppointmentTakesEffectAtTheDayClose();
  failures += TestAppointmentRefusals();
  failures += TestHolderIsOutOfThePoolAndOnHisOwnWork();
  failures += TestYardWithoutGroomAlarm();
  if (failures == 0) {
    std::cout << "unit_core_labor: all checks passed\n";
  }
  return failures;
}
