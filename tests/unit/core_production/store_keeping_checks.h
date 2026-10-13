/// @file
/// @brief The checks of a store's own keeping (#399; unit_levels.csv
/// `keeping_factor`; resources design §4): a vegetable store keeps potatoes
/// three times a clamp's time, the church and the clamp as before, a site
/// and a blank cell at one. Kept out of the module's main.cpp, which is far
/// over the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_STORE_KEEPING_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_STORE_KEEPING_CHECKS_H_

/// @brief Runs every check of the stores' keeping.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheStoresKeepByTheirRoof();

#endif  // TESTS_UNIT_CORE_PRODUCTION_STORE_KEEPING_CHECKS_H_
