/// @file
/// @brief The two ways the chairman buys work with people's strength: the
/// avral on a work (kDeclareRush; unit rules §7) and the cancelled day off
/// (kCancelDayOff; time §9, leisure §6-§7). Boss seq 103, 107 and 109.
/// @threading SINGLE_THREADED
/// Everything here runs in the labor sub-step of the decisions slot (phase
/// 3) on the sim thread: it reads and answers orders, and writes rows the
/// labor day already writes.
///
/// «ОДИН ДОБАВЛЯЕТ МОЩНОСТИ В РАБОЧИЙ ДЕНЬ, ДРУГОЙ ДОБАВЛЯЕТ САМ ДЕНЬ» (time
/// §9). The avral raises what each worker on one work delivers and what it
/// costs him; the cancelled day off makes a Sunday a working day through the
/// one door every module asks (core_common/day_off.h). Both are paid in rest
/// and in the family's satisfaction, and neither is delegated.

#ifndef CORE_LABOR_RUSH_H_
#define CORE_LABOR_RUSH_H_

#include "core_common/labor_state.h"
#include "core_common/world_state.h"
#include "labor_config.h"

namespace core {

/// @brief Reads and answers the pending kDeclareRush, kCancelDayOff and
/// kHarvestWithoutDaysOff. Settled on reading (kDone): the step stands on the
/// field or the site, the cancelled day and the harvest's switch on the
/// chairman's block.
///   * kHarvestWithoutDaysOff (from 0.37.87): sets ChairmanState::
///     harvest_without_days_off to `enable`; refused kRuleForbids when it
///     already stands so. It has no price of its own to book here — the rest
///     not recovered is the day's (labor's close of a working day).
///   * kDeclareRush on a field: refused kNoSuchSubject for a field that is
///     gone, kRuleForbids for one with no work standing (growing, idle, its
///     phase's work done). Sets the step and the phase it stands on; 0 lifts.
///   * kDeclareRush on a unit: the same for a site with building work left.
///   * kCancelDayOff, `enable` 0: the next weekly day off from tomorrow
///     within two weeks, holidays skipped (time §9, «праздники
///     неприкосновенны»); refused kRuleForbids while a cancelled day still
///     stands ahead.
///   * kCancelDayOff, `enable` 1 (B8's door, 0.37.190): TODAY, while today
///     is a day off and no holiday (TodayMayBeDeclaredWorking); the day
///     starts at the next hour's placement (ChairmanState::
///     place_after_declaring). Refused kRuleForbids otherwise.
void ReadRushOrders(WorldState& current);

/// @brief The share an avral adds to this assignment's work: step ×
/// `rush_step_percent` / 100, or 0 when its field or site stands under none
/// (or the field has left the phase it was declared in).
float RushBoost(const LaborConfig& config, const WorldState& world, const WorkAssignment& work);

/// @brief The morning's housekeeping: a field's avral goes out when the
/// field has left the phase it stood on — the work is done, not the day. On
/// the cancelled day off itself the series counts one more. On a season's
/// first day every family's overwork memory is cleared.
void StandDownRushes(WorldState& current);

/// @brief At a worker's pay — the day's close or his walk-off, whichever
/// settles him — while his assignment still says what he did: under an
/// avral, `rush_satisfaction_per_step_day` × step to his family's
/// overwork_penalty; on the cancelled day off, `day_off_cancel_rest_per_series`
/// × its number in the series off his rest and `day_off_cancel_satisfaction`
/// to his family. Nothing for a man who delivered nothing.
void BookRushAtPay(const LaborConfig& config, WorldState& current, ResidentRow& resident);

/// @brief The day's close: the cancelled day, once lived, is spent; a day
/// off actually taken breaks the series.
void CloseRushDay(WorldState& current);

}  // namespace core

#endif  // CORE_LABOR_RUSH_H_
