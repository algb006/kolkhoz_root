/// @file
/// @brief The checks of the meadow's hay lying at the meadow (core_production/
/// field_work.h, LayMownShare; stubs registry row A75): the mown share is
/// laid in the meadow's own heap and carted like a field's, not delivered to
/// the manger and the stores in the hour it is cut — kept out of the module's
/// main.cpp, which is twelve times the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_MEADOW_HAY_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_MEADOW_HAY_CHECKS_H_

/// @brief Runs every check of the meadow's hay.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckMeadowHayLiesAtTheMeadow();

#endif  // TESTS_UNIT_CORE_PRODUCTION_MEADOW_HAY_CHECKS_H_
