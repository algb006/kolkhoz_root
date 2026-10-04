// The groom's logistics (core_logistics/logistics_system.h). Routing stage B,
// B2: the tasks and their levels; B3: the plan of the day; B5: the re-plan;
// B7: the chairman's doors to the tasks. The alarm (B8) is a STUB here, its
// signature final: it comes with its delivery.

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
#include "core_common/emit_event.h"
#include "core_common/event_state.h"
#include "core_common/labor_state.h"
#include "core_common/logistics_rules.h"
#include "core_common/logistics_state.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
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

/// «Дольше часа» (transport §12): the game hours a task of level 0 may wait
/// with nobody on it before the lamp lights.
constexpr Tick kLateAfterHours = 1;

/// A task of level 0, not paused, that nobody serves, at least
/// kLateAfterHours since it entered level 0 (kLogisticsLate's condition).
bool WaitsLate(const WorldState& world, const LogisticsTaskRow& task, Tick now) {
  if (task.level != LogisticsLevel::kUrgent || task.paused ||
      now < task.urgent_since + kLateAfterHours) {
    return false;
  }
  return std::ranges::none_of(world.residents.rows, [&task](const ResidentRow& person) {
    return WorkServesTask(person.work, task);
  });
}

void SettleTaskOrder(OrderRow& order, OrderRefusal refusal) {
  order.status = refusal == OrderRefusal::kNone ? OrderStatus::kDone : OrderStatus::kRefused;
  order.refusal = refusal;
}

/// kSetLogisticsLevel (order_state.h): the task takes the level as its own —
/// its base, so the morning's ageing (AgeAndRaise) counts from it and a trip
/// returns the task to it — and as its level now, the ageing from today.
/// Raised to level 0 it is placed ahead of every window at the next
/// placement and its time at level 0 starts now (urgent_since, B8's lamp);
/// the plan is stale and, for level 0, urgent (GroomPlan's two flags — this
/// door is the writer they had none of). A threat the morning reads (a heap
/// under rain, a hungry herd) still raises the task above the player's
/// level: the player's level is a floor for it, not a lid.
/// The range before anything else: a pending row loaded from a save comes
/// past the boundary's shape.
OrderRefusal SetTaskLevel(WorldState& current, const OrderRow& order) {
  if (order.logistics_level >= LogisticsLevel::kLogisticsLevelCount) {
    return OrderRefusal::kRuleForbids;
  }
  const std::uint32_t row = FindRow(current.logistics_tasks, order.logistics_task);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  LogisticsTaskRow& task = current.logistics_tasks.rows[row];
  if (task.base_level == order.logistics_level && task.level == order.logistics_level) {
    return OrderRefusal::kRuleForbids;
  }
  const bool raised_to_urgent =
      order.logistics_level == LogisticsLevel::kUrgent && task.level != LogisticsLevel::kUrgent;
  task.base_level = order.logistics_level;
  task.level = order.logistics_level;
  task.aged_from_day = current.calendar.day;
  if (raised_to_urgent) {
    task.urgent_since = current.calendar.tick;
    current.groom_plan.urgent_pending = true;
  }
  current.groom_plan.stale = true;
  return OrderRefusal::kNone;
}

/// kPauseLogisticsTask (order_state.h): `enable` 1 pauses — the plan leaves
/// the task out (OpenTasks) and the placement offers no carting of its load
/// (labor_system.cpp, DropPausedLoads), from the next re-plan and the next
/// placement; a carter on it now finishes the hour's carting and is not sent
/// back. `enable` 0 goes on, its ageing counted from today.
OrderRefusal PauseTask(WorldState& current, const OrderRow& order) {
  if (order.enable > 1) {
    return OrderRefusal::kRuleForbids;
  }
  const std::uint32_t row = FindRow(current.logistics_tasks, order.logistics_task);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  LogisticsTaskRow& task = current.logistics_tasks.rows[row];
  const bool pause = order.enable != 0;
  if (task.paused == pause) {
    return OrderRefusal::kRuleForbids;
  }
  task.paused = pause;
  if (!pause) {
    task.aged_from_day = current.calendar.day;
  }
  current.groom_plan.stale = true;
  return OrderRefusal::kNone;
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
    // AND WHEN A DOOR OF THE CHAIRMAN'S CHANGED A TASK (B7, ReadTaskOrders):
    // its level or its pause makes the plan stale, and a task raised to
    // level 0 sets `urgent_pending` too — the flag's only writer. It was
    // written by nothing from 0.37.184 to 0.37.187 (found re-reading 184): a
    // load raised to level 0 by the morning is placed by the hour-1 top-up
    // (labor_system.cpp, TopUpDay), and mid-day no task changes level but by
    // this door.
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

  void ReadTaskOrders(WorldState& current) override {
    // THE CHAIRMAN'S DOORS TO THE TASKS (B7; transport §12, «Вмешательство
    // председателя»; order_state.h): each settles in the step it is read, and
    // each makes the plan stale — re-planned at the next hour (Replan).
    for (OrderRow& order : current.orders.rows) {
      if (order.status != OrderStatus::kPending) {
        continue;
      }
      if (order.kind == OrderKind::kSetLogisticsLevel) {
        SettleTaskOrder(order, SetTaskLevel(current, order));
      } else if (order.kind == OrderKind::kPauseLogisticsTask) {
        SettleTaskOrder(order, PauseTask(current, order));
      }
    }
  }

  void CollectAlarms(const WorldState& state, std::vector<Alarm>& out) const override {
    // «ЛОГИСТИКА НЕ УСПЕВАЕТ» (B8; alarm_state.h, kLogisticsLate).
    const Tick now = state.calendar.tick;
    for (std::uint32_t row = 0; row < state.logistics_tasks.rows.size(); ++row) {
      const LogisticsTaskRow& task = state.logistics_tasks.rows[row];
      if (!WaitsLate(state, task, now)) {
        continue;
      }
      Alarm alarm;
      alarm.kind = AlarmKind::kLogisticsLate;
      alarm.logistics_task = state.logistics_tasks.row_ids[row];
      alarm.amount = static_cast<std::int64_t>(now - task.urgent_since);
      out.push_back(alarm);
    }
  }

  void SayLateLoads(WorldState& current) const override {
    // THE LAMP'S FIRST HOUR, SAID (B8; event_state.h, kUrgentLoadWaits): at
    // the hour a task's wait at level 0 crosses one hour, unserved.
    const Tick now = current.calendar.tick;
    for (std::uint32_t row = 0; row < current.logistics_tasks.rows.size(); ++row) {
      const LogisticsTaskRow& task = current.logistics_tasks.rows[row];
      if (!WaitsLate(current, task, now) || now != task.urgent_since + kLateAfterHours) {
        continue;
      }
      SimEvent& event =
          EmitEvent(current, EventKind::kUrgentLoadWaits, EventSeverity::kInterrupting);
      event.amount = static_cast<std::int64_t>(current.logistics_tasks.row_ids[row].value) |
                     (static_cast<std::int64_t>(task.load_kind) << 32);
    }
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
