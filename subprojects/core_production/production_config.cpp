// Parsing of the production configuration (production_config.h): the five
// tables core_production reads — crops, livestock, unit_types, resources
// (its feed_value column) and feed_links — plus the scalar knobs of farming
// and campaign.
//
// Four of those five are EXPORTS of the design db and must never be edited
// by hand (manual/61-balance-tables.md §4а). That is why this file is fussy
// about column names and units: when a column moves or is renamed upstream,
// the parse has to fail loudly or fall back visibly, never quietly return a
// default that looks plausible.
//
// Policy, shared with every other subsystem factory: a MISSING table keeps
// the canonical defaults (a unit test's world has no tables at all), while a
// PRESENT table that cannot be read is an error. Numbers are range-checked
// here, at parse time, because everything downstream casts them to integers
// or multiplies them into grams.

#include "production_config.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_log/log.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// @brief ResourceId by key of the resources table; invalid when absent.
ResourceId ResourceByKey(const ITable* resources, std::string_view key) {
  if (resources == nullptr) {
    return ResourceId{};
  }
  const std::uint32_t row = resources->FindRowByKey(key);
  return row == kNoTableRow ? ResourceId{} : ResourceId{static_cast<std::uint16_t>(row)};
}

/// @brief Reads a cell as float with `fallback` for an empty cell; a present
/// non-numeric cell fails the parse. The value is range-checked here, at
/// parse time: everything downstream casts these numbers to integers or
/// multiplies them into grams, and a NaN, an infinity or an absurd
/// magnitude would be undefined behaviour there instead of a clear error.
bool CellOrDefault(const ITable& table,
                   std::uint32_t row,
                   std::uint32_t column,
                   float fallback,
                   float low,
                   float high,
                   float& value,
                   std::string& error) {
  if (column == kNoTableColumn || table.CellText(row, column).empty()) {
    value = fallback;
    return true;
  }
  const std::optional<float> cell = table.CellReal(row, column);
  if (!cell) {
    error = "a cell is not a number";
    return false;
  }
  // Written as a positive test so that NaN fails it: NaN compares false
  // against everything, including itself.
  if (!(*cell >= low && *cell <= high)) {
    error = "a cell is out of range";
    return false;
  }
  value = *cell;
  return true;
}

