/// @file
/// @brief The checks of a starved herd's death and the fast-forward (0.37.201;
/// core_production/herd_life.h, RunHungerDeaths): a kolkhoz herd's death of
/// hunger is notable — its episode was said interrupting by kHerdWentHungry —
/// and a household's still interrupts. Kept out of the module's main.cpp,
/// which is twelve times the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_HERD_DEATH_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_HERD_DEATH_CHECKS_H_

/// @brief Runs every check of a starved herd's death severity.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckAStarvedKolkhozHerdDoesNotInterruptAgain();

#endif  // TESTS_UNIT_CORE_PRODUCTION_HERD_DEATH_CHECKS_H_
