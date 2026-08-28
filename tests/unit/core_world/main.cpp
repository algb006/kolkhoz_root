// Unit test of core_world: the composition-root contract shape.
// CreateStartWorld and CreateStandardSimulation get bodies with tasks O0/O2;
// this test grows with them.

#include "core_world/world.h"

int main() {
  // The wiring config must default to the deterministic verification setup:
  // no tables, seed 0, one worker (world.h).
  const core::StandardSimulationConfig config;
  int failures = 0;
  if (config.tables != nullptr) {
    ++failures;
  }
  if (config.world_seed != 0) {
    ++failures;
  }
  if (config.worker_count != 1) {
    ++failures;
  }
  return failures;
}
