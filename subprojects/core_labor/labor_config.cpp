// Parsing of the labor configuration (labor_config.h): tables/labor.csv plus
// the columns labor needs from transport.csv, life.csv, livestock.csv and
// crops.csv.
//
// Policy, shared with every other subsystem factory: a MISSING table keeps
// the canonical defaults (a unit test's world has no tables at all), while a
// PRESENT table that cannot be read is an error — a half-understood balance
// is worse than none. Numbers are range-checked here, at parse time, because
// everything downstream multiplies them into hours and hundredths.

#include "labor_config.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "core_common/calendar.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// @brief Turns "a cell is not a number" into "labor: path_factor: a cell is
/// not a number", building the message with appends only.
void PrefixError(std::string_view table, std::string_view key, std::string& error) {
  std::string message(table);
  message += ": ";
  if (!key.empty()) {
    message += key;
    message += ": ";
  }
  message += error;
  error = std::move(message);
}

/// @brief Reads one cell as a real number.
/// @return false only when the cell is present and unreadable or out of
///         range; an absent column or empty cell leaves `value` untouched.
bool OptionalCell(const ITable& table,
                  std::uint32_t row,
                  std::uint32_t column,
                  float low,
                  float high,
                  float& value,
                  std::string& error) {
  if (row == kNoTableRow || column == kNoTableColumn || table.CellText(row, column).empty()) {
    return true;
  }
  const std::optional<float> cell = table.CellReal(row, column);
  if (!cell) {
    error = "a cell is not a number";
    return false;
  }
  // Written as a positive test so that NaN fails it: NaN compares false
  // against everything, including itself.
  const bool in_range = *cell >= low && *cell <= high;
  if (!in_range) {
    error = "a cell is out of range";
    return false;
  }
  value = *cell;
  return true;
}

/// @brief Reads one key's `value` cell of a key/value table.
bool OptionalValue(const ITable& table,
                   std::string_view key,
                   float low,
                   float high,
                   float& value,
                   std::string& error) {
  return OptionalCell(
      table, table.FindRowByKey(key), table.FindColumn("value"), low, high, value, error);
}

/// One scalar knob of labor.csv: where it lands and what it may be.
struct ScalarKnob {
  std::string_view key;
  float* value;
  float low;
  float high;
};

bool ReadKnobs(const ITable& table,
               std::string_view table_name,
               std::span<const ScalarKnob> knobs,
               std::string& error) {
  for (const ScalarKnob& knob : knobs) {
    if (!OptionalValue(table, knob.key, knob.low, knob.high, *knob.value, error)) {
      PrefixError(table_name, knob.key, error);
      return false;
    }
  }
  return true;
}

bool ParseScalars(const ITable& table, LaborConfig& config, std::string& error) {
  float placement = config.placement_level;
  const std::array<ScalarKnob, 12> knobs = {{
      {.key = "standard_day_hours",
       .value = &config.standard_day_hours,
       .low = 1.0F,
       .high = 24.0F},
      {.key = "travel_limit_hours",
       .value = &config.travel_limit_hours,
       .low = 0.0F,
       .high = 24.0F},
      {.key = "min_usable_hours", .value = &config.min_usable_hours, .low = 0.0F, .high = 24.0F},
      {.key = "path_factor", .value = &config.path_factor, .low = 1.0F, .high = 5.0F},
      {.key = "sleep_hours", .value = &config.sleep_hours, .low = 0.0F, .high = 16.0F},
      {.key = "rest_walkoff_threshold",
       .value = &config.rest_walkoff_threshold,
       .low = 0.0F,
       .high = 100.0F},
      {.key = "rest_recovery_day_off",
       .value = &config.rest_recovery_day_off,
       .low = 0.0F,
       .high = 100.0F},
      {.key = "rest_recovery_idle_day",
       .value = &config.rest_recovery_idle_day,
       .low = 0.0F,
       .high = 100.0F},
      {.key = "health_loss_per_spent_day",
       .value = &config.health_loss_per_spent_day,
       .low = 0.0F,
       .high = 10.0F},
      {.key = "stamina_drain_relief",
       .value = &config.stamina_drain_relief,
       .low = 0.0F,
       .high = 1.0F},
      {.key = "self_education_max_bonus",
       .value = &config.self_education_max_bonus,
       .low = 0.0F,
       .high = 1.0F},
      {.key = "placement_level", .value = &placement, .low = 0.0F, .high = 3.0F},
  }};
  if (!ReadKnobs(table, "labor", knobs, error)) {
    return false;
  }
  config.placement_level = static_cast<std::uint8_t>(placement);
  return true;
}

