/// @file
/// @brief The checks of the herd plan's contract (#1289; livestock.md «План
/// стада»): the order kSetHerdPlan and its refusals, the herd's places, and
/// the birth gate on top of the roof share.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_HERD_PLAN_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_HERD_PLAN_CHECKS_H_

/// @brief Runs every check of the herd plan's contract.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheHerdPlan();

#endif  // TESTS_UNIT_CORE_PRODUCTION_HERD_PLAN_CHECKS_H_
