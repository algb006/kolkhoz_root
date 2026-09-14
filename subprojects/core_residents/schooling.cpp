// The pupils of Epoch I's school (core_residents/schooling.h).

#include "schooling.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/state_table_ops.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// The world_params.csv keys, in the order of the knob list in the parse.
constexpr std::array<std::string_view, 8> kSchoolingWorldParamKeys = {
    "school_enroll_age_from_years",
    "school_enroll_age_to_years",
    "school_walk_radius_primary_m",
    "school_pupil_capacity_level_1",
    "school_pupil_capacity_level_2",
    "school_pupil_capacity_level_3",
    "school_year_start_month",
    "school_year_end_month"};

/// Past these the cell is a typo: a child's age, a walk across the map, a
/// school the size of a town.
constexpr float kOldestChildYears = 20.0F;
constexpr float kFarthestMetres = 20000.0F;
constexpr float kLargestSchool = 10000.0F;

bool Whole(float value) {
  return std::floor(value) == value;
}

/// The month and whether `day` is its first day.
bool FirstDayOfMonth(SimDay day, std::uint8_t month) {
  const std::uint32_t day_of_year = day % kDaysPerYear;
  return day_of_year % kDaysPerMonth == 0 &&
         (day_of_year / kDaysPerMonth) + 1U == static_cast<std::uint32_t>(month);
}

bool IsStandingSchool(const SchoolingConfig& config, const UnitRow& unit) {
  return unit.type.value == config.school_type.value && unit.level > 0;
}

std::uint32_t CapacityOf(const SchoolingConfig& config, const UnitRow& school) {
  const std::size_t index =
      std::min<std::size_t>(static_cast<std::size_t>(school.level), config.pupil_capacity.size()) -
      1U;
  return config.pupil_capacity[index];
}

/// Where a resident's yard is: his family's house, or where it stood.
bool YardOf(const WorldState& current, const ResidentRow& person, Vec2& yard) {
  const std::uint32_t family_row = FindRow(current.families, person.family);
  if (family_row == kNoRow) {
    return false;
  }
  const FamilyRow& family = current.families.rows[family_row];
  const std::uint32_t house_row = FindRow(current.units, family.house);
  yard = house_row != kNoRow ? current.units.rows[house_row].position : family.lost_house_position;
  return true;
}

void Leave(WorldState& current, std::uint32_t row, EducationStage stage) {
  ResidentRow& pupil = current.residents.rows[row];
  const UnitId school = pupil.school;
  pupil.school = UnitId{};
  SimEvent& event = EmitEvent(current, EventKind::kPupilLeftSchool, EventSeverity::kRoutine);
  event.resident = current.residents.row_ids[row];
  event.unit = school;
  event.amount = static_cast<std::int64_t>(stage);
}

}  // namespace

std::span<const std::string_view> SchoolingWorldParamKeys() {
  return kSchoolingWorldParamKeys;
}

bool ParseSchoolingConfig(const ITableSet& tables, SchoolingConfig& config, std::string& error) {
  if (const ITable* const world = tables.FindTable("world_params")) {
    std::array<float, 3> capacities = {static_cast<float>(config.pupil_capacity[0]),
                                       static_cast<float>(config.pupil_capacity[1]),
                                       static_cast<float>(config.pupil_capacity[2])};
    auto start = static_cast<float>(config.year_start_month);
    auto end = static_cast<float>(config.year_end_month);
    const Range months{.low = 1.0F, .high = static_cast<float>(kMonthsPerYear)};
    const Range capacity{.low = 0.0F, .high = kLargestSchool};
    const std::array<ScalarKnob, kSchoolingWorldParamKeys.size()> knobs = {{
        {.key = kSchoolingWorldParamKeys[0],
         .value = &config.enroll_age_from_years,
         .range = {.low = 0.0F, .high = kOldestChildYears}},
        {.key = kSchoolingWorldParamKeys[1],
         .value = &config.enroll_age_to_years,
         .range = {.low = 0.0F, .high = kOldestChildYears}},
        {.key = kSchoolingWorldParamKeys[2],
         .value = &config.walk_radius_primary_m,
         .range = {.low = 0.0F, .high = kFarthestMetres}},
        {.key = kSchoolingWorldParamKeys[3], .value = &capacities[0], .range = capacity},
        {.key = kSchoolingWorldParamKeys[4], .value = &capacities[1], .range = capacity},
        {.key = kSchoolingWorldParamKeys[5], .value = &capacities[2], .range = capacity},
        {.key = kSchoolingWorldParamKeys[6], .value = &start, .range = months},
        {.key = kSchoolingWorldParamKeys[7], .value = &end, .range = months},
    }};
    if (!ReadKnobs(*world, "world_params", knobs, error)) {
      return false;
    }
    if (!Whole(start) || !Whole(end) || !Whole(capacities[0]) || !Whole(capacities[1]) ||
        !Whole(capacities[2])) {
      error = "world_params: a school month or capacity is not a whole number";
      return false;
    }
    if (config.enroll_age_from_years >= config.enroll_age_to_years || start == end) {
      error = "world_params: the school's age band ends before it begins, or its year does";
      return false;
    }
    for (std::size_t level = 0; level < capacities.size(); ++level) {
      config.pupil_capacity[level] = static_cast<std::uint32_t>(capacities[level]);
    }
    config.year_start_month = static_cast<std::uint8_t>(start);
    config.year_end_month = static_cast<std::uint8_t>(end);
  }
  if (const ITable* const unit_types = tables.FindTable("unit_types")) {
    config.school_type = DefIdFromRow<UnitTypeIdTag>(unit_types->FindRowByKey("school"));
  }
  if (const ITable* const professions = tables.FindTable("professions")) {
    config.teacher_post =
        DefIdFromRow<ProfessionIdTag>(professions->FindRowByKey("primary_teacher"));
  }
  return true;
}

