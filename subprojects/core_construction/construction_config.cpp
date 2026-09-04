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

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
#include "core_construction/construction_system.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// Bounds every number read here. Generous on purpose: the point is to
/// catch a moved column or a garbled cell, not to police the balance.
constexpr float kMaxLaborDays = 100'000.0F;
constexpr float kMaxAmount = 10'000'000.0F;
constexpr float kMaxKgPerUnit = 100'000.0F;
/// A store bigger than this is a table error, not a plan: the largest thing
/// the design names is a 2500 t elevator.
constexpr float kMaxStorageTonnes = 1e6F;

/// An amortization term longer than this is a table error rather than a
/// plan: nothing in the design outlives a campaign by four orders.
constexpr float kMaxWearYears = 10'000.0F;

/// A pace multiplier outside this is a table error: the design's fastest
/// named exception is 1.6.
constexpr float kMaxWearFactor = 100.0F;

/// Grams in a tonne — the tables state stores in tonnes, the core counts
/// grams (state model §5).
constexpr Grams kGramsPerTonne = 1'000'000;

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
/// @param error Now carried, because the shared reader has a reason to
///        give and the old private one did not: a bare false told the
///        loader that something in resources.csv was wrong and nothing more.
bool ReadResourceMass(const ITable& resources,
                      std::vector<Grams>& grams_per_unit,
                      std::string& error) {
  const std::uint32_t column = resources.FindColumn("kg_per_unit");
  grams_per_unit.assign(resources.RowCount(), 0);
  if (column == kNoTableColumn) {
    return true;  // an older export: every recipe naming a resource refuses
  }
  for (std::uint32_t row = 0; row < resources.RowCount(); ++row) {
    float kg = 0.0F;
    if (!CellOrDefault(
            resources, row, column, Range{.low = 0.0F, .high = kMaxKgPerUnit}, 0.0F, kg, error)) {
      PrefixError("resources", resources.CellText(row, 0), error);
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
  const std::uint32_t tonnes_col = unit_types.FindColumn("storage_capacity_t");
  const std::uint32_t by_plot_col = unit_types.FindColumn("capacity_by_plot");
  const std::uint32_t has_wear_col = unit_types.FindColumn("has_wear");
  const std::uint32_t wear_factor_col = unit_types.FindColumn("wear_factor");

  config.wear_column_present = has_wear_col != kNoTableColumn;
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
    if (!CellOrDefault(
            unit_types, row, built_col, Range{.low = 0.0F, .high = 1.0F}, 0.0F, number, error)) {
      Fail(error, "unit_types", "player_built is not 0 or 1 in row " + std::to_string(row));
      return false;
    }
    type.player_built = static_cast<std::uint8_t>(number);
    if (!CellOrDefault(unit_types,
                       row,
                       era_col,
                       Range{.low = 1.0F, .high = static_cast<float>(kMaxEra)},
                       1.0F,
                       number,
                       error)) {
      Fail(error, "unit_types", "era is out of range in row " + std::to_string(row));
      return false;
    }
    type.era = static_cast<std::uint8_t>(number);
    if (!CellOrDefault(unit_types,
                       row,
                       tonnes_col,
                       Range{.low = 0.0F, .high = kMaxStorageTonnes},
                       0.0F,
                       number,
                       error)) {
      Fail(error, "unit_types", "storage_capacity_t is out of range in row " + std::to_string(row));
      return false;
    }
    type.storage_capacity_grams = static_cast<Grams>(number) * kGramsPerTonne;
    if (!CellOrDefault(
            unit_types, row, by_plot_col, Range{.low = 0.0F, .high = 1.0F}, 0.0F, number, error)) {
      Fail(error, "unit_types", "capacity_by_plot is not 0 or 1 in row " + std::to_string(row));
      return false;
    }
    type.capacity_by_plot = static_cast<std::uint8_t>(number);
    // Absent COLUMN = 0 for every type, and that means NOTHING WEARS. The
    // honest reading of "no data" (task A5, manual/73-wear-and-repair.md
    // §2): deriving it from the capacity flag or the recipe would be a
    // guess wearing the clothes of a rule.
    //
    // AN EMPTY CELL IN A PRESENT COLUMN IS A DIFFERENT ANSWER, and until
    // task A6 the config could not tell the two apart: both arrived as
    // has_wear = 0, so a hole in the export read as "this one does not wear
    // out" and a whole class of units would have quietly stopped ageing.
    // The column-level flag closed it at the level of the column; this
    // closes it at the level of the cell, which is where it was open.
    //
    // The answer is a REFUSAL and not a default. A column that is there is
    // a column the export means to fill: a blank in it is a hole in the
    // data, not a value, and the one thing that must not happen is for it
    // to read as the answer that costs nothing to notice.
    switch (
        ReadCell(unit_types, row, has_wear_col, Range{.low = 0.0F, .high = 1.0F}, number, error)) {
      case CellState::kRead:
        type.has_wear = static_cast<std::uint8_t>(number);
        break;
      case CellState::kAbsent:
        if (has_wear_col != kNoTableColumn) {
          Fail(error,
               "unit_types",
               "has_wear is empty in row " + std::to_string(row) +
                   " — a present column must answer for every type");
          return false;
        }
        type.has_wear = 0;
        break;
      case CellState::kBad:
        Fail(error, "unit_types", "has_wear is not 0 or 1 in row " + std::to_string(row));
        return false;
    }
    // An empty cell is 1.0 — the class's own pace — because the column names
    // only the exceptions the design lists by name. The floor is ZERO, not
    // one: a type that outlasts its class, a stone shed among timber ones,
    // is as legitimate as one that burns through it, and a floor of 1.0
    // refused the first kind outright. A cell of exactly 0 would mean "never
    // wears", which is what has_wear says, so it falls back to the class.
    if (!CellOrDefault(unit_types,
                       row,
                       wear_factor_col,
                       Range{.low = 0.0F, .high = kMaxWearFactor},
                       1.0F,
                       type.wear_factor,
                       error)) {
      Fail(error, "unit_types", "wear_factor is out of range in row " + std::to_string(row));
      return false;
    }
    if (!(type.wear_factor > 0.0F)) {
      type.wear_factor = 1.0F;
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
  const std::uint32_t tonnes_col = levels.FindColumn("storage_capacity_t");
  const std::uint32_t idle_col = levels.FindColumn("wear_years_idle");
  const std::uint32_t in_use_col = levels.FindColumn("wear_years_in_use");
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
    if (!CellOrDefault(levels,
                       row,
                       level_col,
                       Range{.low = 1.0F, .high = static_cast<float>(kMaxLevel)},
                       0.0F,
                       number,
                       error) ||
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
    if (!CellOrDefault(levels,
                       row,
                       days_col,
                       Range{.low = 0.0F, .high = kMaxLaborDays},
                       0.0F,
                       number,
                       error)) {
      Fail(error, "unit_levels", "labor_days is out of range in row " + std::to_string(row));
      return false;
    }
    step.labor_days = number / kRealDaysPerGameDay;
    if (!CellOrDefault(levels,
                       row,
                       crew_col,
                       Range{.low = 0.0F, .high = static_cast<float>(kMaxCrewCeiling)},
                       0.0F,
                       number,
                       error)) {
      Fail(error, "unit_levels", "max_crew is out of range in row " + std::to_string(row));
      return false;
    }
    step.max_crew = static_cast<std::uint8_t>(number);
    if (!CellOrDefault(levels,
                       row,
                       era_col,
                       Range{.low = 1.0F, .high = static_cast<float>(kMaxEra)},
                       1.0F,
                       number,
                       error)) {
      Fail(error, "unit_levels", "era is out of range in row " + std::to_string(row));
      return false;
    }
    step.era = static_cast<std::uint8_t>(number);
    if (!CellOrDefault(levels,
                       row,
                       tonnes_col,
                       Range{.low = 0.0F, .high = kMaxStorageTonnes},
                       0.0F,
                       number,
                       error)) {
      Fail(
          error, "unit_levels", "storage_capacity_t is out of range in row " + std::to_string(row));
      return false;
    }
    step.storage_capacity_grams = static_cast<Grams>(number) * kGramsPerTonne;
    if (!CellOrDefault(levels,
                       row,
                       idle_col,
                       Range{.low = 0.0F, .high = kMaxWearYears},
                       0.0F,
                       step.wear_years_idle,
                       error) ||
        !CellOrDefault(levels,
                       row,
                       in_use_col,
                       Range{.low = 0.0F, .high = kMaxWearYears},
                       0.0F,
                       step.wear_years_in_use,
                       error)) {
      Fail(error, "unit_levels", "a wear term is out of range in row " + std::to_string(row));
      return false;
    }
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
    if (!CellOrDefault(costs,
                       row,
                       level_col,
                       Range{.low = 1.0F, .high = static_cast<float>(kMaxLevel)},
                       0.0F,
                       number,
                       error) ||
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
    if (!CellOrDefault(
            costs, row, amount_col, Range{.low = 0.0F, .high = kMaxAmount}, 0.0F, number, error)) {
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
    if (config.definitions.units.plot_radius_m[row] > 0.0F) {
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
    const std::uint32_t column = knobs->FindColumn("value");

    // key, ceiling, destination — the knobs of this subsystem, each keeping
    // its canonical default when the table does not name it (task A5).
    struct Knob {
      const char* key;
      float ceiling;
      float* value;
    };

    const Knob knob_list[] = {
        {"demolition_labor_share", 1.0F, &config.demolition_labor_share},
        {"repair_labor_share", 1.0F, &config.repair_labor_share},
        {"repair_spare_parts_per_labor_day", 1e3F, &config.repair_spare_parts_per_labor_day},
        {"old_house_collapse_years", 1e4F, &config.old_house_collapse_years},
    };
    for (const Knob& knob : knob_list) {
      const std::uint32_t row = knobs->FindRowByKey(knob.key);
      if (row != kNoTableRow && !CellOrDefault(*knobs,
                                               row,
                                               column,
                                               Range{.low = 0.0F, .high = knob.ceiling},
                                               *knob.value,
                                               *knob.value,
                                               error)) {
        Fail(error, "construction", std::string(knob.key) + " is out of range");
        return false;
      }
    }
  }

  // The catalogue first: the plot radii and the map side belong to it, and
  // this module reads them from there rather than from its own pass over
  // unit_types.csv — those two columns have a second reader, and a column
  // with two readers has an owner (core_catalog/definitions.h).
  if (!LoadDefinitions(tables, config.definitions, error)) {
    return false;
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
  if (!ReadResourceMass(*resources, grams_per_unit, error)) {
    Fail(error, "resources", "kg_per_unit is out of range");
    return false;
  }

  // What a repair is made of, and the one type that collapses instead of
  // standing as a ruin. Both are keys, resolved here so that no rule of the
  // subsystem has to know a string (task A5).
  config.spare_part_resource = ResourceId{};
  config.spare_part_grams = 0;
  const std::uint32_t spare_row = resources->FindRowByKey("spare_part");
  if (spare_row != kNoTableRow && spare_row < grams_per_unit.size() &&
      grams_per_unit[spare_row] > 0) {
    config.spare_part_resource = ResourceId{static_cast<std::uint16_t>(spare_row)};
    config.spare_part_grams = grams_per_unit[spare_row];
  }
  config.old_house_type = UnitTypeId{};
  const std::uint32_t old_house_row = unit_types->FindRowByKey("old_house");
  if (old_house_row != kNoTableRow) {
    config.old_house_type = UnitTypeId{static_cast<std::uint16_t>(old_house_row)};
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
