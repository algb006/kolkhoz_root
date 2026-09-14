// Simulation run: does UnitSignals::residents_working see the people who work
// at a unit, on the real village?
//
// Host found by fixture (seq 218, 2026-09-14) that the seam counted the barn
// crew alone: a sawmill with its sawyers at work answered "nobody works here"
// every day since 0.20.0. The unit test proves the count on hand-made rows;
// this run proves it where the rows come from the core itself — the village
// steered as in timber_years until the first day sawyers stand at the saw and
// a crew stands on a site, and each such world handed to a real session.
//
// THE BATH IS NOT HERE: the runs' chairman builds no bath. A post holder with
// no day's work counts at his post in his shift (boss, parcel 238), and the
// unit test holds that line.
//
// Usage: unit_signals [seed] (default 1929).

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>

#include "../common/felling_policy.h"
#include "../common/fixture_policy.h"
#include "../common/limit_policy.h"
#include "../common/repair_policy.h"
#include "../common/run_harness.h"
#include "../common/sawmill_policy.h"
#include "../common/sowing_policy.h"
#include "../common/yard_policy.h"
#include "core_boundary/session.h"
#include "core_common/labor_state.h"
#include "core_common/world_state.h"

namespace {

constexpr std::uint32_t kYears = 30;
constexpr std::int32_t kRipenDays = 13;
constexpr std::uint32_t kSeasonLastDay = 42;

/// The first unit, in row order, that residents work at by name under
/// `kind` right now, and how many do; nullopt when none.
struct Crew {
  core::UnitId unit;
  core::ResidentId member;
  std::uint32_t count = 0;
};

std::optional<Crew> FirstCrew(const core::WorldState& world, core::WorkKind kind) {
  std::optional<Crew> crew;
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const core::WorkAssignment& work = world.residents.rows[row].work;
    if (work.kind != kind) {
      continue;
    }
    if (!crew) {
      crew = Crew{.unit = work.unit, .member = world.residents.row_ids[row], .count = 0};
    }
    crew->count += work.unit.value == crew->unit.value ? 1U : 0U;
  }
  return crew;
}

/// The barn crew at `unit` right now: herd care names a herd, and the herd
/// stands at the unit. A site can be a cattle yard under repair, and then its
/// milkmaids are at work there as much as its masons.
std::uint32_t HerdCrewAt(const core::WorldState& world, core::UnitId unit) {
  std::uint32_t count = 0;
  for (const core::ResidentRow& resident : world.residents.rows) {
    if (resident.work.kind != core::WorkKind::kHerdCare) {
      continue;
    }
    for (std::uint32_t row = 0; row < world.herds.rows.size(); ++row) {
      count += world.herds.row_ids[row].value == resident.work.herd.value &&
                       world.herds.rows[row].unit.value == unit.value
                   ? 1U
                   : 0U;
    }
  }
  return count;
}

/// Hands `world` to a fresh session over the same tables and checks the
/// crew's count and one member's address through the seam.
int CheckThroughTheSession(const run::Simulation& started,
                           std::uint64_t seed,
                           const core::WorldState& world,
                           const Crew& crew,
                           const char* what) {
  core::StandardSimulationConfig sim_config;
  sim_config.tables = started.tables.get();
  sim_config.world_seed = seed;
  sim_config.worker_count = 1;
  core::SessionConfig config;
  config.tables = started.tables.get();
  config.simulation = core::CreateStandardSimulation(sim_config);
  std::unique_ptr<core::ISession> session = core::CreateSession(std::move(config));
  if (session == nullptr) {
    std::cout << "FAIL: the session did not assemble\n";
    return 1;
  }
  session->ReplaceWorld(world);
  const core::UnitSignals signals = session->SignalsOfUnit(crew.unit);
  const core::ResidentWhereabouts where = session->WhereaboutsOf(crew.member);
  const std::uint32_t herd_crew = HerdCrewAt(world, crew.unit);
  std::cout << "unit_signals: seed " << seed << ", day " << world.calendar.day << ": " << what
            << " — " << crew.count << " at work there by name and " << herd_crew
            << " at its herd by the rows, residents_working " << signals.residents_working
            << "; one of them is "
            << (where.place == core::Whereabouts::kAtWork ? "at work" : "NOT at work")
            << " at unit " << where.unit.value << "\n";
  int failures = 0;
  failures += run::Expect(crew.count > 0 && signals.residents_working == crew.count + herd_crew,
                          "the seam counts at the unit everyone the rows put to work there");
  failures +=
      run::Expect(where.place != core::Whereabouts::kAtWork || where.unit.value == crew.unit.value,
                  "and a member at work is at that unit");
  return failures;
}

}  // namespace

int main(int argc, char** argv) {
  const std::uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1929;
  run::Simulation started = run::Start(seed);
  if (!started) {
    return 1;
  }
  run::YardPolicy yard(*started.tables);
  run::FixturePolicy fixture(*started.tables);
  run::FellingPolicy felling(*started.tables);
  run::RepairPolicy repairs(*started.tables);
  run::SawmillPolicy sawmill(*started.tables);
  run::LimitPolicy limit(*started.tables);
  run::SowingPolicy chairman(kRipenDays, kSeasonLastDay, false, started.tables.get());
  run::SawmillPolicy::Declare("unit_signals");

  int failures = 0;
  bool sawyers_seen = false;
  bool builders_seen = false;
  for (std::uint32_t day = 0; day < kYears * core::kDaysPerYear; ++day) {
    for (std::uint32_t tick = 0; tick < core::kTicksPerDay; ++tick) {
      started->AdvanceStep();
      if (tick != core::kTicksPerDay / 2) {
        continue;
      }
      const core::WorldState& world = started.State();
      if (!sawyers_seen) {
        if (const std::optional<Crew> crew = FirstCrew(world, core::WorkKind::kUnitWork)) {
          sawyers_seen = true;
          failures += CheckThroughTheSession(started, seed, world, *crew, "sawyers at the saw");
        }
      }
      if (!builders_seen) {
        if (const std::optional<Crew> crew = FirstCrew(world, core::WorkKind::kConstruction)) {
          builders_seen = true;
          failures += CheckThroughTheSession(started, seed, world, *crew, "a crew on a site");
        }
      }
    }
    if (sawyers_seen && builders_seen) {
      break;
    }
    yard.RunDay(*started.simulation);
    fixture.RunDay(*started.simulation);
    felling.RunDay(*started.simulation);
    sawmill.RunDay(*started.simulation);
    limit.RunDay(*started.simulation);
    repairs.RunDay(*started.simulation);
    chairman.RunDay(*started.simulation);
  }
  failures += run::Expect(sawyers_seen, "the run reached a day with sawyers at the saw");
  failures += run::Expect(builders_seen, "and a day with a crew on a site");
  std::cout << (failures == 0 ? "unit_signals: all checks passed\n"
                              : "unit_signals: FAILURES ABOVE\n");
  return failures;
}
