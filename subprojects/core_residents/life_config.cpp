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
// A MISSING table keeps the defaults whole HERE, in the parser — a unit
// test's world has no tables at all. Whether the caller may have that answer
// is decided before the parser runs, at the factory
// (core_tables/required_tables.h): under StubTables::kRefused a set without
// this module's tables never reaches this file.

#include "life_config.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core_catalog/table_value.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// @brief Reads the fifteen keys of life.csv into LifeConfig, each with its
/// range; a missing key is an error (see the file comment: the stage-3 keys
/// are required).
///
/// Two orphans stood here. One documented a dwelling-class constant this
/// file has not declared for some time — a comment describing nothing at
/// all. The other was RequiredValue's brief, saying "one key" over a
/// function that reads fifteen; it moved to core_catalog and left its
/// documentation behind.
bool ParseLifeTable(const ITable& table, LifeConfig& config, std::string& error) {
  float epoch2 = 0.0F;
  float epoch3 = 0.0F;
  // EVERY ONE OF THESE NOW CARRIES ITS RANGE, and that is the whole of task
  // A6 in this file. They used to be read by a local RequiredValue with no
  // bounds at all, so `nan`, `inf` and a stray six-digit typo went straight
  // through to a float-to-int cast — UB-001 and UB-002, reopened by four
  // delivery cycles in a row. The bounds below are the design's own: ages
  // in biological years, percentages as percentages, and a life speed that
  // must be positive because the whole clock divides by it.
  //
  // `migration_per_year` is bounded at a thousand for a reason worth
  // writing down: RunMigration multiplies it by the campaign day before the
  // cast, so an unbounded rate overflows `int32` after enough days rather
  // than at the moment the table is read — which is the far side of the
  // campaign from the mistake.
  constexpr Range kAgeYears{.low = 1.0F, .high = 120.0F};
  constexpr Range kPercent{.low = 0.0F, .high = 100.0F};
  const bool ok =
      RequiredValue(table,
                    "life",
                    "life_speedup",
                    Range{.low = 0.1F, .high = 100.0F},
                    config.life_speedup,
                    error) &&
      RequiredValue(table, "life", "adult_age_years", kAgeYears, config.adult_age_years, error) &&
      RequiredValue(
          table, "life", "marriage_age_years", kAgeYears, config.marriage_age_years, error) &&
      RequiredValue(
          table, "life", "fertility_from_years", kAgeYears, config.fertility_from_years, error) &&
      RequiredValue(
          table, "life", "fertility_to_years", kAgeYears, config.fertility_to_years, error) &&
      RequiredValue(table,
                    "life",
                    "mortality_age_mid_years",
                    kAgeYears,
                    config.mortality_age_mid_years,
                    error) &&
      RequiredValue(table,
                    "life",
                    "mortality_age_old_years",
                    kAgeYears,
                    config.mortality_age_old_years,
                    error) &&
      RequiredValue(table,
                    "life",
                    "mortality_young_percent_per_year",
                    kPercent,
                    config.mortality_young_percent_per_year,
                    error) &&
      RequiredValue(table,
                    "life",
                    "mortality_mid_percent_per_year",
                    kPercent,
                    config.mortality_mid_percent_per_year,
                    error) &&
      RequiredValue(table,
                    "life",
                    "mortality_old_percent_per_year",
                    kPercent,
                    config.mortality_old_percent_per_year,
                    error) &&
      RequiredValue(table,
                    "life",
                    "migration_per_year",
                    Range{.low = 0.0F, .high = 1000.0F},
                    config.migration_per_year,
                    error) &&
      RequiredValue(
          table, "life", "epoch2_population", Range{.low = 1.0F, .high = 1.0e7F}, epoch2, error) &&
      RequiredValue(
          table, "life", "epoch3_population", Range{.low = 1.0F, .high = 1.0e7F}, epoch3, error) &&
      RequiredValue(table,
                    "life",
                    "marriage_chance_percent_per_day",
                    kPercent,
                    config.marriage_chance_percent_per_day,
                    error) &&
      RequiredValue(table,
                    "life",
                    "sex_balance_gain",
                    Range{.low = 0.0F, .high = 10.0F},
                    config.sex_balance_gain,
                    error);
  if (!ok) {
    return false;
  }
  // The casts below are now safe BY THE RANGES ABOVE and not by a second
  // check written beside them: the epoch thresholds are in [1, 1e7], which
  // is inside uint32 with four orders of magnitude to spare, and NaN never
  // got this far. One home for the rule — the range — instead of a range
  // and a guard that have to agree.
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
      {.key = "vitals_base_years",
       .value = &vitals.base_years,
       .range = {.low = 1.0F, .high = 120.0F}},
      {.key = "vitals_satiety_neutral",
       .value = &vitals.satiety_neutral,
       .range = {.low = 0.0F, .high = 100.0F}},
      {.key = "vitals_satiety_to_years_slope",
       .value = &vitals.satiety_to_years_slope,
       .range = {.low = 0.0F, .high = 10.0F}},
      {.key = "vitals_nutrition_years_min",
       .value = &vitals.nutrition_years_min,
       .range = {.low = -50.0F, .high = 0.0F}},
      {.key = "vitals_nutrition_years_max",
       .value = &vitals.nutrition_years_max,
       .range = {.low = 0.0F, .high = 50.0F}},
      {.key = "vitals_medicine_years",
       .value = &vitals.medicine_years,
       .range = {.low = 0.0F, .high = 50.0F}},
      {.key = "vitals_living_years",
       .value = &vitals.living_years,
       .range = {.low = 0.0F, .high = 50.0F}},
      {.key = "vitals_working_conditions_years",
       .value = &vitals.working_conditions_years,
       .range = {.low = -50.0F, .high = 50.0F}},
  }};
  const std::array<ScalarKnob, 11> birth_knobs = {{
      {.key = "birth_satisfaction_bound_1",
       .value = births.satisfaction_bounds.data(),
       .range = {.low = 0.0F, .high = 100.0F}},
      {.key = "birth_satisfaction_bound_2",
       .value = &births.satisfaction_bounds[1],
       .range = {.low = 0.0F, .high = 100.0F}},
      {.key = "birth_satisfaction_bound_3",
       .value = &births.satisfaction_bounds[2],
       .range = {.low = 0.0F, .high = 100.0F}},
      {.key = "birth_satisfaction_bound_4",
       .value = &births.satisfaction_bounds[3],
       .range = {.low = 0.0F, .high = 100.0F}},
      {.key = "birth_multiplier_1",
       .value = births.multipliers.data(),
       .range = {.low = 0.0F, .high = 5.0F}},
      {.key = "birth_multiplier_2",
       .value = &births.multipliers[1],
       .range = {.low = 0.0F, .high = 5.0F}},
      {.key = "birth_multiplier_3",
       .value = &births.multipliers[2],
       .range = {.low = 0.0F, .high = 5.0F}},
      {.key = "birth_multiplier_4",
       .value = &births.multipliers[3],
       .range = {.low = 0.0F, .high = 5.0F}},
      {.key = "birth_multiplier_5",
       .value = &births.multipliers[4],
       .range = {.low = 0.0F, .high = 5.0F}},
      {.key = "birth_satiety_stop",
       .value = &births.satiety_stop,
       .range = {.low = 0.0F, .high = 100.0F}},
      {.key = "birth_mother_health_stop",
       .value = &births.mother_health_stop,
       .range = {.low = 0.0F, .high = 100.0F}},
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

/// The world_params.csv keys THIS module reads, and the one place they are
/// written: the knob list below takes its names from this array by index, so
/// what is declared and what is read are the same array rather than two
/// lists that agree today.
///
/// THE BASE HEIGHTS ARE HERE TOO, and it took one wrong turn to see why. A
/// newborn is given a FRACTION and demography never needs metres — so the
/// first version of this list held three keys and said so. But the seam that
/// answers "how tall is this person" is the RESIDENTS' seam: people are this
/// module's rows, and a height in metres is a fact about a person. The bases
/// are not read by the birth rule; they are read by the answer this module
/// owes the outside.
///
/// Genesis declares the same five for its own read, and the two arrays are
/// deliberate rather than shared: each is the single source for ITS module's
/// read, so the day one of them stops reading a key, its list shrinks with
/// its code instead of waiting for someone to notice.
constexpr std::array<std::string_view, 5> kLifeWorldParamKeys = {"body_height_male_m",
                                                                 "body_height_female_m",
                                                                 "body_height_sigma_frac",
                                                                 "body_height_clamp_sigma",
                                                                 "body_build_sigma_frac"};

bool ParseBodyKnobs(const ITable& world, LifeConfig& config, std::string& error) {
  const std::array<ScalarKnob, kLifeWorldParamKeys.size()> rows = {
      ScalarKnob{.key = kLifeWorldParamKeys[0],
                 .value = &config.body.height_male_m,
                 .range = Range{.low = 0.5F, .high = 3.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[1],
                 .value = &config.body.height_female_m,
                 .range = Range{.low = 0.5F, .high = 3.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[2],
                 .value = &config.body.height_sigma_frac,
                 .range = Range{.low = 0.0F, .high = 0.5F}},
      ScalarKnob{.key = kLifeWorldParamKeys[3],
                 .value = &config.body.clamp_sigma,
                 .range = Range{.low = 0.5F, .high = 6.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[4],
                 .value = &config.body.build_sigma_frac,
                 .range = Range{.low = 0.0F, .high = 0.5F}}};
  return ReadKnobs(world, "world_params", rows, error);
}

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
  if (const ITable* world = tables.FindTable("world_params")) {
    if (!ParseBodyKnobs(*world, config, error)) {
      return false;
    }
  }
  if (const ITable* unit_types = tables.FindTable("unit_types")) {
    // No `!= kNoTableRow` guard any more, and it decided nothing even BEFORE
    // the conversion existed: kNoTableRow is 0xFFFFFFFF, so the truncating
    // cast it guarded already produced 0xFFFF — the invalid id — on exactly
    // the path it was meant to keep the cast off. It was right by accident
    // on the not-found path and pure ceremony on the other.
    config.house_type = DefIdFromRow<UnitTypeIdTag>(unit_types->FindRowByKey("wooden_house"));
  }
  return true;
}

std::span<const std::string_view> LifeWorldParamKeys() {
  return kLifeWorldParamKeys;
}

}  // namespace core
