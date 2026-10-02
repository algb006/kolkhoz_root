/// @file
/// @brief The checks of the carter's road (labor_system.cpp, the hour of
/// work): the road from home to the load is the empty half of the carter's
/// first round trip, which the load's seam has priced already — it is not
/// taken from his day a second time. A ploughman's road to his field is a
/// road and is taken, as before. Kept out of the module's main.cpp, which is
/// four times the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_CARTER_ROAD_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_CARTER_ROAD_CHECKS_H_

/// @brief Runs every check of the carter's road.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheCartersRoad();

#endif  // TESTS_UNIT_CORE_LABOR_CARTER_ROAD_CHECKS_H_
