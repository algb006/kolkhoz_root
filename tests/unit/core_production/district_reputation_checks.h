/// @file
/// @brief The checks of the rating earned beyond the plan (#1043;
/// district_reputation.h) and the plan's weight (plan_due.h). Kept out of the
/// module's main.cpp, which is far over the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_DISTRICT_REPUTATION_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_DISTRICT_REPUTATION_CHECKS_H_

/// @brief Runs every check of the rating beyond the plan.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheRatingBeyondThePlan();

#endif  // TESTS_UNIT_CORE_PRODUCTION_DISTRICT_REPUTATION_CHECKS_H_
