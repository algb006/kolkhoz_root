/// @file
/// @brief The checks of #1764's (A): the first plan's failure forgiven for the theft before
/// acceptance (district_plan.h, FailedOnlyByPreActTheft; JudgePlan).
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_PLAN_CREDIT_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_PLAN_CREDIT_CHECKS_H_

/// @brief Runs every check of the plan's credit for the theft before acceptance.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckThePlanCredit();

#endif  // TESTS_UNIT_CORE_PRODUCTION_PLAN_CREDIT_CHECKS_H_