bool ParseCrops(const ITable& table,
                const ITable* resources,
                std::vector<CropDef>& crops,
                std::string& error) {
  const std::uint32_t resource_col = table.FindColumn("resource");
  if (resource_col == kNoTableColumn) {
    error = "crops: no resource column";
    return false;
  }

  struct Column {
    const char* name;
    float fallback;
    float low;  ///< Inclusive bounds, checked at parse time.
    float high;
  };

  // Months are 1..12 here, so the shift to the core's 0-based Month enum
  // below can never produce a negative value.
  constexpr std::array<Column, 16> kColumns = {{{"is_winter", 0, 0, 1},
                                                {"is_perennial", 0, 0, 1},
                                                {"sow_from_month", 1, 1, 12},
                                                {"sow_to_month", 1, 1, 12},
                                                {"sow_min_temp_c", 0, -50, 50},
                                                {"harvest_from_month", 1, 1, 12},
                                                {"harvest_to_month", 1, 1, 12},
                                                {"harvest_min_temp_c", 0, -50, 50},
                                                {"yield_kg_per_ha", 0, 0, 1e6F},
                                                {"sowing_norm_kg_per_ha", 0, 0, 1e6F},
                                                {"fertility_delta", 0, -100, 100},
                                                {"drought_sensitivity", 0, 0, 1},
                                                {"wet_sensitivity", 0, 0, 1},
                                                // REAL man-days per hectare, as the
                                                // agronomy books write them; the grain
                                                // anchor 3 + 8 is the default until the
                                                // columns exist.
                                                {"sow_days_per_ha", 3, 0, 1000},
                                                {"harvest_days_per_ha", 8, 0, 1000},
                                                {"straw_ratio", 0, 0, 10}}};
  std::array<std::uint32_t, kColumns.size()> columns{};
  for (std::uint32_t index = 0; index < kColumns.size(); ++index) {
    columns[index] = table.FindColumn(kColumns[index].name);
  }
  crops.resize(table.RowCount());
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    std::array<float, kColumns.size()> values{};
    for (std::uint32_t index = 0; index < kColumns.size(); ++index) {
      if (!CellOrDefault(table,
                         row,
                         columns[index],
                         kColumns[index].fallback,
                         kColumns[index].low,
                         kColumns[index].high,
                         values[index],
                         error)) {
        error = "crops: " + error;
        return false;
      }
    }
    CropDef& crop = crops[row];
    crop.resource = ResourceByKey(resources, table.CellText(row, resource_col));
    crop.is_winter = values[0] != 0.0F;
    crop.is_perennial = values[1] != 0.0F;
    // Table months are human 1..12; the core's Month enum is 0-based.
    crop.sow_from_month = static_cast<std::uint8_t>(values[2] - 1.0F);
    crop.sow_to_month = static_cast<std::uint8_t>(values[3] - 1.0F);
    crop.sow_min_temp_c = values[4];
    crop.harvest_from_month = static_cast<std::uint8_t>(values[5] - 1.0F);
    crop.harvest_to_month = static_cast<std::uint8_t>(values[6] - 1.0F);
    crop.harvest_min_temp_c = values[7];
    crop.yield_kg_per_ha = values[8];
    crop.sowing_norm_kg_per_ha = values[9];
    crop.fertility_delta = values[10];
    crop.drought_sensitivity = values[11];
    crop.wet_sensitivity = values[12];
    crop.sow_days_per_ha = values[13] / kRealDaysPerGameDay;
    crop.harvest_days_per_ha = values[14] / kRealDaysPerGameDay;
    crop.straw_ratio = values[15];
  }
  return true;
}

bool ParseFarming(const ITable& table, FarmingConfig& farming, std::string& error) {
  const std::uint32_t value_col = table.FindColumn("value");
  if (value_col == kNoTableColumn) {
    error = "farming: no value column";
    return false;
  }

  struct Entry {
    const char* key;
    float* value;
  };

  const Entry entries[] = {{"fertility_neutral", &farming.fertility_neutral},
                           {"manure_norm_kg_per_ha", &farming.manure_norm_kg_per_ha},
                           {"manure_fertility_bonus", &farming.manure_fertility_bonus},
                           {"fallow_recovery", &farming.fallow_recovery},
                           {"repeat_penalty_per_year", &farming.repeat_penalty_per_year},
                           {"drought_temp_c", &farming.drought_temp_c},
                           {"stress_per_day", &farming.stress_per_day},
                           {"stress_cap", &farming.stress_cap}};
  for (const Entry& entry : entries) {
    const std::uint32_t row = table.FindRowByKey(entry.key);
    const std::optional<float> cell = table.CellReal(row, value_col);
    if (row == kNoTableRow || !cell) {
      error = std::string("farming: row '") + entry.key + "' is missing or not numeric";
      return false;
    }
    // Positive test: NaN and the infinities fail it (see CellOrDefault).
    if (!(*cell >= -1.0e6F && *cell <= 1.0e6F)) {
      error = std::string("farming: value of '") + entry.key + "' is out of range";
      return false;
    }
    *entry.value = *cell;
  }
  if (!(farming.fertility_neutral > 0.0F)) {
    error = "farming: fertility_neutral must be positive";
    return false;
  }
  return true;
}

