/// @file
/// @brief A logistics task as a work assignment — the one translation between
///        the groom's tasks (logistics_state.h) and the labour's orders
///        (labor_state.h): the hauling work on a task's load, and whether a
///        work serves a task. Shared by the plan's builder (core_logistics) and
///        the labour hour that follows the plan (core_labor; routing stage B,
///        B3 + B4) — two copies would be two answers to «which load is this».
/// @threading PARALLEL_READONLY
/// Pure functions of their arguments.

#ifndef CORE_COMMON_LOGISTICS_RULES_H_
#define CORE_COMMON_LOGISTICS_RULES_H_

#include "core_common/labor_state.h"
#include "core_common/logistics_state.h"

namespace core {

/// @brief The hauling work on this task's load: WorkKind::kHauling and the
///        load's id in its place (the field, the stand, the dig, the district's
///        lot, the store being emptied). work_seam.h's WorkSeamOf and
///        WorkPlaceOf answer the load's seam and place for it.
WorkAssignment HaulingWorkOf(const LogisticsTaskRow& task);

/// @brief Whether this work is the hauling of this task's load.
bool WorkServesTask(const WorkAssignment& work, const LogisticsTaskRow& task);

/// @brief Moves a carter's work onto this task's load: the target ids are
///        the task's, everything else of the work stays — his horse, his road
///        to the yard (the way between two loads is the empty half of the new
///        load's first trip, priced in its seam; 0.37.139), his day's pay.
void RetargetWork(WorkAssignment& work, const LogisticsTaskRow& task);

}  // namespace core

#endif  // CORE_COMMON_LOGISTICS_RULES_H_
