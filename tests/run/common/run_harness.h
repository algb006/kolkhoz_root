/// @file
/// @brief The boilerplate every simulation run repeats: the pass/fail
/// counter, the table load, the assembled simulation, the year loop.
/// @threading SINGLE_THREADED
/// Test-side code, called from main() of a run application. The simulation
/// it hands back must be driven from the thread that created it (the step
/// engine binds its scheduler to that thread — core_sim/step.h).
///
/// Header-only and in its own namespace, because it is included by
/// applications that are separate CMake targets and share nothing else.
/// Include it as "../common/run_harness.h": tests/run has no library and no
/// include path of its own, and a relative include needs neither.
///
/// What it deliberately does NOT do is assert anything about the world. The
/// criteria are the runs' own; this file only removes the five copies of
/// "load tables, build a simulation, complain usefully if either failed".

#ifndef TESTS_RUN_COMMON_RUN_HARNESS_H_
#define TESTS_RUN_COMMON_RUN_HARNESS_H_

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief Reports a failed expectation and returns 1, so a run can total its
/// failures with `failures += Expect(...)` and use that as its exit code.
inline int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

/// @brief A loaded table set and the simulation built over it, in one owner.
/// The tables must outlive the simulation (core_world/world.h), which is
/// exactly why they travel together.
struct Simulation {
  std::unique_ptr<core::ITableSet> tables;

  std::unique_ptr<core::ISimulation> simulation;

  /// @brief False when either half failed; the reason is already printed.
  explicit operator bool() const { return tables != nullptr && simulation != nullptr; }

  core::ISimulation& operator*() const { return *simulation; }

  core::ISimulation* operator->() const { return simulation.get(); }

  const core::WorldState& State() const { return simulation->CompletedState(); }
};

/// @brief Loads `tables_dir` and assembles the standard simulation over it.
/// @param seed         Campaign seed; the same seed gives the same village.
/// @param worker_count 1 is the verification mode; any other value must
///                     produce identical results.
/// @param tables_dir   Usually "tables", relative to the working directory —
///                     ctest runs these from the repository root. A doctored
///                     run passes its own copy instead.
/// @return An empty Simulation on failure, with the reason on stdout: a
///         stray working directory is a usage problem, not a run failure,
///         and the message says so.
inline Simulation Start(std::uint64_t seed,
                        std::uint32_t worker_count = 1,
                        std::string_view tables_dir = "tables") {
  Simulation started;
  std::string error;
  started.tables = core::LoadTableSet(tables_dir, &error);
  if (started.tables == nullptr) {
    std::cout << "FAIL: " << tables_dir << " did not load (" << error
              << ") — run from the repo root\n";
    return started;
  }
  core::StandardSimulationConfig config;
  config.tables = started.tables.get();
  config.world_seed = seed;
  config.worker_count = worker_count;
  started.simulation = core::CreateStandardSimulation(config);
  if (started.simulation == nullptr) {
    std::cout << "FAIL: the simulation did not assemble\n";
  }
  return started;
}

/// @brief Advances whole game days.
inline void AdvanceDays(core::ISimulation& simulation, std::uint32_t days) {
  for (std::uint32_t tick = 0; tick < days * core::kTicksPerDay; ++tick) {
    simulation.AdvanceStep();
  }
}

/// @brief Advances one game year (48 days).
inline void AdvanceYear(core::ISimulation& simulation) {
  AdvanceDays(simulation, core::kDaysPerYear);
}

}  // namespace run

#endif  // TESTS_RUN_COMMON_RUN_HARNESS_H_
