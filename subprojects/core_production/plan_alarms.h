/// @file
/// @brief The district plan's alarms: a position the chains will not cover
///        (kPlanPositionUncovered) and a position the turn will not bring to
///        the met share (kPlanPositionShort), and the coverage question the
///        goods loan's repayment asks by the same rule.
/// @threading SINGLE_THREADED
/// Called BETWEEN steps, off the completed buffer, from the sim thread — and
/// at the year's turn by RepayGoodsLoans, inside the production phase, which
/// only reads through it.
///
/// Its own translation unit since 0.36.39: the plan's walk left
/// production_alarms.cpp at 1011 lines once lost slots were counted. It reads
/// the world and the configuration by const reference and owns no state,
/// for the reason production_alarms.h gives.

#ifndef CORE_PRODUCTION_PLAN_ALARMS_H_
#define CORE_PRODUCTION_PLAN_ALARMS_H_

#include <cstdint>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Appends kPlanPositionUncovered for every position of the district's
/// plan and every one of the three calendar years ahead (0 this one, 1 next,
/// 2 the one after) in which the arable grows its crop on fewer hectares than
/// worked arable × area share × plan share (alarm_state.h; year 0 priced off
/// last year's worked arable). A slot already lost does not count (question
/// 278; boss-core-epoch1-resume [98]), and a chain that stands before its
/// first season is read by the year its first slot is grown in. On the
/// year's last day, also kPlanPositionShort for every position that
/// delivered plus takeable (TakeableGrams) will not bring to the met share.
/// @param alarms Appended to; never cleared.
void CollectPlanAlarms(const ProductionConfig& config,
                       const WorldState& world,
                       std::vector<Alarm>& alarms);

/// @brief Whether a position of the district's plan yielding `resource` is
/// uncovered in calendar year `year` (0 this, 1 next, 2 the one after) —
/// kPlanPositionUncovered's test, asked as of `as_of` rather than today: the
/// turn asks it as of the closing year's last day (SeedDayAtTheTurn), its
/// chains not yet turned.
bool PlanPositionUncovered(const ProductionConfig& config,
                           const WorldState& world,
                           ResourceId resource,
                           std::uint32_t year,
                           SimDay as_of);

}  // namespace core

#endif  // CORE_PRODUCTION_PLAN_ALARMS_H_
