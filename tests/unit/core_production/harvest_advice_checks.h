/// @file
/// @brief The checks of the store lamp's move (row 936; core_production's
///        production_alarms.cpp, AdviseOnHarvestShortfall): kHarvestWillNotFit
///        advises a new store only when the village's summed shortfall of the
///        resource reaches a third of its smallest store, else the plan door
///        above the plan, else nothing.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_HARVEST_ADVICE_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_HARVEST_ADVICE_CHECKS_H_

/// @brief Runs every check of the store lamp's move.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheStoreLampsMove();

#endif  // TESTS_UNIT_CORE_PRODUCTION_HARVEST_ADVICE_CHECKS_H_
