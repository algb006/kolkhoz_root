/// @file
/// @brief Implementation of seed_norm_catalog.h.

#include "core_catalog/seed_norm_catalog.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/table_lookup.h"
#include "core_catalog/table_value.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// A 1-based month cell (1..12) read 0-based; blank, absent or 0 is
/// kNoSowingMonth — "not known" — as production reads its crop months
/// (production_config.cpp).
bool ReadMonth(const ITable& crops,
               std::uint32_t row,
               std::string_view column_name,
               std::uint8_t& month,
               std::string& error) {
  float cell = 0.0F;
  if (!OptionalCell(crops,
                    row,
                    crops.FindColumn(column_name),
                    Range{.low = 0.0F, .high = 12.0F},
                    cell,
                    error)) {
    PrefixError("crops", column_name, error);
    return false;
  }
  month = cell >= 1.0F ? static_cast<std::uint8_t>(cell - 1.0F) : kNoSowingMonth;
  return true;
}

}  // namespace

bool ReadSeedNorms(const ITable& crops,
                   const ITable* resources,
                   std::vector<SeedNorm>& norms,
                   std::string& error) {
  norms.assign(crops.RowCount(), SeedNorm{});
  const std::uint32_t resource_column = crops.FindColumn("resource");
  const std::uint32_t norm_column = crops.FindColumn("sowing_norm_kg_per_ha");
  const std::uint32_t winter_column = crops.FindColumn("is_winter");
  for (std::uint32_t row = 0; row < crops.RowCount(); ++row) {
    SeedNorm& norm = norms[row];
    // The latest the held seed is sown: the horizon of its rot margin
    // (0.34.42). And the reaping months, the harvest of its seed that a later
    // sowing waits for (SeedHeldByField; 0.36.34).
    if (!ReadMonth(crops, row, "sow_to_month", norm.sow_to_month, error) ||
        !ReadMonth(crops, row, "harvest_from_month", norm.harvest_from_month, error) ||
        !ReadMonth(crops, row, "harvest_to_month", norm.harvest_to_month, error)) {
      return false;
    }
    // A winter crop's seed is owed from the autumn before its slot
    // (fund_ladder.h); blank or absent is a spring crop.
    float winter = 0.0F;
    if (!OptionalCell(crops, row, winter_column, Range{.low = 0.0F, .high = 1.0F}, winter, error)) {
      PrefixError("crops", "is_winter", error);
      return false;
    }
    // Read as production reads it (production_config.cpp): any non-zero.
    norm.is_winter = winter != 0.0F;
    if (!OptionalCell(crops,
                      row,
                      norm_column,
                      Range{.low = 0.0F, .high = 1.0e5F},
                      norm.sowing_norm_kg_per_ha,
                      error)) {
      PrefixError("crops", "sowing_norm_kg_per_ha", error);
      return false;
    }
    if (resource_column == kNoTableColumn) {
      continue;  // an older crops table without the column at all
    }
    // The SAME cell core_production reads, and it refuses the same things
    // there (0.17.79): a name resources.csv does not carry is a typo, and an
    // unnamed crop has nowhere to put what it grows. A null roster still
    // passes — there is nobody to ask, which is a stub table set and legal.
    if (!RequiredResource(
            resources, crops, row, resource_column, "resource", false, norm.resource, error)) {
      PrefixError("crops", "resource", error);
      return false;
    }
  }
  return true;
}

}  // namespace core
