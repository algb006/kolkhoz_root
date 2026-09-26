#include "core_catalog/road_cost_catalog.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "core_catalog/table_value.h"
#include "core_tables/tables.h"

namespace core {
namespace {

constexpr std::string_view kRoadUnit = "road";

/// The surface a `road` level is: 1 dirt, 2 gravel, 3 asphalt; nothing else.
std::optional<RoadSurface> SurfaceOfLevel(std::int64_t level) {
  switch (level) {
    case 1:
      return RoadSurface::kDirt;
    case 2:
      return RoadSurface::kGravel;
    case 3:
      return RoadSurface::kAsphalt;
    default:
      return std::nullopt;
  }
}

/// The design's epochs with no price: what a set without unit_levels gets.
RoadSurfaceLevels DesignEpochs() {
  RoadSurfaceLevels levels{};
  levels[static_cast<std::size_t>(RoadSurface::kAsphalt)].opens = Epoch::kTwo;
  levels[static_cast<std::size_t>(RoadSurface::kAsphaltWalks)].opens = Epoch::kTwo;
  return levels;
}

constexpr std::array<std::string_view, 1> kRoadToolWorldParamKeys = {"clearing_trudodni_per_ha"};

}  // namespace

std::span<const std::string_view> RoadToolWorldParamKeys() {
  return kRoadToolWorldParamKeys;
}

bool ReadClearingLabour(const ITableSet& tables, float& per_ha, std::string& error) {
  const ITable* const world = tables.FindTable("world_params");
  if (world == nullptr) {
    return true;
  }
  const std::array<ScalarKnob, 1> knobs = {{
      {.key = kRoadToolWorldParamKeys[0],
       .value = &per_ha,
       .range = Range{.low = 0.0F, .high = 2000.0F}},
  }};
  return ReadKnobs(*world, "world_params", knobs, error);
}

bool ReadRoadSurfaceLevels(const ITableSet& tables, RoadSurfaceLevels& levels, std::string& error) {
  RoadSurfaceLevels read = DesignEpochs();
  const ITable* unit_levels = tables.FindTable("unit_levels");
  if (unit_levels == nullptr) {
    levels = read;
    return true;
  }
  const std::uint32_t unit_column = unit_levels->FindColumn("unit");
  const std::uint32_t level_column = unit_levels->FindColumn("level");
  const std::uint32_t era_column = unit_levels->FindColumn("era");
  const std::uint32_t labor_column = unit_levels->FindColumn("labor_days");
  if (unit_column == kNoTableColumn || level_column == kNoTableColumn ||
      era_column == kNoTableColumn || labor_column == kNoTableColumn) {
    error = "unit_levels: a column of unit, level, era, labor_days is missing";
    return false;
  }
  for (std::uint32_t row = 0; row < unit_levels->RowCount(); ++row) {
    if (unit_levels->CellText(row, unit_column) != kRoadUnit) {
      continue;
    }
    const std::optional<std::int64_t> level = unit_levels->CellInteger(row, level_column);
    const std::optional<std::int64_t> era = unit_levels->CellInteger(row, era_column);
    const std::optional<float> labor = unit_levels->CellReal(row, labor_column);
    const std::optional<RoadSurface> surface = level ? SurfaceOfLevel(*level) : std::nullopt;
    if (!surface || !era || *era < 1 || *era > 3 || !labor || *labor < 0.0F) {
      error = "unit_levels: road row " + std::to_string(row) +
              ": level, era or labor_days does not parse or is out of range";
      return false;
    }
    RoadSurfaceLevel& slot = read[static_cast<std::size_t>(*surface)];
    slot.opens = static_cast<Epoch>(*era);
    slot.man_days_per_100m = *labor;
  }
  const ITable* costs = tables.FindTable("unit_level_cost");
  const ITable* resources = tables.FindTable("resources");
  if (costs != nullptr && resources != nullptr) {
    const std::uint32_t unit_column_c = costs->FindColumn("unit");
    const std::uint32_t level_column_c = costs->FindColumn("level");
    const std::uint32_t resource_column = costs->FindColumn("resource");
    const std::uint32_t amount_column = costs->FindColumn("amount");
    const std::uint32_t mass_column = resources->FindColumn("kg_per_unit");
    if (unit_column_c == kNoTableColumn || level_column_c == kNoTableColumn ||
        resource_column == kNoTableColumn || amount_column == kNoTableColumn ||
        mass_column == kNoTableColumn) {
      error =
          "unit_level_cost / resources: a column of unit, level, resource, amount, "
          "kg_per_unit is missing";
      return false;
    }
    for (std::uint32_t row = 0; row < costs->RowCount(); ++row) {
      if (costs->CellText(row, unit_column_c) != kRoadUnit) {
        continue;
      }
      const std::optional<std::int64_t> level = costs->CellInteger(row, level_column_c);
      const std::optional<float> amount = costs->CellReal(row, amount_column);
      const std::optional<RoadSurface> surface = level ? SurfaceOfLevel(*level) : std::nullopt;
      const std::uint32_t resource_row =
          resources->FindRowByKey(costs->CellText(row, resource_column));
      const std::string where = "unit_level_cost: road row " + std::to_string(row);
      if (!surface || !amount || *amount < 0.0F) {
        error = where + ": level or amount does not parse";
        return false;
      }
      if (resource_row == kNoTableRow) {
        error = where + ": names an unknown resource";
        return false;
      }
      const std::optional<float> kg = resources->CellReal(resource_row, mass_column);
      if (!kg || *kg <= 0.0F) {
        // A material with no mass cannot be spent (construction's rule too).
        error = where + ": its resource has no kg_per_unit";
        return false;
      }
      ResourceAmounts& materials = read[static_cast<std::size_t>(*surface)].materials_per_100m;
      if (materials.size() < resources->RowCount()) {
        materials.resize(resources->RowCount(), 0);
      }
      materials[resource_row] +=
          static_cast<Grams>(static_cast<double>(*amount) * static_cast<double>(*kg) *
                             static_cast<double>(kGramsPerKilogram));
    }
  }
  // Asphalt with walks has no level of its own: it costs as asphalt, STUB.
  read[static_cast<std::size_t>(RoadSurface::kAsphaltWalks)] =
      read[static_cast<std::size_t>(RoadSurface::kAsphalt)];
  levels = std::move(read);
  return true;
}

}  // namespace core
