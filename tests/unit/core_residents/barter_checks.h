/// @file
/// @brief The checks of the yards' exchange at the barter counter
/// (core_residents/barter.h), kept out of the module's main.cpp, which is
/// four times the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_RESIDENTS_BARTER_CHECKS_H_
#define TESTS_UNIT_CORE_RESIDENTS_BARTER_CHECKS_H_

/// @brief Runs every check of the exchange.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckBarter();

#endif  // TESTS_UNIT_CORE_RESIDENTS_BARTER_CHECKS_H_
