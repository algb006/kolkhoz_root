/// @file
/// @brief Who is at a funeral today (#1084; funeral_state.h): the one read the
/// labour's morning and the whereabouts make of the funerals table.
/// @threading PARALLEL_READONLY
/// Reads WorldState::funerals only; any phase may call it.

#ifndef CORE_COMMON_FUNERAL_READS_H_
#define CORE_COMMON_FUNERAL_READS_H_

#include "core_common/funeral_state.h"
#include "core_common/ids.h"

namespace core {

struct WorldState;

/// @brief The funeral `resident` walks with today, if any.
/// @param kin Set true for the kin and the household (off the whole day),
///        false for a neighbour (the procession's hours, then work).
/// @return The funeral's row in world.funerals, or kNoRow when he is at none
///         buried today (social.md «Нерабочий день»).
std::uint32_t FuneralOfToday(const WorldState& world, ResidentId resident, bool& kin);

/// @brief A neighbour's hours at the funeral: there and back on foot by the
///        procession's road and the ceremony (social.md «Цена — по
///        расстоянию»; STUB: everybody walks, the carts are not asked yet).
float FuneralNeighbourHours(const FuneralRow& funeral, float walk_hours_per_km);

/// The ceremony at the cemetery, game hours (STUB, econ R5).
inline constexpr float kFuneralCeremonyHours = 1.0F;

/// The party is back from the cemetery by this hour of the burial day: the
/// wake is «после полудня» (social.md). STUB for the whereabouts only.
inline constexpr float kFuneralBackByHour = 12.0F;

}  // namespace core

#endif  // CORE_COMMON_FUNERAL_READS_H_
