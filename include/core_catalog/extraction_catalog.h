/// @file
/// @brief The extraction catalogue: construction design §3's digging numbers
/// out of world_params.csv, and the plots out of extraction_sites.csv.
/// @threading PARALLEL_READONLY
/// Parsed once at assembly on the sim thread; read-only afterwards.
///
/// WHY IN THE CATALOGUE, for the timber catalogue's reason: three modules read
/// it and none owns the others — genesis makes the sites and gives them their
/// stock, production digs and lays the load, the accountant caps a digging
/// crew by the tools (core_catalog/timber_catalog.h).
///
/// The form and the preliminary numbers are boss's decision of 2026-09-14
/// (parcel 270): a plot like a grove, marked for free, dug by any adult with a
/// tool in the stores, all the year round, the dug mass carted like a harvest
/// to its home (resource_stores.csv), and nothing grows back.

#ifndef CORE_CATALOG_EXTRACTION_CATALOG_H_
#define CORE_CATALOG_EXTRACTION_CATALOG_H_

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_tables/tables.h"

namespace core {

/// @brief What is dug. The three the design names (construction design §3)
/// and no more: a fourth would need its numbers in world_params first.
enum class ExtractedMaterial : std::uint8_t {
  kClay = 0,
  kStone,
  kSand,

  /// NOT A VALUE: the count, for the per-material arrays below.
  kExtractedMaterialCount,
};

/// @brief The number of materials, as a plain integer for array sizes.
inline constexpr std::size_t kExtractedMaterialCount =
    static_cast<std::size_t>(ExtractedMaterial::kExtractedMaterialCount);

/// @brief One row of tables/extraction_sites.csv.
struct ExtractionSiteDef {
  ExtractedMaterial material = ExtractedMaterial::kClay;

  /// The resources.csv row of the material.
  ResourceId resource;

  /// The loading point: the contour's vertex nearest a road, metres from the
  /// south-west corner (boss, parcel 272).
  Vec2 position;

  /// Hectares of the plot; the stock is this × the material's density.
  float area_ha = 0.0F;
};

/// @brief Construction design §3, in numbers (boss, parcel 270 — assigned,
/// not measured). Defaults are those figures, kept for a world with no
/// tables; a shipped build reads them.
struct ExtractionCatalog {
  /// Tonnes a hectare holds that may be dug, by material: clay, stone, sand.
  std::array<float, kExtractedMaterialCount> stock_t_per_ha = {8000.0F, 3000.0F, 8000.0F};

  /// Game man-days per tonne dug, by material.
  std::array<float, kExtractedMaterialCount> days_per_t = {0.05F, 0.25F, 0.03F};

  /// Tools in the stores per digger, not spent (a STUB of wear, as felling's).
  float tools_per_worker = 1.0F;

  /// The resources.csv rows of clay, stone and sand; invalid when absent.
  std::array<ResourceId, kExtractedMaterialCount> resources;

  /// The tool resource and one tool's mass (the felling's `tool`).
  ResourceId tool_resource;
  Grams tool_grams = 0;

  /// The plots, in table order: an ExtractionSiteRow's `table_row` indexes it.
  std::vector<ExtractionSiteDef> sites;
};

/// @brief The world_params keys this catalogue reads, for the assembly's
/// known-keys check (core_world/world.cpp).
std::span<const std::string_view> ExtractionWorldParamKeys();

/// @brief Reads world_params' extraction knobs and tables/extraction_sites.csv
/// (optional: a table set without it has no plots). resources.csv names the
/// materials and the tool.
/// @return false with `error` set for a present table that cannot be read — a
///         site naming a resource that is not clay, stone or sand, a negative
///         area, a knob out of range.
bool ParseExtractionCatalog(const ITableSet& tables,
                            ExtractionCatalog& catalog,
                            std::string& error);

/// @brief The material a resource is, by the catalogue's resource rows.
/// @return false for a resource that is dug nowhere.
bool MaterialOf(const ExtractionCatalog& catalog, ResourceId resource, ExtractedMaterial& material);

/// @brief A site's stock at genesis, grams: area × the material's density.
Grams StartStockGrams(const ExtractionCatalog& catalog, const ExtractionSiteDef& site);

/// @brief How many may dig at once, by the tools held: floor(tools held /
/// tools_per_worker). 255 when the catalogue names no tool at all.
std::uint32_t DiggingCrewCap(const ExtractionCatalog& catalog, Grams tool_grams_held);

}  // namespace core

#endif  // CORE_CATALOG_EXTRACTION_CATALOG_H_
