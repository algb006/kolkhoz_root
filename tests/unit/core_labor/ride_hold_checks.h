/// @file
/// @brief The checks of #1205 (c2)'s arm - the windowless hauling leaves the
/// pool's last horse to the people's carts while a site waits and a felling
/// has work (core_labor/assignment.h, AssignmentParams::hold_ride_horse) -
/// kept out of the module's main.cpp, which is over the file-size limit.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_RIDE_HOLD_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_RIDE_HOLD_CHECKS_H_

/// @brief Runs every check of the held ride horse.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckRideHold();

#endif  // TESTS_UNIT_CORE_LABOR_RIDE_HOLD_CHECKS_H_
