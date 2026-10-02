/// @file
/// @brief The kolkhoz herds' fodder forecast to the cut of the next year, and
///        the two alarms that read it: the yellow stage kHerdHayShortAhead
///        and kTooFewHorses's turn to the hay (boss-core-epoch1-resume-
///        2026-09-30 [49] p. 2, [68], [70], [71], [72] p. 2, [85], [86];
///        econ's horse-spring-starvation-2026-09-30.md §4, §4а).
/// @threading SINGLE_THREADED
/// Read-only over a completed state, between steps (CollectAlarms). Nothing
/// here writes the world.
///
/// ONE FORECAST, TWO READERS. The yellow stage asks whether the herds as they
/// will be reach the cut of the next year; the horses' advice asks the same
/// with one adult more in the team. Until 0.37.57 the advice had a forecast of
/// its own — today's heads over one stall season, no calves, no horizon — and
/// the novice bought horses by it while the cows starved beside them, 31 ->
/// 164 in spring year 4 (host, boss-host-horses-hay-03749 [13]).
///
/// THE DAY'S DRAIN IS THE FEED LIGHT'S (stock_lights.cpp, FeedLight): the
/// same held stores (the herd feeding's door, HerdFeedAllowance, and the hay
/// in the fields' heaps) and the same order and ceilings per kind, one
/// function for both (DrainFeedDay). What the forecast adds is the calendar:
/// the need of each day by its month, the offspring, the cut and the moves
/// already made.
///
/// THE OFFSPRING BY THE SAME RULE, NOT BY THE SAME DOOR (boss [85] q. 2,
/// [86] p. 2): running the herd day's own RunBirths and RunMaturation on a
/// copy of the world every step would cost a world a step. The forecast reads
/// the same numbers of livestock.csv and farming.csv and the same four gates
/// — the calving band, a billeted herd, a hungry herd, a kind that keeps
/// sires with none, a horse with no stable — as herd_life.cpp does; a test
/// holds the two to one head a herd on a year of a fixture, a billeted herd
/// included (0.37.57). Deaths and culls are NOT forecast: the need is the
/// larger for it, and a forecast that over-predicts the need lights early,
/// the safe side.
#ifndef CORE_PRODUCTION_HERD_FORECAST_H_
#define CORE_PRODUCTION_HERD_FORECAST_H_

#include <cstdint>
#include <span>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief One herd's fodder need for one day, in feed units.
struct FeedDayNeed {
  LivestockKindId kind;
  float units = 0.0F;
};

/// @brief What the kolkhoz herds may eat, in KILOGRAMS by resource: the herd
///        feeding's own door (HerdFeedAllowance — the stores less every rung
///        the herds stay below) and the hay lying reaped in the fields above
///        the plan's debt (HeapAbovePlanDebt). The feed light's and the
///        forecast's stores, one reading for both.
std::vector<float> HerdFeedHeldKg(const ProductionConfig& config, const WorldState& world);

/// @brief One day of feeding drained off `held_kg` by the herd feeding's own
///        order and ceilings: per need, its kind's links in order, a link's
///        share of the day no more than its `max_share`, work-only feeds
///        carrying nothing (nobody is in the traces in a forecast), a reserve
///        link refused the people's food (PeoplesFoods), a reserve feed at the
///        reserve factor.
/// @param covered Written: the units each need got, index by index.
void DrainFeedDay(const ProductionConfig& config,
                  const std::vector<std::uint8_t>& peoples_foods,
                  std::span<const FeedDayNeed> needs,
                  std::vector<float>& held_kg,
                  std::vector<float>& covered);

/// @brief How far the fodder's forecast looks (0.37.95; boss, econ-boss-hay-
///        term-2026-10-01 [7]).
enum class FeedHorizon : std::uint8_t {
  /// To the NEAREST first scythes: before this year's cut, to its first day;
  /// from the mowing on, to next year's. The yellow lamp's horizon — a
  /// shortage past the nearest cut is two haymakings away and no lamp's
  /// business. In year 1's mowing window there is nothing to forecast yet.
  kNearestScythes,

  /// To the first scythes of the NEXT year from any day of this one (in year
  /// 1 before the first cut, to this year's): this year's cut in the income,
  /// a winter and a spring beyond it. The question of a head bought to stay
  /// («would the hay feed one more horse», kTooFewHorses), and the lamp's own
  /// horizon until 0.37.96 — from 1 January a year and a half and two
  /// calvings on.
  kNextYearsScythes,
};

/// @brief A purchase of feed the forecast is asked to count as made today
///        (0.37.121): what it brings, by resource in KILOGRAMS (the size of
///        ProductionConfig::feed_values), and the day it lands, counted from
///        today.
struct FeedPurchase {
  std::vector<float> goods_kg;
  std::uint32_t land_day = 0;

  /// The year's points it costs, and whether it buys anything at all.
  std::int32_t points = 0;
  bool any = false;
};

