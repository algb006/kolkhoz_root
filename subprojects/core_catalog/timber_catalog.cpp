// The timber catalogue (core_catalog/timber_catalog.h).

#include "core_catalog/timber_catalog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "core_catalog/table_value.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// The world_params.csv keys, in the order of the knob list in Parse.
// NO LOG SHARES HERE. timber_log_share_full/part/none are the export's knobs
// (reader `export` in the design base): db.py works each stand's share out of
// its biome and ships only the result, in timber_stands.csv. The core read
// them for one commit because they were declared `core`; boss re-declared
// them on 2026-09-13 and they left world_params.csv.
constexpr std::array<std::string_view, 11> kTimberWorldParamKeys = {
    "timber_log_m3",
    "timber_grove_stock_m3_per_ha",
    "timber_shelterbelt_stock_m3_per_ha",
    "timber_forest_old_m3_per_ha_year",
    "timber_old_log_share_factor",
    "timber_fallen_vanish_years",
    "timber_felling_days_per_m3",
    "timber_tools_per_feller",
    "timber_board_yield",
    "timber_sawing_days_per_m3",
    "sawmill_sawyers_max"};

bool ParseKind(std::string_view text, TimberStandKind& kind) {
  if (text == "grove") {
    kind = TimberStandKind::kGrove;
    return true;
  }
  if (text == "shelterbelt") {
    kind = TimberStandKind::kShelterbelt;
    return true;
  }
  if (text == "forest_old") {
    kind = TimberStandKind::kForestOld;
    return true;
  }
  return false;
}

