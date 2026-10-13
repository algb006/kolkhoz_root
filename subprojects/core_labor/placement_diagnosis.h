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
};

/// @brief Fills `diagnosis`: the jobs' shortfalls, the released hands, and
///        for each idle candidate the first reason that holds over the jobs
///        left short (IdleReason's order, kUnexplained ahead of all).
/// @pre Every pointer of `plan` is set; the vectors are sized to the jobs
///      and the candidates.
void DiagnosePlacement(const PlacedPlan& plan, PlacementDiagnosis& diagnosis);

}  // namespace core

#endif  // CORE_LABOR_PLACEMENT_DIAGNOSIS_H_
