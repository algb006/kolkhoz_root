/// @file
/// @brief The roof of the potatoes and vegetables as the tables give it: which
/// resources need one, which unit type is their long store, what it holds by
/// level - and the guard that a store the layout suggests is ever marked.
/// @threading SINGLE_THREADED
/// Test-side code, read once when the run's chairman is assembled.
///
/// WHY THIS FILE (#756; boss, 13 October 2026; host's trace econ-host-potato-
/// heap-trace-2026-10-13 seq 2). The run's chairman read «the crops the food
/// store keeps» by the NAME `food_store` in resource_stores.csv. 0.37.217 moved
/// raw potato and vegetables to the vegetable store, the name kept 0 of 12
/// crops, the harvest that needs a roof read as nought for ever - and both
/// rules that hung on it, the clamp's and the store's, never fired again: in
/// 90 of 90 village-years of the canon no clamp and no vegetable store was
/// ever staked, the church was full every 1 December and the field heap was
/// the largest place of rot. A CONDITION ON A VALUE NOBODY WRITES ALWAYS
/// PASSES, and no run said so.
///
/// THE ROOFED SET IS READ OFF THE STORES THAT KEEP THE CROP, not off a name:
/// the resources the CLAMP keeps (the field store of the first year) are the
/// ones that need a roof; their long store is the player-built, numbered
/// (not by-plot) unit type resource_stores.csv names for them - the vegetable
/// store today, the food store before 0.37.217, whatever a later export says.

#ifndef TESTS_RUN_COMMON_CROP_ROOF_H_
#define TESTS_RUN_COMMON_CROP_ROOF_H_

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

/// @brief What needs a roof and what gives it.
struct CropRoof {
  /// The long store's key in unit_types.csv; empty when the tables name none
  /// (said aloud by FixturePolicy::Declare).
  std::string store_key;

  core::UnitTypeId store;

  /// The long store's room by level, grams (index level - 1).
  std::vector<core::Grams> store_grams;

  /// The church store's room at level 1, grams.
  core::Grams church_grams = 0;

  /// 1 per ResourceId the clamp keeps.
  std::vector<std::uint8_t> resource;

  /// kg per hectare by crop row for the crops whose produce the clamp keeps;
  /// 0 for every other crop.
  std::vector<float> yield_kg_per_ha;

  /// How many crop rows are roofed - 0 is the defect this file was written
  /// for, and the policy says it aloud.
  std::uint32_t crops = 0;
};

namespace crop_roof_detail {

inline std::string_view Cell(const core::ITable& table, std::uint32_t row, std::string_view name) {
  const std::uint32_t column = table.FindColumn(name);
  return column == core::kNoTableColumn ? std::string_view{} : table.CellText(row, column);
}

inline float Number(std::string_view cell) {
  return std::strtof(std::string(cell).c_str(), nullptr);
}

}  // namespace crop_roof_detail

