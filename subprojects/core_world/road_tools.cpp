#include "core_world/road_tools.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "core_catalog/definitions.h"
#include "core_catalog/map_obstacle_tables.h"
#include "core_catalog/road_cost_catalog.h"
#include "core_catalog/timber_catalog.h"
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
  if (!ParseTimberCatalog(tables, timber, error)) {
    return std::nullopt;
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

RoadDraftResult RoadTools::Preview(const WorldState& world, const RoadDraft& draft) const {
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
