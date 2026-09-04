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

  // AND THE SAME CONFLICT FROM THE OTHER SIDE (task A8 delivery cycle,
  // second iteration). A work order for a post holder was already refused
  // with kConflictsWithActive; an APPOINTMENT of a man who has a standing
  // work order was not checked at all, so both survived and the standing
  // order overwrote the post placement every morning after. Whichever
  // arrives second is refused: the chairman releases him first, which is
  // one order, not a guess about which of two he meant.
  core::HerdRow cows;
  cows.unit = post.barn;
  cows.adult_count = 6;
  const core::HerdId herd = core::AppendRow(post.world().herds, cows);
  core::OrderRow work_first;
  work_first.kind = core::OrderKind::kAssignWork;
  work_first.status = core::OrderStatus::kPending;
  // The milkmaid's place at the barn, which is free: the groom's at the yard
  // was taken three cases ago, and kNoVacancy would answer before the rule
  // under test was ever reached — a refusal for the wrong reason measures
  // nothing about the reason it claims.
  work_first.resident = post.world().residents.row_ids[1];
  work_first.work = core::WorkKind::kHerdCare;
  work_first.herd = herd;
  const core::OrderId standing = core::AppendRow(post.world().orders, work_first);
  post.RunDay(*labor, 3);
  failures +=
      Expect(post.Order(standing).status == core::OrderStatus::kAccepted, "the work order stands");
  const core::OrderId late_post = post.Issue(post.Appoint(1, post.barn, 1));
  post.RunDay(*labor, 4);
  failures += Expect(post.Order(late_post).refusal == core::OrderRefusal::kConflictsWithActive,
                     "a woman with a standing order is not appointed over it, and it says why");
  failures +=
      Expect(post.world().residents.rows[1].post.profession.value == core::kInvalidDefIdValue,
             "and the refused appointment left her without a post");

  // BUT THE WORKFLOW THE REFUSAL PRESCRIBES MUST NOT BE THE ONE IT BREAKS.
  // "Release him, then appoint him" is one gesture in the office and two
  // rows in one batch; the work verbs are read after the post verbs in the
  // tick, so the appointment meets a standing order that is settled kDone a
  // few statements later. Found by the third iteration of the A8 cycle.
  core::OrderRow let_go;
  let_go.kind = core::OrderKind::kReleaseWork;
  let_go.resident = post.world().residents.row_ids[1];
  const core::OrderId released = post.Issue(let_go);
  const core::OrderId together = post.Issue(post.Appoint(1, post.barn, 1));
  post.RunDay(*labor, 5);
  failures += Expect(post.Order(released).status == core::OrderStatus::kDone,
                     "the release in the same batch is done");
  failures += Expect(post.Order(together).refusal != core::OrderRefusal::kConflictsWithActive,
                     "and the appointment beside it is NOT refused against an order just ended");
  failures += Expect(post.world().residents.rows[1].post.profession.value == 1,
                     "she ends the day a milkmaid: released and appointed in one gesture");

  // AND THE SAME SYMMETRY FROM THE WORK SIDE (boss's decision of 2026-09-04:
  // what a man has already promised has ONE home, and it is the book).
  //
  // The window is between accepting a post order and applying it at the day's
  // close: the resident row still says yesterday. A check that reads the row
  // there answers the right question about the wrong moment.
  // The third of the household, who holds NOTHING: residents 0 and 1 already
  // carry posts from the cases above, and a man with an applied post would be
  // refused by the older half of the rule — the test would pass for the wrong
  // reason and say nothing about the half under test.
  post.world().residents.rows[2].birth_day = -300;  // grown since the child case
  post.world().residents.rows[2].education_stage = core::EducationStage::kHigher;
  const core::OrderId granted = post.Issue(post.Appoint(2, post.barn, 2));
  post.world().calendar.tick = 6 * core::kTicksPerDay;
  core::RefreshCalendarCaches(post.world().calendar);
  {
    const core::WorldState before_grant = post.world();
    labor->RunAssignmentDecisions(before_grant, post.world());
  }
  failures += Expect(post.Order(granted).status == core::OrderStatus::kAccepted,
                     "the appointment is granted and waits for the day's close");
  failures +=
      Expect(post.world().residents.rows[2].post.profession.value == core::kInvalidDefIdValue,
             "and until then the resident row still says he holds nothing");
  core::OrderRow work_second;
  work_second.kind = core::OrderKind::kAssignWork;
  work_second.resident = post.world().residents.row_ids[2];
  work_second.work = core::WorkKind::kHerdCare;
  work_second.herd = herd;
  const core::OrderId late_work = post.Issue(work_second);
  {
    const core::WorldState before_work = post.world();
    labor->RunAssignmentDecisions(before_work, post.world());
  }
  failures += Expect(post.Order(late_work).refusal == core::OrderRefusal::kConflictsWithActive,
                     "a work order over a post granted this morning is refused, not accepted");
  failures += Expect(post.Order(granted).status == core::OrderStatus::kAccepted,
                     "and the appointment it could not overrule is still standing");

  // The mirror of "release, then appoint": DISMISS, then put him to work.
  // One gesture, two rows, and the post verbs are read first — so the work
  // order meets a post that is already on its way out.
  post.RunDay(*labor, 7);
  failures += Expect(post.world().residents.rows[2].post.profession.value == 2,
                     "he is the agronomist by the close of that day");
  const core::OrderId let_out = post.Issue(post.Dismiss(2));
  const core::OrderId together_work = post.Issue(work_second);
  post.RunDay(*labor, 8);
  failures += Expect(post.Order(let_out).status == core::OrderStatus::kDone,
                     "the dismissal in the same batch goes through");
  failures += Expect(post.Order(together_work).refusal != core::OrderRefusal::kConflictsWithActive,
                     "and the work order beside it is NOT refused against a post being ended");

  // THE CANCELLED DISMISSAL. The order above was let past a held post on the
  // strength of a kDismiss — and a dismissal does not settle in the tick it
  // is read: it waits at kAccepted until the day's close, and the
  // presentation may take it back in between. Take it back, and without the
  // tick-by-tick invariant the man keeps his post AND his standing order for
  // the rest of the campaign: two answers to "what does this man do",
  // reached by the one route that goes round both admission checks.
  DayWorld plain(1);
  const core::HerdId barn_herd = plain.AddUnitHerd(4, 20.0F);
  plain.world.residents.rows[0].post.profession = core::ProfessionId{0};
  plain.world.residents.rows[0].post.unit = plain.world.units.row_ids[0];
  core::OrderRow drop;
  drop.kind = core::OrderKind::kDismiss;
  drop.status = core::OrderStatus::kPending;
  drop.resident = plain.world.residents.row_ids[0];
  const core::OrderId dropping = core::AppendRow(plain.world.orders, drop);
  core::OrderRow put_to_work;
  put_to_work.kind = core::OrderKind::kAssignWork;
  put_to_work.status = core::OrderStatus::kPending;
  put_to_work.resident = plain.world.residents.row_ids[0];
  put_to_work.work = core::WorkKind::kHerdCare;
  put_to_work.herd = barn_herd;
  const core::OrderId conditional = core::AppendRow(plain.world.orders, put_to_work);
  {
    const core::WorldState before_batch = plain.world;
    labor->RunAssignmentDecisions(before_batch, plain.world);
  }
  const std::uint32_t granted_row = core::FindRow(plain.world.orders, conditional);
  failures += Expect(granted_row != core::kNoRow && plain.world.orders.rows[granted_row].status ==
                                                        core::OrderStatus::kAccepted,
                     "the work order stands while its dismissal is on the way");

  // The chairman takes the dismissal back — what ISession::CancelOrder does
  // to a kAccepted row.
  plain.world.orders.rows[core::FindRow(plain.world.orders, dropping)].status =
      core::OrderStatus::kCancelled;
  plain.world.calendar.tick = 1;
  core::RefreshCalendarCaches(plain.world.calendar);
  {
    const core::WorldState after_cancel = plain.world;
    labor->RunAssignmentDecisions(after_cancel, plain.world);
  }
  const std::uint32_t stranded = core::FindRow(plain.world.orders, conditional);
  failures += Expect(stranded != core::kNoRow &&
                         plain.world.orders.rows[stranded].status == core::OrderStatus::kRefused,
                     "cancel the dismissal and the order granted against it ends too");
  failures += Expect(stranded != core::kNoRow && plain.world.orders.rows[stranded].refusal ==
                                                     core::OrderRefusal::kConflictsWithActive,
                     "and it says which of the two answers it lost to");
  failures += Expect(plain.world.residents.rows[0].post.profession.value == 0,
                     "the post is the one that survives: the order stood on a dismissal "
                     "that never happened");

  // WITHIN ONE BATCH, AND IN BOTH ORDERS. The post verbs are read before the
  // work verbs, so an appointment used to win every same-batch race however
  // late it was given — the reverse of "the second one yields". The book's
  // row order is the order the chairman gave them in, and it is the only
  // thing that can tell first from second here. A test that tries just one
  // order proves nothing: a rule that always refuses the work order passes
  // it, and so does a rule that always refuses the appointment.
  for (int job_given_first = 0; job_given_first < 2; ++job_given_first) {
    PostWorld race(1);
    core::HerdRow race_cows;
    race_cows.unit = race.barn;
    race_cows.adult_count = 3;
    const core::HerdId race_herd = core::AppendRow(race.world().herds, race_cows);
    race.world().residents.rows[0].sex = core::Sex::kMale;
    core::OrderRow job;
    job.kind = core::OrderKind::kAssignWork;
    job.resident = race.world().residents.row_ids[0];
    job.work = core::WorkKind::kHerdCare;
    job.herd = race_herd;

    core::OrderId job_id;
    core::OrderId post_id;
    if (job_given_first != 0) {
      job_id = race.Issue(job);
      post_id = race.Issue(race.Appoint(0, race.yard, 0));
    } else {
      post_id = race.Issue(race.Appoint(0, race.yard, 0));
      job_id = race.Issue(job);
    }
    race.world().calendar.tick = 0;
    core::RefreshCalendarCaches(race.world().calendar);
    {
      const core::WorldState before_race = race.world();
      labor->RunAssignmentDecisions(before_race, race.world());
    }
    const bool job_won = race.Order(job_id).status == core::OrderStatus::kAccepted &&
                         race.Order(post_id).refusal == core::OrderRefusal::kConflictsWithActive;
    const bool post_won = race.Order(post_id).status == core::OrderStatus::kAccepted &&
                          race.Order(job_id).refusal == core::OrderRefusal::kConflictsWithActive;
    failures +=
        Expect(job_won != post_won, "exactly one of the two stands, and the other says why");
    failures += Expect(job_given_first != 0 ? job_won : post_won,
                       job_given_first != 0 ? "the work order given first is the one that stands"
                                            : "the appointment given first is the one that stands");
  }
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

