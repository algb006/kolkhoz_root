/// @file
/// @brief The funeral step (#1084; core_common/funeral_state.h): a funeral
/// opened the death day, the party off its burial day, the chairman's order,
/// the effects at its close.
/// @threading SINGLE_THREADED
/// Run by core_residents in its sequential decisions sub-step: OpenFunerals
/// from the demography's deaths (before the rows go), TakeAttendOrders every
/// hour, RunFuneralDay at the day's turn after the month's mood is composed.
/// The labour's morning reads who is off through core_common/funeral_reads.h.
///
/// Design: social.md «Как идут похороны» (the human's «Похороны делай»);
/// econ funeral-third-day-2026-10-19 «Ревью целиком» R1; boss, the #1056
/// thread [58], [59], and core-boss-1084-funeral-body [2] (the joint
/// funeral's graves follow its cemetery).
/// STUB, named: the party walks (no cart request yet); the chairman's own
/// illness is not modelled (the core has none), so «болен» never excuses.

#ifndef CORE_RESIDENTS_FUNERAL_RULES_H_
#define CORE_RESIDENTS_FUNERAL_RULES_H_

#include <cstdint>
#include <span>
#include <vector>

#include "core_common/funeral_state.h"
#include "core_common/ids.h"
#include "core_common/world_state.h"

namespace core {

/// @brief What the funeral step reads (STUB values, social.md and econ R5).
struct FuneralConfig {
  /// unit_types.csv "cemetery" (the burial's own type, burial.h).
  UnitTypeId cemetery_type;

  /// The adult age (biological years) and the life speed-up: who of the kin
  /// is an adult, and who is a child for the loud rule.
  float adult_age_years = 16.0F;
  float life_speedup = 4.0F;

  /// The household's small children stay home (STUB, biological years).
  float household_from_years = 7.0F;

  /// Grief and presence, on the metrics' own scales: the kin's mood on the
  /// death day and its fade; presence's mood and the household's
  /// satisfaction; absence's two. The satisfaction lasts kDaysPerMonth.
  float grief_mood = -10.0F;
  std::uint32_t mood_fade_days = 8;
  float presence_mood = 10.0F;
  float presence_satisfaction = 3.0F;
  float absence_mood = -8.0F;
  float absence_satisfaction = -3.0F;
};

/// @brief The party of a death: the dead's adult kin within two hops (by
///        mother, father, spouse and the children who name him), then his
///        household but its small children (`kin_count` of them in all), then
///        one adult of every other household whose house is within
///        kNeighbourMetres of his in his settlement. Kin come from any
///        settlement. `also_dead` (the same day's dead) never walk.
/// @return The residents, kin first; `kin_count` set.
std::vector<ResidentId> FuneralParty(const FuneralConfig& config,
                                     const WorldState& world,
                                     ResidentId deceased,
                                     std::span<const ResidentId> also_dead,
                                     std::uint32_t& kin_count);

/// @brief The cemetery for the same-day dead of one settlement: among the
///        built cemeteries of that settlement (none - of any), the one with
///        the least summed road (RoadKm, on foot from each dead's yard); for
///        one dead it is LayGraveOf's own choice (burial.h, #1367). Invalid
///        when none is built: the graves wait (kBurialHeld) and the funeral
///        still costs its day, at no road.
UnitId CemeteryFor(const FuneralConfig& config,
                   const WorldState& world,
                   std::span<const ResidentId> deceased);

/// @brief The day's deaths become funerals, one a settlement, buried
///        tomorrow: the party, the cemetery, the road; loud by the rule and
///        its cap (one loud burial day in kLoudEveryDays); the invitation
///        (kFuneralInvitation); the kin's grief begun.
/// @param died The residents who die today, read BEFORE their rows go.
/// @return The cemetery each of `died` is laid in, parallel to it: the
///         caller lays the graves there (LayGraveOf).
std::vector<UnitId> OpenFunerals(const FuneralConfig& config,
                                 WorldState& current,
                                 std::span<const ResidentId> died);

/// @brief The chairman's kAttendFuneral orders (`amount` the funeral's id):
///        accepted until its burial day ends (kWent; one a funeral);
///        refused kNoSuchSubject for a funeral that is not, kRuleForbids for
///        one already answered.
void TakeAttendOrders(WorldState& current);

/// @brief At the day's turn: the funerals buried YESTERDAY close - the
///        chairman's answer settled (excused if away in the district that
///        day or at another funeral of it, missed on a loud one otherwise),
///        presence or absence on the kin and the household, the row gone;
///        then every resident's funeral mood term is folded into his mood.
void RunFuneralDay(const FuneralConfig& config, WorldState& current);

/// @brief The funeral mood term of `person` on `day`: the amount fading
///        linearly to nought over config.mood_fade_days.
float FuneralMoodTerm(const FuneralConfig& config, const ResidentRow& person, SimDay day);

/// @brief Adds `amount` to `person`'s funeral mood term from `day`: what is
///        left of the old plus the new, fading from `day` (STUB: one fade).
void AddFuneralMood(const FuneralConfig& config, ResidentRow& person, float amount, SimDay day);

}  // namespace core

#endif  // CORE_RESIDENTS_FUNERAL_RULES_H_
