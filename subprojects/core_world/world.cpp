// Implementation of the core_world boundary (include/core_world/world.h):
// world genesis (task O0) and the wired standard simulation (task O2) — the
// subsystems, the two composite sequential slots and the step engine.

#include "core_world/world.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "campaign_tables.h"
#include "core_common/calendar.h"
#include "core_common/random.h"
#include "core_common/world_state.h"
#include "core_labor/labor_system.h"
#include "core_log/log.h"
#include "core_logistics/logistics_system.h"
#include "core_production/production_system.h"
#include "core_residents/residents_system.h"
#include "core_tables/tables.h"
#include "core_time/time_system.h"

namespace core {

namespace {

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
  StandardSimulation(const StandardSimulationConfig& config,
                     std::unique_ptr<ITimeSystem> time,
                     std::unique_ptr<IResidentsSystem> residents,
                     std::unique_ptr<IProductionSystem> production,
                     std::unique_ptr<ILogisticsSystem> logistics,
                     std::unique_ptr<ILaborSystem> labor)
      : time_(std::move(time)),
        residents_(std::move(residents)),
        production_(std::move(production)),
        logistics_(std::move(logistics)),
        labor_(std::move(labor)),
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

namespace {

/// @brief Weekday by its lowercase English name; `valid` reports success.
Weekday ParseWeekdayNameInternal(std::string_view name, bool& valid) {
  constexpr std::array<std::string_view, kDaysPerWeek> kNames = {
      "monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"};
  for (std::uint32_t index = 0; index < kNames.size(); ++index) {
    if (kNames[index] == name) {
      valid = true;
      return static_cast<Weekday>(index);
    }
  }
  valid = false;
  return Weekday::kMonday;
}

}  // namespace

// Campaign-table readers, shared with genesis.cpp (campaign_tables.h).

Weekday CampaignDayZeroWeekday(const ITableSet& tables) {
  // No campaign table is legal while the setup grows (STUB default: Monday);
  // a present but unreadable value is logged and falls back — genesis
  // returns a value and has no error channel.
  const ITable* campaign = tables.FindTable("campaign");
  if (campaign == nullptr) {
    return Weekday::kMonday;
  }
  const std::uint32_t row = campaign->FindRowByKey("day_zero_weekday");
  const std::uint32_t value_column = campaign->FindColumn("value");
  if (row == kNoTableRow || value_column == kNoTableColumn) {
    LogError("campaign: no day_zero_weekday row or value column; using monday");
    return Weekday::kMonday;
  }
  bool valid = false;
  const Weekday weekday = ParseWeekdayNameInternal(campaign->CellText(row, value_column), valid);
  if (!valid) {
    LogError("campaign: day_zero_weekday is not a weekday name; using monday");
    return Weekday::kMonday;
  }
  return weekday;
}

float CampaignValue(const ITableSet& tables, std::string_view key, float fallback) {
  const ITable* campaign = tables.FindTable("campaign");
  if (campaign == nullptr) {
    return fallback;
  }
  const std::uint32_t row = campaign->FindRowByKey(key);
  const std::uint32_t value_column = campaign->FindColumn("value");
  if (row == kNoTableRow || value_column == kNoTableColumn) {
    return fallback;
  }
  const std::optional<float> value = campaign->CellReal(row, value_column);
  if (!value) {
    LogError("campaign: value of '" + std::string(key) + "' is not a number");
    return fallback;
  }
  return *value;
}

float LifeSpeedupFromTables(const ITableSet& tables) {
  const ITable* life = tables.FindTable("life");
  if (life == nullptr) {
    return 4.0F;
  }
  const std::uint32_t row = life->FindRowByKey("life_speedup");
  const std::uint32_t value_column = life->FindColumn("value");
  if (row == kNoTableRow || value_column == kNoTableColumn) {
    return 4.0F;
  }
  return life->CellReal(row, value_column).value_or(4.0F);
}

std::unique_ptr<ISimulation> CreateStandardSimulation(const StandardSimulationConfig& config) {
  assert(config.tables != nullptr);
  auto time = CreateTimeSystem(*config.tables);
  auto residents = CreateResidentsSystem(*config.tables);
  auto production = CreateProductionSystem(*config.tables);
  auto logistics = CreateLogisticsSystem(*config.tables);
  auto labor = CreateLaborSystem(*config.tables);
  if (!time || !residents || !production || !logistics || !labor) {
    // A factory refused its configuration (it already logged why).
    return nullptr;
  }
  return std::make_unique<StandardSimulation>(config,
                                              std::move(time),
                                              std::move(residents),
                                              std::move(production),
                                              std::move(logistics),
                                              std::move(labor));
}

}  // namespace core
