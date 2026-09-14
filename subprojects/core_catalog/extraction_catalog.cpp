// The extraction catalogue (extraction_catalog.h).

#include "core_catalog/extraction_catalog.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core_catalog/table_lookup.h"
#include "core_catalog/table_value.h"

namespace core {
namespace {

/// In ExtractedMaterial order: the resources.csv key of each material, and the
/// suffix its world_params knobs carry.
constexpr std::array<std::string_view, kExtractedMaterialCount> kMaterialKeys = {
    "clay", "stone", "sand"};

/// The knobs, stock densities then labour per tonne then tools, in the order
/// the parse below indexes them.
constexpr std::array<std::string_view, 7> kExtractionWorldParamKeys = {
    "extraction_stock_t_per_ha_clay",
    "extraction_stock_t_per_ha_stone",
    "extraction_stock_t_per_ha_sand",
    "extraction_days_per_t_clay",
    "extraction_days_per_t_stone",
    "extraction_days_per_t_sand",
    "extraction_tools_per_worker"};

/// The largest mass any conversion of the core accepts — the ceiling of
/// GramsFromFloat (quantities.cpp), as the timber catalogue keeps it.
constexpr double kMostGrams = 9.0e15;

bool ParseSites(const ITable& table, ExtractionCatalog& catalog, std::string& error) {
  const std::uint32_t resource_column = table.FindColumn("resource");
  const std::uint32_t x_column = table.FindColumn("x_m");
  const std::uint32_t y_column = table.FindColumn("y_m");
  const std::uint32_t area_column = table.FindColumn("area_ha");
  if (resource_column == kNoTableColumn || x_column == kNoTableColumn ||
      y_column == kNoTableColumn || area_column == kNoTableColumn) {
    error = "extraction_sites: a column of resource, x_m, y_m, area_ha is missing";
    return false;
  }
  constexpr Range kCoordinate{.low = 0.0F, .high = 100000.0F};
  constexpr Range kArea{.low = 0.0F, .high = 100000.0F};
  catalog.sites.assign(table.RowCount(), ExtractionSiteDef{});
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    ExtractionSiteDef& site = catalog.sites[row];
    const std::string_view key = table.CellText(row, resource_column);
    bool known = false;
    for (std::size_t index = 0; index < kMaterialKeys.size(); ++index) {
      if (key == kMaterialKeys[index]) {
        site.material = static_cast<ExtractedMaterial>(index);
        site.resource = catalog.resources[index];
        known = true;
      }
    }
    if (!known) {
      error = "extraction_sites: row " + std::to_string(row) + ": resource '" + std::string(key) +
              "' is none of clay, stone, sand";
      return false;
    }
    if (!RequiredCell(
            table, "extraction_sites", "x_m", row, x_column, kCoordinate, site.position.x, error) ||
        !RequiredCell(
            table, "extraction_sites", "y_m", row, y_column, kCoordinate, site.position.y, error) ||
        !RequiredCell(
            table, "extraction_sites", "area_ha", row, area_column, kArea, site.area_ha, error)) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::span<const std::string_view> ExtractionWorldParamKeys() {
  return kExtractionWorldParamKeys;
}

bool ParseExtractionCatalog(const ITableSet& tables,
                            ExtractionCatalog& catalog,
                            std::string& error) {
  if (const ITable* const world = tables.FindTable("world_params")) {
    // Ranges wide enough for any honest figure, narrow enough to keep the
    // gram arithmetic off int64's edge: a density of ten thousand tonnes a
    // hectare is a quarry face, a tonne taking ten man-days is stone by hand.
    const std::array<ScalarKnob, kExtractionWorldParamKeys.size()> knobs = {{
        {.key = kExtractionWorldParamKeys[0],
         .value = &catalog.stock_t_per_ha[0],
         .range = {.low = 0.0F, .high = 100000.0F}},
        {.key = kExtractionWorldParamKeys[1],
         .value = &catalog.stock_t_per_ha[1],
         .range = {.low = 0.0F, .high = 100000.0F}},
        {.key = kExtractionWorldParamKeys[2],
         .value = &catalog.stock_t_per_ha[2],
         .range = {.low = 0.0F, .high = 100000.0F}},
        {.key = kExtractionWorldParamKeys[3],
         .value = &catalog.days_per_t[0],
         .range = {.low = 0.001F, .high = 10.0F}},
        {.key = kExtractionWorldParamKeys[4],
         .value = &catalog.days_per_t[1],
         .range = {.low = 0.001F, .high = 10.0F}},
        {.key = kExtractionWorldParamKeys[5],
         .value = &catalog.days_per_t[2],
         .range = {.low = 0.001F, .high = 10.0F}},
        {.key = kExtractionWorldParamKeys[6],
         .value = &catalog.tools_per_worker,
         .range = {.low = 0.0F, .high = 100.0F}},
    }};
    if (!ReadKnobs(*world, "world_params", knobs, error)) {
      return false;
    }
  }
  if (const ITable* const resources = tables.FindTable("resources")) {
    for (std::size_t index = 0; index < kMaterialKeys.size(); ++index) {
      catalog.resources[index] =
          DefIdFromRow<ResourceIdTag>(resources->FindRowByKey(kMaterialKeys[index]));
    }
    const std::uint32_t tool_row = resources->FindRowByKey("tool");
    catalog.tool_resource = DefIdFromRow<ResourceIdTag>(tool_row);
    if (tool_row != kNoTableRow) {
      float tool_kg = 0.0F;
      if (!RequiredCell(*resources,
                        "resources",
                        "kg_per_unit",
                        tool_row,
                        resources->FindColumn("kg_per_unit"),
                        Range{.low = 0.001F, .high = 100000.0F},
                        tool_kg,
                        error)) {
        return false;
      }
      catalog.tool_grams = GramsFromKilograms(tool_kg);
    }
  }
  if (const ITable* const sites = tables.FindTable("extraction_sites")) {
    return ParseSites(*sites, catalog, error);
  }
  return true;
}

bool MaterialOf(const ExtractionCatalog& catalog,
                ResourceId resource,
                ExtractedMaterial& material) {
  if (resource.value == kInvalidDefIdValue) {
    return false;
  }
  for (std::size_t index = 0; index < catalog.resources.size(); ++index) {
    if (catalog.resources[index].value == resource.value) {
      material = static_cast<ExtractedMaterial>(index);
      return true;
    }
  }
  return false;
}

Grams StartStockGrams(const ExtractionCatalog& catalog, const ExtractionSiteDef& site) {
  const auto index = static_cast<std::size_t>(site.material);
  if (index >= catalog.stock_t_per_ha.size() || !(site.area_ha > 0.0F)) {
    return 0;
  }
  const double grams = static_cast<double>(site.area_ha) *
                       static_cast<double>(catalog.stock_t_per_ha[index]) * 1.0e6;
  return grams >= kMostGrams ? static_cast<Grams>(kMostGrams) : static_cast<Grams>(grams);
}

std::uint32_t DiggingCrewCap(const ExtractionCatalog& catalog, Grams tool_grams_held) {
  constexpr std::uint32_t kNoCap = 255;  // AssignmentJob::max_crew is one byte
  if (catalog.tool_resource.value == kInvalidDefIdValue || catalog.tool_grams <= 0 ||
      !(catalog.tools_per_worker > 0.0F)) {
    return kNoCap;
  }
  const double tools =
      std::floor(static_cast<double>(tool_grams_held) / static_cast<double>(catalog.tool_grams));
  const double crew = std::floor(tools / static_cast<double>(catalog.tools_per_worker));
  return crew <= 0.0 ? 0U : crew >= kNoCap ? kNoCap : static_cast<std::uint32_t>(crew);
}

}  // namespace core
