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

/// STUB (boss-core-epoch1-queue [84], [90]): the working days in a row the
/// team's work ration is short of full before kTeamOnHay lights. A number
/// for econ, not measured.
inline constexpr std::uint16_t kTeamOnHayDays = 3;

/// STUB (boss [67], [90]; econ canon-horses-oats.md §4 proposed 10 %): the
/// share of the week's harnessed assignment-days without a horse above which
/// kTooFewHorses lights.
inline constexpr float kTooFewHorsesShare = 0.10F;

/// @brief Appends the store alarms standing in `world` (kStoreFull).
/// @param alarms Appended to; never cleared.
void CollectStoreAlarms(const ProductionConfig& config,
                        const WorldState& world,
                        std::vector<Alarm>& alarms);

/// @brief kMeadowUncutBeforeSnow (alarm_state.h; boss-core-epoch1-queue-
/// 2026-09-29 [25]): the elder's advice, lamp 0, while the farm's meadows
/// have at least kMeadowUncutAdviceShare of their hectares not mown this
/// calendar year in the month before the first snow's day
/// (farming.gather_alarm_snow_day). `amount` = the unmown hectares, whole.
/// @param alarms Appended to; never cleared.
void CollectMeadowAdvice(const ProductionConfig& config,
                         const WorldState& world,
                         std::vector<Alarm>& alarms);

/// @brief Sets the lamp of every kStoreFull in `alarms` from the ones beside
/// it (Alarm::lamp; boss-core-epoch1-queue-2026-09-29 [24], [36]: «красное,
/// когда полнота срывает приём урожая или сева, а не просто „полон“»): lit
/// when a harvest will not fit (kHarvestWillNotFit) or a seed has no room
/// (kSeedHasNoRoom) for a resource this store is a home of (IsHomeOf); a full
/// store nothing is refused at is a line. Called after every production
/// predicate has appended, so the refusals are in the list.
/// @param alarms The whole list; only kStoreFull's lamps are written.
void LightStoreFullLamps(const ProductionConfig& config,
                         const WorldState& world,
                         std::vector<Alarm>& alarms);

/// @brief Appends the field alarms standing in `world`: no room for the
/// harvest ahead, and no seed for the sowing just assigned.
/// @param alarms Appended to; never cleared.
void CollectFieldAlarms(const ProductionConfig& config,
                        const WorldState& world,
                        std::vector<Alarm>& alarms);

// The plan's alarms, CollectPlanAlarms, are in plan_alarms.h since 0.36.39.

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
/// them: fodder running out, a byre over its head count, horses unfed; and
/// the team's two (0.37.8): the work ration short in a row (kTeamOnHay) and
/// the week's harness short of horses (kTooFewHorses).
/// @param alarms Appended to; never cleared.
void CollectHerdAlarms(const ProductionConfig& config,
                       const WorldState& world,
                       std::vector<Alarm>& alarms);

}  // namespace core

#endif  // CORE_PRODUCTION_PRODUCTION_ALARMS_H_
