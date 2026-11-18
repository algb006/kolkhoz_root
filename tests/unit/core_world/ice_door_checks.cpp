#include "ice_door_checks.h"

#include <cstddef>
#include <iostream>
#include <string>

#include "core_catalog/map_obstacle_tables.h"
#include "core_common/flood_losses_view.h"
#include "core_common/forest_facts_view.h"
#include "core_common/geometry.h"
#include "core_common/ice_view.h"
#include "core_common/map_obstacles.h"
#include "core_common/placement_bans.h"
#include "core_common/quantities.h"
#include "core_common/seed_fund_view.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: ice door: " << label << '\n';
  return 1;
}

/// The mean of an area's outline: inside a lake's or a village's round shape.
core::Vec2 MeanOf(const core::MapAreaDef& area) {
  core::Vec2 sum{};
  for (const core::Vec2& point : area.outline) {
    sum.x += point.x;
    sum.y += point.y;
  }
  const auto count = static_cast<float>(area.outline.empty() ? 1U : area.outline.size());
  return core::Vec2{.x = sum.x / count, .y = sum.y / count};
}

const core::MapAreaDef* FirstOf(const core::MapObstacles& map, core::MapAreaKind kind) {
  for (const core::MapAreaDef& area : map.areas) {
    if (area.kind == kind && area.outline.size() >= 3) {
      return &area;
    }
  }
  return nullptr;
}

}  // namespace

int CheckTheIceDoor() {
  int failures = 0;
  const auto shipped = core::LoadTableSet(KOLKHOZ_TABLES_DIR, nullptr);
  if (Expect(shipped != nullptr, "the shipped tables load") != 0) {
    return 1;
  }
  core::MapObstacles map;
  std::string error;
  if (Expect(core::ReadMapObstacles(*shipped, map, error), "the shipped map reads") != 0) {
    return 1;
  }
  core::StandardSimulationConfig config;
  config.tables = shipped.get();
  config.world_seed = 1929;
  const auto simulation = core::CreateStandardSimulation(config);
  if (Expect(simulation != nullptr, "the canon assembles with the ice rules") != 0) {
    return 1;
  }
  const core::MapAreaDef* const lake = FirstOf(map, core::MapAreaKind::kLake);
  const core::MapAreaDef* const village = FirstOf(map, core::MapAreaKind::kVillageZone);
  const core::MapLineDef* river = nullptr;
  for (const core::MapLineDef& line : map.lines) {
    if (line.kind == core::MapLineKind::kRiver && line.points.size() >= 3) {
      river = &line;
      break;
    }
  }
  failures += Expect(lake != nullptr && village != nullptr && river != nullptr,
                     "the shipped map has a lake, a village zone and a river");
  if (lake == nullptr || village == nullptr || river == nullptr) {
    return failures;
  }
  const core::IceView on_lake = simulation->IceAtToday(MeanOf(*lake));
  const core::IceView on_river =
      simulation->IceAtToday(river->points[river->points.size() / 2].position);
  const core::IceView in_village = simulation->IceAtToday(MeanOf(*village));
  std::cout << "ice door, 1 January: lake " << on_lake.thickness_cm << " cm (water "
            << static_cast<int>(on_lake.water) << ", band " << static_cast<int>(on_lake.band)
            << "), river " << on_river.thickness_cm << " cm (water "
            << static_cast<int>(on_river.water) << "), village water "
            << static_cast<int>(in_village.water) << '\n';
  failures += Expect(on_lake.measured && on_river.measured && in_village.measured,
                     "every answer is measured");
  failures +=
      Expect(on_lake.water == core::IceWater::kStill || on_lake.water == core::IceWater::kInflow,
             "the lake's middle is still water (or its inflow)");
  failures +=
      Expect(on_lake.band == core::IceBand::kSafe, "on 1 January the lake holds a walking man");
  failures += Expect(on_river.water == core::IceWater::kRiver && on_river.thickness_cm > 0.0F &&
                         on_river.thickness_cm < on_lake.thickness_cm,
                     "the river's channel is frozen thinner than the lake (over current)");
  failures += Expect(in_village.water == core::IceWater::kNone && in_village.thickness_cm == 0.0F &&
                         in_village.band == core::IceBand::kNoIce,
                     "the village's middle is dry land, with no ice");
  // THE SEED FUND'S DOOR (#1680) on the same canon: measured, and the start's
  // spring sowings hold seed on 1 January.
  const core::SeedFundView fund = simulation->SeedFundToday();
  core::Grams held = 0;
  for (const core::Grams grams : fund.need) {
    held += grams;
  }
  std::cout << "seed fund door, 1 January: " << held / 1000 << " kg held for the next sowings\n";
  failures += Expect(fund.measured && held > 0,
                     "seed fund: measured, and the start's next sowings hold seed");
  // THE PLACEMENT BANS (#1788) on the same canon, at 10 m: the lake's middle is
  // water, the village's middle is no water (a plot or a road may stand there),
  // and the start's units carry their plots.
  const core::PlacementBansView bans = simulation->PlacementBansToday(10.0F);
  const auto at = [&bans](core::Vec2 point) -> std::uint8_t {
    const auto column = static_cast<std::uint32_t>(point.x / bans.cell_m);
    const auto row = static_cast<std::uint32_t>(point.y / bans.cell_m);
    if (column >= bans.columns || row >= bans.columns) {
      return 0xFF;
    }
    return bans.cells[(static_cast<std::size_t>(row) * bans.columns) + column];
  };
  std::size_t plot_cells = 0;
  std::size_t water_cells = 0;
  for (const std::uint8_t cell : bans.cells) {
    plot_cells += (cell & core::kBanPlot) != 0 ? 1U : 0U;
    water_cells += (cell & core::kBanWater) != 0 ? 1U : 0U;
  }
  std::cout << "placement bans, 10 m: " << bans.columns << " columns, water cells " << water_cells
            << ", plot cells " << plot_cells << '\n';
  failures += Expect(bans.measured && bans.columns == 1200 && bans.cells.size() == 1200U * 1200U,
                     "bans: measured, 1 200 × 1 200 cells of 10 m on the 12 km map");
  failures +=
      Expect((at(MeanOf(*lake)) & core::kBanWater) != 0, "bans: the lake's middle is water");
  failures += Expect((at(MeanOf(*village)) & core::kBanWater) == 0 && plot_cells > 0,
                     "bans: the village's middle is no water, and the start's plots are painted");
  // THE FOREST'S FACTS (#1788 (b)): every stand, with its timber.
  const core::ForestFactsView forest = simulation->ForestFactsToday();
  float stock = 0.0F;
  for (const core::ForestStandFact& stand : forest.stands) {
    stock += stand.stock_m3;
  }
  std::cout << "forest facts: " << forest.stands.size() << " stands, " << stock << " m3\n";
  failures += Expect(forest.measured && !forest.stands.empty() && stock > 0.0F,
                     "forest facts: measured, the start's stands with their timber");
  // THE FLOOD'S LOSSES (#1894): year 1 has no flood (it comes from year 2), so
  // the start's answer is measured and empty, of year 1.
  const core::FloodLossesView flood = simulation->FloodLossesThisYearToday();
  failures += Expect(flood.measured && flood.year == 1 && flood.losses.empty(),
                     "flood losses: measured, year 1, nothing lost before any flood");
  return failures;
}
