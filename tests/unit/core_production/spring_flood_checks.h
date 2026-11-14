/// @file
/// @brief The checks of the spring flood (#1375; spring_flood.h, core_common/flood_state.h):
/// its day, what it takes, its projected day and its lamp.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_SPRING_FLOOD_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_SPRING_FLOOD_CHECKS_H_

/// @brief Runs every check of the spring flood.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheSpringFlood();

#endif  // TESTS_UNIT_CORE_PRODUCTION_SPRING_FLOOD_CHECKS_H_
