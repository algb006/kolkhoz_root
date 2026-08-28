// Implementation of the core_world boundary (include/core_world/world.h):
// world genesis (task O0) and the wired standard simulation (task O2) — the
// subsystems, the two composite sequential slots and the step engine.

#include "core_world/world.h"

#include <cassert>
#include <cstdint>
#include <memory>

#include "core_common/calendar.h"
#include "core_common/random.h"
#include "core_common/world_state.h"
#include "core_labor/labor_system.h"
#include "core_logistics/logistics_system.h"
#include "core_production/production_system.h"
#include "core_residents/residents_system.h"
#include "core_time/time_system.h"

namespace core {

namespace {

/// Stream id of the world's sequential RNG. Other streams derived from the
/// same seed (per-subsystem, if ever needed) must pick distinct ids.
constexpr std::uint64_t kWorldRngStream = 0;

/// The composite decisions slot (phase 3): the subsystems' sequential
/// sub-steps in the fixed order of manual/54-modules.md §3 — assignments,
/// then demography, then production decisions. The order is part of the
/// design contract, not a wiring accident.
class DecisionsSlot final : public ISequentialPhase {
 public:
  DecisionsSlot(ILaborSystem& labor, IResidentsSystem& residents, IProductionSystem& production)
      : labor_(&labor), residents_(&residents), production_(&production) {}

  void RunSequential(const WorldState& previous, WorldState& current) override {
    labor_->RunAssignmentDecisions(previous, current);
    residents_->RunDemographyDecisions(previous, current);
    production_->RunProductionDecisions(previous, current);
  }

 private:
  ILaborSystem* labor_;

  IResidentsSystem* residents_;

  IProductionSystem* production_;
};

/// The events slot (phase 7). STUB: the event system is a phase-3 project
/// feature; until then the slot exists and does nothing, per the
/// no-empty-slots rule of the step contract.
class EventsSlotStub final : public ISequentialPhase {
 public:
  void RunSequential(const WorldState& /*previous*/, WorldState& /*current*/) override {}
};

/// The assembled simulation: owns the subsystems, the composite slots and
/// the engine, exposes only ISimulation. Subsystems hold configuration only
/// (subsystem law), so ResetWorld needs no notification fan-out — the
/// engine's own reset is the whole story.
class StandardSimulation final : public ISimulation {
 public:
  explicit StandardSimulation(const StandardSimulationConfig& config)
      : time_(CreateTimeSystem(*config.tables)),
        residents_(CreateResidentsSystem(*config.tables)),
        production_(CreateProductionSystem(*config.tables)),
        logistics_(CreateLogisticsSystem(*config.tables)),
        labor_(CreateLaborSystem(*config.tables)),
        decisions_slot_(*labor_, *residents_, *production_) {
    const StepPhaseSet phases{
        .time_and_weather = &time_->TimeAndWeatherPhase(),
        .needs = &residents_->NeedsPhase(),
        .decisions = &decisions_slot_,
        .production = &production_->ProductionPhase(),
        .logistics = &logistics_->LogisticsPhase(),
        .metrics = &residents_->MetricsPhase(),
        .events = &events_slot_,
    };
    engine_ = CreateStepEngine(
        CreateStartWorld(*config.tables, config.world_seed), phases, config.worker_count);
  }

  void AdvanceStep() override { engine_->AdvanceStep(); }

  const WorldState& CompletedState() const override { return engine_->CompletedState(); }

  void ResetWorld(const WorldState& initial) override { engine_->ResetWorld(initial); }

 private:
  std::unique_ptr<ITimeSystem> time_;

  std::unique_ptr<IResidentsSystem> residents_;

  std::unique_ptr<IProductionSystem> production_;

  std::unique_ptr<ILogisticsSystem> logistics_;

  std::unique_ptr<ILaborSystem> labor_;

  DecisionsSlot decisions_slot_;

  EventsSlotStub events_slot_;

  std::unique_ptr<ISimulation> engine_;
};

}  // namespace

WorldState CreateStartWorld(const ITableSet& /*tables*/, std::uint64_t world_seed) {
  // STUB until stage 3: no resident, family, field or unit rows — the empty
  // world of the stage-1 criterion. Tables are not read yet; the genesis of
  // the designed start (80 residents, 21 yards, 160 ha) is built here at
  // stage 3.
  WorldState world;
  world.world_seed = world_seed;
  world.rng = SeedRngState(world_seed, kWorldRngStream);
  RefreshCalendarCaches(world.calendar, Weekday::kMonday);
  return world;
}

std::unique_ptr<ISimulation> CreateStandardSimulation(const StandardSimulationConfig& config) {
  assert(config.tables != nullptr);
  return std::make_unique<StandardSimulation>(config);
}

}  // namespace core
