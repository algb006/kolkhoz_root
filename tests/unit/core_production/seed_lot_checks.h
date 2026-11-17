/// @file
/// @brief The checks of the district's seed lot (#1377; core-boss-1377-seed-lot
/// [2]): it delivers the most-short seed's shortfall within 0.5-5 t at
/// pro-rated points, debits them and books the grams; it refuses above the
/// year's cap with the grams the cap allows in the row, with no seed short,
/// and with points short; the year's turn clears the book. Kept out of the
/// module's main.cpp, which is far over the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_SEED_LOT_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_SEED_LOT_CHECKS_H_

/// @brief Runs every check of the seed lot on the shipped tables, the lot
/// priced at 30 points as boss's export will price it.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheSeedLot();

#endif  // TESTS_UNIT_CORE_PRODUCTION_SEED_LOT_CHECKS_H_
