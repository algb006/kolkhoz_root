/// @file team_oat_plan.h
/// @brief «ОВСА ПОСЕЯНО МАЛО» - the oat lamp's thin year by the field plan
///        (#1040): the year's reading on 1 January, the field-work day, and
///        the carting that holds the teams.
/// @threading SINGLE_THREADED - the year's turn (phase 6, ReadTheTeamsOatPlan),
///        the herd day (phase 4's sequential tail, IsFieldWorkDay) and the
///        alarms (pure reads).
///
/// THE CONTRACT IS econ's (econ/manual/proposals/oat-lamp-thin-year-contract-
/// 2026-10-17.md, e4b0188 / c31d460; boss, the #1040 thread [2], [4]):
/// - a known, accepted cost of the year - the lean oat slot (f1e0442b) - is
///   said ONCE with its cause and the next step, not lit as a state for most
///   of the year;
/// - the year is thin when (the slot's fields' own oat yield, fertility as it
///   stands - PlannedChainYieldGrams; the contract's ha x 850 changed by boss,
///   the #1040 thread [5] - + the oat the
///   stores hold above the seed and the plan, HeldAboveSeedAndPlan) < the
///   team's need x oat_short_plan_margin;
/// - in a thin year the lamp lights on short field-work days only, one stop
///   a year, a yellow line after.
/// ONE DEVIATION, named: the team's need is the herd day's own work-ration
/// REQUEST of the closed year (YearLedger::team_work_oat_requested), not
/// YearLedger::feed's oat and barley - pigs eat both (feed_links.csv).
/// AND ONE READING, named: «not a rest day» is the world's own day-off door
/// (IsDayOffIn), so a Sunday the chairman declared working counts as one.

#ifndef CORE_PRODUCTION_TEAM_OAT_PLAN_H_
#define CORE_PRODUCTION_TEAM_OAT_PLAN_H_

#include "core_common/ids.h"
#include "core_common/quantities.h"

namespace core {

struct ProductionConfig;
struct WorldState;

/// @brief The horse's first work-only feed (oats in the shipped tables), or
///        an invalid id when the tables have no horse or no such feed.
ResourceId TeamWorkFeed(const ProductionConfig& config);

/// @brief Grams of the team's work feed a request of `units` feed units is:
///        the units over the feed's value per kilogram. 0 with no work feed.
/// @param units  Feed units (WorkRation::room is in FEED units, not grams).
Grams TeamWorkFeedGrams(const ProductionConfig& config, float units);

/// @brief True on a FIELD-WORK DAY (econ §2): not a day off in this world
///        (IsDayOffIn), and an arable field stands in ploughing, harrowing
///        or sowing with work left while its crop's sowing window is open -
///        or, ploughing or harrowing on ground reaped this year, the autumn
///        furrow (whose window is its own season).
bool IsFieldWorkDay(const ProductionConfig& config, const WorldState& world);

/// @brief On 1 January, after the chain has moved and before the books
///        rotate: reads the year's oat by the field plan into
///        TractionWatch (oat_short_by_plan, plan_oat_ha, plan_oat_expected,
///        plan_team_need) and clears the year's stop.
///
/// Side effects: writes current.traction_watch's by-plan fields only.
/// The need is `current.ledger.current.team_work_oat_requested` (the book
/// still being the closing year's here); a book with no request - year 1,
/// or a team that never worked - takes the start estimate: the kolkhoz
/// horses' work ration at the full share times
/// team_need_first_year_working_days.
void ReadTheTeamsOatPlan(const ProductionConfig& config, WorldState& current);

/// @brief The logistics task with the most horse carts in today's plan (the
///        oat lamp's door, kSetLogisticsLevel to lower it), or an invalid id
///        when no cart of today's plan has a horse on a task. Ties go to the
///        lower task id.
LogisticsTaskId TaskHoldingMostTeams(const WorldState& world);

}  // namespace core

#endif  // CORE_PRODUCTION_TEAM_OAT_PLAN_H_
