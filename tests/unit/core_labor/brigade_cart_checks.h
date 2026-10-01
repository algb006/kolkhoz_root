/// @file
/// @brief The checks of the brigade's cart — the reaping of the arable and
/// the sowing go out on one cart when the day's pool has a horse
/// (core_labor/assignment.h, AssignmentJob::brigade_cart; core_common/
/// work_seam.h) — kept out of the module's main.cpp, which is four times the
/// file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_BRIGADE_CART_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_BRIGADE_CART_CHECKS_H_

/// @brief Runs every check of the brigade's cart.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckBrigadeCart();

#endif  // TESTS_UNIT_CORE_LABOR_BRIGADE_CART_CHECKS_H_
