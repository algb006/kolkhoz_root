/// @file
/// @brief The checks of #1778's day orders (core_labor/day_order_book.h): a crew order fills
///        exactly N by the chairman's placement level and the read returns those names; day 1
///        orders, day 2 repeats from yesterday's read - the same N on the same work; a name
///        written by kAssignDayWork or released leaves the crew; fewer free than N fills what
///        there is; a day off moves the order to the next working day; SeasonWorks reads the
///        job's own window; CrewGapsToday names a job left short.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_DAY_ORDER_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_DAY_ORDER_CHECKS_H_

/// @brief Runs every day-order check.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckDayOrders();

#endif  // TESTS_UNIT_CORE_LABOR_DAY_ORDER_CHECKS_H_