bool ParseEfficiencyKnobs(const ITable& table, LaborConfig& config, std::string& error) {
  EfficiencyFactors& factors = config.efficiency;
  SkillBlend& skill = config.skill;
  const std::array<ScalarKnob, 16> knobs = {{
      {.key = "efficiency_health_pivot",
       .value = &factors.health_pivot,
       .low = 0.0F,
       .high = 100.0F},
      {.key = "efficiency_health_slope", .value = &factors.health_slope, .low = 0.0F, .high = 5.0F},
      {.key = "efficiency_mood_pivot", .value = &factors.mood_pivot, .low = 0.0F, .high = 100.0F},
      {.key = "efficiency_mood_slope", .value = &factors.mood_slope, .low = 0.0F, .high = 5.0F},
      {.key = "efficiency_skill_pivot", .value = &factors.skill_pivot, .low = 0.0F, .high = 100.0F},
      {.key = "efficiency_skill_slope", .value = &factors.skill_slope, .low = 0.0F, .high = 5.0F},
      {.key = "rest_step_tired", .value = &factors.rest_step_tired, .low = 0.0F, .high = 100.0F},
      {.key = "rest_step_spent", .value = &factors.rest_step_spent, .low = 0.0F, .high = 100.0F},
      {.key = "rest_factor_tired", .value = &factors.rest_factor_tired, .low = 0.0F, .high = 1.0F},
      {.key = "rest_factor_spent", .value = &factors.rest_factor_spent, .low = 0.0F, .high = 1.0F},
      {.key = "aging_margin_years",
       .value = &factors.aging_margin_years,
       .low = 0.0F,
       .high = 100.0F},
      {.key = "age_decline_per_year",
       .value = &factors.age_decline_per_year,
       .low = 0.0F,
       .high = 1.0F},
      {.key = "age_decline_floor", .value = &factors.age_decline_floor, .low = 0.0F, .high = 1.0F},
      {.key = "skill_earned_weight", .value = &skill.earned_weight, .low = 0.0F, .high = 1.0F},
      {.key = "skill_stamina_weight", .value = &skill.stamina_weight, .low = 0.0F, .high = 1.0F},
      {.key = "skill_schooled_weight", .value = &skill.schooled_weight, .low = 0.0F, .high = 1.0F},
  }};
  return ReadKnobs(table, "labor", knobs, error);
}

/// The education multipliers, one key per completed stage, in the order of
/// the EducationStage enum.
bool ParseEducationFactors(const ITable& table, LaborConfig& config, std::string& error) {
  constexpr std::array<std::string_view, 5> kKeys = {"education_none",
                                                     "education_primary",
                                                     "education_secondary",
                                                     "education_vocational",
                                                     "education_higher"};
  for (std::uint32_t index = 0; index < kKeys.size(); ++index) {
    if (!OptionalValue(table, kKeys[index], 0.1F, 3.0F, config.education_factor[index], error)) {
      PrefixError("labor", kKeys[index], error);
      return false;
    }
  }
  return true;
}

/// One row per work kind, in the order of the WorkKind enum (kNone has no
/// row: nobody is paid for idling).
bool ParseWorkKindRates(const ITable& table, LaborConfig& config, std::string& error) {
  constexpr std::array<std::string_view, 5> kKeys = {
      "plowing", "harrowing", "sowing", "harvest", "herd_care"};
  const std::uint32_t rate_column = table.FindColumn("trudodni_rate");
  const std::uint32_t drain_column = table.FindColumn("rest_drain_per_norm_day");
  for (std::uint32_t index = 0; index < kKeys.size(); ++index) {
    const std::uint32_t row = table.FindRowByKey(kKeys[index]);
    WorkKindRates& rates = config.rates[index + 1];  // +1: WorkKind::kNone is row-less
    if (!OptionalCell(table, row, rate_column, 0.0F, 10.0F, rates.trudodni_rate, error) ||
        !OptionalCell(
            table, row, drain_column, 0.0F, 100.0F, rates.rest_drain_per_norm_day, error)) {
      PrefixError("labor", kKeys[index], error);
      return false;
    }
  }
  return true;
}

