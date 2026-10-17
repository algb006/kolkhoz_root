// Why each idle hand stands idle, asked of the plan's own rules (assignment.h,
// PlacementDiagnosis). Split out of assignment.cpp when the supply's floor
// (#734) took that file past the thousand-line limit: the diagnosis reads the
// finished plan and writes none of it.
// @threading SINGLE_THREADED (the morning placement, decisions phase).

#ifndef CORE_LABOR_PLACEMENT_DIAGNOSIS_H_
#define CORE_LABOR_PLACEMENT_DIAGNOSIS_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "assignment.h"

namespace core {

/// @brief The finished plan, as the diagnosis reads it.
struct PlacedPlan {
  const std::vector<AssignmentJob>* jobs = nullptr;
  const std::vector<AssignmentCandidate>* candidates = nullptr;
  const AssignmentParams* params = nullptr;
  /// Per candidate: his job's index, or kNoJobAssigned.
  const std::vector<std::uint32_t>* result = nullptr;
  /// Per job: why its work was not covered, as the queue wrote it.
  const std::vector<std::optional<JobShortfall>>* shortfall = nullptr;
  /// Per job: 1 when its brigade rode a cart today.
  const std::vector<std::uint8_t>* cart_today = nullptr;
  /// Per job: 1 for a meadow's cut whose mowers walked.
  const std::vector<std::uint8_t>* mowers_walk = nullptr;
  /// Per candidate: 1 for a hand the care's re-pricing took off.
  const std::vector<std::uint8_t>* released = nullptr;
  /// The horses the queue and the people's carts left over (#1034).
  std::uint32_t horses_left = 0;
};

/// @brief Fills `diagnosis`: the jobs' shortfalls, the released hands, and
///        for each idle candidate the first reason that holds over the jobs
///        left short (IdleReason's order, kUnexplained ahead of all).
///
///        A RIDE'S WORK LEFT FOR WANT OF A HORSE IS kNoHorse, NOT kRoad
///        (#1034; boss, the felling-offer thread [1]): a work of the people's
///        cart (TakesThePeoplesCart) the queue marked kRoad — every walker
///        turned away — with nobody placed, no cart out and no horse left,
///        that a ride would have sent a free hand to, is filed kNoHorse, and
///        its idle hands with it. The label only: the plan is not touched.
///        Until 0.37.274 the day read as the road's while the horses had gone
///        to the queue's horse works (one canon village, 12 years: 75 of 140
///        felling mark-days short of hands).
/// @pre Every pointer of `plan` is set; the vectors are sized to the jobs
///      and the candidates.
void DiagnosePlacement(const PlacedPlan& plan, PlacementDiagnosis& diagnosis);

}  // namespace core

#endif  // CORE_LABOR_PLACEMENT_DIAGNOSIS_H_