/// @brief THE LARGEST PURCHASE OF FEED THE LOT'S DOOR WOULD TAKE THIS MORNING
///        (0.37.121): of the catalogue's goods lots that carry a feed the
///        herds eat and that the door would take today (district_limit.h,
///        LimitLotRefusalToday), as many as the year's points cover — the lot
///        with the most feed units a point first, then the next, each as
///        often as the points left allow. It lands on the cart's latest day:
///        the base term and the whole of the delay (a purchase counted as
///        landing early names fewer heads than the fodder is short of).
/// @param blocked_by_store Written when given: true when NO feed lot is
///        buyable and at least one was refused by kNowhereToStore alone —
///        the day's move is the granary, not the lot.
/// @return `any` false with no lot buyable today.
/// @note STUB of the room: a store's FREE room is not asked — the door does
///       not ask it either (SomeStoreAccepts: «full or not»), and what does
///       not fit waits on the cart at the gate. The forecast lands only what
///       a store has room for (StoreHasRoomFor), as for the carts on the
///       road.
FeedPurchase MaximalFeedPurchase(const ProductionConfig& config,
                                 const WorldState& world,
                                 bool* blocked_by_store = nullptr);

/// @brief The forecast's answer.
struct HerdFeedForecast {
  /// True when some day before the horizon a kolkhoz herd is not fed in full.
  bool short_ahead = false;

  /// Whole game days from today to the first such day (0: today).
  std::uint16_t days_ahead = 0;

  /// The days forecast, by the FeedHorizon asked.
  std::uint16_t horizon_days = 0;

  /// The herd underfed first; invalid for a head that has not arrived yet
  /// (a lot of stock on the way) or the extra horse of an empty team.
  HerdId herd;

  /// The feed that ran out first among the underfed herd's links.
  ResourceId first_short;

  /// The most heads unfed on one day: each underfed herd's uncovered units
  /// over its units a head, summed, rounded up — and NEVER MORE THAN THE
  /// HEADS STANDING TODAY (with the stock on the road): the calves to come
  /// are in the need, not in the number the chairman is told (0.37.95).
  std::int64_t heads_short = 0;
};

/// @brief The kolkhoz herds' fodder from today to the first scythes the
///        horizon names — the day a new cut starts to feed them.
///
/// Day by day: the day's need of each herd by the day's month (FeedNeedUnits;
/// the team's summer discount only in a month the night pasture's standing
/// order and conditions would take it out — TeamOutInMonth — every other kind
/// grazing as the herd day lets it); the offspring by the births' rule in the
/// calving band, maturing by the kind's months; the stock bought on the limit
/// arriving on its day (livestock_arrivals); the feed lots on the road landing
/// on their day where a store has room; the expected cut, when this year's
/// is still ahead, landing day by day over the mowing's window — last year's
/// hay (the closed book's harvest) less what this year has laid already.
/// YEAR 1, WITH NO BOOK, LOOKS ONLY TO THE FIRST SCYTHES (boss [88], option
/// (б)): before the mowing, to its first day; in its window, nothing; after
/// it, to next year's cut on the hay actually in. A share of the meadows'
/// ceiling stood here in the first pair of 0.37.57 and lit the yellow all
/// year 1 in every village.
/// @param one_more_horse One adult more in the kolkhoz's team (kTooFewHorses's
///        question; boss [72] p. 2): the first team herd, or a herd of one if
///        the farm has none.
/// @param horizon How far to look. TWO READERS, TWO HORIZONS, ON PURPOSE
///        (boss, 2026-10-02): the yellow lamp asks kNearestScythes — it warns
///        of a shortage the chairman can still act on before the next cut;
///        kTooFewHorses's «would the hay feed one more horse» asks
///        kNextYearsScythes — a horse is bought to stay, and its question is
///        honestly a year's. Do not bring them to one.
/// @param purchase A purchase counted as made today (FeedPurchase; 0.37.121),
///        or nullptr: its goods land on its day beside the carts already on
///        the road — «what would the forecast say AFTER this lot».
/// @return short_ahead false with no kolkhoz herd, no feed roster or no
///         horizon.
HerdFeedForecast ForecastHerdFeed(const ProductionConfig& config,
                                  const WorldState& world,
                                  bool one_more_horse,
                                  FeedHorizon horizon = FeedHorizon::kNextYearsScythes,
                                  const FeedPurchase* purchase = nullptr);

/// @brief The heads a standing kolkhoz herd will have `days` days from today
///        by the forecast's own projection (births by RunBirths's rule and
///        gates, the maturing by the kind's months, the young males the herd
///        has no room for culled as they grow up; no deaths). THE CULL IS IN
///        since 0.37.57: this line said «no deaths, no culls» until 0.37.95,
///        and on 1 October 2026 it was read as the code and sent to boss as
///        the cause of the lamp's number; the check «a cow herd's year and a
///        half» holds the forecast to the herd day (110.0 against 110) — the
///        door the offspring's test holds against a real run (0.37.57). 0 for
///        a herd not in `world`.
float ForecastHerdHeads(const ProductionConfig& config,
                        const WorldState& world,
                        HerdId herd,
                        std::uint32_t days);