/// Ploughing and harrowing, from the design db's own table of field phases.
///
/// They lived in the hand-written farming.csv for a month, and only because
/// they belong to no crop: sowing and harvest are per culture, but breaking
/// the sod is the same work on any land. A number that is COMMON is the kind
/// that goes missing — the specific gets written down, the general gets
/// taken for granted and ends up in a CSV comment (boss parcel 2026-08-31).
///
/// A phase whose `from_crop` is set takes its norm from the crop row instead
/// and is skipped here; ploughing and harrowing are the only two that do not.
bool ParseFieldPhases(const ITable& table, FarmingConfig& farming, std::string& error) {
  struct Phase {
    const char* key;
    float* value;
  };

  const std::array<Phase, 2> phases = {
      {{"plough", &farming.plow_days_per_ha}, {"harrow", &farming.harrow_days_per_ha}}};
  const std::uint32_t days_col = table.FindColumn("labor_days_per_ha");
  for (const Phase& phase : phases) {
    float days = *phase.value * kRealDaysPerGameDay;  // back to REAL for the read
    if (!CellOrDefault(
            table, table.FindRowByKey(phase.key), days_col, days, 0.0F, 1000.0F, days, error)) {
      error = std::string("field_phases: ") + phase.key + ": " + error;
      return false;
    }
    *phase.value = days / kRealDaysPerGameDay;  // the table keeps REAL man-days
  }
  return true;
}

/// The stage-6 herd knobs of farming.csv. Optional as a group: a table set
/// from before this stage keeps the defaults, which equal the shipped file.
bool ParseHerdKnobs(const ITable& table, FarmingConfig& farming, std::string& error) {
  struct Knob {
    const char* key;
    float* value;
    float low;
    float high;
  };

  float pasture_from = static_cast<float>(farming.pasture_from_month) + 1.0F;
  float pasture_to = static_cast<float>(farming.pasture_to_month) + 1.0F;
  float pig_month = static_cast<float>(farming.pig_slaughter_month) + 1.0F;
  float birth_from = static_cast<float>(farming.birth_from_month) + 1.0F;
  float birth_to = static_cast<float>(farming.birth_to_month) + 1.0F;
  float mow_month = static_cast<float>(farming.meadow_cut_month) + 1.0F;
  // Real man-days in the file, game man-days in the config — the same
  // conversion the crop and field-phase norms get, done once at parse.
  float mow_days = farming.meadow_mow_days_per_ha * kRealDaysPerGameDay;
  const std::array<Knob, 18> knobs = {{
      {"unfed_produce_factor", &farming.unfed_produce_factor, 0.0F, 1.0F},
      {"unfed_death_after_days", &farming.unfed_death_after_days, 0.0F, 1000.0F},
      {"unfed_death_percent_per_day", &farming.unfed_death_percent_per_day, 0.0F, 100.0F},
      {"juvenile_feed_factor", &farming.juvenile_feed_factor, 0.0F, 1.0F},
      {"reserve_feed_factor", &farming.reserve_feed_factor, 0.0F, 1.0F},
      {"billet_yield_factor", &farming.billet_yield_factor, 0.0F, 1.0F},
      {"pasture_from_month", &pasture_from, 1.0F, 12.0F},
      {"pasture_to_month", &pasture_to, 1.0F, 12.0F},
      {"pig_slaughter_month", &pig_month, 1.0F, 12.0F},
      {"sow_keep_share", &farming.sow_keep_share, 0.0F, 1.0F},
      {"repeat_penalty_max_years", &farming.repeat_penalty_max_years, 0.0F, 250.0F},
      {"fertility_floor", &farming.fertility_floor, 0.0F, 100.0F},
      {"meadow_yield_kg_per_ha", &farming.meadow_yield_kg_per_ha, 0.0F, 1e5F},
      {"meadow_floodplain_yield_kg_per_ha", &farming.meadow_floodplain_yield_kg_per_ha, 0.0F, 1e5F},
      {"meadow_mow_days_per_ha", &mow_days, 0.0F, 1000.0F},
      {"meadow_cut_month", &mow_month, 1.0F, 12.0F},
      {"birth_from_month", &birth_from, 1.0F, 12.0F},
      {"birth_to_month", &birth_to, 1.0F, 12.0F},
  }};
  const std::uint32_t value_col = table.FindColumn("value");
  for (const Knob& knob : knobs) {
    if (!CellOrDefault(table,
                       table.FindRowByKey(knob.key),
                       value_col,
                       *knob.value,
                       knob.low,
                       knob.high,
                       *knob.value,
                       error)) {
      error = std::string("farming: ") + knob.key + ": " + error;
      return false;
    }
  }
  // Months are human 1..12 in every table of the core; the enum is 0-based.
  farming.pasture_from_month = static_cast<std::uint8_t>(pasture_from - 1.0F);
  farming.pasture_to_month = static_cast<std::uint8_t>(pasture_to - 1.0F);
  farming.pig_slaughter_month = static_cast<std::uint8_t>(pig_month - 1.0F);
  farming.meadow_cut_month = static_cast<std::uint8_t>(mow_month - 1.0F);
  farming.meadow_mow_days_per_ha = mow_days / kRealDaysPerGameDay;
  farming.birth_from_month = static_cast<std::uint8_t>(birth_from - 1.0F);
  farming.birth_to_month = static_cast<std::uint8_t>(birth_to - 1.0F);
  return true;
}