/// Speeds live where every consumer reads them (transport.csv), never
/// duplicated into labor.csv: pedestrian for a hand order, horse_trot for a
/// harnessed one. Real km/h in the file; the chronometer divides.
bool ParseSpeeds(const ITable& table, LaborConfig& config, std::string& error) {
  const std::uint32_t speed_column = table.FindColumn("speed_kmh");
  if (!OptionalCell(table,
                    table.FindRowByKey("pedestrian"),
                    speed_column,
                    0.5F,
                    20.0F,
                    config.walk_speed_kmh,
                    error) ||
      !OptionalCell(table,
                    table.FindRowByKey("horse_trot"),
                    speed_column,
                    0.5F,
                    60.0F,
                    config.harness_speed_kmh,
                    error)) {
    PrefixError("transport", "speed_kmh", error);
    return false;
  }
  return true;
}

bool ParseLivestock(const ITable& table, LaborConfig& config, std::string& error) {
  // The design db calls the column care_days_per_real_year, and says the unit
  // out loud for a reason: what is SPENT is real, what AGES is game. The
  // older hand-written name is read too, so the switch to the export cannot
  // silently hand every kind the cow's default.
  std::uint32_t care_column = table.FindColumn("care_days_per_real_year");
  if (care_column == kNoTableColumn) {
    care_column = table.FindColumn("care_days_per_year");
  }
  // The cow anchor of 49-simulations §2, in REAL man-days a year; the same
  // default stands in for every kind until the column exists.
  constexpr float kDefaultCareRealDaysPerYear = 32.0F;
  config.care_days_per_year.assign(table.RowCount(), kDefaultCareRealDaysPerYear);
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    if (!OptionalCell(
            table, row, care_column, 0.0F, 3650.0F, config.care_days_per_year[row], error)) {
      PrefixError("livestock", "care_days_per_year", error);
      return false;
    }
    config.care_days_per_year[row] /= kRealDaysPerGameDay;
  }
  const std::uint32_t horse = table.FindRowByKey("horse");
  if (horse != kNoTableRow) {
    config.horse_kind = LivestockKindId{static_cast<std::uint16_t>(horse)};
  }
  return true;
}

bool ParseCropWindows(const ITable& table, LaborConfig& config, std::string& error) {
  const std::uint32_t sow_column = table.FindColumn("sow_to_month");
  const std::uint32_t harvest_column = table.FindColumn("harvest_to_month");
  config.crops.assign(table.RowCount(), CropWindows{});
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    // Table months are human 1..12; the core's Month enum is 0-based.
    float sow_to = 12.0F;
    float harvest_to = 12.0F;
    if (!OptionalCell(table, row, sow_column, 1.0F, 12.0F, sow_to, error) ||
        !OptionalCell(table, row, harvest_column, 1.0F, 12.0F, harvest_to, error)) {
      PrefixError("crops", "window", error);
      return false;
    }
    config.crops[row].sow_to_month = static_cast<std::uint8_t>(sow_to - 1.0F);
    config.crops[row].harvest_to_month = static_cast<std::uint8_t>(harvest_to - 1.0F);
  }
  return true;
}

}  // namespace

bool ParseLaborConfig(const ITableSet& tables, LaborConfig& config, std::string& error) {
  if (const ITable* labor = tables.FindTable("labor")) {
    if (!ParseScalars(*labor, config, error) || !ParseEfficiencyKnobs(*labor, config, error) ||
        !ParseEducationFactors(*labor, config, error) ||
        !ParseWorkKindRates(*labor, config, error)) {
      return false;
    }
  }
  if (const ITable* transport = tables.FindTable("transport")) {
    if (!ParseSpeeds(*transport, config, error)) {
      return false;
    }
  }
  if (const ITable* life = tables.FindTable("life")) {
    const std::array<ScalarKnob, 2> knobs = {{
        {.key = "life_speedup", .value = &config.life_speedup, .low = 0.1F, .high = 100.0F},
        {.key = "adult_age_years", .value = &config.adult_age_years, .low = 1.0F, .high = 100.0F},
    }};
    if (!ReadKnobs(*life, "life", knobs, error)) {
      return false;
    }
  }
  if (const ITable* livestock = tables.FindTable("livestock")) {
    if (!ParseLivestock(*livestock, config, error)) {
      return false;
    }
  }
  if (const ITable* crops = tables.FindTable("crops")) {
    if (!ParseCropWindows(*crops, config, error)) {
      return false;
    }
  }
  return true;
}

}  // namespace core