void EnrollPupils(const SchoolingConfig& config, float life_speedup, WorldState& current) {
  if (config.school_type.value == kInvalidDefIdValue) {
    return;
  }

  // The elder first (boss, parcel 354): a school that cannot take everybody
  // takes those who will not get another September.
  struct Candidate {
    std::uint32_t row;
    float age;
  };

  std::vector<Candidate> candidates;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    const ResidentRow& person = current.residents.rows[row];
    const float age = BiologicalAgeYears(life_speedup, person.birth_day, current.calendar.day);
    if (person.school.value == kInvalidEntityIdValue &&
        person.education_stage == EducationStage::kNone && age >= config.enroll_age_from_years &&
        age < config.enroll_age_to_years) {
      candidates.push_back(Candidate{.row = row, .age = age});
    }
  }
  std::ranges::sort(candidates, [&current](const Candidate& left, const Candidate& right) {
    if (left.age != right.age) {
      return left.age > right.age;
    }
    return current.residents.row_ids[left.row].value < current.residents.row_ids[right.row].value;
  });
  // The pupils each school already holds.
  std::vector<std::uint32_t> held(current.units.rows.size(), 0U);
  for (const ResidentRow& person : current.residents.rows) {
    const std::uint32_t school_row = FindRow(current.units, person.school);
    if (school_row != kNoRow) {
      ++held[school_row];
    }
  }
  for (const Candidate& candidate : candidates) {
    Vec2 yard;
    if (!YardOf(current, current.residents.rows[candidate.row], yard)) {
      continue;
    }
    std::uint32_t nearest = kNoRow;
    float nearest_distance = config.walk_radius_primary_m;
    for (std::uint32_t unit_row = 0; unit_row < current.units.rows.size(); ++unit_row) {
      const UnitRow& unit = current.units.rows[unit_row];
      if (!IsStandingSchool(config, unit) || held[unit_row] >= CapacityOf(config, unit)) {
        continue;
      }
      const float distance = std::hypot(unit.position.x - yard.x, unit.position.y - yard.y);
      if (distance <= nearest_distance) {
        nearest = unit_row;
        nearest_distance = distance;
      }
    }
    if (nearest == kNoRow) {
      continue;  // too far, or every school in reach is full
    }
    ++held[nearest];
    ResidentRow& pupil = current.residents.rows[candidate.row];
    pupil.school = current.units.row_ids[nearest];
    SimEvent& event = EmitEvent(current, EventKind::kPupilEnrolled, EventSeverity::kRoutine);
    event.resident = current.residents.row_ids[candidate.row];
    event.unit = pupil.school;
  }
}

void CloseSchoolYear(const SchoolingConfig& config, float life_speedup, WorldState& current) {
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    const ResidentRow& pupil = current.residents.rows[row];
    if (pupil.school.value == kInvalidEntityIdValue) {
      continue;
    }
    bool taught = false;
    for (const ResidentRow& person : current.residents.rows) {
      taught = taught || (person.post.unit.value == pupil.school.value &&
                          person.post.profession.value == config.teacher_post.value);
    }
    if (taught) {
      current.residents.rows[row].education_stage = EducationStage::kPrimary;
      Leave(current, row, EducationStage::kPrimary);
      continue;
    }
    const float age = BiologicalAgeYears(life_speedup, pupil.birth_day, current.calendar.day);
    if (age >= config.enroll_age_to_years) {
      Leave(current, row, EducationStage::kNone);  // the year lost, and no other for him
    }
  }
}

void ReleasePupilsOfGoneSchools(WorldState& current) {
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    const UnitId school = current.residents.rows[row].school;
    if (school.value == kInvalidEntityIdValue) {
      continue;
    }
    const std::uint32_t unit_row = FindRow(current.units, school);
    if (unit_row == kNoRow || current.units.rows[unit_row].level == 0) {
      Leave(current, row, EducationStage::kNone);
    }
  }
}

void RunSchoolDay(const SchoolingConfig& config, float life_speedup, WorldState& current) {
  ReleasePupilsOfGoneSchools(current);
  if (FirstDayOfMonth(current.calendar.day, config.year_end_month)) {
    CloseSchoolYear(config, life_speedup, current);
  }
  if (FirstDayOfMonth(current.calendar.day, config.year_start_month)) {
    EnrollPupils(config, life_speedup, current);
  }
}

}  // namespace core
