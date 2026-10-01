/// @file
/// @brief The checks of the harvest rule 2 — the queue between reapings by
/// the food saved per hand-day (core_labor/assignment.h) — kept out of the
/// module's main.cpp, which is four times the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_HARVEST_ORDER_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_HARVEST_ORDER_CHECKS_H_

/// @brief Runs every check of the reapings' order.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckHarvestOrder();

#endif  // TESTS_UNIT_CORE_LABOR_HARVEST_ORDER_CHECKS_H_
