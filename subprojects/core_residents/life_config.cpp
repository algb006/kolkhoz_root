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
#include <vector>

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
  // epoch2_population and epoch3_population were read here until
  // 2026-09-18, the thresholds of the population door that moved the era.
  // The door went (order_state.h, kAdvanceEra) and the keys left life.csv
  // with it; the table was the core's own, never the design base's.
  return ok;
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
constexpr std::array<std::string_view, 29> kLifeWorldParamKeys = {
    "body_height_male_m",
    "body_height_female_m",
    "body_height_sigma_frac",
    "body_height_clamp_sigma",
    "body_build_sigma_frac",
    // The steps of childhood and the bands they apply on (2026-09-13). This
    // module reads them because it is the one that answers
    // ResidentHeightMeters: a child's height is a fraction of the adult of
    // the same sex, and the fraction without its band is a name with no
    // value — which is exactly what the four fractions were for the hour
    // between the two exports.
    "body_height_infant_frac",
    "body_height_preschool_frac",
    "body_height_school_junior_frac",
    "body_height_school_senior_frac",
    "age_preschool_from_years",
    "age_school_junior_from_years",
    "age_school_senior_from_years",
    "age_adult_from_years",
    // The district's teacher norm and its cart (2026-09-14), for the
    // specialist's arrival. The cart's days are the limit's too; both modules
    // read the one row.
    "teacher_pupils_per_teacher",
    "limit_delivery_days",
    // Personal cleanliness (health design §3, 2026-09-17). The band it starts
    // in, what takes it away and the one thing that gives it back.
    "hygiene_start_min",
    "hygiene_start_max",
    "hygiene_fall_per_day",
    "hygiene_fall_dirty_work_factor",
    "hygiene_fall_heat_extra",
    "hygiene_rise_bath_per_day",
    "hygiene_disease_threshold",
    // The mud season (boss seq 189): the specialist rides the district's cart,
    // so its term stretches as the lot's does — a second reader of one row.
    "mud_speed_factor",
    // The old house «on the brink» (boss seq 191): host's fact's row, read by
    // the wedding too — a couple does not move into it.
    "old_house_near_collapse_wear",
    // The housing ladder (boss seq 197): the tent's months and the days a
    // request for the certificate waits for an answer.
    "tent_from_month",
    "tent_to_month",
    "leave_request_answer_days",
    "lodging_satisfaction_penalty",
    // The hunger alarm's hysteresis (core-host-l1 seq 45).
    "hunger_alarm_clear_margin"};

/// The barrack's places (housing §9; boss seq 197): unit_levels.csv
/// `residents_capacity`, by type and level. A table without the column leaves
/// every unit a single family's.
bool ParseResidentsCapacity(const ITableSet& tables, LifeConfig& config, std::string& error) {
  const ITable* const unit_types = tables.FindTable("unit_types");
  const ITable* const levels = tables.FindTable("unit_levels");
  if (unit_types == nullptr || levels == nullptr) {
    return true;
  }
  const std::uint32_t unit_col = levels->FindColumn("unit");
  const std::uint32_t level_col = levels->FindColumn("level");
  const std::uint32_t people_col = levels->FindColumn("residents_capacity");
  config.residents_capacity.assign(unit_types->RowCount(), {});
  if (people_col == kNoTableColumn) {
    return true;
  }
  for (std::uint32_t row = 0; row < levels->RowCount(); ++row) {
    const std::uint32_t type_row = unit_types->FindRowByKey(levels->CellText(row, unit_col));
    if (type_row == kNoTableRow) {
      continue;  // the construction parser owns this table's rows
    }
    float level = 0.0F;
    float people = 0.0F;
    if (!OptionalCell(*levels, row, level_col, Range{.low = 1.0F, .high = 255.0F}, level, error) ||
        !OptionalCell(
            *levels, row, people_col, Range{.low = 0.0F, .high = 10000.0F}, people, error)) {
      PrefixError("unit_levels", levels->CellText(row, unit_col), error);
      return false;
    }
    if (!(level >= 1.0F)) {
      continue;
    }
    std::vector<float>& ladder = config.residents_capacity[type_row];
    const auto index = static_cast<std::size_t>(level) - 1U;
    if (ladder.size() <= index) {
      ladder.resize(index + 1U, 0.0F);
    }
    ladder[index] = people;
  }
  return true;
}

