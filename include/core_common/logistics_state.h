/// @file
/// @brief The groom's logistics, the state half (routing stage B; transport
///        design §11 «Подвода возит и груз, и людей», «Люди в логистике», §12
///        «Уровни приоритета», «Вмешательство председателя»): the tasks with
///        their levels, and the day's plan of the carts — a chain of legs for
///        each, with what it carries and whom it picks up and sets down.
/// @threading SINGLE_THREADED
/// Written by the logistics sub-step (core_logistics/logistics_system.h) in
/// the decisions phase (3) alone, and by the order book's consumer there; read
/// by the labour hour and the boundary. No parallel phase touches it.
///
/// THE PLAN IS A SCHEDULE OVER THE SEAM (boss, the logistics thread [8]-[9],
/// option (a)): a load is still a hauling seam in man-days that production
/// writes and the labour hour drains, and its tonnes reach the store in the
/// evening (field_haul.cpp, SettleLoad). The plan decides WHICH cart serves
/// WHICH load, IN WHAT ORDER AND WHEN, and whom it carries on the way — and
/// where each cart is in any hour, which the passengers and the urgent task
/// read. STUB core, named: «the load reaches the store in the evening, not at
/// the hour the cart arrives»; trips with cargo aboard are stage C (§12's
/// routing). «What is loaded is carried to the end» is held by the schedule
/// itself: a re-plan never breaks a leg begun.
///
/// A CART IS NOT AN ENTITY (boss [9], default 4): it is the day's record of a
/// horse and its driver, and it carries nothing over a night — in the evening
/// all is unloaded. The plan is built once a day after the morning placement
/// and the top-up, and re-built from each cart's current place by an event
/// (the flag `stale`, looked at once a game hour; a task of level 0 at the
/// next step — boss, the logistics thread [4]).
///
/// CONTRACT, NO IMPLEMENTATION (core rules §12a): the table in WorldState, the
/// codecs and the save format come with the implementation.

#ifndef CORE_COMMON_LOGISTICS_STATE_H_
#define CORE_COMMON_LOGISTICS_STATE_H_

#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/state_table.h"

namespace core {

/// @brief A task's level (transport design §12, «Уровни приоритета»).
///        Values are the design's numbers and are stored: never renumbered.
enum class LogisticsLevel : std::uint8_t {
  /// Live things under threat: a herd short of feed, a house without firewood
  /// in the frost, a load to spoil within a day (econ's numbers, STUB econ;
  /// the logistics thread [5]); and any task the player raised to it. Placed
  /// ahead of all work in the morning (boss [9]) and re-planned at the next
  /// step when one appears.
  kUrgent = 0,

  /// Due by a date: a heap on a field (before the snow), the meadows' hay,
  /// the district's lot (boss [9], default 1).
  kTerm = 1,

  /// By the round: logs off a stand, a dig's load.
  kOrdinary = 2,

  /// When there is power left: the church store's transfer.
  kBackground = 3,

  /// NOT A LEVEL: the count. Written without its number (it is 4), as every
  /// terminator of the boundary's enums is: scripts/boundary_enums.py reads
  /// the last value's NAME, and «kLogisticsLevelCount = 4» stood MISSING in
  /// every suite from 0.37.176 to 0.37.182 unread (0.37.183).
  kLogisticsLevelCount,
};

inline constexpr std::uint32_t kLogisticsLevelCount =
    static_cast<std::uint32_t>(LogisticsLevel::kLogisticsLevelCount);

/// @brief What a task carts — the load's address. One task a load (boss [9],
///        default 2): how many carts it takes is the plan's to decide.
enum class LogisticsLoadKind : std::uint8_t {
  kFieldHeap = 0,  ///< A field's reaped heap (FieldRow::reaped_grams), arable or meadow.
  kStandLogs,      ///< A stand's felled logs (TimberStandRow::load_grams).
  kSiteDig,        ///< A pit's dug load (ExtractionSiteRow::load_grams).
  kDistrictLot,    ///< A timber lot at the district for the village's carts.
  kStoreTransfer,  ///< A store being emptied (UnitRow::emptying): the church's.
  /// The materials fixed for a works and not yet on it (routing stage V2,
  /// 0.37.204; ConstructionState::awaiting): carted from their sender to the
  /// unit named in `unit`. A unit under an emptying order has no such task
  /// until the order is over — one hauling work names one unit. Seam key
  /// `site_supply`.
  kSiteSupply,
  kLogisticsLoadKindCount,  ///< NOT A KIND: the count.
};

/// @brief Who made the task.
enum class LogisticsOrigin : std::uint8_t {
  kAuto = 0,  ///< Made by the game as the load appeared (§12, «заводятся сами»).
  kPlayer,    ///< Made by the chairman's order (§12, «Вмешательство председателя»).

