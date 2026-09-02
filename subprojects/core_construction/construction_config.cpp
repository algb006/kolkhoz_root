// Parsing of the build data (construction_config.h): five exported tables
// plus one hand-written one.
//
// Five of the six are EXPORTS of the design db and must never be edited by
// hand (manual/61-balance-tables.md §4а). That is why this file is fussy
// about column names and cross-table agreement: when a column moves
// upstream, the parse has to fail loudly, never quietly return a plausible
// default. The one hand-written table here is construction.csv, and it
// taught its own lesson — a comment with quotation marks in it took the
// whole table set down, so it now carries none.
//
// Policy, shared with every subsystem factory: a MISSING table keeps the
// documented defaults (a unit test's world has no tables at all, and then
// nothing can be built — the honest answer), while a PRESENT table that
// cannot be read, or that contradicts another, refuses the subsystem.

#include "construction_config.h"

#include <cstdint>
#include <optional>
#include <string_view>

#include "core_common/calendar.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// Bounds every number read here. Generous on purpose: the point is to
/// catch a moved column or a garbled cell, not to police the balance.
constexpr float kMaxLaborDays = 100'000.0F;
constexpr float kMaxAmount = 10'000'000.0F;
constexpr float kMaxKgPerUnit = 100'000.0F;
constexpr float kMaxRadius = 1'000.0F;
constexpr std::uint32_t kMaxEra = 3;
constexpr std::uint32_t kMaxLevel = 32;
constexpr std::uint32_t kMaxCrewCeiling = 255;

/// The build class that means "the player draws the outline": no work, no
/// materials (unit rules §9 — the second of the two legal cases for a type
/// that has a plot and no radius).
constexpr std::string_view kMarkingClass = "plot";

void Fail(std::string& error, std::string_view table, std::string_view what) {
  error = std::string(table);
  error += ": ";
  error += what;
}

/// @brief A cell as a real number, `fallback` for an empty one; a present
/// unreadable or out-of-range cell fails the parse. Written positively so a
/// NaN fails it too.
bool CellOrDefault(const ITable& table,
                   std::uint32_t row,
                   std::uint32_t column,
                   float low,
                   float high,
                   float fallback,
                   float& value) {
  if (column == kNoTableColumn || table.CellText(row, column).empty()) {
    value = fallback;
    return true;
  }
  const std::optional<float> cell = table.CellReal(row, column);
  if (!cell || !(*cell >= low && *cell <= high)) {
    return false;
  }
  value = *cell;
  return true;
}

UnitGate GateFromText(std::string_view text, bool& known) {
  known = true;
  if (text.empty() || text == "era") {
    return UnitGate::kEra;
  }
  if (text == "start") {
    return UnitGate::kStart;
  }
  if (text == "event") {
    return UnitGate::kEvent;
  }
  if (text == "quest") {
    return UnitGate::kQuest;
  }
  if (text == "unit") {
    return UnitGate::kUnit;
  }
  known = false;
  return UnitGate::kEra;
}

/// @brief Grams per one unit of a resource, from resources.csv. Zero means
/// "the tables carry no mass for it", which is a refusal wherever a recipe
/// names it: a material with no mass cannot be moved, stored or spent.
bool ReadResourceMass(const ITable& resources, std::vector<Grams>& grams_per_unit) {
  const std::uint32_t column = resources.FindColumn("kg_per_unit");
  grams_per_unit.assign(resources.RowCount(), 0);
  if (column == kNoTableColumn) {
    return true;  // an older export: every recipe naming a resource refuses
  }
  for (std::uint32_t row = 0; row < resources.RowCount(); ++row) {
    float kg = 0.0F;
    if (!CellOrDefault(resources, row, column, 0.0F, kMaxKgPerUnit, 0.0F, kg)) {
      return false;
    }
    grams_per_unit[row] = static_cast<Grams>(static_cast<double>(kg) * kGramsPerKilogram);
  }
  return true;
}

bool ReadTypes(const ITable& unit_types, ConstructionConfig& config, std::string& error) {
  const std::uint32_t gate_col = unit_types.FindColumn("gate");
  const std::uint32_t built_col = unit_types.FindColumn("player_built");
  const std::uint32_t era_col = unit_types.FindColumn("era");
  const std::uint32_t radius_col = unit_types.FindColumn("plot_radius_m");

  config.types.assign(unit_types.RowCount(), BuildType{});
  for (std::uint32_t row = 0; row < unit_types.RowCount(); ++row) {
    BuildType& type = config.types[row];
    bool known = true;
    type.gate = gate_col == kNoTableColumn
                    ? UnitGate::kEra
                    : GateFromText(unit_types.CellText(row, gate_col), known);
    if (!known) {
      Fail(error, "unit_types", "unknown gate kind in row " + std::to_string(row));
      return false;
    }
    float number = 0.0F;
    if (!CellOrDefault(unit_types, row, built_col, 0.0F, 1.0F, 0.0F, number)) {
      Fail(error, "unit_types", "player_built is not 0 or 1 in row " + std::to_string(row));
      return false;
    }
    type.player_built = static_cast<std::uint8_t>(number);
    if (!CellOrDefault(unit_types, row, era_col, 1.0F, static_cast<float>(kMaxEra), 1.0F, number)) {
      Fail(error, "unit_types", "era is out of range in row " + std::to_string(row));
      return false;
    }
    type.era = static_cast<std::uint8_t>(number);
    if (!CellOrDefault(unit_types, row, radius_col, 0.0F, kMaxRadius, 0.0F, type.plot_radius_m)) {
      Fail(error, "unit_types", "plot_radius_m is out of range in row " + std::to_string(row));
      return false;
    }
  }
  return true;
}