bool ParseBodyKnobs(const ITable& world, LifeConfig& config, std::string& error) {
  // Human months 1..12 in the table, 0-based in the config.
  float tent_from = static_cast<float>(config.tent_from_month) + 1.0F;
  float tent_to = static_cast<float>(config.tent_to_month) + 1.0F;
  const Range months{.low = 1.0F, .high = 12.0F};
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
                 .range = Range{.low = 0.0F, .high = 0.5F}},
      ScalarKnob{.key = kLifeWorldParamKeys[5],
                 .value = &config.body.height_infant_frac,
                 .range = Range{.low = 0.1F, .high = 1.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[6],
                 .value = &config.body.height_preschool_frac,
                 .range = Range{.low = 0.1F, .high = 1.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[7],
                 .value = &config.body.height_school_junior_frac,
                 .range = Range{.low = 0.1F, .high = 1.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[8],
                 .value = &config.body.height_school_senior_frac,
                 .range = Range{.low = 0.1F, .high = 1.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[9],
                 .value = &config.body.age_preschool_from_years,
                 .range = Range{.low = 0.0F, .high = 20.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[10],
                 .value = &config.body.age_school_junior_from_years,
                 .range = Range{.low = 0.0F, .high = 20.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[11],
                 .value = &config.body.age_school_senior_from_years,
                 .range = Range{.low = 0.0F, .high = 20.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[12],
                 .value = &config.body.age_adult_from_years,
                 .range = Range{.low = 0.0F, .high = 30.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[13],
                 .value = &config.teacher_pupils_per_teacher,
                 .range = Range{.low = 1.0F, .high = 200.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[14],
                 .value = &config.specialist_delivery_days,
                 .range = Range{.low = 0.0F, .high = 48.0F}},
      // THE METRIC'S OWN SCALE IS THE RANGE, and that is the rule rather than
      // a sanity bound: hygiene is 0..100 like every other metric here, so a
      // start band or a threshold outside it would name a state a resident
      // can never be in.
      ScalarKnob{.key = kLifeWorldParamKeys[15],
                 .value = &config.hygiene_start_min,
                 .range = Range{.low = 0.0F, .high = 100.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[16],
                 .value = &config.hygiene_start_max,
                 .range = Range{.low = 0.0F, .high = 100.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[17],
                 .value = &config.hygiene_fall_per_day,
                 .range = Range{.low = 0.0F, .high = 100.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[18],
                 .value = &config.hygiene_fall_dirty_work_factor,
                 .range = Range{.low = 1.0F, .high = 10.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[19],
                 .value = &config.hygiene_fall_heat_extra,
                 .range = Range{.low = 0.0F, .high = 100.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[20],
                 .value = &config.hygiene_rise_bath_per_day,
                 .range = Range{.low = 0.0F, .high = 100.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[21],
                 .value = &config.hygiene_disease_threshold,
                 .range = Range{.low = 0.0F, .high = 100.0F}},
      // The same range core_production reads it in: the mud slows, never stops.
      ScalarKnob{.key = kLifeWorldParamKeys[22],
                 .value = &config.specialist_mud_speed_factor,
                 .range = Range{.low = 0.05F, .high = 1.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[23],
                 .value = &config.old_house_near_collapse_wear,
                 .range = Range{.low = 0.0F, .high = 1.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[24], .value = &tent_from, .range = months},
      ScalarKnob{.key = kLifeWorldParamKeys[25], .value = &tent_to, .range = months},
      ScalarKnob{.key = kLifeWorldParamKeys[26],
                 .value = &config.leave_request_answer_days,
                 .range = Range{.low = 0.0F, .high = 48.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[27],
                 .value = &config.lodging_satisfaction_penalty,
                 .range = Range{.low = 0.0F, .high = 100.0F}},
      ScalarKnob{.key = kLifeWorldParamKeys[28],
                 .value = &config.hunger_alarm_clear_margin,
                 .range = Range{.low = 0.0F, .high = 100.0F}}};
  if (!ReadKnobs(world, "world_params", rows, error)) {
    return false;
  }
  config.tent_from_month = static_cast<std::uint8_t>(tent_from - 1.0F);
  config.tent_to_month = static_cast<std::uint8_t>(tent_to - 1.0F);
  return true;
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
  // THE ONE ROW THIS MODULE READS FROM THE WEATHER'S TABLE, and it is read
  // rather than copied for the reason the table itself gives one comment
  // above it: +25 is three facts in this project, two of which happen to
  // share a number. Hygiene's hot day must be the SAME hot day the weather
  // announces, or the two drift the first time anybody moves one of them.
  if (const ITable* weather = tables.FindTable("weather_params")) {
    const std::array<ScalarKnob, 1> knobs = {
        ScalarKnob{.key = "hot_afternoon_c",
                   .value = &config.hot_afternoon_celsius,
                   .range = Range{.low = 0.0F, .high = 50.0F}}};
    if (!ReadKnobs(*weather, "weather_params", knobs, error)) {
      return false;
    }
  }
  if (!ParseMembershipConfig(tables, config.membership, error) ||
      !ParseNightTradeConfig(tables, config.night_trade, error) ||
      !ParseSchoolingConfig(tables, config.schooling, error) ||
      !ParseAlcoholismConfig(tables, config.alcoholism, error) ||
      !ParseSportConfig(tables, config.sport, error)) {
    return false;
  }
  if (!ParseResidentsCapacity(tables, config, error)) {
    return false;
  }
  if (const ITable* unit_types = tables.FindTable("unit_types")) {
    // No `!= kNoTableRow` guard any more, and it decided nothing even BEFORE
    // the conversion existed: kNoTableRow is 0xFFFFFFFF, so the truncating
    // cast it guarded already produced 0xFFFF — the invalid id — on exactly
    // the path it was meant to keep the cast off. It was right by accident
    // on the not-found path and pure ceremony on the other.
    config.school_type = DefIdFromRow<UnitTypeIdTag>(unit_types->FindRowByKey("school"));
    // The one riser hygiene has here (health design §3).
    config.bathhouse_type = DefIdFromRow<UnitTypeIdTag>(unit_types->FindRowByKey("bathhouse"));
    config.reading_hut_type =
        DefIdFromRow<UnitTypeIdTag>(unit_types->FindRowByKey("culture_house"));
    config.old_house_type = DefIdFromRow<UnitTypeIdTag>(unit_types->FindRowByKey("old_house"));
  }
  if (const ITable* professions = tables.FindTable("professions")) {
    config.teacher_post =
        DefIdFromRow<ProfessionIdTag>(professions->FindRowByKey("primary_teacher"));
    config.librarian_post = DefIdFromRow<ProfessionIdTag>(professions->FindRowByKey("librarian"));
  }
  return true;
}

std::span<const std::string_view> LifeWorldParamKeys() {
  // The module's two readers of the table: this file's knobs and the
  // organizations' (membership.cpp), each list the single source for its own
  // read, joined once for the assembly.
  static const std::vector<std::string_view> kAll = [] {
    std::vector<std::string_view> keys(kLifeWorldParamKeys.begin(), kLifeWorldParamKeys.end());
    const std::span<const std::string_view> membership = MembershipWorldParamKeys();
    keys.insert(keys.end(), membership.begin(), membership.end());
    const std::span<const std::string_view> night = NightTradeWorldParamKeys();
    keys.insert(keys.end(), night.begin(), night.end());
    const std::span<const std::string_view> school = SchoolingWorldParamKeys();
    keys.insert(keys.end(), school.begin(), school.end());
    const std::span<const std::string_view> drinking = AlcoholismWorldParamKeys();
    keys.insert(keys.end(), drinking.begin(), drinking.end());
    const std::span<const std::string_view> sport = SportWorldParamKeys();
    keys.insert(keys.end(), sport.begin(), sport.end());
    return keys;
  }();
  return kAll;
}

}  // namespace core