  /// NOT AN ORIGIN: the count, for a mirror's length and the codecs' range
  /// (scripts/boundary_enums.py; 0.37.183).
  kLogisticsOriginCount,
};

/// @brief One task of the groom's logistics.
struct LogisticsTaskRow {
  LogisticsLoadKind load_kind = LogisticsLoadKind::kLogisticsLoadKindCount;

  /// The load, by its kind: one of these is set.
  FieldId field;
  TimberStandId stand;
  ExtractionSiteId extraction_site;
  LimitDeliveryId limit_delivery;
  UnitId unit;

  /// The level the task has now (raised by ageing or by a threat).
  LogisticsLevel level = LogisticsLevel::kOrdinary;

  /// The level it returns to after a trip (§12: «после рейса задача
  /// возвращается на свой уровень») — its kind's default, or the player's.
  LogisticsLevel base_level = LogisticsLevel::kOrdinary;

  LogisticsOrigin origin = LogisticsOrigin::kAuto;

  /// Paused by the chairman: not planned, and its ageing stands still (§12).
  bool paused = false;

  /// The day of the last trip on it, for the ageing (econ [5]: background ->
  /// ordinary after 6 days, ordinary -> term after 4, term never to urgent;
  /// STUB econ). Until its first trip: the day it was made.
  SimDay aged_from_day = 0;

  /// The tick it entered level 0. Since 0.37.192 no longer the lamp's clock
  /// (unserved_light_hours is): kept for the record of the raise.
  Tick urgent_since = 0;

  /// THE LAMP'S CLOCK (kLogisticsLate; save 142, 0.37.192; boss, the
  /// logistics thread [113]-[114]): the game hours of WORKING LIGHT this task
  /// has stood at level 0 with nobody on it, since it was last served — an
  /// hour of light unserved adds one, an hour served sets it to nought, a task
  /// below level 0 keeps nought. Night hours add nothing: nobody carts by
  /// night, and a lamp lit over a load carted all day, because its carters
  /// went home, was the lamp lying (0.37.191's pair B9: lit 13, 9 and 18 days
  /// of years 1, 4 and 5 with no event of a wait).
  std::uint16_t unserved_light_hours = 0;
};

/// @brief One leg of a cart's day (B4b; TRIPS since stage В, F3 (A)). A goods
///        cart's day is: a RIDE from the horse yard (or the house) to its first
///        load; at each load of its chain, its TRIPS - a LOADED leg from the
///        load to the load's RECEIVER (work_seam.h, WorkReceiverOf: the store,
///        the pile, the works it supplies) and an EMPTY leg back to the load,
///        as many as the light holds; a ride between two loads; the ride home
///        at the day's end, from wherever the cart is. A carrier on foot's day
///        is the same from and to his house. A people's cart's day is two
///        legs: out to the work with its riders, back in the evening.
///
/// THE THREE KINDS, by two fields:
///   * a RIDE: `task` invalid, `loaded` 0 - to a load, between loads, home;
///   * a LOADED TRIP: `task` set, `loaded` 1 - from = the load, to = its
///     receiver; the cart's look is the load's form (CargoFormOfLeg);
///   * the WAY BACK of a trip: `task` set, `loaded` 0 - from = the receiver,
///     to = the load; the cart is empty.
/// Until F3 a load was ONE leg with from = to = the load and the trips inside
/// its seam (0.37.139), so no leg ever moved loaded: a rider could not be
/// refused for a load, and the layer could not draw a cart on its way with
/// its load (the human, 5 October 2026: «От этого зависит визуал телеги в
/// пути»).
///
/// THE TRIPS ARE THE PLAN'S DRAWING, NOT THE CARTING: the labour hour drains
/// the load's seam by the norm-days as before (a half trip at the day's end is
/// a load half carried, haul.h), and the trips are cut from the seam's span
/// by the round trip the seam was priced at. What is carted does not change
/// by F3 (its gate: tonnes per village-year row for row).
struct CartLeg {
  /// Where the leg starts and ends (the horse yard, the house, a load, a
  /// load's receiver, the work of a people's cart).
  Vec2 from;
  Vec2 to;