// ---------------------------------------------------------------------------
// Standing work orders (task A8)
// ---------------------------------------------------------------------------

/// The chairman's standing order, driven through the subsystem the way the
/// engine drives it: the book is appended to, labor reads it at the END of
/// the tick, and the override lands the NEXT morning.
int TestStandingWorkOrder() {
  int failures = 0;
  const EmptyTableSet tables;
  const auto labor = core::CreateLaborSystem(tables);
  if (Expect(labor != nullptr, "factory yields a system") != 0) {
    return 1;
  }
  DayWorld day(2);
  // Two jobs far enough apart that the accountant's own choice is visible:
  // a near harvest that will take everybody, and a far one that will not.
  const core::FieldId near_field =
      day.AddField(core::FieldPhase::kHarvest, 40.0F, core::Vec2{.x = 20.0F, .y = 0.0F});
  const core::FieldId far_field =
      day.AddField(core::FieldPhase::kHarvest, 40.0F, core::Vec2{.x = 300.0F, .y = 0.0F});

  core::OrderRow assign;
  assign.kind = core::OrderKind::kAssignWork;
  assign.status = core::OrderStatus::kPending;
  assign.resident = day.world.residents.row_ids[0];
  assign.work = core::WorkKind::kHarvest;
  assign.field = far_field;
  const core::OrderId standing = core::AppendRow(day.world.orders, assign);

  // DAY ONE, MEASURED AT A WORKING HOUR AND AT THE MAN, not at the order.
  // The claim is "a change takes effect after the current day" (time design
  // §11), and the only thing that can show it is WHERE HE STANDS TODAY.
  // Checking the order's status instead proves nothing: the row reads
  // kAccepted whether it was read first in the tick or last, and the first
  // draft of this test did exactly that and passed against a deliberately
  // broken ordering.
  for (std::uint32_t hour = 0; hour <= 8; ++hour) {
    day.world.calendar.tick = hour;
    core::RefreshCalendarCaches(day.world.calendar);
    const core::WorldState previous = day.world;
    labor->RunAssignmentDecisions(previous, day.world);
  }
  failures += Expect(day.world.residents.rows[0].work.field.value != far_field.value,
                     "an order given today moves nobody today");
  for (std::uint32_t hour = 9; hour < core::kTicksPerDay; ++hour) {
    day.world.calendar.tick = hour;
    core::RefreshCalendarCaches(day.world.calendar);
    const core::WorldState previous = day.world;
    labor->RunAssignmentDecisions(previous, day.world);
  }
  const std::uint32_t order_row = core::FindRow(day.world.orders, standing);
  failures += Expect(order_row != core::kNoRow &&
                         day.world.orders.rows[order_row].status == core::OrderStatus::kAccepted,
                     "a work order is accepted and STAYS accepted: that is what standing means");

  // DAY TWO, measured at a working hour — the close clears every assignment,
  // so a test that looks at the end of the day sees nothing at all.
  for (std::uint32_t hour = 0; hour <= 8; ++hour) {
    day.world.calendar.tick = core::kTicksPerDay + hour;
    core::RefreshCalendarCaches(day.world.calendar);
    const core::WorldState previous = day.world;
    labor->RunAssignmentDecisions(previous, day.world);
  }
  failures += Expect(day.world.residents.rows[0].work.field.value == far_field.value,
                     "and from the next morning the chairman's man is where he was sent");
  failures += Expect(day.world.residents.rows[1].work.field.value == near_field.value,
                     "while the accountant keeps placing everybody else");

  // A SECOND order for the same man supersedes the first, and the first is
  // CANCELLED — not done. It never finished; it was taken back.
  core::OrderRow moved = assign;
  moved.field = near_field;
  moved.status = core::OrderStatus::kPending;
  core::AppendRow(day.world.orders, moved);
  day.world.calendar.tick = 2 * core::kTicksPerDay;
  core::RefreshCalendarCaches(day.world.calendar);
  {
    const core::WorldState previous = day.world;
    labor->RunAssignmentDecisions(previous, day.world);
  }
  const std::uint32_t old_row = core::FindRow(day.world.orders, standing);
  failures += Expect(old_row != core::kNoRow &&
                         day.world.orders.rows[old_row].status == core::OrderStatus::kCancelled,
                     "a newer order takes the older one back rather than completing it");
  return failures;
}

