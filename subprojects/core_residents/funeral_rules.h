/// @file
/// @brief The funeral step (#1084; core_common/funeral_state.h): a funeral
/// opened the death day, the party off its burial day, the chairman's order,
/// the effects at its close.
/// @threading SINGLE_THREADED
/// Run by core_residents in its sequential decisions sub-step, after the
/// demography (the day's deaths) and before labour places the next morning.
///
/// CONTRACT ONLY in this delivery's first commit: the declarations, no body.

#ifndef CORE_RESIDENTS_FUNERAL_RULES_H_
#define CORE_RESIDENTS_FUNERAL_RULES_H_

#include <cstdint>
#include <vector>

#include "core_common/funeral_state.h"
#include "core_common/ids.h"
#include "core_common/world_state.h"

namespace core {

/// @brief What the funeral step needs of the tables and the life config.
struct FuneralConfig {
  /// Dense by UnitTypeId: 1 for a cemetery.
  std::vector<std::uint8_t> cemetery;

  /// The adult age (biological years) and the life speed-up: who is an
  /// adult of the party, and who is a child for the loud rule.
  float adult_from_years = 16.0F;
  float life_speedup = 4.0F;

  /// Grief and presence, on the metrics' own scales (STUB, social.md): the
  /// kin's mood on the death day and its recovery days; presence's mood and
  /// days, the household's satisfaction and days; absence's two.
  float grief_mood = -10.0F;
  std::uint32_t grief_days = 8;
  float presence_mood = 10.0F;
  std::uint32_t presence_days = 8;
  float presence_satisfaction = 3.0F;
  float absence_mood = -8.0F;
  float absence_satisfaction = -3.0F;
};

/// @brief The party of a death: the dead's adult kin within two hops and his
///        household but small children (first, `kin_count` of them), then
///        one adult of every other household within kNeighbourMetres of his
///        house in his settlement.
/// @return The residents, kin first; `kin_count` set.
std::vector<ResidentId> FuneralParty(const FuneralConfig& config,
                                     const WorldState& world,
                                     ResidentId deceased,
                                     std::uint32_t& kin_count);

/// @brief The cemetery for the same-day dead of one settlement: the one with
///        the least summed distance to their houses; invalid when the world
///        has none (the funeral still costs its day, at no road).
UnitId CemeteryFor(const FuneralConfig& config,
                   const WorldState& world,
                   const std::vector<ResidentId>& deceased);

/// @brief The day's deaths become funerals, one a settlement, buried
///        tomorrow; the invitation said, loud by the rule and its cap; the
///        kin's grief begun.
/// @param died The residents who died today, read before their rows went.
void OpenFunerals(const FuneralConfig& config,
                  WorldState& current,
                  const std::vector<ResidentId>& died);

/// @brief Whether `resident` is off today for a funeral: kin of a funeral
///        buried today (the whole day) or a neighbour in its procession
///        hours. Read by labour's morning list (IdleReason::kFuneral).
bool AtAFuneralToday(const WorldState& world, ResidentId resident, bool* kin);

/// @brief The chairman's kAttendFuneral orders: accepted until the burial
///        day's ceremony (kWent; one a funeral); refused kNoSuchSubject for
///        a funeral that is not or is over, kRuleForbids for one already
///        answered.
void TakeAttendOrders(WorldState& current);

/// @brief At the burial day's end: the chairman's answer settled (excused,
///        missed), its effects on the kin and the household, the row gone.
void CloseFunerals(const FuneralConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_FUNERAL_RULES_H_