bool ParseLivestock(const ITable& table, std::vector<LivestockDef>& livestock, std::string& error) {
  struct Column {
    const char* name;
    float fallback;
    float low;
    float high;
  };

  // The unit trap of production_config.h in table form: feed and care are
  // REAL rates, ages are GAME units with the x4 acceleration already in them.
  constexpr std::array<Column, 18> kColumns = {{{"sexed", 1, 0, 1},
                                                {"household_only", 0, 0, 1},
                                                {"kolkhoz_only", 0, 0, 1},
                                                {"feed_units_per_real_day", 0, 0, 1000},
                                                {"pasture_coverage_summer", 0, 0, 1},
                                                {"newborn_game_months", 0, 0, 600},
                                                {"adult_from_game_months", 0, 0, 600},
                                                {"life_game_years_min", 0, 0, 100},
                                                {"life_game_years_max", 0, 0, 100},
                                                {"births_per_game_year", 0, 0, 100},
                                                {"litter_heads", 1, 0, 100},
                                                {"males_share", 0, 0, 1},
                                                {"milk_l_per_year", 0, 0, 1e5F},
                                                {"egg_kg_per_year", 0, 0, 1e5F},
                                                {"wool_kg_per_year", 0, 0, 1e5F},
                                                {"manure_kg_per_year", 0, 0, 1e6F},
                                                {"meat_kg_per_head", 0, 0, 1e5F},
                                                {"hide_pieces_per_head", 0, 0, 100}}};
  std::array<std::uint32_t, kColumns.size()> columns{};
  for (std::uint32_t index = 0; index < kColumns.size(); ++index) {
    columns[index] = table.FindColumn(kColumns[index].name);
  }
  const std::uint32_t pelt_col = table.FindColumn("pelt_pieces_per_head");
  const std::uint32_t down_col = table.FindColumn("down_kg_per_head");
  const std::uint32_t self_fed_col = table.FindColumn("household_self_fed");
  const std::uint32_t cap_col = table.FindColumn("household_cap_heads");
  const std::uint32_t group_col = table.FindColumn("household_group");
  livestock.resize(table.RowCount());
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    std::array<float, kColumns.size()> values{};
    for (std::uint32_t index = 0; index < kColumns.size(); ++index) {
      if (!CellOrDefault(table,
                         row,
                         columns[index],
                         kColumns[index].fallback,
                         kColumns[index].low,
                         kColumns[index].high,
                         values[index],
                         error)) {
        error = "livestock: " + error;
        return false;
      }
    }
    LivestockDef& kind = livestock[row];
    kind.sexed = static_cast<std::uint8_t>(values[0]);
    kind.household_only = static_cast<std::uint8_t>(values[1]);
    kind.kolkhoz_only = static_cast<std::uint8_t>(values[2]);
    // REAL fodder units a day -> GAME day: a game year holds 365 real days
    // of eating in 48 of its own, and it is the yearly MASS that has to hold.
    kind.feed_units_per_game_day = values[3] * 365.0F / static_cast<float>(kDaysPerYear);
    kind.pasture_coverage_summer = values[4];
    kind.newborn_game_months = values[5];
    kind.adult_from_game_months = values[6];
    kind.life_game_years_min = values[7];
    kind.life_game_years_max = values[8];
    kind.births_per_game_year = values[9];
    kind.litter_heads = values[10];
    kind.males_share = values[11];
    kind.milk_l_per_year = values[12];
    kind.egg_kg_per_year = values[13];
    kind.wool_kg_per_year = values[14];
    kind.manure_kg_per_year = values[15];
    kind.meat_kg_per_head = values[16];
    kind.hide_pieces_per_head = values[17];
    float self_fed = 0.0F;
    if (!CellOrDefault(table, row, pelt_col, 0, 0, 100, kind.pelt_pieces_per_head, error) ||
        !CellOrDefault(table, row, down_col, 0, 0, 100, kind.down_kg_per_head, error) ||
        !CellOrDefault(table, row, self_fed_col, 0, 0, 1, self_fed, error)) {
      error = "livestock: " + error;
      return false;
    }
    kind.household_self_fed = static_cast<std::uint8_t>(self_fed);
    if (!CellOrDefault(table, row, cap_col, 0, 0, 1000, kind.household_cap_heads, error)) {
      error = "livestock: " + error;
      return false;
    }
    const std::string_view group =
        group_col == kNoTableColumn ? std::string_view{} : table.CellText(row, group_col);
    kind.household_group = group == "stock" ? 1U : (group == "bird" ? 2U : 0U);
    // A band that is empty or inverted would make the age hazard nonsense.
    if (kind.life_game_years_max < kind.life_game_years_min) {
      kind.life_game_years_max = kind.life_game_years_min;
    }
  }
  return true;
}