/// The two corners the delivery cycle found, and neither is a day like the
/// days above.
///
/// UB-001, THE REST DAY. The chairman's standing order used to reach the
/// man on a Sunday or not, depending on something that has nothing to do
/// with him: whether the village had a barn. On a day off the accountant
/// collects no field work, so his job list was empty and StartDay returned
/// before the standing orders — but herd care IS collected on a day off,
/// because animals eat on Sunday, so as soon as one herd stood at a unit
/// the list was not empty, the return did not happen, and the chairman's
/// man worked every Sunday of his life. The corner is therefore a REST DAY
/// IN A VILLAGE WITH A BARN, and a test without the barn measures nothing.
///
/// MEM-001, THE DEAD MAN'S ORDER. A standing order whose man has died could
/// never reach a terminal status: kReleaseWork refuses for kNoSuchSubject
/// before it looks for the standing row, and nothing else moves it. The
/// sweep removes terminal rows only, so the row lived for the rest of the
/// campaign — in every save, and walked by an O(n) scan inside an O(n) loop
/// on all twenty-four ticks of every day.
int TestStandingWorkCorners() {
  int failures = 0;
  const EmptyTableSet tables;
  const auto labor = core::CreateLaborSystem(tables);
  if (Expect(labor != nullptr, "factory yields a system") != 0) {
    return 1;
  }
  DayWorld day(1);
  const core::FieldId field =
      day.AddField(core::FieldPhase::kHarvest, 400.0F, core::Vec2{.x = 20.0F, .y = 0.0F});
  day.AddUnitHerd(10, 30.0F);  // the barn: it gives the day off a job list

  core::OrderRow assign;
  assign.kind = core::OrderKind::kAssignWork;
  assign.status = core::OrderStatus::kPending;
  assign.resident = day.world.residents.row_ids[0];
  assign.work = core::WorkKind::kHarvest;
  assign.field = field;
  const core::OrderId standing = core::AppendRow(day.world.orders, assign);

  // Day 0 is Monday (DayWorld sets day zero's weekday), so day 6 is the
  // Sunday. Run up to it; the order is read on day 0 and stands from day 1.
  for (std::uint32_t index = 0; index <= 5; ++index) {
    day.RunDay(*labor, index);
  }
  // The rest day itself, measured mid-day: the close clears every
  // assignment, so a look after the last tick sees nothing either way.
  for (std::uint32_t hour = 0; hour <= 8; ++hour) {
    day.world.calendar.tick = (6 * core::kTicksPerDay) + hour;
    core::RefreshCalendarCaches(day.world.calendar);
    const core::WorldState previous = day.world;
    labor->RunAssignmentDecisions(previous, day.world);
  }
  failures += Expect(day.world.calendar.weekday == core::Weekday::kSunday,
                     "day six of a week beginning on Monday is the Sunday");
  failures += Expect(day.world.residents.rows[0].work.field.value != field.value,
                     "the chairman commands the work, not the calendar: no field on a rest day");
  for (std::uint32_t hour = 9; hour < core::kTicksPerDay; ++hour) {
    day.world.calendar.tick = (6 * core::kTicksPerDay) + hour;
    core::RefreshCalendarCaches(day.world.calendar);
    const core::WorldState previous = day.world;
    labor->RunAssignmentDecisions(previous, day.world);
  }
  // And the Monday after it: the order did not lapse over the weekend.
  for (std::uint32_t hour = 0; hour <= 8; ++hour) {
    day.world.calendar.tick = (7 * core::kTicksPerDay) + hour;
    core::RefreshCalendarCaches(day.world.calendar);
    const core::WorldState previous = day.world;
    labor->RunAssignmentDecisions(previous, day.world);
  }
  failures += Expect(day.world.residents.rows[0].work.field.value == field.value,
                     "and on the Monday he is back on the job he was ordered to");

  // Now his man dies. The order can no longer be released, superseded or
  // finished by any rule there is, so it is closed here or never.
  core::RemoveRow(day.world.residents, day.world.residents.row_ids[0]);
  day.RunDay(*labor, 8);
  const std::uint32_t orphan = core::FindRow(day.world.orders, standing);
  failures +=
      Expect(orphan != core::kNoRow &&
                 day.world.orders.rows[orphan].status == core::OrderStatus::kRefused &&
                 day.world.orders.rows[orphan].refusal == core::OrderRefusal::kNoSuchSubject,
             "an order whose man is gone ends, and says why: the sweep can take it");
  return failures;
}