bool ParseStands(const ITable& table, TimberCatalog& catalog, std::string& error) {
  const std::uint32_t kind_column = table.FindColumn("kind");
  const std::uint32_t x_column = table.FindColumn("x_m");
  const std::uint32_t y_column = table.FindColumn("y_m");
  const std::uint32_t area_column = table.FindColumn("area_ha");
  const std::uint32_t share_column = table.FindColumn("log_share");
  if (kind_column == kNoTableColumn || x_column == kNoTableColumn || y_column == kNoTableColumn ||
      area_column == kNoTableColumn || share_column == kNoTableColumn) {
    error = "timber_stands: a column of kind, x_m, y_m, area_ha, log_share is missing";
    return false;
  }
  // The map is at most a few tens of kilometres; an old-forest square is a
  // few hundred hectares at most. Wide bounds that still catch a unit slip.
  constexpr Range kCoordinate{.low = 0.0F, .high = 100000.0F};
  constexpr Range kArea{.low = 0.0F, .high = 100000.0F};
  constexpr Range kShare{.low = 0.0F, .high = 1.0F};
  catalog.stands.assign(table.RowCount(), TimberStandDef{});
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    TimberStandDef& stand = catalog.stands[row];
    if (!ParseKind(table.CellText(row, kind_column), stand.kind)) {
      error = "timber_stands: row " + std::to_string(row) + ": kind '" +
              std::string(table.CellText(row, kind_column)) +
              "' is none of grove, shelterbelt, forest_old";
      return false;
    }
    if (!RequiredCell(
            table, "timber_stands", "x_m", row, x_column, kCoordinate, stand.position.x, error) ||
        !RequiredCell(
            table, "timber_stands", "y_m", row, y_column, kCoordinate, stand.position.y, error) ||
        !RequiredCell(
            table, "timber_stands", "area_ha", row, area_column, kArea, stand.area_ha, error) ||
        !RequiredCell(table,
                      "timber_stands",
                      "log_share",
                      row,
                      share_column,
                      kShare,
                      stand.log_share,
                      error)) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::span<const std::string_view> TimberWorldParamKeys() {
  return kTimberWorldParamKeys;
}

bool ParseTimberCatalog(const ITableSet& tables, TimberCatalog& catalog, std::string& error) {
  if (const ITable* const world = tables.FindTable("world_params")) {
    const std::array<ScalarKnob, kTimberWorldParamKeys.size()> knobs = {{
        {.key = kTimberWorldParamKeys[0],
         .value = &catalog.log_m3,
         .range = {.low = 0.01F, .high = 10.0F}},
        {.key = kTimberWorldParamKeys[1],
         .value = &catalog.grove_stock_m3_per_ha,
         .range = {.low = 0.0F, .high = 2000.0F}},
        {.key = kTimberWorldParamKeys[2],
         .value = &catalog.shelterbelt_stock_m3_per_ha,
         .range = {.low = 0.0F, .high = 2000.0F}},
        {.key = kTimberWorldParamKeys[3],
         .value = &catalog.forest_old_m3_per_ha_year,
         .range = {.low = 0.0F, .high = 100.0F}},
        {.key = kTimberWorldParamKeys[4],
         .value = &catalog.old_log_share_factor,
         .range = {.low = 0.0F, .high = 1.0F}},
        {.key = kTimberWorldParamKeys[5],
         .value = &catalog.fallen_vanish_years,
         .range = {.low = 0.0F, .high = 100.0F}},
        {.key = kTimberWorldParamKeys[6],
         .value = &catalog.felling_days_per_m3,
         .range = {.low = 0.0F, .high = 10.0F}},
        {.key = kTimberWorldParamKeys[7],
         .value = &catalog.tools_per_feller,
         .range = {.low = 0.0F, .high = 100.0F}},
        // The sawmill (timber design §8б). Ranges wide enough for any number
        // the design base allows (its min_ok/max_ok sit well inside).
        {.key = kTimberWorldParamKeys[8],
         .value = &catalog.board_yield,
         .range = {.low = 0.0F, .high = 1.0F}},
        {.key = kTimberWorldParamKeys[9],
         .value = &catalog.sawing_days_per_board_m3,
         .range = {.low = 0.0F, .high = 100.0F}},
        {.key = kTimberWorldParamKeys[10],
         .value = &catalog.sawyers_max,
         .range = {.low = 0.0F, .high = 50.0F}},
    }};
    if (!ReadKnobs(*world, "world_params", knobs, error)) {
      return false;
    }
  }
  if (const ITable* const resources = tables.FindTable("resources")) {
    const std::uint32_t log_row = resources->FindRowByKey("log");
    const std::uint32_t tool_row = resources->FindRowByKey("tool");
    const std::uint32_t board_row = resources->FindRowByKey("board");
    catalog.log_resource = DefIdFromRow<ResourceIdTag>(log_row);
    catalog.tool_resource = DefIdFromRow<ResourceIdTag>(tool_row);
    catalog.board_resource = DefIdFromRow<ResourceIdTag>(board_row);
    if (board_row != kNoTableRow) {
      // Boards are measured in cubic metres (resources.csv `measure`), so
      // kg_per_unit is the mass of one cubic metre.
      float board_kg = 0.0F;
      if (!RequiredCell(*resources,
                        "resources",
                        "kg_per_unit",
                        board_row,
                        resources->FindColumn("kg_per_unit"),
                        Range{.low = 0.001F, .high = 100000.0F},
                        board_kg,
                        error)) {
        return false;
      }
      catalog.board_grams_per_m3 = GramsFromKilograms(board_kg);
    }
    if (log_row != kNoTableRow) {
      float log_kg = 0.0F;
      if (!RequiredCell(*resources,
                        "resources",
                        "kg_per_unit",
                        log_row,
                        resources->FindColumn("kg_per_unit"),
                        Range{.low = 0.001F, .high = 100000.0F},
                        log_kg,
                        error)) {
        return false;
      }
      catalog.log_grams = GramsFromKilograms(log_kg);
    }
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
  if (const ITable* const unit_types = tables.FindTable("unit_types")) {
    catalog.sawmill_type = DefIdFromRow<UnitTypeIdTag>(unit_types->FindRowByKey("sawmill"));
  }
  if (const ITable* const stands = tables.FindTable("timber_stands")) {
    return ParseStands(*stands, catalog, error);
  }
  return true;
}

float StartStockM3(const TimberCatalog& catalog, const TimberStandDef& stand) {
  switch (stand.kind) {
    case TimberStandKind::kGrove:
      return stand.area_ha * catalog.grove_stock_m3_per_ha;
    case TimberStandKind::kShelterbelt:
      return stand.area_ha * catalog.shelterbelt_stock_m3_per_ha;
    case TimberStandKind::kForestOld:
    case TimberStandKind::kTimberStandKindCount:
      return 0.0F;
  }
  return 0.0F;
}

float YearlyOldTrunksM3(const TimberCatalog& catalog, const TimberStandDef& stand) {
  return stand.kind == TimberStandKind::kForestOld
             ? stand.area_ha * catalog.forest_old_m3_per_ha_year
             : 0.0F;
}

float OldForestCeilingM3(const TimberCatalog& catalog, const TimberStandDef& stand) {
  return YearlyOldTrunksM3(catalog, stand) * catalog.fallen_vanish_years;
}

Grams LogGramsFromVolume(const TimberCatalog& catalog,
                         const TimberStandDef& stand,
                         float volume_m3) {
  if (!(volume_m3 > 0.0F) || !(catalog.log_m3 > 0.0F) || catalog.log_grams <= 0) {
    return 0;
  }
  const float share = stand.kind == TimberStandKind::kForestOld
                          ? stand.log_share * catalog.old_log_share_factor
                          : stand.log_share;
  const double logs = std::floor(static_cast<double>(volume_m3 * share / catalog.log_m3));
  return static_cast<Grams>(logs) * catalog.log_grams;
}

std::uint32_t FellingCrewCap(const TimberCatalog& catalog, Grams tool_grams_held) {
  constexpr std::uint32_t kNoCap = 255;  // AssignmentJob::max_crew is one byte
  if (catalog.tool_resource.value == kInvalidDefIdValue || catalog.tool_grams <= 0 ||
      !(catalog.tools_per_feller > 0.0F)) {
    return kNoCap;
  }
  const double tools =
      std::floor(static_cast<double>(tool_grams_held) / static_cast<double>(catalog.tool_grams));
  const double crew = std::floor(tools / static_cast<double>(catalog.tools_per_feller));
  return crew <= 0.0 ? 0U : crew >= kNoCap ? kNoCap : static_cast<std::uint32_t>(crew);
}

std::uint32_t UnitWorkPlaces(const TimberCatalog& catalog, UnitTypeId type) {
  if (type.value == kInvalidDefIdValue || type.value != catalog.sawmill_type.value ||
      !(catalog.sawyers_max >= 1.0F)) {
    return 0;
  }
  constexpr float kMostPlaces = 255.0F;  // AssignmentJob::max_crew is one byte
  return static_cast<std::uint32_t>(std::min(std::floor(catalog.sawyers_max), kMostPlaces));
}

float BoardM3FromLogGrams(const TimberCatalog& catalog, Grams log_grams) {
  if (log_grams <= 0 || catalog.log_grams <= 0 || !(catalog.log_m3 > 0.0F)) {
    return 0.0F;
  }
  const double log_m3 = static_cast<double>(log_grams) / static_cast<double>(catalog.log_grams) *
                        static_cast<double>(catalog.log_m3);
  return static_cast<float>(log_m3 * static_cast<double>(catalog.board_yield));
}

Grams LogGramsForBoardM3(const TimberCatalog& catalog, float board_m3) {
  if (!(board_m3 > 0.0F) || !(catalog.board_yield > 0.0F) || !(catalog.log_m3 > 0.0F) ||
      catalog.log_grams <= 0) {
    return 0;
  }
  const double log_m3 = static_cast<double>(board_m3) / static_cast<double>(catalog.board_yield);
  const double grams = std::ceil(log_m3 / static_cast<double>(catalog.log_m3) *
                                 static_cast<double>(catalog.log_grams));
  return static_cast<Grams>(grams);
}

}  // namespace core
