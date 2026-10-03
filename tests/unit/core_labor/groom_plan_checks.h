/// @file
/// @brief The checks of the labour hour against the groom's plan (routing
/// stage B, B3-B4; core_labor/labor_system.cpp, FollowThePlan and
/// MarkTheUrgentLoads; assignment.h, PlacementTier): a cart whose load is
/// carted goes on to the next load of its chain the same day, and a load at
/// level 0 takes the horse ahead of a ploughing — kept out of the module's
/// main.cpp, which is four times the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_GROOM_PLAN_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_GROOM_PLAN_CHECKS_H_

/// @brief Runs every check of the labour hour against the groom's plan.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheGroomsPlan();

#endif  // TESTS_UNIT_CORE_LABOR_GROOM_PLAN_CHECKS_H_
