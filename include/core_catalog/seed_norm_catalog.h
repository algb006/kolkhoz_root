/// @file
/// @brief The seed norms of crops.csv as the fund ladder reads them
///        (core_common/fund_ladder.h, SeedNorm): one reading of the table.
/// @threading PARALLEL_READONLY
/// Parsed once at assembly on the sim thread; read-only afterwards.
///
/// WHY IN THE CATALOGUE (boss-core-seed-ladders [1]-[2]; 0.36.34). The seed
/// rung of the fund ladder and the delivery door keep one rule
/// (SeedHeldByField), and the rule reads the norms: the sowing norm, winter or
/// spring, the last sowing month and the reaping months. core_residents built
/// its norms from the table here and core_production from its own crop
/// definitions — two readings of one table, a seam a rule change would open
/// again. core_residents reads them here; core_production's builder
/// (seed_room.h, SeedNormsOf) is checked against this reading on the shipped
/// tables by its unit test.

#ifndef CORE_CATALOG_SEED_NORM_CATALOG_H_
#define CORE_CATALOG_SEED_NORM_CATALOG_H_

#include <string>
#include <vector>

#include "core_common/fund_ladder.h"
#include "core_tables/tables.h"

namespace core {

/// @brief Reads crops.csv's seed columns into `norms`, dense by CropId (one
///        entry a crop row): `resource` (a key of resources.csv),
///        `sowing_norm_kg_per_ha`, `is_winter` (any non-zero), and the months
///        `sow_to_month`, `harvest_from_month`, `harvest_to_month` — 1-based
///        in the table, 0-based here; blank, absent or 0 is kNoSowingMonth.
/// @param resources The resource roster; null passes every resource cell (a
///        stub table set, legal).
/// @return false with `error` naming the table and column for a
///         present-and-wrong cell, or a resource the roster does not carry.
bool ReadSeedNorms(const ITable& crops,
                   const ITable* resources,
                   std::vector<SeedNorm>& norms,
                   std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_SEED_NORM_CATALOG_H_
