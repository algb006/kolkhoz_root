/// @file
/// @brief The former elder: who he is at the start and where he is now
///        (society design §1а; boss-core-start-quest-facts-2026-09-30 [7],
///        [8]).
/// @threading SINGLE_THREADED
/// Genesis, once the yards are given out; the view between steps.
///
/// THE YARD IS THE START'S, THE DOOR IS THE MAN'S. The elder lives in
/// `yard_21` in every game, so genesis puts the family with the man of the
/// right age there; from then on the door follows him and not the yard — his
/// family may move, and yard_21 is not held from collapse (boss [8] p. 2).
#ifndef CORE_WORLD_START_ELDER_H_
#define CORE_WORLD_START_ELDER_H_

#include <span>
#include <string_view>

#include "core_common/elder_view.h"
#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {

/// @brief «В солидном возрасте» (society design §1а): the age band the elder
/// is drawn from, biological years (world_params `elder_age_min_years`,
/// `elder_age_max_years`; 45..60, STUB — boss [8]).
struct StartElder {
  float age_min_years = 45.0F;
  float age_max_years = 60.0F;
};

/// @brief The world_params keys this file reads, for the assembly's union
/// (core_catalog/table_value.h).
std::span<const std::string_view> StartElderWorldParamKeys();

/// @brief Reads the band. A missing row keeps its documented number silently;
/// an out-of-range row, or a band whose ends are reversed, sends both back to
/// 45..60, logged.
StartElder ReadStartElder(const ITableSet& tables);

/// @brief Makes the elder: the oldest man within the band of the family in
/// `elder_yard`. When that family has none, the housed family whose oldest man
/// within the band is the oldest (the first in row order among equals)
/// changes houses with it — the units' `household` and the families' `house`
/// both — so the elder lives in `elder_yard` all the same: first among the
/// families of `like_yards`, the yards the start lays out like his own, and
/// only when none of them has such a man among the whole village (logged).
/// Writes NamedCharactersState::elder.
/// @param like_yards The houses searched first; empty searches the village.
/// @pre The yards are given out (every family's `house` is set) and nothing
///      has been placed by the house yet — a swap afterwards would leave it
///      at the other yard.
/// @note Logged and left invalid when `elder_yard` is not a yard with a family
///       or no man of the village is within the band; the world is not
///       refused for it.
void SeatStartElder(const StartElder& band,
                    float life_speedup,
                    UnitId elder_yard,
                    std::span<const UnitId> like_yards,
                    WorldState& world);

/// @brief The door's answer (elder_view.h): the elder if he is among the
/// living residents, and his family's house now.
ElderView ElderViewOf(const WorldState& world);

}  // namespace core

#endif  // CORE_WORLD_START_ELDER_H_
