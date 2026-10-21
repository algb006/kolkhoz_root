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

/// @brief A FIELD'S HEAP REAPED IN THE DAY gets its task now (#1056 C4,
///        writer (3)): a task at its kind's default level for every field
///        heap that has none. It APPENDS ONLY - a task whose load went in the
///        afternoon is kept till the next hour 0, as SyncTasks keeps it (its
///        lamp's clock reads its emptied seam as carted, CartedToday).
/// @return The tasks made.
std::uint32_t AppendNewHeapTasks(const LogisticsConfig& config, WorldState& current, SimDay today);

/// @brief A task whose load a carter was placed on today was served: its
///        ageing starts again from today and it returns to its base level
///        (§12: «после рейса задача возвращается на свой уровень»).
void MarkServed(WorldState& current, SimDay today, TaskDayCount& count);

/// @brief What a field heap on row `field_row` stands to lose before it is
///        carted (#1200, econ's urgency; hay-heap-shelf-gates §4b): its EXPECTED
///        WAIT is its seam's cart-days left over the carts on it today (at least
///        one: haulers on a horse whose work is this field), and at least the
///        night ahead (a day: it is carted tomorrow at the earliest), and its loss over
///        that wait is held x (1 - (1 - the day's share)^wait), the share of
///        FieldHeapLossToday - crop-blind. Grams; 0 for an empty heap or one
///        that keeps.
/// @note The wait is NOT capped at the snow edge (econ's wording asks it):
///       logistics holds no snow edge (labour's, derived by its factory). STUB,
///       named; a heap past the snow edge in winter loses too little a day to
///       reach the share on any wait the seams give (0.75 % a night).
Grams ExpectedHeapLoss(const LogisticsConfig& config,
                       const WorldState& world,
                       std::uint32_t field_row);

/// @brief Each task's level today: its base level, raised by ageing from the
///        last trip (a paused task does not age: its count stands still), and
///        set to level 0 by a threat — a field heap to lose at least the
///        config's share of itself over its expected wait (ExpectedHeapLoss,
///        #1200; until then tomorrow's loss against the share or 1 t), or a
///        feed heap while a kolkhoz herd went underfed today. A task entering
///        level 0 records the tick (the alarm's «waits longer than an hour», B8).
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
