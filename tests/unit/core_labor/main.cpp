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
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "assignment.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_labor/labor_system.h"
#include "core_tables/tables.h"
#include "labor_config.h"
#include "labor_day.h"

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
  const float efficiency = core::ResidentEfficiency(config, reference, 30.0F);
  failures += Expect(efficiency > 0.97F && efficiency < 1.03F,
                     "the reference worker is worth exactly one norm day");

  core::ResidentRow illiterate = reference;
  illiterate.education_stage = core::EducationStage::kNone;
  failures += Expect(core::ResidentEfficiency(config, illiterate, 30.0F) < efficiency * 0.9F,
                     "illiteracy costs the canonical 15% (education design §6)");
  failures += Expect(core::ResidentEfficiency(config, reference, 70.0F) < efficiency,
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
  if (failures == 0) {
    std::cout << "unit_core_labor: all checks passed\n";
  }
  return failures;
}
