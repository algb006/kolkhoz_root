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
/// @note A scan of the residents; asked for the hands of a reaping or a
///       sowing only, once a target for the day's road.
bool BrigadeCartIsOut(const WorldState& world, WorkKind kind, FieldId field);

/// @brief Whether this assignment's road is measured at harness speed: its
///        kind rides out (labor_state.h, RidesOut), or it is the cut of a
///        meadow — the one harvest that rides, with a horse mower and hay
///        carts (time design §7; farming design §5), or it is carting on
///        the horse the day's placement gave (WorkAssignment::rides_horse),
///        or it is a reaping of the arable or a sowing whose brigade has its
///        cart out today (BrigadeCartIsOut) — every hand rides with the
///        driver, and with no driver they walk.
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
///        (the plough, the harrow, the mower, the fellers) as a team to its
///        field work. ONE ANSWER for the labour hour and the resident's
///        activity, as WorkRidesOut is.
TravelMode WorkTravelMode(const WorldState& world, const WorkAssignment& work);

/// @brief The day's harness as the placements stand (boss-core-epoch1-queue
///        [42], «одна дверь двух потребителей»).
struct HarnessCount {
  /// The horses the placements hold: one a ploughman or harrower, one a
  /// carter the placement gave one (WorkAssignment::rides_horse), one a
  /// MEADOW for its mowers (the brigade's, not the mower's). May exceed the
  /// herd — the chairman's standing orders can put more men on the plough
  /// than there are horses, and a meadow's horse is counted whether or not
  /// one was left for it.
  std::uint32_t in_traces = 0;

  /// The harnessed assignments, with a horse and without: the ploughmen and
  /// harrowers, EVERY carter — on the horse or on foot — and one a meadow
  /// being mown. `in_traces` never exceeds it.
  std::uint32_t harnessed = 0;

  /// The part of `in_traces` the morning's release takes off when the herd
  /// is short (labor_system.cpp, ReleaseHorselessWork): the ploughmen, the
  /// harrowers and the carters on a horse — not a meadow, whose mowers go
  /// on with scythes.
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

}  // namespace core

#endif  // CORE_COMMON_WORK_SEAM_H_
