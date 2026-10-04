/// @file
/// @brief ILogisticsSystem — the groom's logistics (routing stage B): the
///        tasks and their levels, the day's plan of the carts, the re-plan by
///        event, the chairman's doors to the tasks, the alarm. The state it
///        writes is core_common/logistics_state.h.
/// @threading SINGLE_THREADED
/// All its work runs in the decisions phase (3), on the sim thread, called by
/// core_world in that phase's fixed order AFTER the labour sub-step (it reads
/// the morning's placement and the top-up), and BEFORE the labour hour reads
/// the plan (it is the hour's own read of where a cart is). It never sees
/// worker threads.
///
/// Subsystem law (manual/52-state-model.md): the implementation holds
/// configuration only — every fact lives in WorldState.
///
/// WHAT IT DOES, BY THE DESIGN (transport §11, §12; boss, the logistics thread
/// [1]-[9]):
///   * TASKS (B2): a task is made for each load as it appears (one task a
///     load), at its kind's default level (boss [9], default 1); a paused task
///     is not planned and does not age; a task ages from its last trip (econ
///     [5]: background -> ordinary after 6 days, ordinary -> term after 4, term
///     never to urgent; STUB econ); a threat raises it to level 0 by itself
///     (econ [5]: feed for less than a day, firewood in the frost, a spoiling
///     load); after a trip it returns to its base level.
///   * THE PLAN (B3): once a day, after the morning placement and the top-up:
///     each cart — a horse of the pool and a driver the placement put on the
///     logistics — gets a chain of legs over the tasks by level, round-robin
///     within a level (§12, «Кольцевая очередь»); the people's cart (A3) and
///     the goods carts' passengers (A2) are planned here. Horses by transport
///     §1: the plough, then the carts, then the riders.
///   * THE GROOM'S REQUEST FOR PEOPLE (boss [9], Transport §11 as mended): the
///     carters of a task of level 0 are placed ahead of all work; of levels
///     1-3, at their load's window in the common queue, as today.
///   * RE-PLAN (B5): an event (transport §11's list) marks the plan stale; it
///     is re-built once a game hour from each cart's current place, and at the
///     next step for a task of level 0 (boss [4]); a loaded cart takes urgent
///     cargo only on its way and only within its room — a load is never set
///     down on the road (§11).
///   * DOORS (B7): kAddLogisticsTask, kSetLogisticsLevel, kPauseLogisticsTask,
///     kCancelLogisticsTask — the player manages TASKS, the groom the
///     EXECUTION (the human, 3 October 2026; §12 «Вмешательство председателя»).
///     Each is an event of re-plan.
///   * ALARM (B8): a task of level 0 waiting for a cart longer than a game
///     hour — «Логистика не успевает», naming the load.
///
/// WHAT IT DOES NOT DO: move tonnes. A load's seam is drained by the labour
/// hour and settled by production in the evening as before (boss [9], option
/// (a)); the plan orders and times the work and carries the people.
///
/// CONTRACT, NO IMPLEMENTATION (core rules §12a): the module, its config and
/// its tests come with the implementation; the order kinds and the alarm kind
/// are new words of the seam, named to boss and host then.

#ifndef CORE_LOGISTICS_LOGISTICS_SYSTEM_H_
#define CORE_LOGISTICS_LOGISTICS_SYSTEM_H_

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/logistics_state.h"
#include "core_tables/stub_tables.h"
#include "core_tables/tables.h"

namespace core {

struct WorldState;

/// @brief What the day's plan did, for the run's print (the logistics thread
///        [5]: «печатать без ворот» — re-plans a day, tasks by level, raised by
///        ageing, set to level 0 by themselves by the three threats).
struct LogisticsTally {
  std::uint32_t carts = 0;                                     ///< Carts out today.
  std::uint32_t legs = 0;                                      ///< Legs planned.
  std::uint32_t riders = 0;                                    ///< Seats taken on all legs.
  std::uint32_t replans = 0;                                   ///< Re-plans by event today.
  std::array<std::uint32_t, kLogisticsLevelCount> tasks = {};  ///< Tasks by level at the plan.
  std::uint32_t aged_up = 0;                                   ///< Tasks raised by ageing today.
  std::uint32_t urgent_by_threat = 0;    ///< Tasks set to level 0 by a threat today.
  float urgent_worst_wait_hours = 0.0F;  ///< The longest a level-0 task waited for a cart.
};

/// @brief The boundary of the logistics subsystem.
class ILogisticsSystem {
 public:
  virtual ~ILogisticsSystem() = default;

  /// @brief Once a day, before the plan: makes the tasks for the loads that
  ///        appeared, ends those whose load is gone, ages them, raises the
  ///        threatened to level 0 (B2).
  /// @post Every load of the world has exactly one task; no task names a load
  ///       that is gone.
  virtual void RunTasks(const WorldState& previous, WorldState& current) = 0;

  /// @brief Builds the day's plan (B3), after the morning placement and the
  ///        top-up of the labour sub-step.
  /// @post current.groom_plan is today's, not stale; the carters' and riders'
  ///       work assignments name their carts.
  /// @return The day's tally.
  virtual LogisticsTally BuildPlan(WorldState& current) = 0;

  /// @brief Every game hour: re-builds the rest of the day from each cart's
  ///        current place when the plan is stale, or at once for a task of
  ///        level 0 (B5). A leg begun is never broken.
  /// @return True when it re-planned.
  virtual bool Replan(WorldState& current) = 0;

  /// @brief The chairman's orders on tasks (B7), read where the order book's
  ///        consumers read theirs; each accepted order marks the plan stale.
  virtual void ReadTaskOrders(WorldState& current) = 0;

  /// @brief «Логистика не успевает» (B8): a task of level 0 waiting for a
  ///        cart longer than a game hour, with the load named
  ///        (alarm_state.h, kLogisticsLate).
  virtual void CollectAlarms(const WorldState& state, std::vector<Alarm>& out) const = 0;

  /// @brief Says the lamp's first hour (B8): an interrupting
  ///        kUrgentLoadWaits for each task whose unserved wait at level 0
  ///        crosses one hour this hour. Called once a game hour after the
  ///        plan, in the decisions slot.
  virtual void SayLateLoads(WorldState& current) const = 0;
};

/// @brief Builds the subsystem from the table set (transport.csv: seats, the
///        wait term; the logistics levels' defaults and ageing, STUB econ).
/// @return nullptr when a table it needs is malformed.
std::unique_ptr<ILogisticsSystem> CreateLogisticsSystem(const ITableSet& tables, StubTables stubs);

}  // namespace core

#endif  // CORE_LOGISTICS_LOGISTICS_SYSTEM_H_
