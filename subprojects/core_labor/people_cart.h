/// @file
/// @brief The people's carts and the lone rider of the day's placement
///        (transport design §1, «Один — верхом, двое и больше — подвода», and
///        §11; routing stage A, A3–A4).
/// @threading SINGLE_THREADED
/// A step of PlanDayAssignments (assignment.h): the labour phase's morning
/// placement and its top-up.
///
/// PRIVATE TO core_labor. In assignment.cpp until 0.37.208; moved when that
/// file reached its limit of a thousand lines.

#ifndef CORE_LABOR_PEOPLE_CART_H_
#define CORE_LABOR_PEOPLE_CART_H_

#include <cstdint>
#include <vector>

#include "assignment.h"

namespace core {

/// @brief The plan's state the people's carts are given from
///        (PlanDayAssignments): read the jobs, the hands and the queue; write
///        the placements, the cover, the carts out, the horses left, and who
///        drives and who rides.
struct PeoplesCartPlan {
  const std::vector<AssignmentJob>* jobs = nullptr;
  const std::vector<AssignmentCandidate>* candidates = nullptr;
  const AssignmentParams* params = nullptr;
  const std::vector<std::uint32_t>* order = nullptr;
  std::vector<std::uint32_t>* result = nullptr;
  std::vector<float>* covered = nullptr;
  std::vector<std::uint8_t>* cart_today = nullptr;
  std::uint32_t* horses_left = nullptr;
  std::vector<std::uint8_t>* rides_horse = nullptr;       ///< nullable
  std::vector<std::uint32_t>* rides_cart_with = nullptr;  ///< nullable
};

/// @brief THE PEOPLE'S CARTS AND THE LONE RIDER: after the queue, from the
///        horses it left, in its order. For every job of a work that takes
///        the people's cart (labor_state.h, TakesThePeoplesCart) its far
///        walkers — placed already, or free and reached only by the ride —
///        are carried: two or more on a cart, the first of each load driving;
///        ONE on the horse itself (0.37.208), written as the driver of a cart
///        of one.
///
///        THE LONE RIDER IS JUDGED BY HIS OWN WAY — on foot to the horse yard
///        and on from it, as the labour hour takes a hand who holds a horse
///        (horse_yard_road.h, WorkRoadHours). Where that way leaves him no
///        working day and a cart's seat would (a passenger rides from his
///        house), a SECOND free far hand is seated beside him and they go as
///        a cart of two; with nobody to seat, the job waits. Until 0.37.208 a
///        crew of one got nothing at all, and a far job whose remaining work
///        one hand covers was manned on no morning.
/// @pre  Every pointer of `plan` but the two marked nullable is set; the
///       vectors are sized to the jobs and the candidates.
/// @post Side effects, all through `plan`: result, covered, cart_today,
///       horses_left (one a cart, one a rider), rides_horse on each driver
///       and rider, rides_cart_with on each passenger.
void GivePeoplesCarts(const PeoplesCartPlan& plan);

}  // namespace core

#endif  // CORE_LABOR_PEOPLE_CART_H_
