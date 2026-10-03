// The catalogue of definitions.h.

#include "core_catalog/definitions.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/table_value.h"
#include "core_tables/required_tables.h"

namespace core {
namespace {

/// unit_types.csv `class` for a dwelling. The word is the table's, not the
/// code's: a class the tables rename is a table edit, not a code edit.
constexpr std::string_view kHousingClass = "housing";

/// The widest plot the arithmetic will accept. A radius past this is a
/// typo, not a farmyard — and it would refuse every other plot on the map.
constexpr float kMaxPlotRadiusM = 1000.0F;

/// The largest square map. Not a limit on the design, a limit on the digit
/// count: ten thousand kilometres a side is four orders past the twelve the
/// game uses.
constexpr float kMaxMapSideM = 1.0e7F;

bool ReadUnitTypes(const ITable& unit_types, UnitTypeDefs& defs, std::string& error) {
  const std::uint32_t radius_column = unit_types.FindColumn("plot_radius_m");
  const std::uint32_t body_column = unit_types.FindColumn("footprint_r_m");
  const std::uint32_t class_column = unit_types.FindColumn("class");
  defs.plot_radius_m.assign(unit_types.RowCount(), 0.0F);
  defs.keep_out_radius_m.assign(unit_types.RowCount(), 0.0F);
  defs.is_housing.assign(unit_types.RowCount(), 0);
  defs.parent.assign(unit_types.RowCount(), UnitTypeId{});
  const std::uint32_t parent_column = unit_types.FindColumn("parent");
  for (std::uint32_t row = 0; row < unit_types.RowCount(); ++row) {
    // A parent the roster does not carry is a table error, not a free-standing
    // unit: reading it as "no parent" would let a module stand anywhere.
    if (parent_column != kNoTableColumn) {
      const std::string_view parent_key = unit_types.CellText(row, parent_column);
      if (!parent_key.empty()) {
        const std::uint32_t parent_row = unit_types.FindRowByKey(parent_key);
        if (parent_row == kNoTableRow || parent_row == row) {
          error = "unit_types: row " + std::to_string(row) + ": parent '" +
                  std::string(parent_key) + "' is not another unit type";
          return false;
        }
        defs.parent[row] = UnitTypeId{static_cast<std::uint16_t>(parent_row)};
      }
    }
    if (!CellOrDefault(unit_types,
                       row,
                       radius_column,
                       Range{.low = 0.0F, .high = kMaxPlotRadiusM},
                       0.0F,
                       defs.plot_radius_m[row],
                       error)) {
      PrefixError("unit_types", "plot_radius_m", error);
      return false;
    }
    // The plot where there is one, the body where there is not. A type with
    // both would be a question this code cannot answer, and the tables never
    // pose it: the plot is the larger and the body would vanish inside it
    // anyway, so the plot wins and nothing is lost.
    float body = 0.0F;
    if (!CellOrDefault(unit_types,
                       row,
                       body_column,
                       Range{.low = 0.0F, .high = kMaxPlotRadiusM},
                       0.0F,
                       body,
                       error)) {
      PrefixError("unit_types", "footprint_r_m", error);
      return false;
    }
    defs.keep_out_radius_m[row] = defs.plot_radius_m[row] > 0.0F ? defs.plot_radius_m[row] : body;
    if (class_column != kNoTableColumn && unit_types.CellText(row, class_column) == kHousingClass) {
      defs.is_housing[row] = 1;
    }
  }
  return true;
}

/// The highest rung a cell may name: unit_levels' own bound.
constexpr float kMaxRung = 10.0F;

}  // namespace

// The rungs' words (boss [22]-[24]): `no_residents` — a flag, empty is 0 —
// and `requires_unit` — a unit_types key, empty is none.
bool ReadRungWords(const ITable& unit_types,
                   const ITable& unit_levels,
                   UnitTypeDefs& defs,
                   std::string& error) {
  const std::uint32_t flag_column = unit_levels.FindColumn("no_residents");
  const std::uint32_t requires_column = unit_levels.FindColumn("requires_unit");
  if (flag_column == kNoTableColumn && requires_column == kNoTableColumn) {
    return true;
  }
  const std::uint32_t unit_column = unit_levels.FindColumn("unit");
  const std::uint32_t level_column = unit_levels.FindColumn("level");
  defs.no_residents.assign(unit_types.RowCount(), {});
  defs.requires_unit.assign(unit_types.RowCount(), {});
  for (std::uint32_t row = 0; row < unit_levels.RowCount(); ++row) {
    const std::uint32_t type_row = unit_types.FindRowByKey(unit_levels.CellText(row, unit_column));
    float level = 0.0F;
    float flag = 0.0F;
    if (!CellOrDefault(unit_levels,
                       row,
                       level_column,
                       Range{.low = 1.0F, .high = kMaxRung},
                       0.0F,
                       level,
                       error) ||
        (flag_column != kNoTableColumn &&
         !CellOrDefault(unit_levels, row, flag_column, Range::Unit(), 0.0F, flag, error))) {
      PrefixError("unit_levels", "no_residents", error);
      return false;
    }
    UnitTypeId required;
    if (requires_column != kNoTableColumn) {
      const std::string_view key = unit_levels.CellText(row, requires_column);
      if (!key.empty()) {
        const std::uint32_t required_row = unit_types.FindRowByKey(key);
        if (required_row == kNoTableRow) {
          error = "unit_levels: requires_unit names no unit type: " + std::string(key);
          return false;
        }
        required = DefIdFromRow<UnitTypeIdTag>(required_row);
      }
    }
    if (type_row == kNoTableRow || type_row >= defs.no_residents.size() || !(level >= 1.0F)) {
      continue;  // the level table's own check refuses a malformed row
    }
    const auto index = static_cast<std::size_t>(level) - 1U;
    std::vector<std::uint8_t>& flags = defs.no_residents[type_row];
    std::vector<UnitTypeId>& needs = defs.requires_unit[type_row];
    if (flags.size() <= index) {
      flags.resize(index + 1U, 0);
      needs.resize(index + 1U, UnitTypeId{});
    }
    flags[index] = flag > 0.0F ? 1U : 0U;
    needs[index] = required;
  }
  return true;
}

bool LoadDefinitions(const ITableSet& tables,
                     StubTables stubs,
                     Definitions& definitions,
                     std::string& error) {
  // The catalogue's own read set. It asks here rather than in its callers,
  // because a caller can only require what it BELIEVES the catalogue reads —
  // and until 2026-09-08 not one of them named the map, whose absence left
  // map_side_m at the zero that means "this table set declares no map".
  if (!RequireTables(tables, stubs, "catalogue", {"unit_types", "map"}, &error)) {
    return false;
  }
  if (const ITable* const unit_types = tables.FindTable("unit_types")) {
    if (!ReadUnitTypes(*unit_types, definitions.units, error)) {
      return false;
    }
    // Not required: a set without the level table has no rung that houses
    // nobody, and the construction and residents modules require it anyway.
    if (const ITable* const unit_levels = tables.FindTable("unit_levels")) {
      if (!ReadRungWords(*unit_types, *unit_levels, definitions.units, error)) {
        return false;
      }
    }
  }
  if (const ITable* const map = tables.FindTable("map")) {
    // A map table with no rows and a map table with no side column mean the
    // same thing as no map table at all, and all three leave the zero.
    if (map->RowCount() > 0 && !CellOrDefault(*map,
                                              0,
                                              map->FindColumn("side_m"),
                                              Range{.low = 0.0F, .high = kMaxMapSideM},
                                              0.0F,
                                              definitions.map_side_m,
                                              error)) {
      PrefixError("map", "side_m", error);
      return false;
    }
  }
  return true;
}

}  // namespace core
