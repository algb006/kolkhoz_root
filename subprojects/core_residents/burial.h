/// @file
/// @brief The graves: a death in the village laid in a cemetery with room,
/// and the game's stop when none has any (#1199; cemetery_rules.h).
/// @threading SINGLE_THREADED
/// Called from the demography sub-step of the sequential decisions slot
/// (slot 3), the one writer of UnitRow::graves and
/// WorldState::burials_waiting after genesis and the save's load.
///
/// Design: social.md «Два кладбища» and «Заполнение — метрика юнита»
/// (boss, boss-core-epoch1-resume [16]). The dead is laid in the OLDEST built
/// cemetery with room, by its id (boss [33], (α): «новое размечается, когда
/// старое кончится» - the old ground is used to the end, so its fill_100 is an
/// event the player sees). Until 0.37.317 it was the nearest to the family's
/// house, and the run showed the old one stopping at 37 of 41 the day a nearer
/// new one stood. With none, he waits (burials_waiting) and kBurialHeld stops
/// the game; the oldest with room takes the waiting graves, and kBurialsLaid
/// says so. A death outside the village (the district's
/// ambulance) never reaches here: it lays no grave.

#ifndef CORE_RESIDENTS_BURIAL_H_
#define CORE_RESIDENTS_BURIAL_H_

#include <span>
#include <string>
#include <string_view>

#include "core_common/cemetery_rules.h"
#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {

/// The graves' knobs: which type is a cemetery, and the ground a grave takes.
struct BurialConfig {
  UnitTypeId cemetery_type;                         ///< unit_types.csv "cemetery"
  float m2_per_grave = kCemeteryM2PerGraveDefault;  ///< `cemetery_m2_per_grave`
};

/// @brief The world_params keys this file reads (the assembly declares them).
std::span<const std::string_view> BurialWorldParamKeys();

/// @brief Reads the cemetery's type and the ground a grave takes.
/// @return false with `error` set on a value out of its range.
bool ParseBurialConfig(const ITableSet& tables, BurialConfig& config, std::string& error);

/// @brief The graves `unit` holds at most, by its type's plot
///        (cemetery_rules.h); 0 for a row that is not a built cemetery.
std::uint16_t GraveCapacityOf(const BurialConfig& config,
                              std::span<const float> plot_radius_m,
                              const UnitRow& unit);

/// @brief Lays the grave of `dead`, who died in the village today: one grave
///        in the oldest built cemetery with room. With none, the grave waits —
///        burials_waiting rises and kBurialHeld fires (resident `dead`,
///        amount the waiting).
void LayGraveOf(const BurialConfig& config,
                std::span<const float> plot_radius_m,
                WorldState& current,
                ResidentId dead);

/// @brief Lays the graves that wait, into the cemeteries with room, the
///        oldest first; kBurialsLaid per cemetery that took any (unit, amount).
///        Nothing while none waits.
void LayWaitingGraves(const BurialConfig& config,
                      std::span<const float> plot_radius_m,
                      WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_BURIAL_H_
