// The groom's logistics (core_logistics/logistics_system.h). Routing stage B,
// B2: the tasks and their levels; B3: the plan of the day. The re-plan (B5),
// the chairman's doors (B7) and the alarm (B8) are STUBs here, their
// signatures final: they come with their deliveries.

#include "core_logistics/logistics_system.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/labor_state.h"
#include "core_common/logistics_state.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_tables/required_tables.h"
#include "logistics_config.h"
#include "logistics_plan.h"
#include "logistics_tasks.h"

namespace core {
namespace {

/// The carts there are against the carts the plan planned: a carter on a horse
/// (a goods cart) or a driver of a people's cart, by id and kind. A carrier on
/// foot is not asked — his chain is his placement's load and the near ones.
bool CartsChanged(const WorldState& world, const GroomPlan& plan) {
  std::size_t there = 0;
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const WorkAssignment& work = world.residents.rows[row].work;
    const bool goods = work.kind == WorkKind::kHauling && work.rides_horse != 0;
    const bool people = TakesThePeoplesCart(work.kind) && work.rides_horse != 0;
    if (!goods && !people) {
      continue;
    }
    ++there;
    const ResidentId driver = world.residents.row_ids[row];
    const bool planned = std::ranges::any_of(plan.carts, [&](const CartPlan& cart) {
      return cart.driver.value == driver.value && !cart.on_foot && cart.people_cart == people;
    });
    if (!planned) {
      return true;
    }
  }
  const auto planned_carts = static_cast<std::size_t>(
      std::ranges::count_if(plan.carts, [](const CartPlan& cart) { return !cart.on_foot; }));
  return planned_carts != there;
}

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

  bool Replan(WorldState& current) override {
    // THE RE-PLAN BY EVENT (B5; transport §11; boss, the logistics thread
    // [61]-[62]): once a game hour after the plan's own hour, the plan is
    // stale when the carts it planned are not the carts there are — a driver
    // who lost his horse or his work, a cart the hour's top-up or a door gave
    // since; the rest of the day is then planned again from where each cart
    // is (BuildGroomPlan, `earlier`). The tasks themselves change once a day,
    // at hour 0, before the plan.
    //
    // `urgent_pending` («a load of level 0 waits») is read here and WRITTEN BY
    // NOTHING yet (0.37.185, found re-reading 0.37.184): a load raised to
    // level 0 is placed by the hour-1 top-up (labor_system.cpp, TopUpDay),
    // and mid-day no task changes level in the core. Its writer is the
    // chairman's door «raise to level 0» (B7) — until then this reading of it
    // never fires, and it is said here rather than claimed.
    GroomPlan& plan = current.groom_plan;
    if (plan.day != current.calendar.day || HourFromTick(current.calendar.tick) < 2) {
      return false;
    }
    plan.stale = plan.stale || CartsChanged(current, plan);
    if (!plan.stale && !plan.urgent_pending) {
      return false;
    }
    LogisticsTally tally;
    const GroomPlan earlier = plan;
    plan = BuildGroomPlan(config_, current, tally, &earlier);
    return true;
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
