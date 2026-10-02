/// @file
/// @brief The checks of the carter on foot (core_common/haul.h,
/// WalkerShareOfCartDay; manual/75-logistics.md §9): while the settlement has
/// carts the queue gives a heap its horses and nobody on foot, the hands left
/// with no work carry last and write off their own carry, a log is never
/// carried on a back, and a settlement with no horse carries as it always
/// did — kept out of the module's main.cpp, which is four times the
/// file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_CARTER_ON_FOOT_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_CARTER_ON_FOOT_CHECKS_H_

/// @brief Runs every check of the carter on foot.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckCarterOnFoot();

#endif  // TESTS_UNIT_CORE_LABOR_CARTER_ON_FOOT_CHECKS_H_