  /// The task whose load this leg is a trip of - loaded or on its way back;
  /// invalid on a ride (to a load, between loads, home) and on a people's
  /// cart.
  LogisticsTaskId task;

  /// 1 while the cart carries the task's load on this leg (from the load to
  /// its receiver); 0 on a ride and on a trip's way back. Save 153.
  std::uint8_t loaded = 0;

  /// The ticks it leaves and arrives — ESTIMATED when the plan is built
  /// (B4b; boss, the logistics thread [11], default 2): an empty leg by its
  /// road at the pace; a load leg from its first carting hour to the hour its
  /// seam, shared with every cart on it, is carted or the light ends. The
  /// labour hour drains the seam and moves the cart on when it is empty, not
  /// by these ticks (B4): they say where a cart is in an hour, for whoever
  /// asks (the passengers, B5's re-plan, the watchdog). Not finer than an
  /// hour: one tick is a game hour.
  Tick depart = 0;
  Tick arrive = 0;

  /// Who rides this leg beside the driver — seated at `from` or on the way —
  /// within the cart's seats (transport.csv `seats`: the goods cart's bench
  /// two, the people's cart six): the passengers the labour hour seated this
  /// morning (cart_passengers.h, stage A2; work_seam.h, the people's cart),
  /// written on the cart's first leg; and since F3 (B) the passengers the
  /// plan seats for the EVENING (logistics_plan.h, SeatEveningRiders), written
  /// on the leg that picks them up - a loaded trip only if its load's form
  /// seats riders (CargoFormSeatsRiders: not logs, manure or the live), a
  /// ride home always.
  std::vector<ResidentId> riders;
};

/// @brief One cart's day: a horse of the pool and its driver.
struct CartPlan {
  /// The driver: a resident placed on the logistics this morning (the groom's
  /// request, closed by the morning placement — boss [9]).
  ResidentId driver;

  /// True for a people's cart (A3: two or more to one far object), false for
  /// the goods cart with its bench.
  bool people_cart = false;

  /// True for a carrier on foot (B4b; boss, the logistics thread [11],
  /// default 1): no horse, no seats — the plan gives him the nearest loads of
  /// his load's level within a walk, never a far one.
  bool on_foot = false;

  /// The legs in order; the cart is at the end of the last leg it has begun.
  std::vector<CartLeg> legs;
};

/// @brief The day's plan of all carts.
struct GroomPlan {
  /// The day it was built for; a plan of another day is no plan.
  SimDay day = 0;

  std::vector<CartPlan> carts;

  /// An event made the plan old (transport §11's list of events): re-planned
  /// at the next game hour from each cart's current place, or at the next
  /// step when the event is a task of level 0 (boss, the logistics thread [4]).
  bool stale = false;

  /// A task of level 0 appeared since the plan was built: re-plan at the next
  /// step, not at the hour (boss [4]).
  bool urgent_pending = false;
};

using LogisticsTaskTable = StateTable<LogisticsTaskId, LogisticsTaskRow>;

}  // namespace core

#endif  // CORE_COMMON_LOGISTICS_STATE_H_
