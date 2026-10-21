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
#include "core_common/quantities.h"
#include "core_common/state_table.h"
#include "core_common/trip_terms.h"

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
  /// THE MILK LEG (#1056 C1.1; econ's stage C contract 574c366; boss, the
  /// #1056 thread [4], [7]): a kolkhoz herd's milk carted from its barn
  /// (the herd's unit stock) to the store the village keeps milk in, every
  /// day the barn holds it - herd care, so worked on rest days. Addressed by
  /// `herd`; its seam is HerdRow::leg_days_remaining. Seam key `milk_leg`.
  kMilkLeg,
  /// A RICK'S SUPPLY (#1056 C1.2; econ's stage C contract 574c366): hay
  /// carted from another rick to the rick named in `unit`, while its herds'
  /// hay stands below its threshold (UnitRow::rick_supply; the task's
  /// `threshold_days`). Level 2 by its kind; the urgent rule's level 0 is the
  /// job's, as for a meadow's stack (labor_system.cpp). Seam key `rick_supply`.
  kRickSupply,
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

/// @brief WHY A TASK STANDS AT LEVEL 0 (#1099, save 174; econ
/// standing-crop-before-keeping-heap-2026-10-19): labour keeps a load above
/// every window only for some reasons. A heap urgent by its own rot gives way
/// to a standing crop whose snow comes before the heap's shelf ends; the
/// player's order and a hungry herd never do.
enum class LogisticsUrgentBy : std::uint8_t {
  kNone = 0,  ///< Not at level 0.
  kSpoil,     ///< A field heap that rots by tomorrow's share (HeapSpoils).
  kHunger,    ///< A feed heap while a kolkhoz herd is hungry.
  kPlayer,    ///< The chairman's level 0 (base_level kUrgent).
  kAged,      ///< Level 0 by its kind's own level or its days waiting (AgedLevel).

  /// NOT A REASON: the count, for the codecs' range.
  kLogisticsUrgentByCount,
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
  /// kMilkLeg: the herd row the barn's leg is written on (save 170) - the
  /// barn's first MILKED kolkhoz herd: the cows' while they stand billeted in
  /// the stable with the horses (milk_cart.cpp, «ONE LEG A BARN»; #1117). The
  /// milk is the barn's; on the day the cows leave for their yard the
  /// leftover's leg stays on the barn's first herd, of any kind.
  HerdId herd;

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
  /// has stood at level 0 - or, a rick's supply, at any level (#1056 C1.2,
  /// the wide alarm) - with nobody on it, since it was last served — an
  /// hour of light unserved adds one, an hour served sets it to nought, a task
  /// below level 0 keeps nought. Night hours add nothing: nobody carts by
  /// night, and a lamp lit over a load carted all day, because its carters
  /// went home, was the lamp lying (0.37.191's pair B9: lit 13, 9 and 18 days
  /// of years 1, 4 and 5 with no event of a wait).
  std::uint16_t unserved_light_hours = 0;

  /// «ВАЖНЕЕ ПОЛЕВЫХ РАБОТ» (#734, save 158; transport design «Пол подвоза»):
  /// the chairman's switch on a works' supply (kSiteSupply alone) - its job
  /// stands ahead of every job with a window WHOLE, behind the loads at level
  /// 0, not for the floor's share (core_common/site_supply_floor.h). Written
  /// by kSetLogisticsLevel (`enable`, order_state.h); gone with the task when
  /// the recipe is on the site - a task is its load's, and a works given more
  /// later starts a plain one. Sits in the padding after the lamp's clock.
  bool over_fields = false;

  /// THE RICK'S THRESHOLD (#1056 C1.2, save 171): for kRickSupply, the days
  /// of its herds' hay the rick was priced against (UnitRow::rick_supply,
  /// threshold_days), copied as the task is synced; 0 for every other kind.
  /// Sits in the padding after over_fields.
  std::uint8_t threshold_days = 0;

  /// Why `level` is kUrgent this day (#1099, save 174), written beside it by
  /// the morning's ageing (logistics_tasks.cpp); kNone below level 0.
  LogisticsUrgentBy urgent_by = LogisticsUrgentBy::kNone;

  /// WHO DRAINED THE LOAD'S SEAM since its last settle (#671's instrument;
  /// save 155): the seam units carriers on foot and carts took off it, added
  /// by the labour hour as it drains (hauled_book.h, NoteHaulDrain) and spent
  /// by production's settle as it converts them, which splits the grams it
  /// moves between YearLedger::hauled_on_foot and hauled_by_cart by these two
  /// (FootShareOf, SpendHaulNotes). A unit of the seam is a cart-day while the settlement
  /// has carts and a walker's day while it has none — the split is of the
  /// seam, so the grams follow the units whatever they are.
  float drained_on_foot = 0.0F;
  float drained_by_cart = 0.0F;

