// Parsing of the demography configuration (life_config.h): tables/life.csv,
// tables/demography.csv, tables/satisfaction.csv and the one row
// core_residents needs from unit_types.csv.
//
// Two policies live here on purpose, and the seam is the stage they arrived
// in. The stage-3 keys are REQUIRED when their table is present: they have
// shipped in every table set since the calendar existed, and a life.csv that
// lost life_speedup is a broken file, not an old one. The stage-6 keys —
// the vitals block of decision 105 and the birth conditions of 106 — are
// OPTIONAL: a table set written before this stage must still load, and its
// defaults here equal the canonical table contents anyway.
//
// A MISSING table always keeps the defaults whole: a unit test's world has
// no tables at all.

#include "life_config.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core_tables/tables.h"
#include "table_read.h"

namespace core {
namespace {

/// @brief Reads one key's value cell; a missing key is an error (see the
/// file comment: the stage-3 keys are required).
bool RequiredValue(const ITable& table, std::string_view key, float& value, std::string& error) {
  const std::uint32_t row = table.FindRowByKey(key);
  const std::uint32_t column = table.FindColumn("value");
  if (row == kNoTableRow || column == kNoTableColumn) {
    error = "life: no row '" + std::string(key) + "' or no value column";
    return false;
  }
  const std::optional<float> cell = table.CellReal(row, column);
  if (!cell) {
    error = "life: value of '" + std::string(key) + "' is not a number";
    return false;
  }
  value = *cell;
  return true;
}

bool ParseLifeTable(const ITable& table, LifeConfig& config, std::string& error) {
  float epoch2 = 0.0F;
  float epoch3 = 0.0F;
  const bool ok =
      RequiredValue(table, "life_speedup", config.life_speedup, error) &&
      RequiredValue(table, "adult_age_years", config.adult_age_years, error) &&
      RequiredValue(table, "marriage_age_years", config.marriage_age_years, error) &&
      RequiredValue(table, "fertility_from_years", config.fertility_from_years, error) &&
      RequiredValue(table, "fertility_to_years", config.fertility_to_years, error) &&
      RequiredValue(table, "mortality_age_mid_years", config.mortality_age_mid_years, error) &&
      RequiredValue(table, "mortality_age_old_years", config.mortality_age_old_years, error) &&
      RequiredValue(table,
                    "mortality_young_percent_per_year",
                    config.mortality_young_percent_per_year,
                    error) &&
      RequiredValue(
          table, "mortality_mid_percent_per_year", config.mortality_mid_percent_per_year, error) &&
      RequiredValue(
          table, "mortality_old_percent_per_year", config.mortality_old_percent_per_year, error) &&
      RequiredValue(table, "migration_per_year", config.migration_per_year, error) &&
      RequiredValue(table, "epoch2_population", epoch2, error) &&
      RequiredValue(table, "epoch3_population", epoch3, error) &&
      RequiredValue(table,
                    "marriage_chance_percent_per_day",
                    config.marriage_chance_percent_per_day,
                    error) &&
      RequiredValue(table, "sex_balance_gain", config.sex_balance_gain, error);
  if (!ok) {
    return false;
  }
  // Value validation: a float-to-uint cast of a negative, NaN or huge value
  // is UB, and a non-positive life speed divides to infinity. Written as
  // positive tests so that NaN fails them — NaN compares false against
  // everything, so the earlier `<= 0 || > 1e9` form let it through.
  const bool speed_ok = config.life_speedup > 0.0F;
  const bool epoch2_ok = epoch2 >= 1.0F && epoch2 <= 1.0e9F;
  const bool epoch3_ok = epoch3 >= 1.0F && epoch3 <= 1.0e9F;
  if (!speed_ok || !epoch2_ok || !epoch3_ok) {
    error = "life: life_speedup must be positive and epoch thresholds sane";
    return false;
  }
  config.epoch2_population = static_cast<std::uint32_t>(epoch2);
  config.epoch3_population = static_cast<std::uint32_t>(epoch3);
  return true;
}

/// The stage-6 additions to life.csv: life expectancy (decision 105) and
/// what family conditions do to births (decision 106).
bool ParseVitalsAndBirths(const ITable& table, LifeConfig& config, std::string& error) {
  VitalsConfig& vitals = config.vitals;
  BirthConditionsConfig& births = config.birth_conditions;
  const std::array<ScalarKnob, 8> vitals_knobs = {{
      {.key = "vitals_base_years", .value = &vitals.base_years, .low = 1.0F, .high = 120.0F},
      {.key = "vitals_satiety_neutral",
       .value = &vitals.satiety_neutral,
       .low = 0.0F,
       .high = 100.0F},
      {.key = "vitals_satiety_to_years_slope",
       .value = &vitals.satiety_to_years_slope,
       .low = 0.0F,
       .high = 10.0F},
      {.key = "vitals_nutrition_years_min",
       .value = &vitals.nutrition_years_min,
       .low = -50.0F,
       .high = 0.0F},
      {.key = "vitals_nutrition_years_max",
       .value = &vitals.nutrition_years_max,
       .low = 0.0F,
       .high = 50.0F},
      {.key = "vitals_medicine_years", .value = &vitals.medicine_years, .low = 0.0F, .high = 50.0F},
      {.key = "vitals_living_years", .value = &vitals.living_years, .low = 0.0F, .high = 50.0F},
      {.key = "vitals_working_conditions_years",
       .value = &vitals.working_conditions_years,
       .low = -50.0F,
       .high = 50.0F},
  }};
  const std::array<ScalarKnob, 11> birth_knobs = {{
      {.key = "birth_satisfaction_bound_1",
       .value = births.satisfaction_bounds.data(),
       .low = 0.0F,
       .high = 100.0F},
      {.key = "birth_satisfaction_bound_2",
       .value = &births.satisfaction_bounds[1],
       .low = 0.0F,
       .high = 100.0F},
      {.key = "birth_satisfaction_bound_3",
       .value = &births.satisfaction_bounds[2],
       .low = 0.0F,
       .high = 100.0F},
      {.key = "birth_satisfaction_bound_4",
       .value = &births.satisfaction_bounds[3],
       .low = 0.0F,
       .high = 100.0F},
      {.key = "birth_multiplier_1", .value = births.multipliers.data(), .low = 0.0F, .high = 5.0F},
      {.key = "birth_multiplier_2", .value = &births.multipliers[1], .low = 0.0F, .high = 5.0F},
      {.key = "birth_multiplier_3", .value = &births.multipliers[2], .low = 0.0F, .high = 5.0F},
      {.key = "birth_multiplier_4", .value = &births.multipliers[3], .low = 0.0F, .high = 5.0F},
      {.key = "birth_multiplier_5", .value = &births.multipliers[4], .low = 0.0F, .high = 5.0F},
      {.key = "birth_satiety_stop", .value = &births.satiety_stop, .low = 0.0F, .high = 100.0F},
      {.key = "birth_mother_health_stop",
       .value = &births.mother_health_stop,
       .low = 0.0F,
       .high = 100.0F},
  }};
  return ReadKnobs(table, "life", vitals_knobs, error) &&
         ReadKnobs(table, "life", birth_knobs, error);
}

bool ParseEpochRows(const ITable& table, LifeConfig& config, std::string& error) {
  constexpr std::array<std::string_view, 3> kKeys = {"epoch_1", "epoch_2", "epoch_3"};
  const std::uint32_t children_column = table.FindColumn("children_per_family");
  const std::uint32_t mortality_column = table.FindColumn("child_mortality_percent");
  const std::uint32_t outflow_column = table.FindColumn("outflow_percent_per_year");
  if (children_column == kNoTableColumn || mortality_column == kNoTableColumn ||
      outflow_column == kNoTableColumn) {
    error = "demography: a required column is missing";
    return false;
  }
  for (std::uint32_t index = 0; index < kKeys.size(); ++index) {
    const std::uint32_t row = table.FindRowByKey(kKeys[index]);
    const auto children = table.CellReal(row, children_column);
    const auto mortality = table.CellReal(row, mortality_column);
    const auto outflow = table.CellReal(row, outflow_column);
    if (row == kNoTableRow || !children || !mortality || !outflow) {
      error = "demography: row '" + std::string(kKeys[index]) + "' is missing or not numeric";
      return false;
    }
    config.epochs[index] = {.children_per_family = *children,
                            .child_mortality_percent = *mortality,
                            .outflow_percent_per_year = *outflow};
  }
  return true;
}

bool ParseWeightRows(const ITable& table, LifeConfig& config, std::string& error) {
  constexpr std::array<std::string_view, 3> kKeys = {"epoch_1", "epoch_2", "epoch_3"};
  const std::uint32_t satiety_column = table.FindColumn("weight_satiety");
  const std::uint32_t common_column = table.FindColumn("weight_common_cause");
  const std::uint32_t needs_column = table.FindColumn("weight_needs");
  const std::uint32_t rest_column = table.FindColumn("weight_rest");
  if (satiety_column == kNoTableColumn || common_column == kNoTableColumn ||
      needs_column == kNoTableColumn || rest_column == kNoTableColumn) {
    error = "satisfaction: a required column is missing";
    return false;
  }
  for (std::uint32_t index = 0; index < kKeys.size(); ++index) {
    const std::uint32_t row = table.FindRowByKey(kKeys[index]);
    const auto satiety = table.CellReal(row, satiety_column);
    const auto common = table.CellReal(row, common_column);
    const auto needs = table.CellReal(row, needs_column);
    const auto rest = table.CellReal(row, rest_column);
    if (row == kNoTableRow || !satiety || !common || !needs || !rest) {
      error = "satisfaction: row '" + std::string(kKeys[index]) + "' is missing or not numeric";
      return false;
    }
    config.weights[index] = {
        .satiety = *satiety, .common_cause = *common, .needs = *needs, .rest = *rest};
  }
  return true;
}

}  // namespace

bool ParseLifeConfig(const ITableSet& tables, LifeConfig& config, std::string& error) {
  if (const ITable* life = tables.FindTable("life")) {
    if (!ParseLifeTable(*life, config, error) || !ParseVitalsAndBirths(*life, config, error)) {
      return false;
    }
  }
  if (const ITable* demography = tables.FindTable("demography")) {
    if (!ParseEpochRows(*demography, config, error)) {
      return false;
    }
  }
  if (const ITable* satisfaction = tables.FindTable("satisfaction")) {
    if (!ParseWeightRows(*satisfaction, config, error)) {
      return false;
    }
  }
  if (const ITable* unit_types = tables.FindTable("unit_types")) {
    const std::uint32_t house = unit_types->FindRowByKey("house");
    if (house != kNoTableRow) {
      config.house_type = UnitTypeId{static_cast<std::uint16_t>(house)};
    }
  }
  return true;
}

}  // namespace core
