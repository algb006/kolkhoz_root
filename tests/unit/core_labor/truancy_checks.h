/// @file
/// @brief The checks of #1568 link 1, the drinker who does not come out
/// (core_labor/truancy.h): the chance by alcoholism, the switch, and the share
/// of days a man at the cap stays home.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_TRUANCY_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_TRUANCY_CHECKS_H_

/// @brief Runs every truancy check.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTruancy();

#endif  // TESTS_UNIT_CORE_LABOR_TRUANCY_CHECKS_H_