/// Four corners the SECOND iteration of the delivery cycle found, and every
/// one of them is about the fixes the first iteration made. This is the
/// cycle reviewing its own edits, which is what it is for.
///
/// 1. The rest day must stop the same work it stops for everyone else and
///    no more: a blanket "no standing orders on a Sunday" dropped the
///    ordered cowman back into the pool while animals still had to be fed.
/// 2. A construction order names a UNIT; the target check was looking for
///    it in the fields table.
/// 3. Closing orphaned orders had to come LAST, or a kReleaseWork arriving
///    in the same step was told there was nothing to release.
/// 4. A post and a standing order were refused in one direction only, so
///    kAppoint after kAssignWork left both alive and the order overwrote
///    the post every morning.
int TestSecondIterationCorners() {
  int failures = 0;
  const EmptyTableSet tables;
  const auto labor = core::CreateLaborSystem(tables);
  if (Expect(labor != nullptr, "factory yields a system") != 0) {
    return 1;
  }
  DayWorld day(2);
  const core::HerdId herd = day.AddUnitHerd(10, 30.0F);
  const core::FieldId field =
      day.AddField(core::FieldPhase::kHarvest, 400.0F, core::Vec2{.x = 20.0F, .y = 0.0F});

  // One man ordered to the barn, one to the field. The Sunday must part them.
  core::OrderRow to_barn;
  to_barn.kind = core::OrderKind::kAssignWork;
  to_barn.status = core::OrderStatus::kPending;
  to_barn.resident = day.world.residents.row_ids[0];
  to_barn.work = core::WorkKind::kHerdCare;
  to_barn.herd = herd;
  core::AppendRow(day.world.orders, to_barn);
  core::OrderRow to_field;
  to_field.kind = core::OrderKind::kAssignWork;
  to_field.status = core::OrderStatus::kPending;
  to_field.resident = day.world.residents.row_ids[1];
  to_field.work = core::WorkKind::kHarvest;
  to_field.field = field;
  core::AppendRow(day.world.orders, to_field);

  for (std::uint32_t index = 0; index <= 5; ++index) {
    day.RunDay(*labor, index);
  }
  for (std::uint32_t hour = 0; hour <= 8; ++hour) {  // the Sunday, mid-day
    day.world.calendar.tick = (6 * core::kTicksPerDay) + hour;
    core::RefreshCalendarCaches(day.world.calendar);
    const core::WorldState previous = day.world;
    labor->RunAssignmentDecisions(previous, day.world);
  }
  failures += Expect(day.world.calendar.weekday == core::Weekday::kSunday, "day six is the Sunday");
  failures += Expect(day.world.residents.rows[0].work.kind == core::WorkKind::kHerdCare &&
                         day.world.residents.rows[0].work.herd.value == herd.value,
                     "the ordered cowman keeps his barn on a rest day: animals eat on Sunday");
  failures += Expect(day.world.residents.rows[1].work.field.value != field.value,
                     "and the ordered harvester does not go to the field");

  // A construction order names a unit. Its target must be looked for among
  // UNITS: routed to the fields table it can only ever be refused.
  DayWorld site_day(1);
  core::UnitRow site;
  site.level = 0;
  site.construction.labor_days_remaining = 10.0F;
  const core::UnitId site_id = core::AppendRow(site_day.world.units, site);
  core::OrderRow build;
  build.kind = core::OrderKind::kAssignWork;
  build.status = core::OrderStatus::kPending;
  build.resident = site_day.world.residents.row_ids[0];
  build.work = core::WorkKind::kConstruction;
  build.unit = site_id;
  const core::OrderId building = core::AppendRow(site_day.world.orders, build);
  site_day.RunDay(*labor, 0);
  const std::uint32_t build_row = core::FindRow(site_day.world.orders, building);
  failures += Expect(build_row != core::kNoRow && site_day.world.orders.rows[build_row].status ==
                                                      core::OrderStatus::kAccepted,
                     "an order to build names a unit, and the unit is where it is looked for");

  // A release arriving in the same step as the target's disappearance must
  // still be a release, not "there is nothing to release him from".
  DayWorld gone_day(1);
  const core::HerdId doomed = gone_day.AddUnitHerd(4, 25.0F);
  core::OrderRow tend;
  tend.kind = core::OrderKind::kAssignWork;
  tend.status = core::OrderStatus::kPending;
  tend.resident = gone_day.world.residents.row_ids[0];
  tend.work = core::WorkKind::kHerdCare;
  tend.herd = doomed;
  const core::OrderId standing = core::AppendRow(gone_day.world.orders, tend);
  gone_day.RunDay(*labor, 0);
  core::RemoveRow(gone_day.world.herds, doomed);
  core::OrderRow release;
  release.kind = core::OrderKind::kReleaseWork;
  release.status = core::OrderStatus::kPending;
  release.resident = gone_day.world.residents.row_ids[0];
  const core::OrderId freeing = core::AppendRow(gone_day.world.orders, release);
  gone_day.world.calendar.tick = core::kTicksPerDay;
  core::RefreshCalendarCaches(gone_day.world.calendar);
  {
    const core::WorldState previous = gone_day.world;
    labor->RunAssignmentDecisions(previous, gone_day.world);
  }
  const std::uint32_t freed_row = core::FindRow(gone_day.world.orders, freeing);
  failures += Expect(freed_row != core::kNoRow &&
                         gone_day.world.orders.rows[freed_row].status == core::OrderStatus::kDone,
                     "a release in the step the herd vanished is still a release");
  const std::uint32_t stood = core::FindRow(gone_day.world.orders, standing);
  failures += Expect(
      stood != core::kNoRow && gone_day.world.orders.rows[stood].status == core::OrderStatus::kDone,
      "and the order it ended is done, not refused behind its back");
  return failures;
}

