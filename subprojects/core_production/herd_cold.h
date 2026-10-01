/// @file
/// @brief The cold ladder of the kolkhoz herds: one counter of cold nights a
///        herd, the stage read off it, what each stage costs (Livestock
///        design, «Замерзание — метрика скота», «Числа лестницы — Эпоха I»,
///        in force by boss-core-start-no-yards [15]; 0.37.62).
/// @threading SINGLE_THREADED
/// Phase 4, inside the herd day's walk (herd_system.cpp, RunHerdDay), and
/// field work's traction factor, which reads the herds' state between steps.
///
/// WHERE THE COLD IS. A herd's place is its own unit (HerdRow::unit): a WARM
/// place when its rung is warm by unit_levels.csv `warm_place` or it is
/// insulated with straw (UnitRow::insulated), a COLD one otherwise. The heads
/// on billet stand outside the metric — «колхозный скот по чужим дворам: там
/// за ними смотрят хозяева» — so a herd wholly on billet has no count, and
/// the heads a herd has under its roof are what the stage speaks of.
///
/// WHEN THE COLD IS: THE CALENDAR'S WINTER, AND NO OTHER MONTH (world_params
/// `livestock_cold_first_month`..`livestock_cold_last_month`, December to
/// February; the human's word of 2026-10-01 through boss, billet thread [5],
/// [8]; 0.37.69). Out of it no night counts and the counter stands at
/// nought — an open yard in April or October is no cold place. The measure
/// before it was the weather's lot: of 27 villages a cow in an open yard
/// from 1 April «мёрзла» on 11 in the spring and on all 27 from October.
/// The player is given one date to have the yards warm by, the first of
/// December.
///
/// ONE COUNTER, AS HUNGER HAS ONE. A night below the kind's threshold for
/// its place adds a step, one below still frost two, any other night takes
/// two off; never below nought. The morning after a move from a cold place
/// into a warm one it starts again from nought (HerdRow::cold_place_yesterday).
/// «Мёрзнет» from 1: the produce and the draught fall. «Замерзает» from
/// `livestock_freezing_counter`: a share of the adults under the roof a day.
///
/// THE NIGHT IS THE DAY'S OWN (WeatherState: mean − swing), counted in the
/// herd day of that day: the weather knows the night from the morning, and
/// the herd day runs once. The draught's «назавтра» is the next step's
/// field work reading the counter this day wrote.
#ifndef CORE_PRODUCTION_HERD_COLD_H_
#define CORE_PRODUCTION_HERD_COLD_H_

#include <cstdint>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/herd_state.h"
#include "core_common/ids.h"

namespace core {

struct LivestockDef;
struct ProductionConfig;
struct UnitRow;
struct WorldState;
struct YearLedger;

/// @brief Whether a standing unit is a warm place for the ladder: its rung
///        warm by the table, or insulated. A site (level 0) is not a place.
bool UnitIsWarmPlace(const ProductionConfig& config, const UnitRow& unit);

/// @brief The heads of a herd under its roof tonight: every rung, less the
///        billet.
std::uint16_t HeadsUnderRoof(const HerdRow& herd);

/// @brief Moves HerdRow::cold_nights by tonight and writes
///        HerdRow::cold_place_yesterday. A family's own herd, a herd wholly on
///        billet and a kind that freezes nowhere keep nought.
/// @pre The day's billet is written (HerdRow::billeted_count).
void CountColdNight(const ProductionConfig& config,
                    const LivestockDef& kind,
                    HerdRow& herd,
                    const WorldState& world);

/// @brief «Мёрзнет»: a counter of one or more with heads under a cold roof.
bool HerdFreezing(const HerdRow& herd);

/// @brief «Замерзает»: freezing, and the counter at or past the threshold.
bool HerdFreezingToDeath(const ProductionConfig& config, const HerdRow& herd);

/// @brief The produce multiplier of the cold: the kind's
///        `freezing_produce_factor` over the share of the herd under the
///        cold roof; 1 when the herd does not freeze.
float ColdProduceFactor(const LivestockDef& kind, const HerdRow& herd);

/// @brief The settlement's draught under the cold, one number as the ration
///        is one (world_state.h, traction_ration): 1 − the share of the
///        kolkhoz draught heads freezing × (1 − the kind's
///        `freezing_draught_factor`). 1 when no draught herd freezes.
float ColdDraughtFactor(const ProductionConfig& config, const WorldState& world);

/// @brief «Замерзает»: the day's toll, a share of the adults under the roof,
///        fractional through HerdRow::frost_progress. Books herd_frozen by
///        kind and says kHerdFroze.
void RunFrostDeaths(const ProductionConfig& config,
                    const LivestockDef& kind,
                    HerdRow& herd,
                    HerdId herd_id,
                    WorldState& world,
                    YearLedger& book);

/// @brief Whether `month` lies in the cold's season
///        (FarmingConfig::cold_first_month..cold_last_month, wrapping the
///        year's turn): the only months a night is counted in.
bool InColdSeason(const ProductionConfig& config, Month month);

/// @brief Days from `day_of_year` to the first day of the cold's season; 0
///        inside it.
std::uint32_t DaysToColdSeason(const ProductionConfig& config, std::uint32_t day_of_year);

/// @brief The cold's two alarms (alarm_state.h): kHerdFreezing for every
///        kolkhoz herd freezing today — `amount` the heads under the cold
///        roof, `advice` kInsulateStraw while the stores hold one
///        insulation's straw; and kHerdColdAhead in the autumn before the
///        cold's season opens — `amount` the heads that will stand under a
///        cold roof then, the herd up to its roof's room; `days_ahead` to
///        the season's first day; `advice` kWarmYard when the unit's next
///        rung is warm, kInsulateStraw otherwise. The autumn is from the
///        end of the pasture (FarmingConfig::pasture_to_month) to the
///        season.
void CollectHerdColdAlarms(const ProductionConfig& config,
                           const WorldState& world,
                           std::vector<Alarm>& alarms);

}  // namespace core

#endif  // CORE_PRODUCTION_HERD_COLD_H_
