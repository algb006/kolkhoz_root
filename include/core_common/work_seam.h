/// @file
/// @brief The seam a work assignment drains, and the phase-to-kind rule.
/// @threading PARALLEL_READONLY
/// Pure reads of a world plus one assignment. The mutable overload hands
/// back a pointer into the state being written and is for the sequential
/// labour sub-step alone; the const one is for anybody asking what a man is
/// doing, including the boundary between steps.
///
/// WHY IT IS SHARED AND NOT A PRIVATE HELPER OF THE LABOUR MODULE. The seam
/// answers two questions with one body: "how much is there left to drain"
/// (labour) and "is there anything to work with at all" — which is exactly
/// the difference between kIdle and kBlocked, the one part of an idleness
/// signal the player can act on (resident_activity.h). A second copy of
/// these seven cases would drift the day somebody adds a work kind, and the
/// two answers would disagree about the same man in the same hour.

#ifndef CORE_COMMON_WORK_SEAM_H_
#define CORE_COMMON_WORK_SEAM_H_

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/road_route.h"
#include "core_common/world_state.h"

namespace core {

/// @brief The work a field in this phase is waiting for; kNone when the
/// field is growing, idle or fallow.
constexpr WorkKind KindOfPhase(FieldPhase phase) {
  switch (phase) {
    case FieldPhase::kPlowing:
      return WorkKind::kPlowing;
    case FieldPhase::kHarrowing:
      return WorkKind::kHarrowing;
    case FieldPhase::kSowing:
      return WorkKind::kSowing;
    case FieldPhase::kHarvest:
      return WorkKind::kHarvest;
    case FieldPhase::kIdle:
    // Not a phase, and it waits for no work: handled beside the
    // phases that wait for none, so this switch can stay without a
    // default and keep a new phase a compile error.
    case FieldPhase::kFieldPhaseCount:
    case FieldPhase::kGrowing:
      return WorkKind::kNone;
  }
  return WorkKind::kNone;
}

/// @brief The seam this assignment drains, or nullptr when its target is
///        gone or has moved on to work of another kind.
/// @note nullptr is not "nothing left to do": it is "there is no longer
///       anything here to do it TO". A seam that exists and reads zero is
///       the other half of the same story, and the two are told apart by
///       the caller, not here.
const float* WorkSeamOf(const WorldState& world, const WorkAssignment& work);

/// @brief The same seam, writable. Sequential callers only — the labour
///        sub-step drains it hourly in row order.
float* WorkSeamOf(WorldState& world, const WorkAssignment& work);

/// @brief Where the work of this assignment is done: the field's centre, the
///        site, the unit the herd stands at, or a timber stand's loading point.
/// @return false when the target is gone — the same case WorkSeamOf answers
///         with nullptr, and for the same reason.
bool WorkPlaceOf(const WorldState& world, const WorkAssignment& work, Vec2& place);

/// @brief Where a resident's day starts and ends: his family's house, or —
///        for a family in a tent — the plot its house stood on.
/// @return false for a family with neither a house nor a tent: a roofless
///         family the day's structural work has not yet placed, or a
///         hand-built world.
bool HomePositionOf(const WorldState& world, FamilyId family, Vec2& home);

/// @brief Whether a work of this kind on this land goes out on the brigade's
///        ONE cart when the day's pool has a horse for it: the reaping of the
///        arable and the sowing (farming design §6, «Дорога пешком съедает
///        световой день»; AssignmentJob::brigade_cart; 0.37.88).
bool RidesTheBrigadesCart(WorkKind kind, LandKind land);

/// @brief Whether a brigade's cart is out on `field` today: somebody placed
///        on this kind of work there holds the horse (WorkAssignment::
///        rides_horse — the first hand the placement put on the field).
/// @note A scan of the residents; asked for the hands of a reaping, a sowing
///       or a meadow's cut only (its mower, 0.37.168), once a target for the
///       day's road.
bool BrigadeCartIsOut(const WorldState& world, WorkKind kind, FieldId field);

/// @brief Whether this assignment rides THE PEOPLE'S CART today (routing
///        stage A, A3; labor_state.h, TakesThePeoplesCart): its kind walks,
///        and it drives the cart (WorkAssignment::rides_horse) or sits on one
///        whose driver (WorkAssignment::rides_cart_of) still holds his horse
///        on the same work and target. A seat on a GOODS cart is not this
///        (cart_passengers.h): its driver carts, another kind.
/// @note A row lookup of the driver; asked once a target for the day's road.
bool RidesThePeoplesCart(const WorldState& world, const WorkAssignment& work);

/// @brief Whether this assignment's road is measured at harness speed: its
///        kind rides out (labor_state.h, RidesOut), or it is the cut of a
///        meadow whose mower is out today — the one harvest that rides, with
///        a horse mower and hay carts (time design §7; farming design §5) —
///        or it is carting on the horse the day's placement gave
///        (WorkAssignment::rides_horse), or it is a reaping of the arable or
///        a sowing whose brigade has its cart out today (BrigadeCartIsOut) —
///        every hand rides with the driver, and with no driver they walk —
///        or it rides the people's cart (RidesThePeoplesCart).
///
/// THE MOWERS WITH NO HORSE WALK (routing stage A, A4; 0.37.168): the
/// meadow's horse is written on its first mower, as the brigade's cart is on
/// its driver, and with the pool dry when the queue came to the meadow they
/// go with scythes on foot. Until 0.37.168 they rode whether or not a horse
/// was left for them.
///
/// THE LABOUR HOUR AND THE RESIDENT'S ACTIVITY ASK THIS, and the assignment
/// asks the same question of its job (AssignmentJob::harnessed). Until
/// 2026-09-14 the meadow cut rode in the assignment and walked in the hour:
/// mowers were sent by the ride and then lost the ride's hours from their
/// day (boss, parcel 312: "плечо и выработка меряются одной меркой").
bool WorkRidesOut(const WorldState& world, const WorkAssignment& work);

/// @brief By which way this assignment travels (road_route.h, TravelMode;
///        0.36.2): on foot unless it rides out (WorkRidesOut); riding, a
///        carter with logs off a stand as a log cart, any other carter as a
///        cart with produce — roads only (roads design §11) — and the rest
///        (the plough, the harrow, the mower, the reaping or sowing brigade
///        on its one cart — 0.37.89 — and the people's cart, 0.37.168) as a
///        team to its work. ONE ANSWER for the labour hour and the resident's
///        activity, as WorkRidesOut is.
TravelMode WorkTravelMode(const WorldState& world, const WorkAssignment& work);

/// @brief The day's harness as the placements stand (boss-core-epoch1-queue
///        [42], «одна дверь двух потребителей»).
struct HarnessCount {
  /// The horses the placements hold: one a ploughman or harrower, one a
  /// carter the placement gave one (WorkAssignment::rides_horse), one a
  /// MEADOW whose mower is out (the brigade's, written on its first mower),
  /// one a brigade's cart and one a people's cart (on their drivers). May
  /// exceed the herd — the chairman's standing orders can put more men on
  /// the plough than there are horses. A meadow mown with no horse holds
  /// none since 0.37.168 (A4); until then it was counted whether or not one
  /// was left for it.
  std::uint32_t in_traces = 0;