bool ParseUnitTypes(const ITable& table, std::vector<UnitTypeDef>& types, std::string& error) {
  // Tonnes, not kilograms: the store counts in what the design counts in,
  // and the unit is in the column's name so that nobody has to remember it.
  // The older hand-written file used storage_capacity_kg; both are read, and
  // whichever is present wins — that keeps the switch to the export from
  // silently zeroing every warehouse.
  const std::uint32_t tonnes_col = table.FindColumn("storage_capacity_t");
  const std::uint32_t kilograms_col = table.FindColumn("storage_capacity_kg");
  const std::uint32_t heads_col = table.FindColumn("livestock_heads");
  const std::uint32_t heads_old_col = table.FindColumn("livestock_capacity_head");
  const std::uint32_t by_plot_col = table.FindColumn("capacity_by_plot");
  types.resize(table.RowCount());
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    UnitTypeDef& type = types[row];
    float tonnes = 0.0F;
    float by_plot = 0.0F;
    if (!CellOrDefault(table, row, tonnes_col, 0, 0, 1e6F, tonnes, error) ||
        !CellOrDefault(table, row, kilograms_col, 0, 0, 1e9F, type.storage_capacity_kg, error) ||
        !CellOrDefault(table, row, heads_col, 0, 0, 1e6F, type.livestock_capacity_head, error) ||
        !CellOrDefault(table,
                       row,
                       heads_old_col,
                       type.livestock_capacity_head,
                       0,
                       1e6F,
                       type.livestock_capacity_head,
                       error) ||
        !CellOrDefault(table, row, by_plot_col, 0, 0, 1, by_plot, error)) {
      error = "unit_types: " + error;
      return false;
    }
    if (tonnes > 0.0F) {
      type.storage_capacity_kg = tonnes * 1000.0F;
    }
    type.capacity_by_plot = static_cast<std::uint8_t>(by_plot);
  }
  return true;
}

/// Feed values live on the RESOURCE (a kilogram of oat is one fodder unit),
/// so they are read off the resource roster and kept dense by ResourceId.
bool ParseFeedValues(const ITable& table, std::vector<float>& values, std::string& error) {
  const std::uint32_t column = table.FindColumn("feed_value");
  values.assign(table.RowCount(), 0.0F);
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    if (!CellOrDefault(table, row, column, 0, 0, 10, values[row], error)) {
      error = "resources: feed_value: " + error;
      return false;
    }
  }
  return true;
}

