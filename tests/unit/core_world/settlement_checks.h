/// @file
/// @brief The checks of the settlement step (#1089; settlement_rules.h): the
/// seed, the daily join and the outlying yard, a form by the two turns, a
/// lone far pair that forms nothing (V4), a merge keeping the elder's id (V5),
/// the gong's move and its freeze (V3, V6).
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_WORLD_SETTLEMENT_CHECKS_H_
#define TESTS_UNIT_CORE_WORLD_SETTLEMENT_CHECKS_H_

/// @brief Runs every check of the settlement step.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheSettlements();

#endif  // TESTS_UNIT_CORE_WORLD_SETTLEMENT_CHECKS_H_