  /// The harnessed assignments, with a horse and without: the ploughmen and
  /// harrowers, every carter on a horse, every driver of a brigade's or a
  /// people's cart, one a meadow whose mower is out — and the
  /// carters ON FOOT ONLY AS FAR AS THE LOAD WANTED A CART: of the walkers at
  /// a load, no more than the cart-days its seam holds beyond its riders.
  /// `in_traces` never exceeds it.
  ///
  /// EVERY CARTER ON FOOT UNTIL 0.37.105, and that was right while the queue
  /// sent a walker only where it wanted a carter. Since 0.37.105 the hands
  /// left with no work carry too (assignment.h, walker_share_of_cart_day):
  /// thirty idle on a rain day would read as thirty cart-days pulled by hand,
  /// light «too few horses» (production_alarms.cpp) — the advice the run's
  /// chairman buys horses on — and sink the mechanisation share the era's
  /// readiness reads. In a settlement with no horse the seam is in a
  /// walker's days and holds every walker the queue sent: they all count, as
  /// before.
  std::uint32_t harnessed = 0;

  /// The part of `in_traces` the morning's release takes off when the herd
  /// is short (labor_system.cpp, ReleaseHorselessWork): the ploughmen, the
  /// harrowers and the carters on a horse; and the horse of a cart that
  /// carries a crew — a brigade's, a people's, a meadow's mower — which the
  /// release takes off the driver and leaves the work: the crew walks, the
  /// mowers go on with scythes.
  std::uint32_t releasable = 0;
};

/// @brief Counts the harness off the day's assignments.
///
/// ONE COUNT FOR THREE READERS: the labour sub-step releases the work the
/// herd cannot carry by `in_traces`; the herd day pays the oats and books the
/// mechanisation share off both halves. Until 0.37.2 the herd day counted
/// the plough and the harrow alone, and a horse in a cart was neither in the
/// traction nor in the oats (econ, horse-traction.md), though the placement
/// takes it out of the pool «exactly as ploughing does».
HarnessCount CountHarness(const WorldState& world);

/// @brief Whether the settlement owns a grown kolkhoz horse at all — and so a
///        cart: the start's canon hands out horses and tackle together (boss,
///        2026-09-03), a draught horse IS a cart of its load.
///
/// ONE PREDICATE FOR TWO MODULES (0.37.105): production writes a load's
/// hauling seam in cart-days while this holds and in a walker's days while
/// it does not; labour gives a carrier on foot a part of a cart-day
/// (haul.h, WalkerShareOfCartDay) while it holds and a whole day while it
/// does not. It lived in core_production alone (field_haul.cpp,
/// DraughtHorsesFree) while only production asked; a second copy in labour
/// would be the seam between two right ladders nobody guards.
/// A household's own horse is not counted, as it never was there.
/// @param horse_kind The livestock row of the horse; an invalid id answers
///        false.
bool SettlementHasCarts(const WorldState& world, LivestockKindId horse_kind);

}  // namespace core

#endif  // CORE_COMMON_WORK_SEAM_H_
