/// @file
/// @brief Membership of the three organizations — pioneers, komsomol, party —
/// and the character trait that decides it, ideology (society design §3, §5;
/// metrics design §2 "Что складывается в детстве", "И статус растит
/// идейность"; boss, parcel 334).
/// @threading SINGLE_THREADED
/// Runs in the residents' decisions sub-step (phase 3) on the sim thread: the
/// waves on their days, the year's ideology at the year's turn, the birth's
/// ideology inside demography. It writes ResidentRow::social_status, ideology
/// and the crime metrics, and emits events, so it can only live in a
/// sequential slot.
///
/// WHAT DECIDES, in boss's numbers (assigned, not measured):
///   * two waves a year for all three organizations — the first day of
///     September (autumn) and of March (spring); leaving by age happens on the
///     wave's day too;
///   * pioneers 10–14, at school age, crime metrics at zero; a grade of 4 in
///     the autumn wave, of 3 in the spring wave; out at 14;
///   * komsomol 14–26, ideology ≥ 45 (≥ 35 for a pioneer), mood ≥ 40, crime at
///     zero; out at 26 unless the party took him;
///   * party from 18, ideology ≥ 65, mood ≥ 50, crime at zero, and 25 years old
///     or ideology ≥ 80; joining takes him out of the komsomol; for life;
///   * joining zeroes the crime metrics (society §5, "вступление обнуляет");
///   * ideology at birth is the parents' mean ± 15; once a year until 16: +2
///     at school age, +2 as a pioneer, −3 in a hungry year (the settlement's
///     mean satiety of the year below 50); fixed at 16;
///   * while in an organization, once a year: +0.5 pioneer, +1 komsomol, +1.5
///     party, never past 80 by this road; what was gained stays on leaving;
///   * one wave on the campaign's first day, so the start has members;
///   * a teacher or librarian of up to 26 arrives a komsomol member.
///
/// STUB, each with its place: "учится в школе" is the school-age band until
/// the pupils' enrollment door (queue item 4); a grade the core does not yet
/// keep (current_grade 0) passes the spring wave and not the autumn one; the
/// party's recommendations and the family's standing in the village are not
/// asked (the standing is the host's); drinking parents and a far school do
/// not lower a child's ideology; mood does not move in the core.

#ifndef CORE_RESIDENTS_MEMBERSHIP_H_
#define CORE_RESIDENTS_MEMBERSHIP_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core_common/random.h"
#include "core_common/resident_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {

/// @brief Which wave a day is.
enum class MembershipWave : std::uint8_t {
  kNone = 0,
  kAutumn,  ///< The first day of `autumn_wave_month`: good pupils first.
  kSpring,  ///< The first day of `spring_wave_month`: the rest.
};

/// @brief The organizations' and ideology's numbers (world_params.csv).
/// Defaults are boss's figures of parcel 334, kept for a world with no tables.
/// Each field's key is its own name, except the two months, which are
/// `membership_autumn_wave_month` and `membership_spring_wave_month`.
struct MembershipConfig {
  std::uint8_t autumn_wave_month = 9;
  std::uint8_t spring_wave_month = 3;

  float pioneer_age_from_years = 10.0F;
  float pioneer_age_to_years = 14.0F;
  float pioneer_autumn_grade_min = 4.0F;
  float pioneer_spring_grade_min = 3.0F;

  float komsomol_age_from_years = 14.0F;
  float komsomol_age_to_years = 26.0F;
  float komsomol_ideology_min = 45.0F;
  float komsomol_pioneer_ideology_min = 35.0F;
  float komsomol_mood_min = 40.0F;

  float party_age_from_years = 18.0F;
  float party_ideology_min = 65.0F;
  float party_mood_min = 50.0F;
  float party_seniority_age_years = 25.0F;
  float party_early_ideology_min = 80.0F;

  float ideology_birth_spread = 15.0F;
  float ideology_lock_age_years = 16.0F;
  float ideology_school_year_gain = 2.0F;
  float ideology_pioneer_year_gain = 2.0F;
  float ideology_hungry_year_loss = 3.0F;
  float ideology_hungry_satiety_below = 50.0F;

  float ideology_status_gain_pioneer = 0.5F;
  float ideology_status_gain_komsomol = 1.0F;
  float ideology_status_gain_party = 1.5F;
  float ideology_status_cap = 80.0F;

  /// A specialist the district sends is a komsomol member up to this age.
  float specialist_komsomol_age_max_years = 26.0F;
};

/// @brief The world_params.csv keys this file reads, for the assembly's
/// declared-readers check (core_world/world.cpp).
std::span<const std::string_view> MembershipWorldParamKeys();

/// @brief Reads the knobs. A missing table or key keeps the default.
/// @return false with `error` naming the key for a value out of range — a
///         month outside 1..12, an age band upside down, a threshold outside
///         0..100, a negative gain.
bool ParseMembershipConfig(const ITableSet& tables, MembershipConfig& config, std::string& error);

/// @brief The wave `day` is, by its month and day in the month.
MembershipWave WaveOfDay(const MembershipConfig& config, SimDay day);

/// @brief What organization `person` belongs in after a wave, by the rules
///        above: the status kept, joined, changed or lost.
/// @param age_years Biological age, as every age rule of the module.
/// @return The new status; equal to person.social_status when nothing moves.
SocialStatus StatusAfterWave(const MembershipConfig& config,
                             const ResidentRow& person,
                             float age_years,
                             MembershipWave wave);

/// @brief A wave over the whole village: every resident's StatusAfterWave is
///        applied; a status that moved raises kSocialStatusChanged, and a
///        joining zeroes the crime metrics. Residents in row order — the
///        outcome of one does not depend on another, so the order cannot
///        change who joins.
/// @param life_speedup LifeConfig::life_speedup, for the biological age.
void RunMembershipWave(const MembershipConfig& config,
                       float life_speedup,
                       MembershipWave wave,
                       WorldState& current);

/// @brief A newborn's ideology: the mean of the parents present ± the spread,
///        drawn from the world's stream, clamped to 0..100.
/// @param father Null when the child has none.
float BirthIdeology(const MembershipConfig& config,
                    RngState& rng,
                    const ResidentRow& mother,
                    const ResidentRow* father);

/// @brief The year's turn: under the lock age, the childhood's gains and the
///        hungry year's loss; in an organization, the status gain up to the
///        cap. Clamped to 0..100.
/// @param school_from_years, school_to_years The school-age band of the body
///        knobs (age_school_junior_from_years, age_adult_from_years): the
///        STUB of "at school" until the enrollment door.
/// @pre The closing year's settlement satiety has been folded into
///      VitalsState::satiety_year_means (its last element is that year).
void TurnIdeologyYear(const MembershipConfig& config,
                      float life_speedup,
                      float school_from_years,
                      float school_to_years,
                      WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_MEMBERSHIP_H_