int TestReleaseWork() {
  int failures = 0;
  const EmptyTableSet tables;
  const auto labor = core::CreateLaborSystem(tables);
  if (Expect(labor != nullptr, "factory yields a system") != 0) {
    return 1;
  }
  DayWorld day(1);
  const core::FieldId field =
      day.AddField(core::FieldPhase::kHarvest, 40.0F, core::Vec2{.x = 20.0F, .y = 0.0F});
  core::OrderRow assign;
  assign.kind = core::OrderKind::kAssignWork;
  assign.status = core::OrderStatus::kPending;
  assign.resident = day.world.residents.row_ids[0];
  assign.work = core::WorkKind::kHarvest;
  assign.field = field;
  const core::OrderId standing = core::AppendRow(day.world.orders, assign);
  day.RunDay(*labor, 0);

  // Releasing a man nobody ordered is refused, and refused BY RULE rather
  // than by subject: the man exists, the order does not.
  core::OrderRow stray;
  stray.kind = core::OrderKind::kReleaseWork;
  stray.status = core::OrderStatus::kPending;
  stray.resident = core::ResidentId{999};
  const core::OrderId no_such = core::AppendRow(day.world.orders, stray);

  core::OrderRow release;
  release.kind = core::OrderKind::kReleaseWork;
  release.status = core::OrderStatus::kPending;
  release.resident = day.world.residents.row_ids[0];
  const core::OrderId freed = core::AppendRow(day.world.orders, release);

  day.world.calendar.tick = core::kTicksPerDay;
  core::RefreshCalendarCaches(day.world.calendar);
  {
    const core::WorldState previous = day.world;
    labor->RunAssignmentDecisions(previous, day.world);
  }
  const auto status = [&](core::OrderId id) {
    const std::uint32_t row = core::FindRow(day.world.orders, id);
    // A removed row reads as kRefused rather than as a status of its own:
    // there is no "no status", and the sweep only removes what ended.
    return row == core::kNoRow ? core::OrderStatus::kRefused : day.world.orders.rows[row].status;
  };
  failures += Expect(status(freed) == core::OrderStatus::kDone, "the release is done");
  failures += Expect(status(standing) == core::OrderStatus::kDone,
                     "and the standing order it ended is done too: it ran as ordered");
  const std::uint32_t stray_row = core::FindRow(day.world.orders, no_such);
  failures += Expect(stray_row != core::kNoRow && day.world.orders.rows[stray_row].refusal ==
                                                      core::OrderRefusal::kNoSuchSubject,
                     "releasing a man who does not exist is refused for the man");
  return failures;
}

