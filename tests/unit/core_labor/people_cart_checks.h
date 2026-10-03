/// @file
/// @brief The checks of the people's cart — two or more hands to one far
/// object ride a cart from the horses the plough and the goods carts left
/// (core_labor/assignment.h, PlanDayAssignments; core_common/work_seam.h,
/// RidesThePeoplesCart; routing stage A, A3) — and of the two «ride free»
/// brought to it: the fellers and the mowers with no horse walk (A4).
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_PEOPLE_CART_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_PEOPLE_CART_CHECKS_H_

/// @brief Runs every check of the people's cart.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckThePeoplesCart();

#endif  // TESTS_UNIT_CORE_LABOR_PEOPLE_CART_CHECKS_H_
