/// @file
/// @brief The checks of the harvest rule 1 — the harvest without days off:
/// the door (core_common/day_off.h) and the chairman's switch
/// (core_labor/rush.h) — kept out of the module's main.cpp, which is four
/// times the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_HARVEST_DAYS_OFF_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_HARVEST_DAYS_OFF_CHECKS_H_

/// @brief Runs every check of the harvest without days off.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckHarvestDaysOff();

#endif  // TESTS_UNIT_CORE_LABOR_HARVEST_DAYS_OFF_CHECKS_H_
