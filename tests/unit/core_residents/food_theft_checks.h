/// @file
/// @brief The checks of #1568 link 2 (core_residents/food_theft.h): the night
/// theft at an open store and none at a guarded one, the book's gap, the
/// audit's remark before its trigger, the write-off, and the switch.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_RESIDENTS_FOOD_THEFT_CHECKS_H_
#define TESTS_UNIT_CORE_RESIDENTS_FOOD_THEFT_CHECKS_H_

/// @brief Runs every link 2 check.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckFoodTheft();

#endif  // TESTS_UNIT_CORE_RESIDENTS_FOOD_THEFT_CHECKS_H_
