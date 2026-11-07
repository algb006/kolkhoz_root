/// @file main.cpp
/// @brief alarm_cost - a PROFILING PROBE, not a test (7 Nov 2026; boss, core-boss-1573-hay-rungs
/// [10] (4)):
///        loads a world saved by thirty_years (KOLKHOZ_SAVE_LAST) and collects the alarms over it N
///        times, so a profiler (valgrind --tool=callgrind) can say where the refresh's cost is.
///        Prints the time and the alarm kinds; exits 1 with «NOT MEASURED» when the world does not
///        load. Usage: alarm_cost WORLD.klhz [N=24]   (run from the core's root: it reads `tables`)
/// @threading SINGLE_THREADED

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/world_state.h"
#include "core_save/save.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cout << "alarm_cost: NOT MEASURED - usage: alarm_cost WORLD.klhz [N]\n";
    return 1;
  }
  const std::uint32_t times =
      argc > 2 ? static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10)) : 24U;
  std::string error;
  const std::unique_ptr<core::ITableSet> tables = core::LoadTableSet("tables", &error);
  if (tables == nullptr) {
    std::cout << "alarm_cost: NOT MEASURED - tables: " << error << '\n';
    return 1;
  }
  core::WorldState world;
  if (!core::LoadWorldFromFile(argv[1], *tables, &world, &error)) {
    std::cout << "alarm_cost: NOT MEASURED - the world: " << error << '\n';
    return 1;
  }
  core::StandardSimulationConfig config;
  config.tables = tables.get();
  config.world_seed = world.world_seed;
  config.worker_count = 1;
  config.stub_tables = core::StubTables::kRefused;
  const std::unique_ptr<core::ISimulation> simulation = core::CreateStandardSimulation(config);
  if (simulation == nullptr) {
    std::cout << "alarm_cost: NOT MEASURED - the simulation did not assemble\n";
    return 1;
  }
  simulation->ResetWorld(world);
  std::vector<core::Alarm> alarms;
  const auto began = std::chrono::steady_clock::now();
  for (std::uint32_t index = 0; index < times; ++index) {
    alarms.clear();
    simulation->CollectAlarms(alarms);
  }
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
  std::cout << "alarm_cost: " << times << " collections over " << world.residents.rows.size()
            << " residents, day " << world.calendar.day << ": " << seconds << " s; alarm kinds";
  for (const core::Alarm& alarm : alarms) {
    std::cout << ' ' << static_cast<int>(alarm.kind);
  }
  std::cout << '\n';
  return 0;
}
