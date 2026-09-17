/// @file
/// @brief The night pasture: the chairman's standing order, the three
/// conditions under which the team spends a summer night on the floodplain,
/// and the first night said once (livestock design, «Ночное — единственный
/// выпас, и он ночной»; boss, parcels 63 and 65).
/// @threading SINGLE_THREADED
/// Both entry points run from the production sub-step of the decisions slot
/// (phase 3) on the sim thread — the order when it is read, the night at the
/// day's first tick. They write ChairmanState and the step's events, so they
/// can only live in a sequential slot.
///
/// WHAT THIS IS FOR, and it is not the quest. The horses' summer feed
/// discount (`pasture_coverage_summer` = 0.5) was applied unconditionally
/// from day zero — to a team standing in private yards, with no yard, no
/// order and no children. The design says that discount IS the night
/// pasture and nothing else: «Ночное — это самовыпас: летнюю ночь табун
/// кормится травой сам. Прибавка меряется сеном и овсом, которых не съели.»
///
/// So the gain had been paid out before its cause existed. Measured on
/// 2026-09-17 over fifteen years on the canonical seed: switching the
/// discount off costs 6.443 t of oats (+26.7 %) and 115.869 t of hay
/// (+6.1 %). That is what this file puts behind its three conditions.

#ifndef CORE_PRODUCTION_NIGHT_PASTURE_H_
#define CORE_PRODUCTION_NIGHT_PASTURE_H_

#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Whether the team is out on the floodplain TONIGHT: the standing
///        order given, the school holidays running, the team gathered in one
///        place, and children of the senior school band to keep it.
///
/// THE FEED DISCOUNT READS THIS AND NOTHING ELSE. A summer month is not
/// enough and never was: the pasture season runs five months and the holidays
/// three, and the two were never the same question.
bool TeamOutTonight(const ProductionConfig& config, const WorldState& world);

/// @brief Reads a kGrazeAtNight order: checks the three conditions, sets the
///        standing order and draws the camp's place on a floodplain meadow
///        from the world's own generator.
/// @return kNone, or kRuleForbids — for a night outside the holidays, a team
///         still standing in private yards, or a village with no children of
///         the age. Every one of the three is mended by the calendar or by
///         building, never by a different order, which is why none of them
///         has a refusal of its own.
OrderRefusal OrderNightPasture(const ProductionConfig& config, WorldState& current);

/// @brief At the day's first tick: says kNightPastureBegan the first evening
///        the team actually goes out, and never again.
void RunNightPasture(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_NIGHT_PASTURE_H_
