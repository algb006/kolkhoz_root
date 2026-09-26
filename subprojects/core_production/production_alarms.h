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

/// @brief Appends kPlanPositionUncovered for every position of the district's
/// plan and every one of the chains' three years in which the arable grows
/// its crop on fewer hectares than worked arable × area share × plan share
/// (alarm_state.h; year 0 priced off last year's worked arable). On the
/// year's last day, also kPlanPositionShort for every position that delivered
/// plus takeable (TakeableGrams) will not bring to the met share.
/// @param alarms Appended to; never cleared.
void CollectPlanAlarms(const ProductionConfig& config,
                       const WorldState& world,
                       std::vector<Alarm>& alarms);

/// @brief Appends kWinterCropUnsowable for every arable field whose chain
/// puts a winter crop in the slot after slot k (k = 0, 1, 2; the chain is a
/// circle, slot 2 is followed by slot 0 of the next round) right after a crop
/// whose reaping opens no earlier than the winter crop's last sowing month
/// (alarm_state.h); `amount` = k + 1.
/// @param alarms Appended to; never cleared.
void CollectWinterCropUnsowableAlarms(const ProductionConfig& config,
                                      const WorldState& world,
                                      std::vector<Alarm>& alarms);

/// @brief Appends kSowingWillNotFit for every spring field in the plough whose
/// harnessed work the team cannot finish by the last day its crop can be sown
/// and still ripen before the snow (alarm_state.h). The days are spent in the
/// order the fields must be sown; `amount` is the square metres short.
/// @param alarms Appended to; never cleared.
void CollectSowingAlarms(const ProductionConfig& config,
                         const WorldState& world,
                         std::vector<Alarm>& alarms);

/// @brief Appends kHarvestWillNotBeGathered for every annual the snow gates
/// that the village cannot reap by the snow at its reaping pace — the
/// season's best day, or every hand of working age before the season's
/// first reaping — the days spent in the order the fields ripen; `amount` is
/// the grams the snow will take (alarm_state.h).
/// @param alarms Appended to; never cleared.
void CollectGatherAlarms(const ProductionConfig& config,
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
