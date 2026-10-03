// The groom's logistics (core_logistics/logistics_system.h). Routing stage B,
// B2: the tasks and their levels; B3: the plan of the day. The re-plan (B5),
// the chairman's doors (B7) and the alarm (B8) are STUBs here, their
// signatures final: they come with their deliveries.

#include "core_logistics/logistics_system.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_tables/required_tables.h"
#include "logistics_config.h"
#include "logistics_plan.h"
#include "logistics_tasks.h"

namespace core {
namespace {

class LogisticsSystem final : public ILogisticsSystem {
 public:
  explicit LogisticsSystem(LogisticsConfig config) : config_(std::move(config)) {}

  void RunTasks(const WorldState& /*previous*/, WorldState& current) override {
    // ONCE A DAY, AFTER THE MORNING'S PLACEMENT (core_world: the decisions
    // slot calls this after the labour sub-step): hour 0, so the carters
    // placed this morning mark their loads served.
    if (HourFromTick(current.calendar.tick) != 0) {
      return;
    }
    const SimDay today = current.calendar.day;
    TaskDayCount count;
    SyncTasks(config_, current, today, count);
    MarkServed(current, today, count);
    AgeAndRaise(config_, current, today, current.calendar.tick, count);
  }

  LogisticsTally BuildPlan(WorldState& current) override {
    // THE PLAN OF THE DAY (B3; logistics_plan.h): over the carts the
    // morning's placement and the top-up put on a horse.
    LogisticsTally tally;
    current.groom_plan = BuildGroomPlan(config_, current, tally);
    return tally;
  }

  bool Replan(WorldState& /*current*/) override {
    return false;  // STUB: B5, the re-plan by event
  }

  void ReadTaskOrders(WorldState& /*current*/) override {
    // STUB: B7, the chairman's doors to the tasks
  }

  void CollectAlarms(const WorldState& /*state*/, std::vector<Alarm>& /*out*/) const override {
    // STUB: B8, «Логистика не успевает»
  }

 private:
  LogisticsConfig config_;
};

}  // namespace

std::unique_ptr<ILogisticsSystem> CreateLogisticsSystem(const ITableSet& tables, StubTables stubs) {
  LogisticsConfig config;
  std::string error;
  // Every table the config reads, refused by name under the strict word
  // (required_tables.h). 0.37.176 took the stubs and asked nothing: a set
  // without logistics.csv assembled on the defaults (unit_core_world, red).
  if (!RequireTables(tables,
                     stubs,
                     "logistics",
                     {"logistics", "resources", "transport", "labor", "world_params", "livestock"},
                     &error) ||
      !ParseLogisticsConfig(tables, config, error)) {
    LogError("logistics: " + error);
    return nullptr;
  }
  return std::make_unique<LogisticsSystem>(std::move(config));
}

}  // namespace core
