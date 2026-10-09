/// @file
/// @brief The placement's two measures of a hand for a job — his one way and
///        the ranked list of the free — for the parts of the day's plan that
///        live outside assignment.cpp (people_cart.cpp).
/// @threading SINGLE_THREADED
/// Pure functions of their arguments; called from the labour phase's morning
/// placement and its top-up.
///
/// PRIVATE TO core_labor. Split off assignment.cpp at 0.37.208, when the
/// people's carts left it for a file of their own (the file stood at its
/// limit of a thousand lines); the definitions stay in assignment.cpp, beside
/// the judgement of a candidate they are made of.

#ifndef CORE_LABOR_PLACEMENT_RANK_H_
#define CORE_LABOR_PLACEMENT_RANK_H_

#include <cstdint>
#include <vector>

#include "assignment.h"

namespace core {

/// @brief A worker considered for one job, with everything the pick order needs.
struct RankedPick {
  std::uint32_t candidate_index = 0;

  std::uint32_t resident_row = 0;

  /// Norm man-days this worker would deliver on this job today.
  float daily_norm = 0.0F;

  float score = 0.0F;

  /// Sort-first flag on horse works from level 1 up: a horse-locked
  /// resident is useless anywhere else, so spending him here preserves
  /// everyone else's flexibility.
  bool prefer = false;
};

/// @brief One way from a candidate's home to a job, game hours: by the roads
///        when the caller measured them (AssignmentParams::road_km; 0.36.2) —
///        the way the labour hour will measure his day by — and the straight
///        line otherwise.
/// @param rides       Measured at the harness pace, by the job's riding way.
/// @param takes_horse He holds the horse himself: on foot to the horse yard
///        first, and on from it (AssignmentParams::yard_walk_hours and
///        yard_ride_km; 0.37.158), when the caller measured the yard.
float OneWayHours(const AssignmentJob& job,
                  std::uint32_t job_index,
                  const AssignmentCandidate& candidate,
                  const AssignmentParams& params,
                  bool rides,
                  bool takes_horse);

/// @brief Everyone still free who may and can reach this job today, best
///        first.
/// @param result       The plan so far: kNoJobAssigned marks the free.
/// @param road_refused Out: how many free workers the road rule alone turned
///        away.
std::vector<RankedPick> RankCandidates(const AssignmentJob& job,
                                       std::uint32_t job_index,
                                       const std::vector<AssignmentCandidate>& candidates,
                                       const std::vector<std::uint32_t>& result,
                                       const AssignmentParams& params,
                                       std::uint32_t& road_refused);

}  // namespace core

#endif  // CORE_LABOR_PLACEMENT_RANK_H_