/// The feeding order. ROW ORDER IS THE PRIORITY (design db feed_link.sort):
/// staple before reserve, own feed before bought concentrate, fodder grain
/// before bread grain. Nothing is sorted here — sorting would duplicate the
/// canon in code, which is exactly what the sort column exists to prevent.
bool ParseFeedLinks(const ITable& table,
                    const ITable* livestock,
                    const ITable* resources,
                    std::vector<FeedLinkDef>& links,
                    std::string& error) {
  const std::uint32_t kind_col = table.FindColumn("livestock");
  const std::uint32_t resource_col = table.FindColumn("resource");
  const std::uint32_t reserve_col = table.FindColumn("reserve");
  const std::uint32_t share_col = table.FindColumn("max_share");
  const std::uint32_t work_only_col = table.FindColumn("work_only");
  if (kind_col == kNoTableColumn || resource_col == kNoTableColumn) {
    error = "feed_links: no livestock or resource column";
    return false;
  }
  links.clear();
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    const std::uint32_t kind_row =
        livestock == nullptr ? kNoTableRow : livestock->FindRowByKey(table.CellText(row, kind_col));
    const std::uint32_t resource_row =
        resources == nullptr ? kNoTableRow
                             : resources->FindRowByKey(table.CellText(row, resource_col));
    if (kind_row == kNoTableRow || resource_row == kNoTableRow) {
      continue;  // a link to a kind or resource this table set does not have
    }
    float reserve = 0.0F;
    float max_share = 1.0F;
    float work_only = 0.0F;
    if (!CellOrDefault(table, row, reserve_col, 0, 0, 1, reserve, error) ||
        !CellOrDefault(table, row, share_col, 1, 0, 1, max_share, error) ||
        !CellOrDefault(table, row, work_only_col, 0, 0, 1, work_only, error)) {
      error = "feed_links: " + error;
      return false;
    }
    links.push_back(FeedLinkDef{.kind = LivestockKindId{static_cast<std::uint16_t>(kind_row)},
                                .resource = ResourceId{static_cast<std::uint16_t>(resource_row)},
                                .reserve = static_cast<std::uint8_t>(reserve),
                                .max_share = max_share,
                                .work_only = static_cast<std::uint8_t>(work_only)});
  }
  return true;
}

/// @brief LivestockKindId by key; invalid when the roster has no such kind.
LivestockKindId KindByKey(const ITable* livestock, std::string_view key) {
  if (livestock == nullptr) {
    return LivestockKindId{};
  }
  const std::uint32_t row = livestock->FindRowByKey(key);
  return row == kNoTableRow ? LivestockKindId{} : LivestockKindId{static_cast<std::uint16_t>(row)};
}

/// @brief Says out loud which optional columns a table set does not carry.
///
/// An absent optional column takes its default, which is the right behaviour
/// and a quiet one: the run stays green and the world is simply a different
/// world. That is how max_share and straw_ratio sat inert for a while —
/// present in the design db, absent from the export, and nothing said so.
/// A table set that predates a column is legitimate; a table set that lost
/// one is a bug, and only a line in the log tells them apart.
void ReportMissingColumns(const ITable& table,
                          std::string_view name,
                          std::span<const std::string_view> columns) {
  std::string missing;
  for (const std::string_view column : columns) {
    if (table.FindColumn(column) == kNoTableColumn) {
      missing += missing.empty() ? "" : ", ";
      missing += std::string(column);
    }
  }
  if (!missing.empty()) {
    LogWarning(std::string(name) + ": optional columns absent, defaults used: " + missing);
  }
}

/// @brief UnitTypeId by key; invalid when the roster has no such type.
UnitTypeId UnitTypeByKey(const ITable* unit_types, std::string_view key) {
  if (unit_types == nullptr) {
    return UnitTypeId{};
  }
  const std::uint32_t row = unit_types->FindRowByKey(key);
  return row == kNoTableRow ? UnitTypeId{} : UnitTypeId{static_cast<std::uint16_t>(row)};
}

}  // namespace

