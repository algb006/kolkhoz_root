/// @file
/// @brief The checks of the supply seam of a works (routing stage V2,
/// 0.37.204; core_production/field_haul.h, SettleSiteSupply and
/// PriceSiteSupply): the seam is written from what awaits and the way; the
/// carted share lands on the site in its hour; a seam drained to nought
/// brings the last gram; a paused works is not carted to; a load that appears
/// with no seam gets one that hour; and the price asked before the start is
/// the seam's own. Kept out of the module's main.cpp, which is twelve times
/// the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_SITE_SUPPLY_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_SITE_SUPPLY_CHECKS_H_

/// @brief Runs every check of the supply seam.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheSupplyOfAWorksIsCarted();

/// @brief The supply on foot (#671): each resource by its own form's trip,
///        one resource a trip in id order, logs by the pair, the estimate
///        before the start the seam's own.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheHandSupplyGoesOneResourceATrip();

/// @brief The floor's memory (#734; ConstructionState::supply_idle_days): a
///        working evening with nothing landed counts, a day off and a pause
///        neither count nor reset, a day grams landed starts from nought.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheSupplyCountsItsIdleDays();

#endif  // TESTS_UNIT_CORE_PRODUCTION_SITE_SUPPLY_CHECKS_H_
