/// @file
/// @brief DayOrderBook - the state behind the chairman's day orders (#1778; day_orders.h):
///        which day each one is due, the residents a crew order picked, and yesterday's lines
///        the layer re-issues for «Повторить вчерашние».
/// @threading SINGLE_THREADED
/// Written by core_labor in the decisions slot (the read of the work orders, the morning
/// placement, the day's close); read between steps by the reads in day_orders.h.
///
/// WHY A BOOK OF ITS OWN (core-boss-1778-day-orders-contract [4]-[5]): terminal order rows
/// are removed in the events slot of the step they end (order_state.h), so yesterday's kDone
/// day orders do not survive to be read, and an OrderRow has no list to hold a crew's names.

#ifndef CORE_COMMON_DAY_ORDER_STATE_H_
#define CORE_COMMON_DAY_ORDER_STATE_H_

#include <vector>

#include "core_common/calendar.h"
#include "core_common/day_orders.h"
#include "core_common/ids.h"

namespace core {

/// @brief One accepted day order (kAssignCrew or kAssignDayWork) and the working day it is for.
struct DayOrderDue {
  OrderId order;

  /// The day whose morning places it. Set to the day after acceptance; moved to the next day
  /// at the close of a day off, so the order runs on the next WORKING day (society design
  /// «от следующего рабочего дня»).
  SimDay day = 0;
};

/// @brief One resident a kAssignCrew picked.
struct CrewPick {
  OrderId crew;
  ResidentId resident;
};

/// @brief The day orders' book (WorldState::day_orders; saved).
struct DayOrderBook {
  /// The accepted day orders waiting for their day or running it, in acceptance order.
  std::vector<DayOrderDue> due;

  /// The crews' picks, in the order the placement picked them. A name the player released
  /// (kReleaseWork) or wrote elsewhere by name (kAssignDayWork) is taken out.
  std::vector<CrewPick> picks;

  /// The last working day whose close ran, and the day orders that closed on it, in the shape
  /// they are issued again. Empty with the day set: «вчера нарядов не было». 0 and empty before
  /// the first working day closed.
  SimDay yesterday_day = 0;
  std::vector<DayOrderLine> yesterday;

  /// Set at the first working day's close: before it, YesterdayDayOrders has nothing to say.
  bool yesterday_known = false;
};

}  // namespace core

#endif  // CORE_COMMON_DAY_ORDER_STATE_H_
