/// @file
/// @brief The figure of a person: how tall and how broad, as fractions of
/// the base of their sex, and the one rule that draws them.
/// @threading PARALLEL_READONLY
/// Pure functions of a seed, an id and the knobs — they hold no state and
/// touch none, so any thread may ask them anything. Used at birth on the sim
/// thread: by genesis for the starting generation, by the demography
/// sub-step for everyone born or arriving after.
///
/// WHY THE RULE LIVES HERE AND NOT WHERE PEOPLE ARE BORN. There are three
/// places a person comes into the world — the founding, a birth, an arrival
/// — and a draw written three times is three answers to one question, which
/// is the shape of nearly every defect this core has chased. The VALUE of
/// each knob has one home (world_params.csv); the RULE has this one.
///
/// AND THE SEAM CARRIES METRES, once, from here. The core is the only place
/// that holds both halves — the person's fraction and the base of their sex
/// — so it is the only place that can answer "is the wife taller than her
/// husband", which the design asks for by name. The graphics layer does not
/// read the metres: it scales bone by the FRACTION, and builds its own
/// figure from the same base.

#ifndef CORE_COMMON_BODY_H_
#define CORE_COMMON_BODY_H_

#include <cstdint>

#include "core_common/random.h"
#include "core_common/resident_state.h"

namespace core {

/// @brief The knobs of the figure, from world_params.csv.
///
/// ONE SPREAD FOR BOTH SEXES, deliberately (boss, 2026-09-06): the
/// difference between 6.5 cm and 6.0 cm of sigma is invisible in play, and
/// two rows would be two places for one number to drift apart in.
struct BodyKnobs {
  /// Base height in metres, by sex. From the asset generator's own figures,
  /// so that the core and the model are built off one number.
  float height_male_m = 1.66F;

  float height_female_m = 1.58F;

  /// Standard deviation of the height, as a FRACTION of the base.
  float height_sigma_frac = 0.037F;

  /// The draw is cut at this many sigma, so the tail cannot produce a
  /// person no mesh can carry.
  float clamp_sigma = 2.5F;

  /// The same for width.
  float build_sigma_frac = 0.07F;
};

/// @brief One draw from a clamped normal distribution, in fractions.
///
/// A COUNTER HASH AND NOT THE WORLD'S RNG STREAM, and this is the whole
/// design of the field rather than a detail of it. Drawing from the stream
/// would have cost four draws per person — and a draw taken is a draw every
/// LATER decision no longer gets, so adding the figure would have quietly
/// rerolled the whole village: different stamina, different weddings,
/// different harvest. It did, in the first version of this code, and the
/// balance runs caught it — the year's labour fell just under its reference
/// band and truancy stopped happening at all.
///
/// > **A NEW FACT MUST NOT MOVE THE FACTS THAT WERE ALREADY THERE.**
///
/// So the figure is a pure function of (world seed, person, salt), exactly
/// as the weather is a pure function of (seed, day). Nothing is consumed,
/// nothing is stored, and the same person in the same world is the same
/// height however many times you ask.
///
/// Clamped rather than resampled for the same reason at a smaller scale:
/// resampling consumes an unpredictable number of positions and makes the
/// sequence depend on the values it produced.
float DrawBodyDeviation(std::uint64_t world_seed,
                        std::uint64_t person,
                        std::uint64_t salt,
                        float sigma_frac,
                        float clamp_sigma);

/// @brief Fills a newly made person's figure with an independent draw.
/// @param person The id this person WILL have (StateTable::next_id_value
///        before the append): the draw is keyed by it, so the figure is the
///        same on a replay and after a load.
void RollBody(std::uint64_t world_seed,
              std::uint64_t person,
              const BodyKnobs& knobs,
              ResidentRow& row);

/// @brief Fills a child's figure: the parents' mean plus a draw of HALF the
/// spread (boss's design addition, 2026-09-06).
///
/// WHY INHERITANCE AT ALL, since the human did not ask for it: families
/// become recognisable. A lanky father and his lanky sons read as kin from
/// a hundred metres with no label on anybody, which is exactly what a live
/// signal is. Remove this function and heights stay merely random — the
/// mechanic degrades rather than breaks.
void RollBodyFromParents(std::uint64_t world_seed,
                         std::uint64_t person,
                         const BodyKnobs& knobs,
                         const ResidentRow& mother,
                         const ResidentRow& father,
                         ResidentRow& child);

/// @brief How tall this person is in metres.
///
/// ADULTS ONLY, and that is named rather than forgotten: the world has no
/// base height for the steps of childhood — the asset generator carries two
/// figures, a man and a woman — so a child's height in metres is a question
/// nobody can answer, and this returns 0 for one. The FRACTION is valid at
/// every age, because it is a fact about the person and not about their
/// current size.
/// @param adult Whether this person has reached adulthood; the caller knows
///        the age rule and this header must not learn it.
float HeightMeters(const ResidentRow& person, const BodyKnobs& knobs, bool adult);

}  // namespace core

#endif  // CORE_COMMON_BODY_H_
