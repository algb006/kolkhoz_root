/// @file
/// @brief Internal to core_residents: the free house a household moves into.
/// @threading SINGLE_THREADED
/// Called from the residents' decisions sub-step (phase 3), where every
/// structural change of the settlement is made.
///
/// ONE FAMILY, ONE HOUSE, AND NO HOUSE FROM NOTHING (life-cycle design §11-12;
/// housing design §20). Until 2026-09-14 this header also raised a house from
/// nothing for every wedding and every roofless family that found no free
/// one — a STUB that overrode the rule it stood for, and made the thirty-year
/// population an upper bound rather than a curve (boss, parcels 240, 253,
/// 257). A house now comes only from the chairman's building.

#ifndef CORE_RESIDENTS_HOUSING_H_
#define CORE_RESIDENTS_HOUSING_H_

#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "life_config.h"

namespace core {

/// @brief The first FREE house, in row order: a unit of a housing type,
///        standing (level 1 or more), with no household in it — new, freed,
///        or inherited at the start (life-cycle §12). Invalid when there is
///        none.
UnitId FreeHouse(const LifeConfig& config, const WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_HOUSING_H_
