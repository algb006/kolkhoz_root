// The order of the works' supply (#595): sites with one supply deadline, one queue.
/// @file
/// @threading PARALLEL_READONLY - reads the world; written by nobody. Asked
///            in the decisions phase by the groom's plan (core_logistics,
///            logistics_plan.cpp OpenTasks: the first site's loads are drawn
///            first) and by the labour sub-step's supply offer (core_labor,
///            labor_system.cpp: the first site's job ranks first inside the
///            windowless tier) - the two doors to one action, which is why
///            the rule has one home and both call it.
///
/// THE RULE (boss, transport design «Стройки с равным сроком…», 38fc2dc3;
/// the finding of host's seed-9 trace, 11 October 2026: nine houses started
/// on day 12 all stood «delivering» on days 13-21 with nothing short and
/// nobody on them, since the supply served every open site at once, and the
/// first house came on day 22 against day 15 under the earlier core): THE
/// QUEUE IS AN ORDER, NOT AN EXCLUSION. Every site whose materials await
/// their carting keeps its task and its labour offer; the queue says who is
/// served first:
///   * a RAISED site («Поднять в очереди», kRaiseSite,
///     ConstructionState::raised) before every unraised one, raised sites
///     among themselves by the older start;
///   * then the oldest start (ConstructionState::supply_since_day), then the
///     unit's id - a total order, so two readers get one list.
///   * The next site works with what the first cannot use that day: the
///     plan draws the first site's loads before the next site's, and the
///     placement fills the first site's supply job before the next site's.
///   * A site awaiting nothing is not in the queue (AnythingAwaits); a paused
///     or emptying site neither.
/// THE FIRST EDITION (0.37.232's first pair, withdrawn before any commit)
/// gated the task list - one site offered, the next only when the first
/// stood with nothing at its senders - and starved every site behind the
/// first: nine villages, construction work-days y2 -40 %, y3 -32 %, houses
/// by y4 -15 %, the pen's wait 17-35 -> 48-63 days, the supply's cart-days
/// y1 -35 % (the carts went to the hay), the idle hands at noon +3..+8 a day.
/// The order keeps the first house's gate (building by ~day 15: its carts
/// and hands are first) without the starving.
#pragma once

#include <cstdint>
#include <vector>

namespace core {

struct WorldState;

/// @brief The rank of a site that is not in the queue (SiteSupplyRank).
inline constexpr std::uint32_t kNoSupplyRank = 0xFFFFFFFFU;

/// @brief The unit ROWS whose works await materials, in the order they are
///        served: raised first, then the oldest start, then the id.
/// @param world Read only.
/// @return Rows of `world.units` (not ids); empty when no site awaits
///         anything. A site paused, emptying or awaiting nothing is never in
///         it.
std::vector<std::uint32_t> SiteSupplyOrder(const WorldState& world);

/// @brief This site's place in SiteSupplyOrder: 0 is served first;
///        kNoSupplyRank when the site is not in the queue.
/// @param unit_row A row of `world.units`.
std::uint32_t SiteSupplyRank(const WorldState& world, std::uint32_t unit_row);

}  // namespace core
