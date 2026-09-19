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
///        or inherited at the start (life-cycle §12). A house held for a
///        specialist only when `cold` — the roofless family's house, in the
///        months a tent is impossible (boss seq 191). Invalid when none.
UnitId FreeHouse(const LifeConfig& config, const WorldState& current, bool cold);

/// @brief FreeHouse for a NEWCOMER — a wedding couple or a migrant: the same,
///        less an old house on the brink, wear at or above
///        old_house_near_collapse_wear of the scale (boss seq 191; migrants by
///        boss's word the same day), and less a house held for a specialist.
///        The couple waits on in the parents' households, the migrant does
///        not come. Only the roofless still take such a house. Invalid when
///        no other is free.
UnitId FreeHouseNotOnTheBrink(const LifeConfig& config, const WorldState& current);

/// @brief People a unit holds when many families share it (the barrack,
///        housing §9; LifeConfig::residents_capacity at the unit's level); 0
///        for a single family's house, a site or a ruin.
float ResidentsCapacity(const LifeConfig& config, const UnitRow& unit);

/// @brief The first barrack, in row order, with room for `people` more: its
///        capacity less everybody living in it (every family whose house it
///        is). Invalid when none has room. The ladder's second rung (§20),
///        and a couple's or a migrant's roof when no house is free (§9).
UnitId BarrackPlace(const LifeConfig& config, const WorldState& current, std::uint32_t people);

/// @brief A NEWCOMER's roof — a couple's, a migrant's (housing §9 «свадьбы не
///        встают», «переселенцы размещаются»; boss seq 197): a free house not
///        on the brink, else a barrack place for `people`. `shared` says which:
///        true for the barrack, whose `household` the newcomer must not take.
///        Invalid when neither is there.
UnitId HomeForNewcomers(const LifeConfig& config,
                        const WorldState& current,
                        std::uint32_t people,
                        bool& shared);

}  // namespace core

#endif  // CORE_RESIDENTS_HOUSING_H_
