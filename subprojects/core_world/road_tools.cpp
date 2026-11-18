#include "core_world/road_tools.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "core_catalog/definitions.h"
#include "core_catalog/map_obstacle_tables.h"
#include "core_catalog/map_roads.h"
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
  // The district's road, by map road (7e; the order genesis numbers them in).
  std::vector<MapRoadDef> map_roads;
  if (!ReadMapRoads(tables, map_roads, error)) {
    return std::nullopt;
  }
  tools.district_by_map_road_.reserve(map_roads.size());
  for (const MapRoadDef& map_road : map_roads) {
    tools.district_by_map_road_.push_back(map_road.district);
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
  tools.edge_setback_m_ = definitions.edge_build_setback_m;  // #1788
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
    if (area.kind == MapAreaKind::kStoneQuarry || area.kind == MapAreaKind::kClayPit ||
        area.kind == MapAreaKind::kSandPit) {
      tools.pits_.push_back(area.outline);
    }
  }
  tools.start_land_ = StartLandOf(tools.obstacles_);
  return tools;
}

MeadowMarkGround RoadTools::MeadowGround(bool with_raster) const {
  MeadowMarkGround ground;
  ground.raster = with_raster ? Raster() : nullptr;
  ground.start_land = &start_land_;
  ground.pits = &pits_;
  ground.plot_radius_m = plot_radius_m_;
  ground.map_side_m = map_side_m_;
  return ground;
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

namespace {

/// The ban bits a cell's obstacle flags carry (#1788; placement_bans.h).
std::uint8_t BansOfFlags(std::uint8_t flags, bool forest_allowed) {
  std::uint8_t bans = 0;
  if ((flags & (kObstacleWater | kObstacleRiver | kObstacleFloodplain)) != 0) {
    bans |= kBanWater;
  }
  if ((flags & kObstacleForest) != 0 && !forest_allowed) {
    bans |= kBanForest;
  }
  // THE RESERVE'S OWN UNIT - STUB: «свой юнит — можно» (rules §13, #1886), but
  // no reserve names its unit anywhere today, so the exception never fires and
  // a reserve bans every building (boss files the reserve → unit mapping).
  if ((flags & kObstacleReserve) != 0) {
    bans |= kBanReserve;
  }
  return bans;
}

/// The least distance from `point` to the segment from `a` to `b`.
float DistanceToSegment(Vec2 point, Vec2 a, Vec2 b) {
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  const float length_squared = (dx * dx) + (dy * dy);
  float along = 0.0F;
  if (length_squared > 0.0F) {
    along =
        std::clamp((((point.x - a.x) * dx) + ((point.y - a.y) * dy)) / length_squared, 0.0F, 1.0F);
  }
  return std::hypot(point.x - (a.x + (dx * along)), point.y - (a.y + (dy * along)));
}

/// Whether a circle of `radius_m` round `center` touches a laid road's surface
/// (a road of kind kRoad; a path blocks nothing, rules §13).
bool TouchesARoad(const WorldState& world, Vec2 center, float radius_m, float half_width_m) {
  for (const RoadRow& road : world.roads.rows) {
    if (road.kind != RoadKind::kRoad) {
      continue;
    }
    for (std::size_t index = 1; index < road.axis.size(); ++index) {
      if (DistanceToSegment(center, road.axis[index - 1].position, road.axis[index].position) <
          radius_m + half_width_m) {
        return true;
      }
    }
  }
  return false;
}

/// Whether a circle lies (in part) within the edge band of a map of `side_m`.
bool InEdgeBand(Vec2 center, float radius_m, float side_m, float setback_m) {
  if (setback_m <= 0.0F || side_m <= 0.0F) {
    return false;
  }
  return center.x - radius_m < setback_m || center.y - radius_m < setback_m ||
         side_m - center.x - radius_m < setback_m || side_m - center.y - radius_m < setback_m;
}

}  // namespace

std::uint8_t RoadTools::BansUnder(const WorldState& world,
                                  Vec2 center,
                                  float radius_m,
                                  bool forest_allowed,
                                  float edge_setback_m) const {
  std::uint8_t bans = 0;
  if (const ObstacleRaster* const raster = Raster(); raster != nullptr && raster->HasMap()) {
    // THE CONTOUR'S CELLS: every raster cell centre within the circle, and
    // the centre itself (a circle smaller than a cell).
    const float step = raster->CellMetres();
    bans |= BansOfFlags(raster->FlagsAt(center), forest_allowed);
    for (float dy = -radius_m; dy <= radius_m; dy += step) {
      for (float dx = -radius_m; dx <= radius_m; dx += step) {
        if ((dx * dx) + (dy * dy) > radius_m * radius_m) {
          continue;
        }
        bans |= BansOfFlags(raster->FlagsAt(Vec2{.x = center.x + dx, .y = center.y + dy}),
                            forest_allowed);
      }
    }
  }
  if (TouchesARoad(world, center, radius_m, config_.road_half_width_m)) {
    bans |= kBanRoad;
  }
  if (InEdgeBand(center, radius_m, map_side_m_, edge_setback_m)) {
    bans |= kBanEdge;
  }
  return bans;
}

PlacementBansView RoadTools::BansMap(const WorldState& world, float cell_m) const {
  const float edge_setback_m = edge_setback_m_;
  PlacementBansView view;
  if (map_side_m_ <= 0.0F || cell_m <= 0.0F) {
    return view;
  }
  view.measured = true;
  view.cell_m = cell_m;
  view.columns = static_cast<std::uint32_t>(std::ceil(map_side_m_ / cell_m));
  view.cells.assign(static_cast<std::size_t>(view.columns) * view.columns, 0);
  const ObstacleRaster* const raster = Raster();
  const std::vector<RoadUnitDisc> plots = [&] {
    std::vector<RoadUnitDisc> discs;
    for (const UnitRow& unit : world.units.rows) {
      const std::size_t type = unit.type.value;
      if (type < keep_out_radius_m_.size() && keep_out_radius_m_[type] > 0.0F) {
        discs.push_back(
            RoadUnitDisc{.centre = unit.position, .radius_m = keep_out_radius_m_[type]});
      }
    }
    return discs;
  }();
  for (std::uint32_t row = 0; row < view.columns; ++row) {
    for (std::uint32_t column = 0; column < view.columns; ++column) {
      const Vec2 centre{.x = (static_cast<float>(column) + 0.5F) * cell_m,
                        .y = (static_cast<float>(row) + 0.5F) * cell_m};
      std::uint8_t bans = 0;
      if (raster != nullptr && raster->HasMap()) {
        bans |= BansOfFlags(raster->FlagsAt(centre), false);
      }
      if (InEdgeBand(centre, 0.0F, map_side_m_, edge_setback_m)) {
        bans |= kBanEdge;
      }
      view.cells[(static_cast<std::size_t>(row) * view.columns) + column] = bans;
    }
  }
  // THE ROADS AND THE PLOTS, painted by their own shapes over the cells they
  // reach (cheaper than asking every cell of 12 km about every road).
  const auto paint = [&view, cell_m](Vec2 centre, float reach, std::uint8_t bit, auto inside) {
    const auto clamp_cell = [&view](float metres, float cell) {
      const float index = std::floor(metres / cell);
      return static_cast<std::uint32_t>(
          std::clamp(index, 0.0F, static_cast<float>(view.columns - 1U)));
    };
    for (std::uint32_t row = clamp_cell(centre.y - reach, cell_m);
         row <= clamp_cell(centre.y + reach, cell_m);
         ++row) {
      for (std::uint32_t column = clamp_cell(centre.x - reach, cell_m);
           column <= clamp_cell(centre.x + reach, cell_m);
           ++column) {
        const Vec2 at{.x = (static_cast<float>(column) + 0.5F) * cell_m,
                      .y = (static_cast<float>(row) + 0.5F) * cell_m};
        if (inside(at)) {
          view.cells[(static_cast<std::size_t>(row) * view.columns) + column] |= bit;
        }
      }
    }
  };
  for (const RoadUnitDisc& plot : plots) {
    paint(plot.centre, plot.radius_m, kBanPlot, [&plot](Vec2 at) {
      return std::hypot(at.x - plot.centre.x, at.y - plot.centre.y) <= plot.radius_m;
    });
  }
  const float half = config_.road_half_width_m;
  for (const RoadRow& road : world.roads.rows) {
    if (road.kind != RoadKind::kRoad) {
      continue;
    }
    for (std::size_t index = 1; index < road.axis.size(); ++index) {
      const Vec2 a = road.axis[index - 1].position;
      const Vec2 b = road.axis[index].position;
      const Vec2 mid{.x = (a.x + b.x) * 0.5F, .y = (a.y + b.y) * 0.5F};
      const float reach = (std::hypot(b.x - a.x, b.y - a.y) * 0.5F) + half;
      paint(mid, reach, kBanRoad, [a, b, half](Vec2 at) {
        return DistanceToSegment(at, a, b) < half;
      });
    }
  }
  return view;
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
  // Taken since 7d: open while any road may be taken at all (removable),
  // else nothing to work on. Whether a given piece goes is the selection's.
  // Only a path's or a dirt road's: a paved road's demolition is road work
  // (7e) and would be refused (static review of 0.36.31).
  const bool any_removable =
      std::any_of(world.roads.rows.begin(), world.roads.rows.end(), [](const RoadRow& road) {
        return road.removable != 0 &&
               (road.surface == RoadSurface::kNone || road.surface == RoadSurface::kDirt);
      });
  states[static_cast<std::size_t>(RoadTool::kDemolish)] =
      any_removable ? RoadToolClosed::kOpen : RoadToolClosed::kNothingToWork;
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
  // The places of map_places.csv, and every field by its centre (§17).
  std::vector<RoadAnchorArea> areas;
  areas.reserve(obstacles_.places.size() + world.fields.rows.size());
  for (std::size_t index = 0; index < obstacles_.places.size(); ++index) {
    areas.push_back(
        RoadAnchorArea{.place = DefIdFromRow<MapPlaceIdTag>(static_cast<std::uint32_t>(index)),
                       .field = FieldId{},
                       .point = obstacles_.places[index].point});
  }
  for (std::size_t row = 0; row < world.fields.rows.size(); ++row) {
    areas.push_back(RoadAnchorArea{.place = MapPlaceId{},
                                   .field = world.fields.row_ids[row],
                                   .point = world.fields.rows[row].center});
  }
  RoadPieceSite site;
  site.roads = &world.roads;
  site.road_works = &world.road_works;
  site.district_by_map_road = district_by_map_road_;
  site.units = units;
  site.areas = areas;
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
