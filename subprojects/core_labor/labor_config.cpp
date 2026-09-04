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

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// @brief Turns "a cell is not a number" into "labor: path_factor: a cell is
/// not a number", building the message with appends only.
bool ParseScalars(const ITable& table, LaborConfig& config, std::string& error) {
  float placement = config.placement_level;
  const std::array<ScalarKnob, 12> knobs = {{
      {.key = "standard_day_hours",
       .value = &config.standard_day_hours,
       .range = {.low = 1.0F, .high = 24.0F}},
      {.key = "travel_limit_hours",
       .value = &config.travel_limit_hours,
       .range = {.low = 0.0F, .high = 24.0F}},
      {.key = "min_usable_hours",
       .value = &config.min_usable_hours,
       .range = {.low = 0.0F, .high = 24.0F}},
      {.key = "path_factor", .value = &config.path_factor, .range = {.low = 1.0F, .high = 5.0F}},
      {.key = "sleep_hours", .value = &config.sleep_hours, .range = {.low = 0.0F, .high = 16.0F}},
      {.key = "rest_walkoff_threshold",
       .value = &config.rest_walkoff_threshold,
       .range = {.low = 0.0F, .high = 100.0F}},
      {.key = "rest_recovery_day_off",
       .value = &config.rest_recovery_day_off,
       .range = {.low = 0.0F, .high = 100.0F}},
      {.key = "rest_recovery_idle_day",
       .value = &config.rest_recovery_idle_day,
       .range = {.low = 0.0F, .high = 100.0F}},
      {.key = "health_loss_per_spent_day",
       .value = &config.health_loss_per_spent_day,
       .range = {.low = 0.0F, .high = 10.0F}},
      {.key = "stamina_drain_relief",
       .value = &config.stamina_drain_relief,
       .range = {.low = 0.0F, .high = 1.0F}},
      {.key = "self_education_max_bonus",
       .value = &config.self_education_max_bonus,
       .range = {.low = 0.0F, .high = 1.0F}},
      {.key = "placement_level", .value = &placement, .range = {.low = 0.0F, .high = 3.0F}},
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
       .range = {.low = 0.0F, .high = 100.0F}},
      {.key = "efficiency_health_slope",
       .value = &factors.health_slope,
       .range = {.low = 0.0F, .high = 5.0F}},
      {.key = "efficiency_mood_pivot",
       .value = &factors.mood_pivot,
       .range = {.low = 0.0F, .high = 100.0F}},
      {.key = "efficiency_mood_slope",
       .value = &factors.mood_slope,
       .range = {.low = 0.0F, .high = 5.0F}},
      {.key = "efficiency_skill_pivot",
       .value = &factors.skill_pivot,
       .range = {.low = 0.0F, .high = 100.0F}},
      {.key = "efficiency_skill_slope",
       .value = &factors.skill_slope,
       .range = {.low = 0.0F, .high = 5.0F}},
      {.key = "rest_step_tired",
       .value = &factors.rest_step_tired,
       .range = {.low = 0.0F, .high = 100.0F}},
      {.key = "rest_step_spent",
       .value = &factors.rest_step_spent,
       .range = {.low = 0.0F, .high = 100.0F}},
      {.key = "rest_factor_tired",
       .value = &factors.rest_factor_tired,
       .range = {.low = 0.0F, .high = 1.0F}},
      {.key = "rest_factor_spent",
       .value = &factors.rest_factor_spent,
       .range = {.low = 0.0F, .high = 1.0F}},
      {.key = "aging_margin_years",
       .value = &factors.aging_margin_years,
       .range = {.low = 0.0F, .high = 100.0F}},
      {.key = "age_decline_per_year",
       .value = &factors.age_decline_per_year,
       .range = {.low = 0.0F, .high = 1.0F}},
      {.key = "age_decline_floor",
       .value = &factors.age_decline_floor,
       .range = {.low = 0.0F, .high = 1.0F}},
      {.key = "skill_earned_weight",
       .value = &skill.earned_weight,
       .range = {.low = 0.0F, .high = 1.0F}},
      {.key = "skill_stamina_weight",
       .value = &skill.stamina_weight,
       .range = {.low = 0.0F, .high = 1.0F}},
      {.key = "skill_schooled_weight",
       .value = &skill.schooled_weight,
       .range = {.low = 0.0F, .high = 1.0F}},
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
    if (!OptionalValue(table,
                       kKeys[index],
                       Range{.low = 0.1F, .high = 3.0F},
                       config.education_factor[index],
                       error)) {
      PrefixError("labor", kKeys[index], error);
      return false;
    }
  }
  return true;
}

