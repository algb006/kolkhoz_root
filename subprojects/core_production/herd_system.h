/// @file
/// @brief The herd day: feeding, produce, offspring, aging, slaughter.
/// @threading SINGLE_THREADED
/// Runs in the production sub-step of the decisions phase (slot 3), once per
/// day boundary, from the sim thread. It walks whole tables — herds, units,
/// families — and draws from the world's sequential RNG, so it can only live
/// in a sequential slot.
///
/// Model: manual/66-food-model.md §6. Design sources: livestock design §6
/// (the age ladder, the lifespan band, "above capacity go to the yards, not
/// under the knife"), §11 (the fodder unit and what eats what), the mobs
/// canon (counts by rung, never per-animal ages), and boss's answers of
/// 2026-08-30 on billeting and on the game-unit ages.
///
/// THE UNIT TRAP, restated because this is where it would bite: livestock
/// ages are GAME units with the x4 life acceleration already applied by the
/// design, while feed and care rates are REAL. Nothing here divides an age
/// by the life speedup.

#ifndef CORE_PRODUCTION_HERD_SYSTEM_H_
#define CORE_PRODUCTION_HERD_SYSTEM_H_

#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Runs one day of every herd, in this order: billeting, feeding,
/// produce, maturation, offspring, deaths, slaughter.
///
/// The order is the day's own logic and not an accident. Billeting first,
/// because how much room there is decides both the leak on today's produce
/// and whether anything is born at all. Feeding before produce, because a
/// hungry day costs milk the same day. Deaths before slaughter, so that a
/// head is never both.
///
/// @pre Called once per day boundary, from the sequential decisions slot.
/// @note A config without a livestock roster (a table-less test world) makes
///       the whole day a no-op rather than an error.
void RunHerdDay(const ProductionConfig& config, WorldState& current);

/// @brief Is there a stable standing? The kolkhoz yard's SECOND step is the
/// stable, and only its roof brings foals (livestock design §5). A yard at
/// step one is a pen: it houses horses and breeds none.
bool StableBuilt(const WorldState& world, const ProductionConfig& config);

/// @brief The day's fodder need of one herd, in feed units.
/// @param month 0-based; inside the pasture season the grass covers its
///        share, outside it the whole norm comes from the stores.
///
/// Public so that the feed light forecasts with the SAME arithmetic the day
/// actually runs on (stock_lights.h). A forecast that recomputed the need
/// beside this one would drift from it the first time a rung or a factor
/// moved, and it would drift silently.
/// @param grazing_tonight Whether this kind is out at grass tonight. The
///        pasture months are necessary and no longer sufficient: for the
///        TEAM the summer discount IS the night pasture, and it stands on the
///        chairman's order, the collective yard and the children
///        (night_pasture.h). Every other kind grazes unconditionally and
///        passes true.
///
///        A FORECAST PASSES FALSE, and that is a decision rather than a
///        default: the feed light and the fodder fund must not spend a gain
///        that depends on an order being kept and children being there. A
///        reserve that counted on it would be short in exactly the year the
///        chairman changed his mind.
float FeedNeedUnits(const ProductionConfig& config,
                    const LivestockDef& kind,
                    const HerdRow& herd,
                    std::uint8_t month,
                    bool grazing_tonight);

/// @brief What the fodder fund holds of one resource, in grams: the working
/// stock's WORK RATION FOR THE YEAR (resources design §6, third rung).
///
/// MEASURED OFF THE HARNESS AND NOT OFF THE HARVEST: counted from the animals
/// that will pull the plough, in feed units, and a share of the year's need
/// is what a working animal may take as grain at all. The share is the
/// resource's own cap — `max_share` of its `work_only` row in feed_links.csv —
/// and NOT traction_full_ration_share; the two agree on oats (0.5), barley
/// and compound feed are 0.4. The daily need is the CURRENT month's times the
/// year — a named simplification, the pasture months discount it.
///
/// ONE HOME FOR TWO READERS since 2026-09-18: the fund order (kUnsealFund)
/// and the accumulation limit's base (district_plan.cpp) — a village that
/// holds its team's oats is keeping house, not hoarding (boss seq 83).
///
/// THE MONTH DOES NOT MOVE IT for a working team, and that was measured
/// rather than assumed (2026-09-18): the fund passes `grazing_tonight`
/// false, and for the team the summer discount IS the night pasture, so
/// forty horses hold 73 t of oats in every month of the year. A "read it at
/// January" parameter was written for the limit on the contrary reading and
/// taken out again the same hour — it changed nothing.
Grams FodderFundGrams(const ProductionConfig& config,
                      const WorldState& current,
                      ResourceId resource);

