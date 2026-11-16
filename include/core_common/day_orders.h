/// @file
/// @brief The chairman's day orders (#1778; society design §1 «Наряд игрока — число, имена
///        видны», «Повторить вчерашние»; boss's decisions, core-boss-1778-day-orders-contract [2]):
///        the read of the names a crew order filled, the read of yesterday's day orders the layer
///        re-issues, and the read of this season's field works with their deadlines.
/// @threading SINGLE_THREADED
/// Plain data, built on the sim thread between steps.
///
/// THE TWO ORDERS (order_state.h): kAssignCrew - «N on a work at a field / herd / unit for the
/// next working day», the names picked by the chairman's placement level; kAssignDayWork - one
/// named resident on a work for one working day. Both are ONE DAY long (orders design «Наряд -
/// работа человека на сутки»); kAssignWork stays the standing order. A crew's people come out of
/// the accountant's pool for that day, as a kAssignWork resident does (boss [2] point 2).
///
/// «REPEAT YESTERDAY'S» HAS NO DOOR (society design l.170: «новой двери не требует (слой ставит
/// вчерашние наряды игрока заново)»; boss [2] point 1): the layer reads YesterdayDayOrders and
/// issues them again; refusals come back row by row, as for any order.

#ifndef CORE_COMMON_DAY_ORDERS_H_
#define CORE_COMMON_DAY_ORDERS_H_

#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"

namespace core {

/// @brief The names one kAssignCrew filled (ISession::CrewNames): who will be on the work in
///        the morning, readable from the step that accepted the order - «имена видны в строке до
///        начала утра».
struct CrewView {
  /// The crew order asked about; invalid when the id named no accepted kAssignCrew (then the
  /// rest is empty).
  OrderId crew;

  /// The work and its place, as the order named them (exactly one target valid, by `work`).
  WorkKind work = WorkKind::kNone;
  FieldId field;
  HerdId herd;
  UnitId unit;

  /// The N the chairman wrote (OrderRow::crew_size).
  std::uint16_t asked = 0;

  /// The residents the order holds for its day, in the order the placement picked them. Fewer
  /// than `asked` when fewer were free: the order is still accepted and fills what there is
  /// (boss [2], the contract's item 1). A name the player replaced by kReleaseWork is gone from
  /// here; the resident he wrote in by kAssignDayWork is a day row of its own, not a crew name.
  std::vector<ResidentId> names;

  /// The working day the crew works (the day after acceptance, skipping a day of rest).
  SimDay day = 0;

  /// False until #1778's body: the door stands and fills nothing - say «not measured», not «no
  /// names» (STUB, return when the body lands).
  bool measured = false;
};

/// @brief What a day order was: a crew (work, place, N) or one named resident.
enum class DayOrderForm : std::uint8_t {
  kCrew = 0,  ///< kAssignCrew: `count` people, names left to the placement.
  kNamed,     ///< kAssignDayWork: `resident` by name.

  kDayOrderFormCount,  ///< Not a form: the count (boundary_enums: every seam enum is closed).
};

/// @brief One of yesterday's day orders, in the shape the layer issues it again.
struct DayOrderLine {
  DayOrderForm form = DayOrderForm::kCrew;
  WorkKind work = WorkKind::kNone;
  FieldId field;
  HerdId herd;
  UnitId unit;
  std::uint16_t count = 0;  ///< kCrew: the N written; kNamed: 1.
  ResidentId resident;      ///< kNamed: who; invalid for kCrew.
};

/// @brief The last working day's day orders (ISession::YesterdayDayOrders) - the button
///        «Повторить вчерашние наряды» and its grey reason «вчера нарядов не было».
struct YesterdayDayOrders {
  /// The working day they were for; 0 with `lines` empty before the first one.
  SimDay day = 0;
  std::vector<DayOrderLine> lines;

  /// False until #1778's body (STUB): an empty list then means «not measured», not «вчера
  /// нарядов не было».
  bool measured = false;
};

/// @brief One field work of this season with its deadline (ISession::SeasonWorks) - the table
///        «Расставить самому» joins it with the construction sites' read (OfficeWorkbook) on its
///        side (boss [2] point 3). Field works only: sowing, harvest, haymaking per field; herd
///        care has no deadline and stays out.
struct SeasonWorkLine {
  FieldId field;
  WorkKind work = WorkKind::kNone;

  /// The window, from the SAME source the accountant's deadline rule reads, so the table and
  /// the morning placement cannot disagree: the first day the work may start and the last day
  /// it counts as on time.
  SimDay opens = 0;
  SimDay deadline = 0;

  /// The work's size and what is done of it, hectares - the lag a layer paints (#1789's
  /// «Работа» sublayer reads the same line).
  float area_ha = 0.0F;
  float done_ha = 0.0F;
};

/// @brief The season's field works (ISession::SeasonWorks).
struct SeasonWorks {
  /// False until #1778's body: the read stands, no line is built - say «not measured», not «no
  /// works» (STUB, return when the body lands).
  bool measured = false;
  std::vector<SeasonWorkLine> lines;
};

}  // namespace core

#endif  // CORE_COMMON_DAY_ORDERS_H_