  /// THE TRIP'S FACTS (#1056 C3; core_common/trip_terms.h): written by
  /// production's evening for every open task a cart prices, read by the
  /// groom's plan, which draws whole trips from them and writes the task's
  /// seam (CartTripModel::kPlanned). All zero under every other model, on
  /// foot, and for a task the evening has not priced yet.
  TaskTripTerms trip_terms = TaskTripTerms{};

  /// CARRIED ALONG ANOTHER TASK'S TRIPS today (C3, rulings 3-4): the task
  /// whose cart takes this load too - a field heap's leftover pooled into
  /// another field's last load, or a back load on another load's way home.
  /// Invalid when the load goes by its own trips. Written by the plan.
  LogisticsTaskId carried_by = LogisticsTaskId{};

  /// What THIS task's seam drains for each cart-day the carrier's seam drains
  /// (work_seam.h, CoDrainCarriedTasks): this task's priced share of the
  /// shared legs over the carrier's own drawn cart-days. 0 when not carried.
  float carried_share = 0.0F;
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
///
/// SINCE C3 THE DRAWN TRIPS ARE THE PRICE (CartTripModel::kPlanned;
/// core_common/trip_terms.h): the plan draws WHOLE trips from the task's
/// TaskTripTerms (a cart's load each, the price's own round trip and heap
/// point) and writes the task's seam from them. Two more legs it may draw:
///   * a POOLED trip (ruling 3): a field heap's last part-load takes another
///     field's leftover on the way to the store - the leg from the first heap
///     to the second is the first task's, loaded; the leg from the second to
///     the store carries both, `pooled_task` naming the second;
///   * a BACK LOAD (ruling 4): at a receiver, instead of the way back empty,
///     a loaded leg of another task whose load lies near - an ordinary loaded
///     leg of that task on this cart's chain.
/// Under every other model the trips are drawn as before and price nothing.
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

  /// 1 when the cart went out as a SLEDGE (SL; transport design §16 «Телега
  /// и сани — одно и то же имущество, меняющее обличье по наличию снега… на
  /// стоянке во дворе конюшни»): the day it left the stable lay under snow
  /// (core_common/cart_gear.h, SledgeDay). The layer's visual; save 175.
  std::uint8_t sledge = 0;

  /// THE TRIP'S ONE RESOURCE (#671, porter_leg.h; save 155 with the
  /// implementation): for a carrier on foot, the one resource his back holds
  /// on this trip and its way back - one resource a trip, a works' supply
  /// walked in resource-id order. For a cart, the first resource the leg's
  /// load carries (the cart may mix; its look is CargoFormOfLeg's). Invalid
  /// on a ride and a people's cart.
  ResourceId resource = ResourceId{};

  /// THE TRIP'S GRAMS FOR ONE CARRIER (#671; save 155): on a loaded leg of a
  /// carrier on foot, what his back holds - at most WalkerTripGrams of the
  /// resource's form and his hold, less on a load's last trip; half the pair's
  /// load for a stretcher or a log. 0 on an empty leg, and on every leg of a
  /// cart (its load is the seam's, not a back's).
  Grams carried = 0;

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

  /// THE SAME MOMENTS TO THE MINUTE (#1056 C3, T1; save 178): minutes from the
  /// midnight of the plan's day, as the plan's clock had them before the ticks
  /// floored them to the hour. The labour hour drains a priced load by them
  /// (CartHourOf's overlap). Until T1 a half of 0.6 h begun at 8.2 h was the
  /// leg 8->8, no time at all, and the legs held 0.78 of the hours the plan
  /// priced (core-boss-oats-b-pair [98]). The floored ticks stay for every
  /// reader that counts hours. CollectPorterLegs (leg_loads.h) still sends
  /// the ticks and has no reader in the layer yet (ue, 21 October 2026): the
  /// reader to come takes these minutes.
  std::uint16_t depart_minute = 0;
  std::uint16_t arrive_minute = 0;

  /// 1 on a load of the chain the cart never carted, kept in its place with
  /// no time on it (KeepInTheChain; save 178). Until T1 «no time» was read as
  /// depart == arrive, which a real trip floored to the hour also is.
  std::uint8_t kept_in_chain = 0;

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

  /// THE SECOND LOAD OF A POOLED LEG (#1056 C3, ruling 3): on the loaded leg
  /// from the second field's heap to the store, the second field's task
  /// beside `task`; invalid on every other leg. The layer shows both loads.
  LogisticsTaskId pooled_task = LogisticsTaskId{};
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

  /// THE OTHER OF A PAIR (#671; transport design §2; save 155): for a carrier
  /// on foot on a stretcher (manure) or under a log (logs by two,
  /// LogPairMayCarry), the other carrier, whose plan has the same legs with
  /// the same ticks and names this one back. Invalid alone - every cart, every
  /// other form, and the odd hand on manure, who carries it as bulk.
  ResidentId partner = ResidentId{};

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