/// @brief Reads the roof off resource_stores.csv, unit_types.csv,
///        unit_levels.csv and crops.csv. A table set without them gives an
///        empty roof.
inline CropRoof ReadCropRoof(const core::ITableSet& tables) {
  using crop_roof_detail::Cell;
  using crop_roof_detail::Number;
  CropRoof roof;
  const core::ITable* const stores = tables.FindTable("resource_stores");
  const core::ITable* const crops = tables.FindTable("crops");
  const core::ITable* const levels = tables.FindTable("unit_levels");
  const core::ITable* const types = tables.FindTable("unit_types");
  const core::ITable* const resources = tables.FindTable("resources");
  if (stores == nullptr || crops == nullptr || levels == nullptr || types == nullptr ||
      resources == nullptr) {
    return roof;
  }
  // 1. What the clamp keeps.
  roof.resource.assign(resources->RowCount(), 0);
  const auto kept_by_clamp = [&](std::string_view resource) {
    for (std::uint32_t row = 0; row < stores->RowCount(); ++row) {
      if (Cell(*stores, row, "resource") == resource && Cell(*stores, row, "storage") == "clamp") {
        return true;
      }
    }
    return false;
  };
  for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
    roof.resource[row] = kept_by_clamp(resources->CellText(row, 0)) ? 1U : 0U;
  }
  // 2. Their long store: the first storage of theirs that is a unit type the
  // player builds and that holds a number of tonnes (not an outline).
  for (std::uint32_t row = 0; row < stores->RowCount() && roof.store_key.empty(); ++row) {
    const std::string_view storage = Cell(*stores, row, "storage");
    const std::uint32_t type = types->FindRowByKey(storage);
    if (storage == "clamp" || type == core::kNoTableRow ||
        !kept_by_clamp(Cell(*stores, row, "resource"))) {
      continue;
    }
    if (Number(Cell(*types, type, "player_built")) > 0.0F &&
        !(Number(Cell(*types, type, "capacity_by_plot")) > 0.0F)) {
      roof.store_key = std::string(storage);
      roof.store = core::UnitTypeId{static_cast<std::uint16_t>(type)};
    }
  }
  // 3. The crops under it, at the table's yield.
  roof.yield_kg_per_ha.assign(crops->RowCount(), 0.0F);
  for (std::uint32_t crop = 0; crop < crops->RowCount(); ++crop) {
    if (kept_by_clamp(Cell(*crops, crop, "resource"))) {
      roof.yield_kg_per_ha[crop] = Number(Cell(*crops, crop, "yield_kg_per_ha"));
      ++roof.crops;
    }
  }
  // 4. The room, by level.
  for (std::uint32_t row = 0; row < levels->RowCount(); ++row) {
    const std::string_view unit = Cell(*levels, row, "unit");
    const auto level = static_cast<std::size_t>(Number(Cell(*levels, row, "level")));
    const core::Grams grams =
        core::GramsFromKilograms(1000.0F * Number(Cell(*levels, row, "storage_capacity_t")));
    if (unit == "church_store" && level == 1) {
      roof.church_grams = grams;
    }
    if (!roof.store_key.empty() && unit == roof.store_key && level >= 1) {
      if (roof.store_grams.size() < level) {
        roof.store_grams.resize(level, 0);
      }
      roof.store_grams[level - 1] = grams;
    }
  }
  return roof;
}

/// @brief THE GUARD: every unit type that is a STORE by resource_stores.csv
///        (named as a `storage` or a `unit` there), has a point in
///        suggestions.csv, and has NO ROW in the world - neither standing nor
///        staked. The layout drew a store nobody ever marked.
/// @return Their keys, in the suggestions' order; empty when every suggested
///         store has a row.
inline std::vector<std::string> SuggestedStoresNeverMarked(const core::ITableSet& tables,
                                                           const core::WorldState& world) {
  using crop_roof_detail::Cell;
  std::vector<std::string> missing;
  const core::ITable* const suggestions = tables.FindTable("suggestions");
  const core::ITable* const stores = tables.FindTable("resource_stores");
  const core::ITable* const types = tables.FindTable("unit_types");
  if (suggestions == nullptr || stores == nullptr || types == nullptr) {
    return missing;
  }
  for (std::uint32_t row = 0; row < suggestions->RowCount(); ++row) {
    const std::string_view key = Cell(*suggestions, row, "unit_type");
    const std::uint32_t type = types->FindRowByKey(key);
    if (type == core::kNoTableRow) {
      continue;
    }
    bool is_store = false;
    for (std::uint32_t line = 0; line < stores->RowCount() && !is_store; ++line) {
      is_store = Cell(*stores, line, "storage") == key || Cell(*stores, line, "unit") == key;
    }
    bool has_row = false;
    for (const core::UnitRow& unit : world.units.rows) {
      has_row = has_row || (unit.type.value == type && unit.dead == 0);
    }
    bool listed = false;
    for (const std::string& name : missing) {
      listed = listed || name == key;
    }
    if (is_store && !has_row && !listed) {
      missing.emplace_back(key);
    }
  }
  return missing;
}

}  // namespace run

#endif  // TESTS_RUN_COMMON_CROP_ROOF_H_
