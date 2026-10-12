// The care's re-pricing after the people's carts (#709; boss, 12 October 2026).
// @threading SINGLE_THREADED (the morning placement, decisions phase).
//
// WHY: the accountant fills a herd's care up to its demand by his own estimate
// of a hand's day - and he prices the hand ON FOOT, because the people's carts
// are given only AFTER the main pass, from the horses it left
// (people_cart.h). Seed 1936, year 1, the cattle yard 954 m out: demand 3.71
// man-days, a hand priced at 0.10-0.63 of a day, 9 -> 17 hands placed; then
// 8-16 of them rode the cart, worked full hours and drained the care by noon,
// while a ripe potato field stood with nobody on it (econ's split14 §3a,
// host's print for #698). The estimate and the delivery were priced on two
// different ways.
//
// THE RULE (boss's ruling; time design «Пока не приступили — можно менять»:
// until a hand begins, the placement may change him freely): after the
// people's carts, each care job's hands are priced again by the way each
// actually travels - riding if a cart carries him, on foot if not. Those
// beyond the demand by that price come off the care, the lowest-ranked first
// and never a cart's driver (he holds the horse the others ride), and the
// caller offers them to the jobs below in the morning queue's own order (the
// second pass of PlanDayAssignments). No hour-1 or noon placement.

#ifndef CORE_LABOR_CARE_REPRICE_H_
#define CORE_LABOR_CARE_REPRICE_H_

#include <cstdint>
#include <vector>

#include "assignment.h"

namespace core {

/// @brief What the re-pricing reads and writes.
struct CareRepricePlan {
  const std::vector<AssignmentJob>* jobs = nullptr;
  const std::vector<AssignmentCandidate>* candidates = nullptr;
  const AssignmentParams* params = nullptr;
  std::vector<std::uint32_t>* result = nullptr;
  std::vector<float>* covered = nullptr;
  const std::vector<std::uint8_t>* cart_today = nullptr;
  const std::vector<std::uint8_t>* rides_horse = nullptr;  ///< nullable: then nobody drives
  std::vector<std::uint32_t>* rides_cart_with = nullptr;   ///< nullable: then nobody rides
  /// Out, sized to the candidates: 1 for each hand taken off a care.
  std::vector<std::uint8_t>* released = nullptr;
};

/// @brief Re-prices every herd care's crew by the way each hand travels and
///        takes off the hands beyond its demand (see the file's comment).
///        A care's `covered` becomes its crew's re-priced output.
/// @return How many hands were taken off, all cares together.
/// @pre Every pointer but the two marked nullable is set; the vectors are
///      sized to the jobs and the candidates.
std::uint32_t RepriceCare(const CareRepricePlan& plan);

}  // namespace core

#endif  // CORE_LABOR_CARE_REPRICE_H_
