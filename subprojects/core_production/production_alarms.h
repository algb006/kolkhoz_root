/// @file
/// @brief The three alarm walks core_production owns: stores, fields, herds.
/// @threading SINGLE_THREADED
/// Called BETWEEN steps, off the completed buffer, from the sim thread —
/// never from a worker and never inside a phase.
///
/// THE SEAM IS THE SIGNATURE, NOT AN AGREEMENT (boss, 2026-09-06). Everything
/// here takes the world by const reference and the parsed configuration by
/// const reference, and owns no state of its own; a walk that needed to MOVE
/// the world would not compile on this side. That is why the alarms live in
/// their own translation unit rather than beside the sowing and the harvest:
/// a watchman kept inside the thing it watches can quietly become part of it,
/// and the file that used to hold both had grown to 1413 lines — past the
/// project's hard limit of 1000, which names the illness and says nothing
/// about the cure. The cut by MEANING is what makes the two halves
/// describable; the line count merely says one was due.
///
/// The same shape as stock_lights.h next door, and for the same reason.

#ifndef CORE_PRODUCTION_PRODUCTION_ALARMS_H_
#define CORE_PRODUCTION_PRODUCTION_ALARMS_H_

#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Appends the store alarms standing in `world` (kStoreFull).
/// @param alarms Appended to; never cleared.
void CollectStoreAlarms(const ProductionConfig& config,
                        const WorldState& world,
                        std::vector<Alarm>& alarms);

/// @brief Appends the field alarms standing in `world`: no room for the
/// harvest ahead, and no seed for the sowing just assigned.
/// @param alarms Appended to; never cleared.
void CollectFieldAlarms(const ProductionConfig& config,
                        const WorldState& world,
                        std::vector<Alarm>& alarms);

/// @brief Appends the herd alarms standing in `world`, the stable's among
/// them: fodder running out, a byre over its head count, horses unfed.
/// @param alarms Appended to; never cleared.
void CollectHerdAlarms(const ProductionConfig& config,
                       const WorldState& world,
                       std::vector<Alarm>& alarms);

}  // namespace core

#endif  // CORE_PRODUCTION_PRODUCTION_ALARMS_H_
