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
/// WHAT DECIDES, in boss's numbers (assigned, not measured), for everyone of
/// 16 and older, at the first day of the month, of the month that closed:
///   * up +2 when the village has a distiller (the supply);
///   * up +1 in a winter month (December to February) for one who had not a
///     single day of work in it (idleness and winter);
///   * up +1 when his family's satisfaction is under 40;
///   * down −1 when he holds a post or worked at least 3 of the month's 4 days
///     (the design's "two thirds of the working days");
///   * down −1 when he is married;
///   * a woman's change is a quarter of a man's ("пьют мужчины");
///   * never above 60 in Epoch I, never below 0;
///   * a crossing of 20, 40 or 60, either way, raises kAlcoholismBandCrossed.
///
/// STUB: the bands' effects — work quality, truancy, the inclination to crime
/// — are a door of their own; company, shocks and tradition (weddings,
/// wakes, the harvest's end) do not move the metric yet; the chairman's
/// attention and treatment do not lower it.

#ifndef CORE_RESIDENTS_ALCOHOLISM_H_
#define CORE_RESIDENTS_ALCOHOLISM_H_

#include <span>
#include <string>
#include <string_view>

#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {

/// @brief The drinking numbers (world_params.csv). Defaults are boss's
/// figures of parcel 364, kept for a world with no tables.
struct AlcoholismConfig {
  float adult_from_years = 16.0F;        ///< `alcohol_adult_from_years`
  float gain_with_distiller = 2.0F;      ///< `alcohol_gain_with_distiller`
  float gain_winter_idle = 1.0F;         ///< `alcohol_gain_winter_idle`
  float gain_low_satisfaction = 1.0F;    ///< `alcohol_gain_low_satisfaction`
  float low_satisfaction_below = 40.0F;  ///< `alcohol_low_satisfaction_below`
  float loss_employed = 1.0F;            ///< `alcohol_loss_employed`
  float employed_days_min = 3.0F;        ///< `alcohol_employed_days_min`, of the month's days
  float loss_married = 1.0F;             ///< `alcohol_loss_married`
  float women_factor = 0.25F;            ///< `alcohol_women_factor`
  float epoch1_cap = 60.0F;              ///< `alcohol_epoch1_cap`
};

/// @brief The world_params.csv keys this file reads.
std::span<const std::string_view> AlcoholismWorldParamKeys();

/// @brief Reads the knobs.
/// @return false with `error` naming the key for a value out of range.
bool ParseAlcoholismConfig(const ITableSet& tables, AlcoholismConfig& config, std::string& error);

/// @brief The lower edge of the band `alcoholism` is in: 0, 20, 40, 60 or 80.
int AlcoholismBand(float alcoholism);

/// @brief The month's turn: every adult's alcoholism moved by the rules above
///        for the month that closed, crossings said, and every resident's
///        days_worked_this_month cleared.
/// @param life_speedup LifeConfig::life_speedup, for the biological age.
/// @pre Called on the first day of a month, before anything counts a day of
///      the new month.
void TurnAlcoholismMonth(const AlcoholismConfig& config, float life_speedup, WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_ALCOHOLISM_H_
