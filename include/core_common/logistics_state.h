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

  /// NOT A LEVEL: the count.
  kLogisticsLevelCount = 4,
};

inline constexpr std::uint32_t kLogisticsLevelCount =
    static_cast<std::uint32_t>(LogisticsLevel::kLogisticsLevelCount);

/// @brief What a task carts — the load's address. One task a load (boss [9],
///        default 2): how many carts it takes is the plan's to decide.
enum class LogisticsLoadKind : std::uint8_t {
  kFieldHeap = 0,           ///< A field's reaped heap (FieldRow::reaped_grams), arable or meadow.
  kStandLogs,               ///< A stand's felled logs (TimberStandRow::load_grams).
  kSiteDig,                 ///< A pit's dug load (ExtractionSiteRow::load_grams).
  kDistrictLot,             ///< A timber lot at the district for the village's carts.
  kStoreTransfer,           ///< A store being emptied (UnitRow::emptying): the church's.
  kLogisticsLoadKindCount,  ///< NOT A KIND: the count.
};

/// @brief Who made the task.
enum class LogisticsOrigin : std::uint8_t {
  kAuto = 0,  ///< Made by the game as the load appeared (§12, «заводятся сами»).
  kPlayer,    ///< Made by the chairman's order (§12, «Вмешательство председателя»).
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

  /// The tick it entered level 0, for the alarm «Логистика не успевает» — a
  /// task of level 0 waiting for a cart longer than a game hour (§12).
  Tick urgent_since = 0;
};

/// @brief One leg of a cart's day.
struct CartLeg {
  /// Where the leg starts and ends (the horse yard, a load, a store, a point
  /// of the way where somebody boards or gets off).
  Vec2 from;
  Vec2 to;

  /// The task whose load the cart carries on this leg; invalid on an empty
  /// leg (to a load, home in the evening) and on a people's cart.
  LogisticsTaskId task;

  /// The ticks it leaves and arrives, by the way's road and the harness pace.
  /// STUB core, named (boss, the logistics thread [12], default 2): the legs
  /// follow the load's seam and carry no times — both 0 from B3 (0.37.177).
  Tick depart = 0;
  Tick arrive = 0;

  /// Who rides this leg beside the driver — seated at `from` or on the way —
  /// within the cart's seats (transport.csv `seats`: the goods cart's bench
  /// two, the people's cart six). Empty from B3: the passengers of the first
  /// leg are seated by the labour hour (cart_passengers.h, stage A2) and
  /// written nowhere in the plan.
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
