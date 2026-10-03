/// @file
/// @brief The checks of the day with a horse (core_common/horse_yard_road.h):
/// once the team is stabled, a ploughman and a carter walk from home to the
/// horse yard and ride from there (livestock design §5) — the ploughman's
/// road is walk and ride, the carter's is the walk alone. Before the team is
/// stabled, and for a yard at the house, the day is as it was.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_HORSE_YARD_ROAD_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_HORSE_YARD_ROAD_CHECKS_H_

/// @brief Runs every check of the day with a horse.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheHorseYardRoad();

#endif  // TESTS_UNIT_CORE_LABOR_HORSE_YARD_ROAD_CHECKS_H_
