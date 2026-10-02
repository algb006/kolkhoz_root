/// @file
/// @brief The checks of the cart of the compressed year (core_common/haul.h;
/// transport.csv, `load_scale`): the labour's reading of a cart's load is
/// the table's tonnes times its scale, the same number production prices a
/// trip by — kept out of the module's main.cpp, which is four times the
/// file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_CART_LOAD_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_CART_LOAD_CHECKS_H_

/// @brief Runs every check of the labour's cart load.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheCartOfTheCompressedYear();

#endif  // TESTS_UNIT_CORE_LABOR_CART_LOAD_CHECKS_H_
