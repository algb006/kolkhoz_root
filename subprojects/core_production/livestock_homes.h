/// @file
/// @brief The kinds' houses (livestock.csv `home_unit`): how many places a
/// kind's house has free, and the walk that puts a kolkhoz herd with no roof
/// into its house (boss, boss-core-epoch1-5 seq 45-48; livestock design,
/// «Коровник и хлев — разные постройки, свиньи с овцами в хлеву»).
/// @threading SINGLE_THREADED
/// Production decisions sub-step (phase 3), the herd day and the order book,
/// on the sim thread.
///
/// WHY IT EXISTS. Only genesis and StableHorses ever gave a kolkhoz herd a
/// unit. A herd a limit lot founded stood billeted whole for good — and a
/// billeted herd never breeds — even after its house was built: fifteen
/// piglets bought in year 3, none born, no pig left by year 4 on nine seeds of
/// nine (host's plan700 --herd-deaths on 0.35.1).

#ifndef CORE_PRODUCTION_LIVESTOCK_HOMES_H_
#define CORE_PRODUCTION_LIVESTOCK_HOMES_H_

#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Places free for `kind` in its house, summed over every standing
///        unit of the house's type: the level's livestock places less every
///        kolkhoz head standing there, of any kind — the barn is one room for
///        the pigs and the sheep, shared by heads. Never below nought.
/// @return -1 for a kind with no house (LivestockDef::home_unit invalid).
float HomePlacesFree(const ProductionConfig& config, const WorldState& world, LivestockKindId kind);

/// @brief Puts every kolkhoz herd that stands under no roof into its kind's
///        house, the first standing unit of the type with a free place. A herd
///        larger than the free places moves in all the same, and the rest of it
///        is billeted by the herd day (RunBilleting) until room appears.
///
/// NOT THE HORSES: their door is StableHorses — the groom's post, the team
/// gathered from the yards and its stallion — and a second door here would
/// move the team without either.
/// A family's herd and one billeted at a household are not the kolkhoz's to
/// move.
void HouseHomelessHerds(const ProductionConfig& config, WorldState& world);

}  // namespace core

#endif  // CORE_PRODUCTION_LIVESTOCK_HOMES_H_