/// The four ways a work order is turned down. Each of them is a real rule
/// with a real cost if it is missed: a child sent to the harvest, a groom
/// pulled off his post by a stray order, a field that was ploughed under
/// yesterday. The refusal has to name WHICH — "no" without a reason is what
/// the boundary already said before this task.
int TestAssignWorkRefusals() {
  int failures = 0;
  const EmptyTableSet tables;
  const auto labor = core::CreateLaborSystem(tables);
  if (Expect(labor != nullptr, "factory yields a system") != 0) {
    return 1;
  }
  DayWorld day(2);
  const core::FieldId field =
      day.AddField(core::FieldPhase::kHarvest, 40.0F, core::Vec2{.x = 20.0F, .y = 0.0F});

  // The second man holds a post: he already has an answer to "what does this
  // man do", and a work order would be a second one.
  day.world.residents.rows[1].post.profession = core::ProfessionId{0};
  day.world.residents.rows[1].post.unit = day.world.units.row_ids[0];

  // A child of the same household: born today, and life runs four times the
  // calendar, so he is nowhere near the working age.
  core::ResidentRow child;
  child.family = day.family;
  child.birth_day = 0;
  const core::ResidentId child_id = core::AppendRow(day.world.residents, child);

  const auto order = [&](core::ResidentId resident, core::FieldId target) {
    core::OrderRow row;
    row.kind = core::OrderKind::kAssignWork;
    row.status = core::OrderStatus::kPending;
    row.resident = resident;
    row.work = core::WorkKind::kHarvest;
    row.field = target;
    return core::AppendRow(day.world.orders, row);
  };
  const core::OrderId no_man = order(core::ResidentId{999}, field);
  const core::OrderId no_field = order(day.world.residents.row_ids[0], core::FieldId{999});
  const core::OrderId on_post = order(day.world.residents.row_ids[1], field);
  const core::OrderId too_young = order(child_id, field);

  {
    const core::WorldState previous = day.world;
    labor->RunAssignmentDecisions(previous, day.world);
  }
  const auto refusal = [&](core::OrderId id) {
    const std::uint32_t row = core::FindRow(day.world.orders, id);
    if (row == core::kNoRow || day.world.orders.rows[row].status != core::OrderStatus::kRefused) {
      return core::OrderRefusal::kNone;  // not refused at all: the test says so below
    }
    return day.world.orders.rows[row].refusal;
  };
  failures += Expect(refusal(no_man) == core::OrderRefusal::kNoSuchSubject,
                     "work for a man who does not exist is refused for the man");
  failures += Expect(refusal(no_field) == core::OrderRefusal::kNoSuchSubject,
                     "and work on a field that does not exist, for the field");
  failures += Expect(refusal(on_post) == core::OrderRefusal::kConflictsWithActive,
                     "a post holder is not to be ordered off his post by a work order");
  failures += Expect(refusal(too_young) == core::OrderRefusal::kNotEligible,
                     "and a child is not eligible for work at all");
  failures += Expect(day.world.residents.rows[1].post.profession.value == 0,
                     "the refused order left the post where it was");
  return failures;
}

