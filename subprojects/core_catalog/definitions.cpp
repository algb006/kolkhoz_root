// The catalogue of definitions.h.

#include "core_catalog/definitions.h"

#include <string_view>

#include "core_catalog/table_value.h"

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
  const std::uint32_t class_column = unit_types.FindColumn("class");
  defs.plot_radius_m.assign(unit_types.RowCount(), 0.0F);
  defs.is_housing.assign(unit_types.RowCount(), 0);
  for (std::uint32_t row = 0; row < unit_types.RowCount(); ++row) {
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
    if (class_column != kNoTableColumn && unit_types.CellText(row, class_column) == kHousingClass) {
      defs.is_housing[row] = 1;
    }
  }
  return true;
}

}  // namespace

bool LoadDefinitions(const ITableSet& tables, Definitions& definitions, std::string& error) {
  if (const ITable* const unit_types = tables.FindTable("unit_types")) {
    if (!ReadUnitTypes(*unit_types, definitions.units, error)) {
      return false;
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
