/// @file
/// @brief The checks of a plan position's due (#1036; plan_due.h): the split
/// by the sown mix and the district rating's band, one home for the letter
/// and the forecasts. Kept out of the module's main.cpp, which is far over the
/// file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_PLAN_DUE_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_PLAN_DUE_CHECKS_H_

/// @brief Runs every check of the plan's due.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckThePlanDue();

#endif  // TESTS_UNIT_CORE_PRODUCTION_PLAN_DUE_CHECKS_H_
