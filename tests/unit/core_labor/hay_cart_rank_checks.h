/// @file
/// @brief The checks of the rank of the meadow's hay cart (labor_system.cpp,
/// the field's load; LaborConfig::hay_cart_need_days): the hay lying mown at
/// a meadow goes by the manger's need. While the stores hold the days of hay
/// ahead its carting has no window and is the last of the carts with none —
/// behind the fallow for this autumn's winter crop (0.37.128) and behind a
/// stand's logs (0.37.132); with the stores short it has a field load's
/// window and goes before both. An arable field's load keeps its window
/// whatever the manger says. Kept out of the module's main.cpp, which is
/// four times the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_HAY_CART_RANK_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_HAY_CART_RANK_CHECKS_H_

/// @brief Runs every check of the hay cart's rank.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheHayCartsRank();

#endif  // TESTS_UNIT_CORE_LABOR_HAY_CART_RANK_CHECKS_H_
