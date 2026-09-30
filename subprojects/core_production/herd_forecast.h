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

/// @brief The forecast's answer.
struct HerdFeedForecast {
  /// True when some day before the horizon a kolkhoz herd is not fed in full.
  bool short_ahead = false;

  /// Whole game days from today to the first such day (0: today).
  std::uint16_t days_ahead = 0;

  /// The days forecast: to the next year's first scythes (in year 1 before
  /// the first cut, to this year's).
  std::uint16_t horizon_days = 0;

  /// The herd underfed first; invalid for a head that has not arrived yet
  /// (a lot of stock on the way) or the extra horse of an empty team.
  HerdId herd;

  /// The feed that ran out first among the underfed herd's links.
  ResourceId first_short;

  /// The most heads unfed on one day: each underfed herd's uncovered units
  /// over its units a head, summed, rounded up.
  std::int64_t heads_short = 0;
};

/// @brief The kolkhoz herds' fodder from today to the first scythes of the
///        next year — the day the new cut starts to feed them.
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
/// @return short_ahead false with no kolkhoz herd, no feed roster or no
///         horizon.
HerdFeedForecast ForecastHerdFeed(const ProductionConfig& config,
                                  const WorldState& world,
                                  bool one_more_horse);

/// @brief The heads a standing kolkhoz herd will have `days` days from today
///        by the forecast's own projection (births by RunBirths's rule and
///        gates, the maturing by the kind's months; no deaths, no culls) — the
///        door the offspring's test holds against a real run (0.37.57). 0 for
///        a herd not in `world`.
float ForecastHerdHeads(const ProductionConfig& config,
                        const WorldState& world,
                        HerdId herd,
                        std::uint32_t days);

/// @brief The first move for a feed that runs out (AlarmAdvice): the hay —
///        kCutHay; a feed no store would take today, nor a site under way
///        would — kGranaryForFeed («амбар под комбикорм»); anything else,
///        straw or silage or a feed a store takes, which a lot of the resource
///        answers — kNone.
AlarmAdvice AdviceForShortFeed(const ProductionConfig& config,
                               const WorldState& world,
                               ResourceId feed);

/// @brief kHerdHayShortAhead (alarm_state.h) off ForecastHerdFeed: one alarm
///        while the forecast is short. Always `lamp = 0`.
void CollectHerdForecastAlarms(const ProductionConfig& config,
                               const WorldState& world,
                               std::vector<Alarm>& alarms);

}  // namespace core

#endif  // CORE_PRODUCTION_HERD_FORECAST_H_