/// @brief RUNG 3 OF THE LADDER TODAY, grams of one fund feed: its share of
/// ONE work ration of the kolkhoz's working stock until the next reaping of
/// the kind's staple, laid down the feeding order — the staple as far as the
/// herd may eat of it (FeedAllowance, capped by its last reaping: boss,
/// boss-core-epoch1-resume seq 14, answer 2) and its max_share lets it cover,
/// a reserve feed only for the rest (host's barley trace, boss-core-epoch1-2
/// seq 1). Not the year's fund above: that one is the unsealing's old
/// ceiling and the accumulation limit's base, and a year held in April
/// starves the spring beside a store the team cannot eat by August.
/// @return 0 for a resource that is no fund feed; never more than the
/// allowance of it, so the answer moves with the stores and with the other
/// fund feeds.
Grams FodderClaimGrams(const ProductionConfig& config,
                       const WorldState& current,
                       ResourceId resource);

/// @brief FodderClaimGrams for every resource, dense by ResourceId — the
/// size core_common/fund_ladder.h (FodderRungLeft) is handed.
ResourceAmounts FodderClaim(const ProductionConfig& config, const WorldState& current);

/// @brief What NOBODY eats today because next year needs it (0.37.2;
/// boss-core-epoch1-queue [52]-[54] (2), [59]-[60] (г)): for every produce
/// of a crop, what next year's own harvest will not pay of next year's
/// positions and the year after's seed (plan_alarms.h, NextYearUnpaidGrams),
/// held with the rot of its wait to the end of next year (district_plan.h,
/// HeldForDeliveryGrams). Dense by ResourceId.
///
/// ONE HOLD, TWO EATERS: the herds stay above it (FeedAllowance), and the
/// people's issue does (IProductionSystem::NextYearHold, residents'
/// IssueReserve). The herds alone held it first, and the issue then took the
/// good year's rye the lean year needed: with the harness the good years
/// gave out 4.4 t against 3.4, the lean ones 0.1 against 1.2, and the rye of
/// years 4, 7 and 10 failed on the canon.
ResourceAmounts NextYearHold(const ProductionConfig& config, const WorldState& world);

/// @brief The ploughing's oats, held from the team's other work (0.37.2;
/// boss-core-epoch1-queue [59]-[60], (а); resources design §6: the fodder
/// fund is kept «from the team that will plough»).
///
/// THE PLOUGH IS THE FUND'S FIRST EATER. With the carts' horses fed their
/// oats (the harness, work_seam.h), the carts ate through the winter and
/// the spring's plough went to work on hay: the team's ration on the
/// ploughing days fell from 1.00 to 0.00 (median of the years' means, nine
/// seeds). On a day nobody ploughs or harrows the herd leaves this much of
/// the plough's feed in the store; on a ploughing day it is the plough's.
struct PloughFeedHold {
  /// The feed held: the horse kind's first work feed that is no reserve —
  /// the oats; invalid when the tables name none.
  ResourceId resource;

  /// Grams held today; nought outside the window below.
  Grams grams = 0;

  /// Horse-days of ploughing and harrowing it is sized for: the last closed
  /// book's man-days of the two (one horse a ploughman), or in the first
  /// year, which has no book, the arable under chains at the ploughing and
  /// harrowing norms a hectare.
  float horse_days = 0.0F;

  /// The book the horse-days were read off: its year, or 0 in the first year
  /// (off the chains) — what the instrument prints as its basis.
  std::uint32_t book_year = 0;

  /// Held from the feed's reaping — or the end of its reaping window, reaped
  /// or not — to the end of the spring sowing's latest window, the
  /// ploughing's season; between them the stores hold the old feed and the
  /// new one is on its way.
  bool held = false;
};

/// @brief PloughFeedHold as of `world`'s today. The whole book year's
/// ploughing is held, spring and the autumn's fallow alike — the book does not
/// split them, and the error is to the plough's side.
PloughFeedHold PloughFeedHoldOf(const ProductionConfig& config, const WorldState& world);

/// @brief The milk the KOLKHOZ's herds give in one day at each herd's factor
/// today (YieldFactor: billeting, underfeeding), grams — the same sum
/// RunProduce delivers, read without delivering it. A household's cow is the
/// household's and not in it. The milk position's base (district §9: «дойные
/// на день объявления × надой × множитель стада на тот день»).
Grams KolkhozMilkDayGrams(const ProductionConfig& config, const WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_HERD_SYSTEM_H_