bool ReadLevels(const ITable& levels,
                const ITable& unit_types,
                ConstructionConfig& config,
                std::string& error) {
  const std::uint32_t unit_col = levels.FindColumn("unit");
  const std::uint32_t level_col = levels.FindColumn("level");
  const std::uint32_t era_col = levels.FindColumn("era");
  const std::uint32_t days_col = levels.FindColumn("labor_days");
  const std::uint32_t class_col = levels.FindColumn("build_class");
  const std::uint32_t crew_col = levels.FindColumn("max_crew");
  if (unit_col == kNoTableColumn || level_col == kNoTableColumn) {
    Fail(error, "unit_levels", "no 'unit' or 'level' column");
    return false;
  }

  for (std::uint32_t row = 0; row < levels.RowCount(); ++row) {
    const std::uint32_t type_row = unit_types.FindRowByKey(levels.CellText(row, unit_col));
    if (type_row == kNoTableRow || type_row >= config.types.size()) {
      Fail(error, "unit_levels", "row " + std::to_string(row) + " names an unknown unit");
      return false;
    }
    float number = 0.0F;
    if (!CellOrDefault(levels, row, level_col, 1.0F, static_cast<float>(kMaxLevel), 0.0F, number) ||
        number < 1.0F) {
      Fail(error, "unit_levels", "level is out of range in row " + std::to_string(row));
      return false;
    }
    const auto level = static_cast<std::uint32_t>(number);

    std::vector<BuildLevel>& ladder = config.types[type_row].levels;
    if (ladder.size() < level) {
      ladder.resize(level);
    }
    BuildLevel& step = ladder[level - 1];

    // Real man-days in the table, game man-days in the core: every consumer
    // divides once at parse time (root rules §9, calendar.h).
    if (!CellOrDefault(levels, row, days_col, 0.0F, kMaxLaborDays, 0.0F, number)) {
      Fail(error, "unit_levels", "labor_days is out of range in row " + std::to_string(row));
      return false;
    }
    step.labor_days = number / kRealDaysPerGameDay;
    if (!CellOrDefault(
            levels, row, crew_col, 0.0F, static_cast<float>(kMaxCrewCeiling), 0.0F, number)) {
      Fail(error, "unit_levels", "max_crew is out of range in row " + std::to_string(row));
      return false;
    }
    step.max_crew = static_cast<std::uint8_t>(number);
    if (!CellOrDefault(levels, row, era_col, 1.0F, static_cast<float>(kMaxEra), 1.0F, number)) {
      Fail(error, "unit_levels", "era is out of range in row " + std::to_string(row));
      return false;
    }
    step.era = static_cast<std::uint8_t>(number);
    step.is_marking = static_cast<std::uint8_t>(
        class_col != kNoTableColumn && levels.CellText(row, class_col) == kMarkingClass ? 1 : 0);
  }
  return true;
}

