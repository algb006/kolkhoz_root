/// @file
/// @brief The checks of a sown grass stand's age (0.37.212; fields design
/// «три-четыре года берут укосы, пока травостой не выродится»; econ's page
/// clover-stand-age-2026-10-10): the yield by the stand's summer under the
/// table's ceiling, the fertility banked only while the stand is alive, the
/// summer counted at each cut, the stand renewed by the rotation's order and
/// the loss said before it (GrassStandOn).
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_GRASS_STAND_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_GRASS_STAND_CHECKS_H_

/// @brief Runs every check of the grass stand's age.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheGrassStandsAge();

#endif  // TESTS_UNIT_CORE_PRODUCTION_GRASS_STAND_CHECKS_H_
