/// @file
/// @brief The timber catalogue: timber design §8a's numbers out of
/// world_params.csv, and the stands out of timber_stands.csv.
/// @threading PARALLEL_READONLY
/// Parsed once at assembly on the sim thread; read-only afterwards.
///
/// WHY IN THE CATALOGUE AND NOT IN core_production. Three modules read it and
/// none of them owns the others: genesis makes the stands and gives the
/// groves their stock, production fells and adds the old forest's trunks,
/// and the accountant caps a felling crew by the tools. A number read in
/// three places with three copies of its parse is three numbers; the
/// catalogue is where a definition read out of a table lives (core_catalog's
/// own rule: definitions, no behaviour).

#ifndef CORE_CATALOG_TIMBER_CATALOG_H_
#define CORE_CATALOG_TIMBER_CATALOG_H_

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/timber_state.h"
#include "core_tables/tables.h"

namespace core {

/// @brief One row of tables/timber_stands.csv.
struct TimberStandDef {
  TimberStandKind kind = TimberStandKind::kGrove;

  /// The loading point, metres from the south-west corner.
  Vec2 position;

  /// Hectares: the grove's contour, the belt's length × both sides × width,
  /// or the old forest's area within a team's reach.
  float area_ha = 0.0F;

  /// The share of the stock that is logs, 0..1, baked by the map tool from
  /// the biome's species; the rest is firewood.
  float log_share = 0.0F;
};

/// @brief Timber design §8a, in numbers. Defaults are the design's figures,
/// kept for a world with no tables; a shipped build reads them.
struct TimberCatalog {
  float log_m3 = 0.25F;                       ///< One log, cubic metres.
  float grove_stock_m3_per_ha = 30.0F;        ///< A grove's standing stock.
  float shelterbelt_stock_m3_per_ha = 40.0F;  ///< A belt's standing stock.
  float forest_old_m3_per_ha_year = 0.05F;    ///< Old trunks falling in a year.
  float old_log_share_factor = 0.5F;          ///< An old trunk's logs, of the species' share.
  float fallen_vanish_years = 2.0F;           ///< Years a fallen trunk lies (Epoch I).
  float felling_days_per_m3 = 0.05F;          ///< Game man-days per cubic metre felled.
  float tools_per_feller = 1.0F;              ///< Tools in the stores per feller, not spent.

  /// The sawmill (timber design §8б, boss 2026-09-13 — assigned, not measured).
  float board_yield = 0.55F;              ///< Share of a log's volume that becomes boards.
  float sawing_days_per_board_m3 = 1.0F;  ///< Game man-days per cubic metre of BOARDS.
  float sawyers_max = 2.0F;               ///< Craftsmen at the saw at once.

  /// Every stand, in table row order: TimberStandRow::table_row indexes it.
  std::vector<TimberStandDef> stands;

  /// The log and the tool in the resource roster; invalid when the roster
  /// has no such key (a table-less world).
  ResourceId log_resource;
  ResourceId tool_resource;

  /// Mass of one log, grams (resources.csv kg_per_unit).
  Grams log_grams = 0;

  /// Mass of one tool, grams (resources.csv kg_per_unit), to count the tools
  /// lying in the stores in pieces.
  Grams tool_grams = 0;

  /// The board in the resource roster, invalid without one, and the mass of
  /// one cubic metre of boards (resources.csv kg_per_unit; boards are
  /// measured in m³).
  ResourceId board_resource;
  Grams board_grams_per_m3 = 0;
};

/// @brief The world_params.csv keys this catalogue reads, for the assembly's
/// declared-readers check (core_world/world.cpp). Taken from the same array
/// the parse indexes.
std::span<const std::string_view> TimberWorldParamKeys();

/// @brief Reads the catalogue. A missing table keeps the defaults; a present
/// one that cannot be understood refuses.
/// @return false with `error` naming the table, row and column.
bool ParseTimberCatalog(const ITableSet& tables, TimberCatalog& catalog, std::string& error);

/// @brief The standing stock a stand starts the campaign with, cubic metres:
/// area × density for a grove or a belt, nothing for the old forest, which
/// only ever holds the trunks that have fallen (timber design §8a).
float StartStockM3(const TimberCatalog& catalog, const TimberStandDef& stand);

/// @brief The cubic metres the old forest of this stand adds in a year.
float YearlyOldTrunksM3(const TimberCatalog& catalog, const TimberStandDef& stand);

/// @brief The most old-forest stock a stand can hold: a trunk lies
/// `fallen_vanish_years` and is gone, so at a steady fall the stand never
/// holds more than that many years of it.
float OldForestCeilingM3(const TimberCatalog& catalog, const TimberStandDef& stand);

/// @brief The logs `volume_m3` of this stand yields, in whole-log grams: the
/// stand's log share (halved again for old trunks), divided into logs of
/// `log_m3`. Fractions of a log are not laid down.
Grams LogGramsFromVolume(const TimberCatalog& catalog,
                         const TimberStandDef& stand,
                         float volume_m3);

/// @brief How many may fell at once in a village holding `tool_grams_held`
///        grams of tools: whole tools ÷ `tools_per_feller` (Строительство §10,
///        "нет топора — нет и порубки"). A catalogue with no tool or no
///        per-feller need caps nothing and answers the largest crew a job
///        can name.
std::uint32_t FellingCrewCap(const TimberCatalog& catalog, Grams tool_grams_held);

}  // namespace core

#endif  // CORE_CATALOG_TIMBER_CATALOG_H_
