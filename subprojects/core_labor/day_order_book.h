/// @file
/// @brief The chairman's day orders in core_labor (#1778): kAssignCrew picks N names by his
///        placement level, kAssignDayWork sends one named resident, both for one working day;
///        the book (core_common/day_order_state.h) keeps the due days, the picks and yesterday's
///        lines the layer re-issues.
/// @threading SINGLE_THREADED
/// Decisions slot (phase 3): read at the end of the tick with the other work orders, applied
/// after the standing orders in the morning, closed at the day's close.
///
/// THE DAY (orders design «Наряд - работа человека на сутки»; society design §1): an order read
/// on day d is due on d + 1; a day off moves it to the next day at that day's close, so it runs
/// on the next WORKING day. On its day it is applied after the standing orders - the day's word
/// wins for that day - with the standing order's skips (rain, frost, winter, no work on the
/// target, away, past the rest limit). At that day's close it goes kDone, whatever the weather
/// did with it (STUB: a rained-out day order is not carried over; the layer's «Повторить
/// вчерашние» re-issues it), and its line becomes one of yesterday's.

#ifndef CORE_LABOR_DAY_ORDER_BOOK_H_
#define CORE_LABOR_DAY_ORDER_BOOK_H_

#include "core_common/day_orders.h"
#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "labor_config.h"

namespace core {

/// @brief Reads the pending kAssignCrew and kAssignDayWork rows: refuses or accepts each, sets
///        its due day and, for a crew, picks its names.
///
/// kAssignCrew: refused kNoSuchSubject when the target is gone, kWrongLand when the land cannot
/// carry the work (the standing order's rule); accepted otherwise, with as many of `crew_size`
/// picked as are free (CrewView::names may be shorter than asked). Free: one a work order could
/// name (kAssignWork's own check: alive, of age, no post), with a household, not away, and not
/// already held for that day by a standing order, another crew or a day work.
///
/// THE PICK BY THE CHAIRMAN'S LEVEL (LaborConfig::chairman_placement_level): level 0, «по
/// наличию», takes the free in the accountant's rotation order - rows from (due day mod roster)
/// onward, wrapping - so the turn moves day by day, as the accountant's does (AssignmentParams::
/// rotation). STUB: levels 1-3 pick as 0 does; return when the chairman's level reads an avatar.
///
/// kAssignDayWork: kAssignWork's check; accepted, it takes its resident out of any crew's picks
/// for that day («вписанного по имени игра не трогает» - and his name is no longer the
/// placement's), and a newer day work for the same resident cancels the older.
/// @pre Called at the end of the tick, after ReadWorkOrders.
void ReadDayOrders(const LaborConfig& config, WorldState& current);

/// @brief Takes `resident` out of a crew's picks and ends his kAssignDayWork (kDone) - the
///        kReleaseWork of a day order.
/// @return Whether he was in either.
bool ReleaseFromDayOrders(WorldState& current, ResidentId resident);

/// @brief Sends today's day orders' people, after the standing orders (work_orders.h,
///        ApplyOrderedWorks, the same skips): every pick of every crew due today, then every day
///        work due today.
/// @pre Called from StartDay after ApplyStandingWork.
void ApplyDayOrders(WorldState& current, bool day_off, float walkoff_rest);

/// @brief The day's close for the day orders: on a day off, today's due move to tomorrow; on a
///        working day they go kDone, their lines replace yesterday's (empty when none ran), and
///        their picks are dropped.
/// @pre Called from CloseDay.
void CloseDayOrders(WorldState& current, bool day_off);

/// @brief ISession::CrewNames's answer from the book.
CrewView CrewNamesOf(const WorldState& state, OrderId crew);

/// @brief ISession::YesterdayDayOrdersOf's answer from the book.
YesterdayDayOrders YesterdayDayOrdersIn(const WorldState& state);

}  // namespace core

#endif  // CORE_LABOR_DAY_ORDER_BOOK_H_