/// @brief WHETHER «HURRY THE CUT» IS A MOVE TODAY (0.37.123; boss, host-boss-
///        pin-0-37-109-2026-10-02 [19] p. 3): some meadow stands in its cut
///        with mowing left AND its avral (kDeclareRush on that cut) is below
///        the last step (kMaxRushStep). The lamp's kCutHay asks this. Until
///        then it asked the first half alone, and on host's 27 villages the
///        lamp said «the cut» on 287 days of 762 with every meadow rushed to
///        the limit — a move the door would not take, and a ladder that never
///        went on to the lot or the heads.
/// @note Read-only; a field's rush counts only on the phase it was declared
///       in (FieldRow::rush_phase).
bool CutCanBeHurried(const WorldState& world);

/// @brief The hay lamp's advice: the two moves and their numbers
///        (alarm_state.h — Alarm::advice, advice_resource, advice_amount,
///        advice_more, amount_more).
struct FodderAdvice {
  AlarmAdvice advice = AlarmAdvice::kNone;
  ResourceId advice_resource;
  std::int64_t advice_amount = 0;
  AlarmAdvice advice_more = AlarmAdvice::kNone;
  std::int64_t amount_more = 0;
};

/// @brief THE LADDER OF THE HAY LAMP'S ADVICE (0.37.121; boss, host-boss-pin-
///        0-37-109-2026-10-02 [11], [12], [15]; the human, 2 October 2026:
///        «игра не должна сама убивать стадо» — the chairman decides, so the
///        game owes him the number). For a forecast that is short, the first
///        of these that holds:
///   1. kCutHay — a meadow stands in its cut with mowing left (0.37.92) and
///      its avral below the last step (CutCanBeHurried; 0.37.123);
///   2. kBuyFeed — the door would take a feed lot today (MaximalFeedPurchase)
///      AND the forecast after that purchase is short of fewer heads:
///      `advice_resource`, `advice_amount` the feed and the grams bought;
///      the heads still short after it — `advice_more` = kReduceHerd,
///      `amount_more`;
///   3. kGranaryForFeed — no feed lot is buyable today and one was refused
///      for want of a store alone: «finish the granary», whether its site is
///      under way or not yet marked; `advice_more` = kReduceHerd with
///      `amount_more` the heads short today;
///   4. kReduceHerd — nothing else helps: the district sells no feed the
///      herds eat, the points are spent, or what they buy feeds no head more
///      (a feed's share of the ration is capped — feed_links.csv max_share —
///      and the hole is in the hay). Its heads are the alarm's `amount`.
///
/// WHATEVER FEED RAN OUT FIRST. Until 0.37.121 the ladder was the hay's
/// alone (AdviceForShortFeed): any other first short feed got «the granary»
/// or nothing, and kReduceHerd stood behind «the catalogue sells no feed» —
/// a line no world with a feed lot in its tables could reach.
/// @param forecast The short forecast the lamp read (its heads_short is the
///        number before any move).
/// @return All fields none and nought for a forecast that is not short.
FodderAdvice AdviseOnShortFodder(const ProductionConfig& config,
                                 const WorldState& world,
                                 const HerdFeedForecast& forecast);

/// @brief Whether the hay IN THE STORES is below what the kolkhoz herds will
///        eat of it in the next `days` days (IProductionSystem::
///        StoredHayShortWithin; 0.37.132): the herds standing today fed day
///        by day by the forecast's own drain (DrainFeedDay) off the herd
///        feeding's allowance of the stores alone — the heaps lying at the
///        meadows are NOT held here, HerdFeedHeldKg's other half — each day's
///        need by its month, the team at grass by TeamOutInMonth as in the
///        forecast. Short on the first day the drain leaves no hay AND a herd
///        that eats hay is not fed in full: stores holding exactly the days
///        asked are not short, and a herd underfed with hay in store is
///        another feed's matter.
/// @param days The days ahead, today the first; 0 answers false.
/// @return False with no hay resource, no feed roster, no kolkhoz herd whose
///         kind has a link to hay, or in days such herds want nothing.
/// @note No offspring, no cut to come, no cart on the road: the need is the
///       smaller for it and the answer comes later, the safe side for a
///       threshold that takes the horses from the plough.
bool StoredHayShortWithin(const ProductionConfig& config,
                          const WorldState& world,
                          std::uint32_t days);

/// @brief kHerdHayShortAhead (alarm_state.h) off ForecastHerdFeed: one alarm
///        while the forecast is short. Always `lamp = 0`.
void CollectHerdForecastAlarms(const ProductionConfig& config,
                               const WorldState& world,
                               std::vector<Alarm>& alarms);

}  // namespace core

#endif  // CORE_PRODUCTION_HERD_FORECAST_H_
