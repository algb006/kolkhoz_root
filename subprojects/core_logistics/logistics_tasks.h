/// @file
/// @brief The groom's tasks' day (routing stage B, B2; transport design §12
///        «Уровни приоритета»): one task a load, made as the load appears and
///        ended with it; the ageing from the last trip; the self-raise to
///        level 0 by a threat. Pure functions over the world, so the unit
///        tests drive them without a simulation.
/// @threading SINGLE_THREADED
/// Called by the logistics sub-step in the decisions phase (3), once a day,
/// after the morning's placement (logistics_system.cpp).

#ifndef CORE_LOGISTICS_LOGISTICS_TASKS_H_
#define CORE_LOGISTICS_LOGISTICS_TASKS_H_

#include <cstdint>

#include "core_common/calendar.h"
#include "logistics_config.h"

namespace core {

struct WorldState;

/// @brief What one day did to the tasks, for the run's print (econ [5]:
///        printed, no gate).
struct TaskDayCount {
  std::uint32_t made = 0;              ///< Tasks made for loads that appeared.
  std::uint32_t ended = 0;             ///< Tasks ended with their load.
  std::uint32_t served = 0;            ///< Tasks a carter went to today.
  std::uint32_t aged_up = 0;           ///< Raised by ageing today.
  std::uint32_t urgent_by_spoil = 0;   ///< At level 0 by a spoiling heap.
  std::uint32_t urgent_by_hunger = 0;  ///< At level 0 by a hungry herd.
};

/// @brief Makes a task for every load that has none, at its kind's default
///        level, and ends every task whose load is gone (a heap carted, logs
///        taken, a lot fetched, a store emptied). A player's task (B7) is
///        ended with its load too.
/// @post Every load of the world has exactly one task, and no task names a
///       load that is gone.
void SyncTasks(const LogisticsConfig& config,
               WorldState& current,
               SimDay today,
               TaskDayCount& count);

/// @brief A task whose load a carter was placed on today was served: its
///        ageing starts again from today and it returns to its base level
///        (§12: «после рейса задача возвращается на свой уровень»).
void MarkServed(WorldState& current, SimDay today, TaskDayCount& count);

/// @brief Each task's level today: its base level, raised by ageing from the
///        last trip (a paused task does not age: its count stands still), and
///        set to level 0 by a threat — a heap to lose at least the config's
///        share or tonnes by tomorrow's spoilage, or a feed heap while a
///        kolkhoz herd went underfed today. A task entering level 0 records
///        the tick (the alarm's «waits longer than an hour», B8).
/// @note Firewood in the frost (econ [5]'s third threat) is STUB: the core has
///       no firewood yet (heating design, the queue's «холод, дрова и печи»).
///       «Feed for less than a day of a full ration» is STUB too: a herd
///       already underfed today is the threat read here.
void AgeAndRaise(const LogisticsConfig& config,
                 WorldState& current,
                 SimDay today,
                 Tick now,
                 TaskDayCount& count);

}  // namespace core

#endif  // CORE_LOGISTICS_LOGISTICS_TASKS_H_
