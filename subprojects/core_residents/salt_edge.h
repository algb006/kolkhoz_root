/// @file
/// @brief «Край» and «Без соли» (#1630 (C); health §4 step 4; epochs §8; econ's
/// salt-ending-c §1-§2): the village's run of half its families on step 4 and
/// the ending's condition, the step's deaths and the families that leave.
/// @threading SINGLE_THREADED
/// The residents' decisions sub-step, once a day, after RunSaltDay has set the
/// day's steps, in this order: the run and the condition (judged before the
/// day's departures), the deaths, the leaving.
///
/// THE RULE (every number STUB econ, salt_issue.h's config):
///   * a family COUNTS when at least one member is in the village (not away
///     in the district) - barrack lodgers and single-person families too;
///   * the run: the days running that at least salt_edge_village_share of the
///     counted families stand on step 4; it breaks the day the share falls
///     under it. At salt_edge_series_days: kSaltEndingDue, kInterrupting, once
///     a campaign (SaltState::ending_raised);
///   * deaths: a member of a step-4 family under salt_edge_child_years or from
///     salt_edge_elder_years (biological) dies salt_edge_death_share a day,
///     DeathCause::kSalt, through the death path (BuryTheDead);
///   * leaving: a family salt_edge_leave_days into step 4 (its count at
///     salt_step4_days + that) leaves whole: kFamilyLeftForSalt, each member
///     kResidentLeft; YearLedger departures and departures_salt.
/// OFF (salt_need_on 0): nothing.

#ifndef CORE_RESIDENTS_SALT_EDGE_H_
#define CORE_RESIDENTS_SALT_EDGE_H_

#include "core_common/world_state.h"
#include "life_config.h"

namespace core {

/// @brief The day's run and condition, the step 4 deaths and the leaving.
/// Side effects: SaltState::half_on_edge_days / ending_raised, kSaltEndingDue;
///        residents removed (deaths, departures) with their events and books.
void RunSaltEdgeDay(const LifeConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_SALT_EDGE_H_
