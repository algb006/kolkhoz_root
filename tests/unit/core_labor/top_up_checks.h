/// @file
/// @brief The checks of the hour-1 top-up against the work that has no window
/// (core_labor/labor_system.cpp, TopUpDay; assignment.h, PlacementTier): a
/// phase production opens after the morning takes the horses and the hands
/// the morning gave to a district's lot, a stand's logs or a planting — kept
/// out of the module's main.cpp, which is four times the file-size limit
/// already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_TOP_UP_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_TOP_UP_CHECKS_H_

/// @brief Runs every check of the top-up against windowless work.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTopUpAgainstWindowlessWork();

#endif  // TESTS_UNIT_CORE_LABOR_TOP_UP_CHECKS_H_
