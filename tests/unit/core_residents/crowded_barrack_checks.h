/// @file
/// @brief The checks of row 456 (core_residents' housing.h, demography.cpp,
///        housing_ladder.cpp): no wedding into a barrack place (the human's
///        «Нет», 11 October 2026), and «Тесный барак» - families in a barrack
///        over its places light the housing lamp's (a) and climb into a free
///        house first.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_RESIDENTS_CROWDED_BARRACK_CHECKS_H_
#define TESTS_UNIT_CORE_RESIDENTS_CROWDED_BARRACK_CHECKS_H_

/// @brief Runs every check of row 456.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckNoWeddingIntoABarrackAndTheCrowdedBarrack();

#endif  // TESTS_UNIT_CORE_RESIDENTS_CROWDED_BARRACK_CHECKS_H_
