/// @file
/// @brief The checks of the oat lamp's thin year by the field plan (#1040;
/// team_oat_plan.h): the request's units, the year's reading, the field-work
/// day and the lamp's by-plan behaviour. Kept out of the module's main.cpp,
/// which is far over the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_TEAM_OAT_PLAN_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_TEAM_OAT_PLAN_CHECKS_H_

/// @brief Runs every check of the oat lamp's thin year.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheTeamsOatPlan();

#endif  // TESTS_UNIT_CORE_PRODUCTION_TEAM_OAT_PLAN_CHECKS_H_