int TestWorkforceIsTheLaborRule() {
  int failures = 0;
  const EmptyTableSet tables;
  const auto labor = core::CreateLaborSystem(tables);
  if (Expect(labor != nullptr, "factory yields a system") != 0) {
    return 1;
  }
  DayWorld day(3);
  // A child, and a grown man with no household to start the day from: the
  // two reasons the core knows that a birthday alone does not tell.
  day.world.residents.rows[1].birth_day = -10;            // an infant
  day.world.residents.rows[2].family = core::FamilyId{};  // nobody's household

  const core::WorkforceCount before = labor->CountWorkforce(day.world);
  failures += Expect(before.employable == 1,
                     "the count is the labor rule: an adult with a home, and nobody else");
  failures += Expect(before.idle == 1, "and before the day is placed he is standing about");
  failures += Expect(labor->CanBeOrdered(day.world, day.world.residents.row_ids[0]),
                     "the man himself answers yes");
  failures += Expect(!labor->CanBeOrdered(day.world, day.world.residents.row_ids[1]),
                     "the child answers no");
  failures += Expect(!labor->CanBeOrdered(day.world, core::ResidentId{999}),
                     "and a man who does not exist answers no rather than crashing");

  // Put him to work and he stops being idle — the number the HUD paints red
  // is the one that moves.
  day.AddField(core::FieldPhase::kHarvest, 40.0F, core::Vec2{.x = 20.0F, .y = 0.0F});
  for (std::uint32_t hour = 0; hour <= 8; ++hour) {
    day.world.calendar.tick = hour;
    core::RefreshCalendarCaches(day.world.calendar);
    const core::WorldState previous = day.world;
    labor->RunAssignmentDecisions(previous, day.world);
  }
  const core::WorkforceCount working = labor->CountWorkforce(day.world);
  failures += Expect(working.employable == 1 && working.idle == 0,
                     "a placed man is employable and not idle");
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
  failures += TestStandingWorkOrder();
  failures += TestReleaseWork();
  failures += TestAssignWorkRefusals();
  failures += TestStandingWorkCorners();
  failures += TestSecondIterationCorners();
  failures += TestWorkforceIsTheLaborRule();
  if (failures == 0) {
    std::cout << "unit_core_labor: all checks passed\n";
  }
  return failures;
}
