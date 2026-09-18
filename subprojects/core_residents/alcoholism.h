/// @file
/// @brief The drinking of Epoch I — how a resident's alcoholism moves month by
/// month, and the bands it crosses (crime design §6, "Алкоголизм в ядре Эпохи
/// I — числами"; boss, parcel 364).
/// @threading SINGLE_THREADED
/// Runs in the residents' decisions sub-step (phase 3) on the sim thread, on
/// the first day of each month. It writes ResidentRow::alcoholism and clears
/// days_worked_this_month, and emits events, so it can only live in a
/// sequential slot.
///
/// WHAT DECIDES, in boss's numbers (assigned, not measured), for every MAN of
/// 16 and older, at the first day of the month, of the month that closed —
/// a woman has no such metric and stays at 0 (boss, 2026-09-18):
///   * up +0.5 when the village has a distiller (the supply);
///   * up +0.5 in a winter month (December to February) for one who had not a
///     single day of work in it (idleness and winter) — unless an open
///     reading hut is within his reach (sport.h);
///   * up +1 when his family's satisfaction is under 40;
///   * down −1 when he holds a post or worked at least 3 of the month's 4 days
///     (the design's "two thirds of the working days");
///   * down −0.5 when he is married;
///   * the three halves are econ's rebalance (seq 130, boss seq 132): at +2,
///     +1 and −1 the building village drank itself up (≈32 → 34–43 over two
///     years), against the human's «сам по себе алкоголизм снижается
///     медленно»;
///   * down −1 when the village has turned without a distiller for the second
///     month running or longer — the human's word, 2026-09-18: «Если люди
///     долго не пьют то алкоголизм медленно уменьшается». The count is
///     NightTheftTally::dry_months, kept here at each turn;
///   * never above 60 in Epoch I, never below 0;
///   * a crossing of 20, 40 or 60, either way, raises kAlcoholismBandCrossed.
///
/// STUB: the bands' effects — work quality, truancy, the inclination to crime
/// — are a door of their own; company, shocks and tradition (weddings,
/// wakes, the harvest's end) do not move the metric yet; the chairman's
/// attention and treatment do not lower it. The one way the chairman lowers
/// it is the supply: a distiller taken (kTakeNightTrader) and the +0.5 goes
/// when the LAST one does — and comes back at the year's turn.

#ifndef CORE_RESIDENTS_ALCOHOLISM_H_
#define CORE_RESIDENTS_ALCOHOLISM_H_

#include <span>
#include <string>
#include <string_view>

#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "night_trade.h"
#include "sport.h"

namespace core {

/// @brief The drinking numbers (world_params.csv). Defaults are boss's
/// figures of parcel 364 with econ's three halves (seq 130), kept for a
/// world with no tables.
struct AlcoholismConfig {
  float adult_from_years = 16.0F;        ///< `alcohol_adult_from_years`
  float gain_with_distiller = 0.5F;      ///< `alcohol_gain_with_distiller`
  float gain_winter_idle = 0.5F;         ///< `alcohol_gain_winter_idle`
  float gain_low_satisfaction = 1.0F;    ///< `alcohol_gain_low_satisfaction`
  float low_satisfaction_below = 40.0F;  ///< `alcohol_low_satisfaction_below`
  float loss_employed = 1.0F;            ///< `alcohol_loss_employed`
  float employed_days_min = 3.0F;        ///< `alcohol_employed_days_min`, of the month's days
  float loss_married = 0.5F;             ///< `alcohol_loss_married`
  float loss_sober = 1.0F;               ///< `alcohol_loss_sober`
  float sober_months_min = 2.0F;         ///< `alcohol_sober_months_min`
  float epoch1_cap = 60.0F;              ///< `alcohol_epoch1_cap`
};

/// @brief The world_params.csv keys this file reads.
std::span<const std::string_view> AlcoholismWorldParamKeys();

/// @brief Reads the knobs.
/// @return false with `error` naming the key for a value out of range.
bool ParseAlcoholismConfig(const ITableSet& tables, AlcoholismConfig& config, std::string& error);

/// @brief The lower edge of the band `alcoholism` is in: 0, 20, 40, 60 or 80.
int AlcoholismBand(float alcoholism);

/// @brief The month's turn, for the month that closed:
///        - each yard's dry months counted — a yard is dry when no distiller
///          SUPPLIED that month stands within reach (NearestSuppliedDistiller;
///          register 207);
///        - every adult man's alcoholism moved by the rules above, the +2 by
///          the samogon at HIS yard and the sobriety by his yard's dryness;
///          every woman's held at 0; crossings said;
///        - THE PURCHASE (crime §6, «Самогон стоит семье»; register 205): a
///          man in the 21–40 band buys `buy_kg_drinks`, in 41–60
///          `buy_kg_abuses`, out of his family's pantry — grain, then
///          potato, then sugar — into the pantry of the nearest supplied
///          distiller's family; an empty pantry buys nothing;
///        - the settlement's alcoholism taken, its crossings of 20 and 40
///          said;
///        - every resident's days_worked_this_month cleared;
///        - THE SPORTS FIELD (sport.h; register 223): in a counted month a
///          resident of 16 and over who went to the field takes −1 off a
///          man's drinking, sportiness at `sober_from` or over takes −1 more,
///          and every such resident's sportiness moves; the field's month is
///          then cleared.
/// @param night The distillers' reach, purchase and raw-material order.
/// @param sport The field's and sportiness's numbers.
/// @param life_speedup LifeConfig::life_speedup, for the biological age.
/// @pre Called on the first day of a month, before anything counts a day of
///      the new month.
void TurnAlcoholismMonth(const AlcoholismConfig& config,
                         const NightTradeConfig& night,
                         const SportConfig& sport,
                         float life_speedup,
                         WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_ALCOHOLISM_H_
