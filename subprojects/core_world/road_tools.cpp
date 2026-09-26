#include "core_world/road_tools.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "core_catalog/definitions.h"
#include "core_catalog/map_obstacle_tables.h"
#include "core_catalog/road_cost_catalog.h"
#include "core_catalog/table_value.h"
#include "core_catalog/timber_catalog.h"
#include "core_common/road_pieces.h"
#include "core_common/world_state.h"
#include "core_tables/stub_tables.h"
#include "core_tables/tables.h"

namespace core {

std::optional<RoadTools> RoadTools::Read(const ITableSet& tables, std::string& error) {
  RoadTools tools;
  RoadSurfaceLevels levels{};
  if (!ReadMapObstacles(tables, tools.obstacles_, error) ||
      !ReadRoadSurfaceLevels(tables, levels, error)) {
    return std::nullopt;
  }
  for (std::size_t surface = 0; surface < levels.size(); ++surface) {
    tools.opens_[surface] = levels[surface].opens;
    tools.costs_[surface].man_days_per_100m = levels[surface].man_days_per_100m;
    tools.costs_[surface].materials_per_100m = levels[surface].materials_per_100m;
  }
  Definitions definitions;
  if (!LoadDefinitions(tables, StubTables::kAllowed, definitions, error)) {
    return std::nullopt;
  }
  TimberCatalog timber;
  if (!ParseTimberCatalog(tables, timber, error) ||
      !ReadClearingLabour(tables, tools.config_.clearing_trudodni_per_ha, error)) {
    return std::nullopt;
  }
  // The access distance a unit is on the network by (declared by
  // core_production, its first reader; read here as well, one row).
  if (const ITable* world_params = tables.FindTable("world_params")) {
    const std::array<ScalarKnob, 1> knobs = {{
        {.key = "road_access_m",
         .value = &tools.road_access_m_,
         .range = Range{.low = 0.0F, .high = 500.0F}},
    }};
    if (!ReadKnobs(*world_params, "world_params", knobs, error)) {
      return std::nullopt;
    }
  }
  tools.plot_radius_m_ = definitions.units.plot_radius_m;
  tools.keep_out_radius_m_ = definitions.units.keep_out_radius_m;
  tools.map_side_m_ = definitions.map_side_m;
  // The old orchard's own stock is a STUB: the grove's (boss [64] item 2).
  tools.timber_m3_per_ha_ = timber.grove_stock_m3_per_ha;
  for (const MapLineDef& line : tools.obstacles_.lines) {
    if (line.kind != MapLineKind::kRiver) {
      continue;  // a brook is crossed anywhere
    }
    for (const MapLinePoint& point : line.points) {
      if (point.mark == MapLineMark::kFord) {
        tools.fords_.push_back(RoadFord{
            .position = point.position,
            .reach_m = point.half_width_m + tools.config_.ford_extra_m,
        });
      }
    }
  }
  for (const MapAreaDef& area : tools.obstacles_.areas) {
    if (area.kind == MapAreaKind::kVillageZone) {
      tools.village_.push_back(area.outline);
    }
  }
  return tools;
}

const ObstacleRaster* RoadTools::Raster() const {
  if (obstacles_.areas.empty() && obstacles_.lines.empty()) {
    return nullptr;
  }
  if (!raster_) {
    // The map's side, or — a set with map areas and no map.csv — the far
    // edge of what is drawn, so nothing drawn falls off the raster.
    float side = map_side_m_;
    if (side <= 0.0F) {
      for (const MapAreaDef& area : obstacles_.areas) {
        for (const Vec2& point : area.outline) {
          side = std::max({side, point.x, point.y});
        }
      }
      for (const MapLineDef& line : obstacles_.lines) {
        for (const MapLinePoint& point : line.points) {
          side = std::max({side, point.position.x, point.position.y});
        }
      }
      side += kObstacleCellMetres;
    }
    raster_ = std::make_shared<ObstacleRaster>(obstacles_, side, kObstacleCellMetres);
  }
  return raster_.get();
}

std::vector<RoadUnitDisc> RoadTools::UnitDiscs(const WorldState& world) const {
  std::vector<RoadUnitDisc> units;
  units.reserve(world.units.rows.size());
  for (const UnitRow& unit : world.units.rows) {
    const std::size_t type = unit.type.value;
    if (type >= keep_out_radius_m_.size()) {
      continue;
    }
    const bool has_plot = type < plot_radius_m_.size() && plot_radius_m_[type] > 0.0F;
    const float radius =
        has_plot ? plot_radius_m_[type] * config_.plot_core_share : keep_out_radius_m_[type];
    if (radius > 0.0F) {
      units.push_back(RoadUnitDisc{.centre = unit.position, .radius_m = radius});
    }
  }
  return units;
}

RoadToolStates RoadTools::ToolStates(const WorldState& world) const {
  const auto open = [&](RoadSurface surface) {
    return EpochIndex(opens_[static_cast<std::size_t>(surface)]) <= EpochIndex(world.epoch);
  };
  // Built tools not written yet say so; a closed epoch is said first, being
  // the reason that holds after the tool is built.
  const auto built_later = [&](RoadSurface surface) {
    return open(surface) ? RoadToolClosed::kNotYetBuilt : RoadToolClosed::kByEpoch;
  };
  RoadToolStates states{};
  // Laid since 7c: open, when their epoch is (the trace refuses them by the
  // same `opens_` otherwise — the menu must not say open over it).
  const auto laid_now = [&](RoadSurface surface) {
    return open(surface) ? RoadToolClosed::kOpen : RoadToolClosed::kByEpoch;
  };
  states[static_cast<std::size_t>(RoadTool::kLayPath)] = laid_now(RoadSurface::kNone);
  states[static_cast<std::size_t>(RoadTool::kLayDirt)] = laid_now(RoadSurface::kDirt);
  states[static_cast<std::size_t>(RoadTool::kLayGravel)] = built_later(RoadSurface::kGravel);
  states[static_cast<std::size_t>(RoadTool::kLayAsphalt)] = built_later(RoadSurface::kAsphalt);
  states[static_cast<std::size_t>(RoadTool::kLayAsphaltWalks)] =
      built_later(RoadSurface::kAsphaltWalks);
  states[static_cast<std::size_t>(RoadTool::kUpgradeToGravel)] = built_later(RoadSurface::kGravel);
  states[static_cast<std::size_t>(RoadTool::kUpgradeToAsphalt)] =
      built_later(RoadSurface::kAsphalt);
  states[static_cast<std::size_t>(RoadTool::kUpgradeToAsphaltWalks)] =
      built_later(RoadSurface::kAsphaltWalks);
  states[static_cast<std::size_t>(RoadTool::kDemolish)] = RoadToolClosed::kNotYetBuilt;
  return states;
}

RoadPieces RoadTools::Select(const WorldState& world,
                             const RoadSelection& selection,
                             RoadOperation operation) const {
  std::vector<RoadAnchorUnit> units;
  units.reserve(world.units.rows.size());
  for (std::size_t row = 0; row < world.units.rows.size(); ++row) {
    units.push_back(RoadAnchorUnit{.unit = world.units.row_ids[row],
                                   .position = world.units.rows[row].position});
  }
  RoadPieceSite site;
  site.roads = &world.roads;
  site.units = units;
  site.road_access_m = road_access_m_;
  site.raster = Raster();
  site.village = village_;
  site.costs = costs_;
  for (std::size_t surface = 0; surface < costs_.size(); ++surface) {
    site.costs[surface].open = EpochIndex(opens_[surface]) <= EpochIndex(world.epoch);
  }
  site.road_half_width_m = config_.road_half_width_m;
  site.path_half_width_m = config_.path_half_width_m;
  return SelectRoadPiecesOn(site, selection, operation);
}

RoadDraftResult RoadTools::Trace(const WorldState& world, const RoadDraft& draft) const {
  const std::vector<RoadUnitDisc> units = UnitDiscs(world);
  RoadTraceSite site;
  site.raster = Raster();
  site.fords = fords_;
  site.village = village_;
  site.units = units;
  site.roads = &world.roads;
  site.map_side_m = map_side_m_;
  site.timber_m3_per_ha = timber_m3_per_ha_;
  site.costs = costs_;
  for (std::size_t surface = 0; surface < costs_.size(); ++surface) {
    site.costs[surface].open = EpochIndex(opens_[surface]) <= EpochIndex(world.epoch);
  }
  return TraceRoad(site, draft, config_);
}

}  // namespace core
