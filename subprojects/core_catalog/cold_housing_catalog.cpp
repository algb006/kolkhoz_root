// The tables' data of the cold-housing rule (core_catalog/cold_housing_catalog.h).

#include "core_catalog/cold_housing_catalog.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/ids.h"

namespace core {
namespace {

long CellNumber(const ITable& table, std::uint32_t row, std::uint32_t column) {
  if (column == kNoTableColumn) {
    return 0;
  }
  const std::string text(table.CellText(row, column));
  return text.empty() ? 0 : std::strtol(text.c_str(), nullptr, 10);
}

}  // namespace

bool ReadColdHousing(const ITableSet& tables, ColdHousing& cold, std::string& error) {
  cold = ColdHousing{};
  const ITable* const types = tables.FindTable("unit_types");
  if (const ITable* const livestock = tables.FindTable("livestock")) {
    const std::uint32_t home_col = livestock->FindColumn("home_unit");
    const std::uint32_t cold_col = livestock->FindColumn("cold_night_cold_place_c");
    for (std::uint32_t row = 0; row < livestock->RowCount(); ++row) {
      UnitTypeId home;
      if (home_col != kNoTableColumn && types != nullptr) {
        const std::string_view key = livestock->CellText(row, home_col);
        if (!key.empty()) {
          const std::uint32_t type_row = types->FindRowByKey(key);
          if (type_row == kNoTableRow) {
            error = "livestock: row " + std::to_string(row) + " home_unit '" + std::string(key) +
                    "' names no unit type";
            return false;
          }
          home = UnitTypeId{static_cast<std::uint16_t>(type_row)};
        }
      }
      cold.home_by_kind.push_back(home);
      const bool freezes =
          cold_col != kNoTableColumn && !livestock->CellText(row, cold_col).empty();
      cold.freezes_by_kind.push_back(freezes ? 1U : 0U);
    }
  }
  const ITable* const levels = tables.FindTable("unit_levels");
  if (levels == nullptr || types == nullptr) {
    return true;
  }
  const std::uint32_t unit_col = levels->FindColumn("unit");
  const std::uint32_t level_col = levels->FindColumn("level");
  const std::uint32_t warm_col = levels->FindColumn("warm_place");
  if (unit_col == kNoTableColumn || level_col == kNoTableColumn || warm_col == kNoTableColumn) {
    return true;
  }
  cold.warm_by_type_level.resize(types->RowCount());
  for (std::uint32_t row = 0; row < levels->RowCount(); ++row) {
    const std::uint32_t type_row = types->FindRowByKey(levels->CellText(row, unit_col));
    const long level = CellNumber(*levels, row, level_col);
    if (type_row == kNoTableRow || level < 0 || level > 255) {
      continue;
    }
    std::vector<std::uint8_t>& rungs = cold.warm_by_type_level[type_row];
    if (rungs.size() <= static_cast<std::size_t>(level)) {
      rungs.resize(static_cast<std::size_t>(level) + 1U, 0U);
    }
    rungs[static_cast<std::size_t>(level)] = CellNumber(*levels, row, warm_col) != 0 ? 1U : 0U;
  }
  return true;
}

}  // namespace core
