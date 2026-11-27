/// @file
/// @brief The checks of #2261 stage 1 (heating.h): the burn's formula and the day's burn from
/// the buffer, the start's seed once, the empty dwelling not stoked.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_RESIDENTS_HEATING_CHECKS_H_
#define TESTS_UNIT_CORE_RESIDENTS_HEATING_CHECKS_H_

/// @brief Runs every check of the heating's stage 1.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheHeatingBurn();

#endif  // TESTS_UNIT_CORE_RESIDENTS_HEATING_CHECKS_H_
