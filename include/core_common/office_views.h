/// @file
/// @brief What three windows of the first-day office show, as the core hands
/// it over: the workbook («Тетради» — хозяйство и работы), the plan
/// («Графики» — план и сдача) and the district's limit («Папка» — внешний
/// мир). Read-only views; no order goes through them.
/// @threading SINGLE_THREADED
/// Plain data, built on the sim thread between steps (ISession, ISimulation)
/// and copied out by the caller.
///
/// WHY DOORS AND NOT State() (boss-core-epoch1-resume [100]; boss-core-
/// epoch1-queue [1], item 3). The office's rule is «решают по таблице,
/// объясняет объект» (office design §2): a row of the window is exactly what
/// the chairman decides by. Most of it lies in WorldState already; what does
/// not is a RULE of the core — why a man stood without work this morning, how
/// old he is at this world's speed of life, whether a position counts as
/// delivered, which lots of the district's catalogue may be bought today and
/// at what price. A layer that worked those out for itself would be a second
/// home of each rule, and the first to drift.
///
/// «ПРИЁМ» HAS NO DOOR: the design has no reception queue (office intro draft:
/// «Дверь — Приём — за ней пока тихо»), and the core has none. A door with no
/// design behind it would be an interface for a system nobody wrote down.

#ifndef CORE_COMMON_OFFICE_VIEWS_H_
#define CORE_COMMON_OFFICE_VIEWS_H_

#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"

namespace core {

/// @brief One resident on the workbook's page: who, whose household, how old,
/// what he does today — or, free, why.
struct WorkbookLine {
  ResidentId resident;

  /// The household he lives in; invalid for none.
  FamilyId family;

  /// Biological age in years: the world's speed of life applied to the days
  /// since his birth (life.csv; calendar.h, BiologicalAgeYears).
  float age_years = 0.0F;

  /// Whether he could be given a work order at all (ISession::CanBeOrdered's
  /// answer): of working age, alive, with a household.
  bool can_be_ordered = false;

  /// Today's placement (WorkAssignment::kind); kNone when free.
  WorkKind work = WorkKind::kNone;

  /// Where the work is, when it has a place: the field, or the unit (a site,
  /// a yard, a store). Invalid otherwise; the rest of the placement is read
  /// from ResidentRow::work.
  FieldId field;
  UnitId unit;

  /// WHY WITHOUT A PLACEMENT THIS MORNING: the accountant's reason for a
  /// working adult he left free (IdleReason; ResidentRow::idle_reason).
  /// kIdleReasonCount — NOT A REASON — when he was placed, or when nobody
  /// asked: a child, the old, one away in the district, a day before the
  /// first morning's plan.
  IdleReason idle = IdleReason::kIdleReasonCount;
};

/// @brief One position of the district's plan for the year.
struct PlanLine {
  /// The produce owed.
  ResourceId resource;

  /// The year's figure, grams.
  Grams due = 0;

  /// Shipped against it since the spring announcement, grams.
  Grams delivered = 0;

  /// Whether what is shipped already counts as the position delivered — the
  /// district's met share (campaign.csv plan_met_share; the turn's own
  /// PositionDelivered).
  bool met = false;
};

/// @brief The plan window: the year's positions and their term.
struct PlanBook {
  /// Whether the district has spoken this year (PlanState::announced): a
  /// spring with nothing asked is not a year with no district.
  bool announced = false;

  /// The day the position is judged: the year's turn (district design §9,
  /// the delivery at the turn). The first day of the next calendar year.
  SimDay deadline = 0;

  /// Every produce with a figure this year, by resource row — the spring's
  /// milk position with them; empty before the announcement. The winter's
  /// milk shipped with no position (PlanState::delivered_outside) is not a
  /// position and is not listed.
  std::vector<PlanLine> positions;
};

/// @brief One lot of the district's catalogue as the window lists it.
struct LimitLotLine {
  LimitLotId lot;

  /// Its price in limit points; -1 while the design base leaves it blank —
  /// «not written yet», never sold (limit_catalog.csv).
  std::int32_t points = -1;

  /// Whether a purchase would be taken today, and if not, why — the lot's own
  /// answer (district_limit.h, LotOrderable: kGateClosed for a later era,
  /// kRuleForbids for no price, no amount, or a kind this build does not
  /// sell here); for the MTS column its own rules (MtsColumnRefusal: one out
  /// already, too late for its window, no standing field camp); for a goods
  /// or a livestock lot THE DOOR'S OWN ANSWER since 0.37.121 (district_limit.h,
  /// LimitLotRefusalToday): kNoRoomForStock, kNowhereToStore, then the
  /// balance's kLimitShort. UNTIL THEN THE WINDOW ASKED THE BALANCE ALONE —
  /// «the window says whether the district would sell, not whether the
  /// village has room» — and a lot it showed as buyable was refused at the
  /// order: the same seam as the hay lamp's «buy» (host, 269 refusals of 497).
  OrderRefusal orderable = OrderRefusal::kRuleForbids;
};

/// @brief A cart on its way from the district — goods (limit_deliveries) or
/// heads of stock (livestock_arrivals).
struct LimitCartLine {
  /// The lot it carries; invalid for the district's goods loan, which has no
  /// lot (goods_loan.h).
  LimitLotId lot;

  /// The day it arrives: at the village for the district's cart; at the
  /// district centre, ready to be fetched, for an own-carts lot.
  SimDay arrive_day = 0;

  /// Its day has come and its goods are not all in yet: waiting at the gate
  /// for room, or at the district centre for the village's carters (static
  /// review of 0.37.0: "on the way" listed carts that had arrived).
  bool arrived = false;

  /// The village's own carts fetch it (LimitDeliveryRow::own_carts: a lot
  /// carrying logs); false for the district's cart and for stock.
  bool own_carts = false;
};

/// @brief The limit window: the year's points, the catalogue, the carts.
struct LimitBook {
  /// Points left to spend this year (LimitState::points).
  std::int32_t points = 0;

  /// Every lot of the catalogue, in its table order.
  std::vector<LimitLotLine> catalogue;

  /// Every lot bought and not yet all in: the goods carts in their table's
  /// row order, then the stock's (a cart removed takes the last row's place,
  /// so it is not the order of purchase). The MTS column on the road is not
  /// a cart and is read from State() (mts_column).
  std::vector<LimitCartLine> on_the_way;
};

}  // namespace core

#endif  // CORE_COMMON_OFFICE_VIEWS_H_