/// One row per work kind, in the order of the WorkKind enum (kNone has no
/// row: nobody is paid for idling).
bool ParseWorkKindRates(const ITable& table, LaborConfig& config, std::string& error) {
  // In WorkKind order, because the loop indexes by position: kNone has no
  // row, so entry N lands on kind N+1. A key the table does not carry keeps
  // the compiled default, and that is how BOTH construction and hauling
  // stand today: labor.csv is generated from the design db and carries
  // neither row. The keys are listed so that the day the db grows them the
  // core reads them without a rebuild — and so that the reader is not left
  // wondering why two of the eight kinds are missing.
  constexpr std::array<std::string_view, 7> kKeys = {
      "plowing", "harrowing", "sowing", "harvest", "herd_care", "construction", "hauling"};
  const std::uint32_t rate_column = table.FindColumn("trudodni_rate");
  const std::uint32_t drain_column = table.FindColumn("rest_drain_per_norm_day");
  for (std::uint32_t index = 0; index < kKeys.size(); ++index) {
    const std::uint32_t row = table.FindRowByKey(kKeys[index]);
    WorkKindRates& rates = config.rates[index + 1];  // +1: WorkKind::kNone is row-less
    if (!OptionalCell(table,
                      row,
                      rate_column,
                      Range{.low = 0.0F, .high = 10.0F},
                      rates.trudodni_rate,
                      error) ||
        !OptionalCell(table,
                      row,
                      drain_column,
                      Range{.low = 0.0F, .high = 100.0F},
                      rates.rest_drain_per_norm_day,
                      error)) {
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
                    Range{.low = 0.5F, .high = 20.0F},
                    config.walk_speed_kmh,
                    error) ||
      !OptionalCell(table,
                    table.FindRowByKey("horse_trot"),
                    speed_column,
                    Range{.low = 0.5F, .high = 60.0F},
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
    if (!OptionalCell(table,
                      row,
                      care_column,
                      Range{.low = 0.0F, .high = 3650.0F},
                      config.care_days_per_year[row],
                      error)) {
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

/// @brief The education stage a `min_education` cell names. The keys are the
/// design db's own (`education_level.key`), and "any" is the floor rather
/// than a special case: everybody has at least no schooling.
bool ParseEducationStage(std::string_view key, EducationStage& stage, std::string& error) {
  if (key.empty() || key == "any") {
    stage = EducationStage::kNone;
    return true;
  }
  if (key == "primary") {
    stage = EducationStage::kPrimary;
    return true;
  }
  if (key == "secondary") {
    stage = EducationStage::kSecondary;
    return true;
  }
  if (key == "vocational") {
    stage = EducationStage::kVocational;
    return true;
  }
  if (key == "higher") {
    stage = EducationStage::kHigher;
    return true;
  }
  error = "min_education names no education stage";
  return false;
}

bool ParseSexRule(std::string_view key, PostSexRule& rule, std::string& error) {
  if (key.empty() || key == "any") {
    rule = PostSexRule::kAny;
    return true;
  }
  if (key == "female") {
    rule = PostSexRule::kFemale;
    return true;
  }
  if (key == "male") {
    rule = PostSexRule::kMale;
    return true;
  }
  error = "gender is neither any, female nor male";
  return false;
}

/// tables/professions.csv — the roster of posts (manual/74-posts.md §6).
/// A ProfessionId IS the row number here, like every other definition table.
bool ParseProfessions(const ITable& table, LaborConfig& config, std::string& error) {
  const std::uint32_t education_column = table.FindColumn("min_education");
  const std::uint32_t min_age_column = table.FindColumn("min_age");
  const std::uint32_t max_age_column = table.FindColumn("max_age");
  const std::uint32_t gender_column = table.FindColumn("gender");
  const std::uint32_t single_column = table.FindColumn("single_post");
  config.professions.assign(table.RowCount(), ProfessionDef{});
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    ProfessionDef& post = config.professions[row];
    if (education_column != kNoTableColumn &&
        !ParseEducationStage(table.CellText(row, education_column), post.min_education, error)) {
      PrefixError("professions", table.CellText(row, 0), error);
      return false;
    }
    if (gender_column != kNoTableColumn &&
        !ParseSexRule(table.CellText(row, gender_column), post.sex_rule, error)) {
      PrefixError("professions", table.CellText(row, 0), error);
      return false;
    }
    float single = 0.0F;
    if (!OptionalCell(table,
                      row,
                      min_age_column,
                      Range{.low = 0.0F, .high = 120.0F},
                      post.min_age_years,
                      error) ||
        !OptionalCell(table,
                      row,
                      max_age_column,
                      Range{.low = 0.0F, .high = 120.0F},
                      post.max_age_years,
                      error) ||
        !OptionalCell(table, row, single_column, Range{.low = 0.0F, .high = 1.0F}, single, error)) {
      PrefixError("professions", table.CellText(row, 0), error);
      return false;
    }
    post.single_post = static_cast<std::uint8_t>(single);
  }
  return true;
}

/// tables/unit_staff.csv — who a unit has room for. A row naming a unit type
/// or a post that is not in its own table is an error and not a silent skip:
/// a staff line nobody can ever satisfy is a broken export, and the day it
/// appears is the day to say so.
bool ParseUnitStaff(const ITable& table,
                    const ITableSet& tables,
                    LaborConfig& config,
                    std::string& error) {
  const ITable* unit_types = tables.FindTable("unit_types");
  const ITable* professions = tables.FindTable("professions");
  if (unit_types == nullptr || professions == nullptr) {
    error = "unit_staff needs unit_types and professions, and one of them is missing";
    PrefixError("unit_staff", "", error);
    return false;
  }
  const std::uint32_t unit_column = table.FindColumn("unit");
  const std::uint32_t profession_column = table.FindColumn("profession");
  const std::uint32_t level_column = table.FindColumn("level");
  const std::uint32_t slots_column = table.FindColumn("slots");
  if (unit_column == kNoTableColumn || profession_column == kNoTableColumn) {
    error = "unit_staff has no unit or profession column";
    PrefixError("unit_staff", "", error);
    return false;
  }
  config.staff.clear();
  config.staff.reserve(table.RowCount());
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    const std::uint32_t type_row = unit_types->FindRowByKey(table.CellText(row, unit_column));
    const std::uint32_t post_row =
        professions->FindRowByKey(table.CellText(row, profession_column));
    if (type_row == kNoTableRow || post_row == kNoTableRow) {
      error = "a staff line names a unit type or a post that no table has";
      PrefixError("unit_staff", table.CellText(row, unit_column), error);
      return false;
    }
    StaffSlot slot;
    slot.unit_type = UnitTypeId{static_cast<std::uint16_t>(type_row)};
    slot.profession = ProfessionId{static_cast<std::uint16_t>(post_row)};
    float level = 0.0F;
    float slots = 0.0F;
    if (!OptionalCell(table, row, level_column, Range{.low = 0.0F, .high = 255.0F}, level, error) ||
        !OptionalCell(
            table, row, slots_column, Range{.low = 0.0F, .high = 65535.0F}, slots, error)) {
      PrefixError("unit_staff", table.CellText(row, unit_column), error);
      return false;
    }
    slot.level = static_cast<std::uint8_t>(level);
    slot.slots = static_cast<std::uint16_t>(slots);
    config.staff.push_back(slot);
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
    if (!OptionalCell(table, row, sow_column, Range{.low = 1.0F, .high = 12.0F}, sow_to, error) ||
        !OptionalCell(
            table, row, harvest_column, Range{.low = 1.0F, .high = 12.0F}, harvest_to, error)) {
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
        {.key = "life_speedup",
         .value = &config.life_speedup,
         .range = {.low = 0.1F, .high = 100.0F}},
        {.key = "adult_age_years",
         .value = &config.adult_age_years,
         .range = {.low = 1.0F, .high = 100.0F}},
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
  if (const ITable* professions = tables.FindTable("professions")) {
    if (!ParseProfessions(*professions, config, error)) {
      return false;
    }
  }
  if (const ITable* staff = tables.FindTable("unit_staff")) {
    if (!ParseUnitStaff(*staff, tables, config, error)) {
      return false;
    }
  }
  // The groom, by the key the roster gives him. It is a LITERAL and not a
  // knob on purpose: core_production looks the same post up for itself (the
  // herd day owns the herds it moves — stable_horses.h), and a key that one
  // module could be told to change while the other could not would let the
  // alarm watch one post while the transfer waited on another. One name, in
  // one place, read twice. A roster without him is not an error: a
  // table-less test world has no posts at all, and then there is no yard
  // alarm to raise either.
  if (const ITable* professions = tables.FindTable("professions")) {
    const std::uint32_t row = professions->FindRowByKey(kGroomPostKey);
    if (row != kNoTableRow) {
      config.groom_post = ProfessionId{static_cast<std::uint16_t>(row)};
    }
  }
  return true;
}

}  // namespace core