bool ReadRecipes(const ITable& costs,
                 const ITable& unit_types,
                 const ITable& resources,
                 const std::vector<Grams>& grams_per_unit,
                 ConstructionConfig& config,
                 std::string& error) {
  const std::uint32_t unit_col = costs.FindColumn("unit");
  const std::uint32_t level_col = costs.FindColumn("level");
  const std::uint32_t resource_col = costs.FindColumn("resource");
  const std::uint32_t amount_col = costs.FindColumn("amount");
  if (unit_col == kNoTableColumn || level_col == kNoTableColumn || resource_col == kNoTableColumn ||
      amount_col == kNoTableColumn) {
    Fail(error, "unit_level_cost", "a required column is missing");
    return false;
  }

  for (std::uint32_t row = 0; row < costs.RowCount(); ++row) {
    const std::uint32_t type_row = unit_types.FindRowByKey(costs.CellText(row, unit_col));
    if (type_row == kNoTableRow || type_row >= config.types.size()) {
      Fail(error, "unit_level_cost", "row " + std::to_string(row) + " names an unknown unit");
      return false;
    }
    float number = 0.0F;
    if (!CellOrDefault(costs, row, level_col, 1.0F, static_cast<float>(kMaxLevel), 0.0F, number) ||
        number < 1.0F) {
      Fail(error, "unit_level_cost", "level is out of range in row " + std::to_string(row));
      return false;
    }
    const auto level = static_cast<std::uint32_t>(number);
    std::vector<BuildLevel>& ladder = config.types[type_row].levels;
    if (level > ladder.size()) {
      Fail(error, "unit_level_cost", "row " + std::to_string(row) + " costs a level with no row");
      return false;
    }

    const std::uint32_t resource_row = resources.FindRowByKey(costs.CellText(row, resource_col));
    if (resource_row == kNoTableRow || resource_row >= grams_per_unit.size()) {
      Fail(error, "unit_level_cost", "row " + std::to_string(row) + " names an unknown resource");
      return false;
    }
    if (grams_per_unit[resource_row] <= 0) {
      // Without a mass the material cannot be spent, stored or carried, and
      // a silent zero would build everything out of nothing.
      Fail(error,
           "unit_level_cost",
           "row " + std::to_string(row) + " names a resource with no kg_per_unit");
      return false;
    }
    if (!CellOrDefault(costs, row, amount_col, 0.0F, kMaxAmount, 0.0F, number)) {
      Fail(error, "unit_level_cost", "amount is out of range in row " + std::to_string(row));
      return false;
    }

    BuildMaterial material;
    material.resource = ResourceId{static_cast<std::uint16_t>(resource_row)};
    material.grams = static_cast<Grams>(static_cast<double>(number) *
                                        static_cast<double>(grams_per_unit[resource_row]));
    ladder[level - 1].recipe.push_back(material);
  }
  return true;
}

/// @brief The cross-table check unit rules §9 asks for by name: a type that
/// says it has a plot must name either a radius or the marking class, and
/// "there is no third legal case".
bool CheckPlots(const ITable& unit_types, const ConstructionConfig& config, std::string& error) {
  const std::uint32_t has_plot_col = unit_types.FindColumn("has_plot");
  if (has_plot_col == kNoTableColumn) {
    return true;
  }
  for (std::uint32_t row = 0; row < config.types.size(); ++row) {
    const BuildType& type = config.types[row];
    if (type.player_built == 0 || unit_types.CellText(row, has_plot_col) != "1") {
      continue;
    }
    if (type.plot_radius_m > 0.0F) {
      continue;
    }
    const bool marked_out = !type.levels.empty() && type.levels.front().is_marking != 0;
    if (!marked_out) {
      Fail(error,
           "unit_types",
           "row " + std::to_string(row) +
               " is built and claims a plot, but names neither a radius nor the marking class");
      return false;
    }
  }
  return true;
}

}  // namespace

bool ParseConstructionConfig(const ITableSet& tables,
                             ConstructionConfig& config,
                             std::string& error) {
  const ITable* const knobs = tables.FindTable("construction");
  if (knobs != nullptr) {
    const std::uint32_t row = knobs->FindRowByKey("demolition_labor_share");
    const std::uint32_t column = knobs->FindColumn("value");
    if (row != kNoTableRow && !CellOrDefault(*knobs,
                                             row,
                                             column,
                                             0.0F,
                                             1.0F,
                                             config.demolition_labor_share,
                                             config.demolition_labor_share)) {
      Fail(error, "construction", "demolition_labor_share is out of range");
      return false;
    }
  }

  if (const ITable* const map = tables.FindTable("map")) {
    const std::uint32_t column = map->FindColumn("side_m");
    if (map->RowCount() > 0 && column != kNoTableColumn &&
        !CellOrDefault(*map, 0, column, 0.0F, 1e7F, 0.0F, config.map_side_m)) {
      Fail(error, "map", "side_m is out of range");
      return false;
    }
  }

  const ITable* const unit_types = tables.FindTable("unit_types");
  const ITable* const resources = tables.FindTable("resources");
  if (unit_types == nullptr || resources == nullptr) {
    return true;  // a table-less world: nothing can be built, and that is all
  }
  if (!ReadTypes(*unit_types, config, error)) {
    return false;
  }
  std::vector<Grams> grams_per_unit;
  if (!ReadResourceMass(*resources, grams_per_unit)) {
    Fail(error, "resources", "kg_per_unit is out of range");
    return false;
  }

  const ITable* const levels = tables.FindTable("unit_levels");
  if (levels == nullptr) {
    return true;  // no ladder: again, nothing can be built
  }
  if (!ReadLevels(*levels, *unit_types, config, error)) {
    return false;
  }
  const ITable* const costs = tables.FindTable("unit_level_cost");
  if (costs != nullptr &&
      !ReadRecipes(*costs, *unit_types, *resources, grams_per_unit, config, error)) {
    return false;
  }
  return CheckPlots(*unit_types, config, error);
}

}  // namespace core