bool ParseProductionConfig(const ITableSet& tables, ProductionConfig& config, std::string& error) {
  const ITable* resources = tables.FindTable("resources");
  const ITable* livestock = tables.FindTable("livestock");
  const ITable* unit_types = tables.FindTable("unit_types");
  if (const ITable* crops = tables.FindTable("crops")) {
    if (!ParseCrops(*crops, resources, config.crops, error)) {
      return false;
    }
    constexpr std::array<std::string_view, 2> kOptional = {"straw_ratio", "sow_days_per_ha"};
    ReportMissingColumns(*crops, "crops", kOptional);
  }
  if (livestock != nullptr && !ParseLivestock(*livestock, config.livestock, error)) {
    return false;
  }
  if (unit_types != nullptr && !ParseUnitTypes(*unit_types, config.unit_types, error)) {
    return false;
  }
  if (resources != nullptr && !ParseFeedValues(*resources, config.feed_values, error)) {
    return false;
  }
  if (const ITable* feed_links = tables.FindTable("feed_links")) {
    if (!ParseFeedLinks(*feed_links, livestock, resources, config.feed_links, error)) {
      return false;
    }
    constexpr std::array<std::string_view, 1> kOptional = {"max_share"};
    ReportMissingColumns(*feed_links, "feed_links", kOptional);
  }
  if (const ITable* farming = tables.FindTable("farming")) {
    if (!ParseFarming(*farming, config.farming, error) ||
        !ParseHerdKnobs(*farming, config.farming, error)) {
      return false;
    }
  }
  if (const ITable* field_phases = tables.FindTable("field_phases")) {
    if (!ParseFieldPhases(*field_phases, config.farming, error)) {
      return false;
    }
  }
  if (const ITable* campaign = tables.FindTable("campaign")) {
    float percent = 0.0F;
    if (!CellOrDefault(*campaign,
                       campaign->FindRowByKey("plan_grain_share_percent"),
                       campaign->FindColumn("value"),
                       0.0F,
                       0.0F,
                       100.0F,
                       percent,
                       error)) {
      error = "campaign: plan_grain_share_percent: " + error;
      return false;
    }
    config.plan_grain_share = percent / 100.0F;
  }
  constexpr std::array<std::string_view, 6> kGrainKeys = {
      "rye", "wheat", "oat", "barley", "buckwheat", "pea"};
  config.plan_grain_resources.clear();
  for (const std::string_view key : kGrainKeys) {
    const ResourceId grain = ResourceByKey(resources, key);
    if (grain.value != kInvalidDefIdValue) {
      config.plan_grain_resources.push_back(grain);
    }
  }
  config.manure_resource = ResourceByKey(resources, "manure");
  config.hay_resource = ResourceByKey(resources, "hay");
  config.straw_resource = ResourceByKey(resources, "straw");
  config.milk_resource = ResourceByKey(resources, "milk");
  config.egg_resource = ResourceByKey(resources, "egg");
  config.wool_resource = ResourceByKey(resources, "wool");
  config.meat_resource = ResourceByKey(resources, "meat");
  config.hide_resource = ResourceByKey(resources, "hide");
  config.pelt_resource = ResourceByKey(resources, "mink_pelt");
  config.down_resource = ResourceByKey(resources, "down");
  config.compost_heap_type = UnitTypeByKey(unit_types, "manure_pile");
  // The stable is not a unit of its own: it is the SECOND level of the
  // kolkhoz yard (boss 2026-08-30), and phase 1 keeps every unit at its
  // genesis level. So the id stays invalid and horse breeding stays blocked
  // — the start canon's own rule, arrived at by the canon's own route.
  // The stable is the SECOND step of the kolkhoz yard, and foals come only
  // under its roof (livestock design §5). The type is resolved here; whether
  // one is BUILT is a question about the world, asked once a day in
  // herd_system.cpp. Phase 1 has no construction, so core_world raises it as
  // a stub at the turn of the first year — see world.cpp, RaiseKolkhozYard.
  config.stable_type = UnitTypeByKey(unit_types, "horse_yard");
  config.horse_kind = KindByKey(livestock, "horse");
  config.pig_kind = KindByKey(livestock, "pig");
  return true;
}

}  // namespace core
