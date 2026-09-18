/// @file
/// @brief The sports field and the sportiness it grows — the first of the
/// four levers against the drinking village (register 223; leisure §12;
/// metrics §2 «Спортивность в Эпохе I — числами»; question 225; boss seq
/// 123-127).
/// @threading SINGLE_THREADED
/// Called from the residents' decisions sub-step (phase 3) on the sim thread:
/// the day's count, and the month's reading from inside the drinking's turn
/// (alcoholism.cpp).
///
/// WHO GOES (register 223, boss seq 127): a resident of `goer_age_max`
/// years or under and alcoholism at or under `goer_alcohol_max` goes by
/// himself, when a stadium of step 1 or more stands within `field_radius_m`
/// of his home. The chairman's talk (lever ③) will add the others. The rest
/// of the hidden urge to train (metrics §2) has no numbers and is not built.
///
/// THE READING HUT (lever ②; register 223; crime §6; econ §2.2; boss seq
/// 129): a `culture_house` of step 1 or more within `hut_radius_m` of a
/// man's home, in a winter month (December to February), takes the winter
/// idleness +1 off him, and −`hut_alcohol_loss` off a goer — the same goer as
/// the field's, measured from the hut. The hut is open every evening: no
/// weather, no open days. It grows no sportiness — it is not sport. One
/// «goes» per month: the field's and the hut's losses do not add up.
/// The hut is open only while somebody holds its post — its librarian (boss
/// seq 133: «без человека изба — пустой сруб»); the stadium needs no one.

#ifndef CORE_RESIDENTS_SPORT_H_
#define CORE_RESIDENTS_SPORT_H_

#include <span>
#include <string>
#include <string_view>

#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {

/// @brief The field's and the sportiness's numbers (world_params.csv, boss's
/// export; STUB, econ's figures appointed by boss). Defaults for a world
/// with no tables.
struct SportConfig {
  UnitTypeId stadium_type;               ///< unit_types.csv "stadium"
  float field_radius_m = 1000.0F;        ///< `sport_field_radius_m`
  float open_temp_c = 10.0F;             ///< `sport_open_temp_c`, the day's mean
  float open_days_min = 2.0F;            ///< `sport_open_days_min`, of the month's four
  float goer_age_max = 30.0F;            ///< `sport_goer_age_max`, under
  float goer_alcohol_max = 20.0F;        ///< `sport_goer_alcohol_max`, at or under
  float field_alcohol_loss = 1.0F;       ///< `sport_field_alcohol_loss`
  float gain_goer = 3.0F;                ///< `sportiness_gain_goer`
  float gain_drinker_share = 0.5F;       ///< `sportiness_gain_drinker_share`
  float drinker_above = 40.0F;           ///< `sportiness_drinker_above`
  float decay = 1.0F;                    ///< `sportiness_decay`, a month without
  float decay_old_from = 40.0F;          ///< `sportiness_decay_old_from`, years
  float decay_old_extra = 1.0F;          ///< `sportiness_decay_old_extra`
  float sober_from = 30.0F;              ///< `sportiness_sober_from`
  float sportiness_alcohol_loss = 1.0F;  ///< `sportiness_alcohol_loss`
  UnitTypeId culture_house_type;         ///< unit_types.csv "culture_house"
  float hut_radius_m = 1000.0F;          ///< `reading_hut_radius_m`
  float hut_alcohol_loss = 1.0F;         ///< `reading_hut_alcohol_loss`
};

/// @brief The world_params.csv keys this file reads.
std::span<const std::string_view> SportWorldParamKeys();

/// @brief Reads the knobs and the stadium's type.
/// @return false with `error` naming the key for a value out of range.
bool ParseSportConfig(const ITableSet& tables, SportConfig& config, std::string& error);

/// @brief The day's count: an open day — mean at `open_temp_c` or over, no
/// precipitation, not the day after a downpour (heavy hours) — adds one to
/// the month's open days; then today's downpour is remembered for tomorrow.
/// @pre Once a day, after the month's turn (the day counts in the new month).
void CountSportDay(const SportConfig& config, WorldState& current);

/// @brief Whether the month that closed COUNTS: its open days reached
/// `open_days_min`.
bool SportMonthCounted(const SportConfig& config, const WorldState& world);

/// @brief Whether this resident went to the field in a counted month: goes
/// by himself (age and alcoholism, see @file) and a stadium stands within
/// the radius of his home.
bool GoesToTheField(const SportConfig& config,
                    const WorldState& world,
                    const ResidentRow& person,
                    float age_years);

/// @brief Whether an open reading hut (culture_house, step 1 or more, its
/// post held) stands within `hut_radius_m` of this resident's home. False
/// with no home. Scans the residents for the post: called monthly.
bool ReachesTheHut(const SportConfig& config, const WorldState& world, const ResidentRow& person);

/// @brief Whether this resident goes by himself, to the field or the hut:
/// under `goer_age_max` years and at or under `goer_alcohol_max` (@file).
bool GoesBySelf(const SportConfig& config, const ResidentRow& person, float age_years);

/// @brief The month's sportiness for a resident of 16 and over: +gain_goer
/// if he went (× gain_drinker_share above `drinker_above` alcoholism), else
/// −decay; −decay_old_extra more past `decay_old_from` years. Clamped 0..100.
void TurnSportiness(const SportConfig& config, bool went, float age_years, ResidentRow& person);

}  // namespace core

#endif  // CORE_RESIDENTS_SPORT_H_
