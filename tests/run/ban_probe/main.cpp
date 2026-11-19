/// @file
/// @brief TEST ONLY (host's ask, 20 Nov 2026): which placement ban holds a unit type at a
///        point on the start world of a seed, and the nearest points the build order would
///        take - asked of the core's own door (ISimulation::BuildBansToday) and FreePlot.
///        usage: ban_probe SEED TYPE_KEY X Y [DAYS]
/// @threading SINGLE_THREADED

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../common/run_harness.h"
#include "core_catalog/definitions.h"
#include "core_common/plot.h"

namespace {

std::string BanNames(std::uint8_t bits) {
  if (bits == 0) {
    return "none";
  }
  // placement_bans.h: 1 water, 2 forest, 4 reserve, 8 road, 16 plot, 32 edge
  static const char* const kNames[] = {"water", "forest", "reserve", "road", "plot", "edge"};
  std::string out;
  for (int bit = 0; bit < 6; ++bit) {
    if ((bits & (1U << bit)) != 0) {
      out += (out.empty() ? "" : "+") + std::string(kNames[bit]);
    }
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 5) {
    std::cout << "usage: ban_probe SEED TYPE_KEY X Y [DAYS]\n";
    return 2;
  }
  const std::uint64_t seed = std::stoull(argv[1]);
  const std::string key = argv[2];
  const core::Vec2 at{.x = std::stof(argv[3]), .y = std::stof(argv[4])};
  const std::uint32_t days = argc > 5 ? static_cast<std::uint32_t>(std::stoul(argv[5])) : 0U;
  run::Simulation started = run::Start(seed);
  if (started.simulation == nullptr) {
    return 1;
  }
  run::AdvanceDays(*started.simulation, days);
  const core::ITable* types = started.tables->FindTable("unit_types");
  const std::uint32_t row = types == nullptr ? core::kNoTableRow : types->FindRowByKey(key);
  if (row == core::kNoTableRow) {
    std::cout << "FAIL: no unit type " << key << '\n';
    return 1;
  }
  const core::UnitTypeId type{static_cast<std::uint16_t>(row)};
  core::Definitions definitions;
  std::string error;
  core::LoadDefinitions(*started.tables, core::StubTables::kAllowed, definitions, error);
  const core::ISimulation& sim = *started.simulation;
  const core::WorldState& world = sim.CompletedState();
  const std::vector<float>& radii = definitions.units.keep_out_radius_m;
  const float radius = row < radii.size() ? radii[row] : 0.0F;
  const std::uint8_t bits = sim.BuildBansToday(type, at);
  const core::Vec2 free = core::FreePlot(world.units, definitions.Plots(), at, radius);
  std::cout << "BANPROBE seed " << seed << " day " << world.calendar.day << ' ' << key << " at "
            << at.x << ',' << at.y << " bans " << BanNames(bits) << " (bits "
            << static_cast<unsigned>(bits) << ") keep_out_radius_m " << radius
            << " FreePlot_answers " << free.x << ',' << free.y << '\n';
  int shown = 0;
  for (int ring = 1; ring <= 60 && shown < 6; ++ring) {
    const float distance = 10.0F * static_cast<float>(ring);
    const int steps = 8 * ring;
    for (int step = 0; step < steps && shown < 6; ++step) {
      const float angle = 6.2831853F * static_cast<float>(step) / static_cast<float>(steps);
      const core::Vec2 point{.x = std::round(at.x + distance * std::cos(angle)),
                             .y = std::round(at.y + distance * std::sin(angle))};
      if (sim.BuildBansToday(type, point) != 0) {
        continue;
      }
      const core::Vec2 placed = core::FreePlot(world.units, definitions.Plots(), point, radius);
      if (std::hypot(placed.x - point.x, placed.y - point.y) > 0.5F) {
        continue;
      }
      std::cout << "BANPROBE free " << point.x << ',' << point.y << " distance_m " << distance
                << '\n';
      ++shown;
    }
  }
  if (shown == 0) {
    std::cout << "BANPROBE free: none within 600 m\n";
  }
  return 0;
}
