/// @file
/// @brief The checks of the floor under a works' supply (#734; core_common/
///        site_supply_floor.h; assignment.h, AssignmentJob::supply_floor and
///        over_fields): the morning queue gives a supply on the floor carters
///        with horses ahead of the windows, up to the floor's common budget
///        and in the queue's order; nobody on foot; nothing when the loads at
///        level 0 took the team; and «важнее полевых работ» fills it whole.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_SUPPLY_FLOOR_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_SUPPLY_FLOOR_CHECKS_H_

/// @brief Runs every check of the supply's floor.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheSupplyFloor();

#endif  // TESTS_UNIT_CORE_LABOR_SUPPLY_FLOOR_CHECKS_H_
