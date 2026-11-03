/// @file
/// @brief The graves: a death in the village laid in the nearest cemetery by
/// road of the dead's settlement, room not asked (#1199, #1367; cemetery_rules.h).
/// @threading SINGLE_THREADED
/// Called from the demography sub-step of the sequential decisions slot
/// (slot 3), the one writer of UnitRow::graves and
/// WorldState::burials_waiting after genesis and the save's load.
///
/// Design: social.md «Два кладбища» and «Заполнение — метрика юнита»
/// (boss, boss-core-epoch1-resume [16]), «Где хоронят» and «Ёмкость не
/// ограничена» (#1367, the human's word of 1 November 2026, #1564). The dead
/// is laid in the NEAREST built cemetery BY ROAD (RoadKm, on foot from his
/// yard) of his own settlement; none of it - the nearest of any; no yard - the
/// oldest by id. The room is NOT asked: past its area the rows grow denser.
/// (0.37.317 to #1367: the oldest with room, boss [33] (α); before it, the
/// nearest by straight line.) NO STOP: only a world with no cemetery built
/// keeps graves waiting (burials_waiting, kBurialHeld notable); the first that
/// stands takes them and kBurialsLaid says so. A death outside the village
/// (the district's ambulance) never reaches here: it lays no grave.

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

/// @brief The graves `unit`'s plot area holds (cemetery_rules.h) - the fill's
///        divisor, not a limit since #1367; 0 for a row that is not a built
///        cemetery.
std::uint16_t GraveCapacityOf(const BurialConfig& config,
                              std::span<const float> plot_radius_m,
                              const UnitRow& unit);

/// @brief Lays the grave of `dead`, who died in the village today: one grave
///        in the nearest built cemetery by road of his settlement (room not
///        asked). With none built, the grave waits — burials_waiting rises and
///        kBurialHeld fires as notable, no stop (resident `dead`, amount the
///        waiting).
/// @param funeral_cemetery The cemetery his funeral goes to (#1084,
///        funeral_rules.h): the grave is laid there when it is a built
///        cemetery; invalid - his own nearest, as above.
void LayGraveOf(const BurialConfig& config,
                std::span<const float> plot_radius_m,
                WorldState& current,
                ResidentId dead,
                UnitId funeral_cemetery);

/// @brief Lays the graves that wait, all into the oldest built cemetery;
///        kBurialsLaid names it (unit, amount).
///        Nothing while none waits.
void LayWaitingGraves(const BurialConfig& config,
                      std::span<const float> plot_radius_m,
                      WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_BURIAL_H_
