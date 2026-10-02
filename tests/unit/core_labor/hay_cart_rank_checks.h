/// @file
/// @brief The checks of the rank of the meadow's hay cart (labor_system.cpp,
/// HaulWindow): hay lying mown at a meadow does not spoil and its carting is
/// windowless, below the fallow for this autumn's winter crop — as a stand's
/// logs since 0.36.19; an arable field's load keeps its window. Kept out of
/// the module's main.cpp, which is four times the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_HAY_CART_RANK_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_HAY_CART_RANK_CHECKS_H_

/// @brief Runs every check of the hay cart's rank.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheHayCartsRank();

#endif  // TESTS_UNIT_CORE_LABOR_HAY_CART_RANK_CHECKS_H_
