// Parsing of the boundary's two knobs (boundary_config.h).
//
// The reader is written out here rather than shared with core_residents'
// table_read.h: that header is private to its module, and the boundary names
// no subsystem by design (manual/70-boundary.md §9). Two knobs are cheaper to
// read than a dependency that would let a subsystem's rules leak across the
// seam.

#include "boundary_config.h"

#include <cstdint>
#include <optional>
#include <string_view>

#include "core_tables/tables.h"

namespace core {
namespace {

constexpr std::uint32_t kMonthsPerBioYear = 12;

/// @brief Reads one key's `value` cell of a key/value table.
/// @param low,high Inclusive bounds; the test is written positively so that
///        a NaN cell fails it (NaN compares false against everything).
/// @return false only when the cell is present and unreadable or out of
///         range; an absent row, column or empty cell keeps `value`.
bool OptionalValue(const ITable& table,
                   std::string_view key,
                   float low,
                   float high,
                   float& value,
                   std::string& error) {
  const std::uint32_t row = table.FindRowByKey(key);
  const std::uint32_t column = table.FindColumn("value");
  if (row == kNoTableRow || column == kNoTableColumn || table.CellText(row, column).empty()) {
    return true;
  }
  const std::optional<float> cell = table.CellReal(row, column);
  if (!cell) {
    error = "life: ";
    error += key;
    error += ": a cell is not a number";
    return false;
  }
  if (!(*cell >= low && *cell <= high)) {
    error = "life: ";
    error += key;
    error += ": a cell is out of range";
    return false;
  }
  value = *cell;
  return true;
}

}  // namespace

bool ParseBoundaryConfig(const ITableSet& tables, BoundaryConfig& config, std::string& error) {
  if (const ITable* const map = tables.FindTable("map")) {
    const std::uint32_t side_col = map->FindColumn("side_m");
    if (map->RowCount() > 0 && side_col != kNoTableColumn) {
      const std::optional<float> side = map->CellReal(0, side_col);
      if (!side || !(*side > 0.0F && *side <= 1e7F)) {
        error = "map: side_m is missing or out of range";
        return false;
      }
      config.map_side_m = *side;
    }
  }
  const ITable* const life = tables.FindTable("life");
  if (life == nullptr) {
    return true;  // no table: the defaults above are the canonical values
  }
  if (!OptionalValue(*life, "life_speedup", 0.1F, 100.0F, config.life_speedup, error)) {
    return false;
  }
  // Stated in months by the design, so read in months and converted once.
  float infant_age_months = config.infant_age_bio_years * static_cast<float>(kMonthsPerBioYear);
  if (!OptionalValue(*life, "infant_age_months", 0.0F, 240.0F, infant_age_months, error)) {
    return false;
  }
  config.infant_age_bio_years = infant_age_months / static_cast<float>(kMonthsPerBioYear);
  return true;
}

}  // namespace core
