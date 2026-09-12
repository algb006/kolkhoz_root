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

  /// HOW TALL A CHILD IS AS A FRACTION OF THE ADULT OF THE SAME SEX, by step
  /// of childhood (world_params.csv, 2026-09-13). One fraction for both
  /// sexes on purpose: the difference between a man and a woman already sits
  /// in the number the fraction multiplies, and two rows per step would be
  /// two places for one number to drift (boss, the same argument the sigma
  /// is single for).
  ///
  /// THE FRACTIONS ARRIVED WITHOUT THEIR BANDS, and for half an hour they
  /// were unusable: the steps of childhood lived as NAMES in the asset work
  /// and had no boundary in years anywhere in the tables. Boss named them the
  /// same night (world_params.csv, age_*_from_years), and the pair is the
  /// rule: a fraction without a band is a name without a value.
  float height_infant_frac = 0.45F;

  float height_preschool_frac = 0.65F;

  float height_school_junior_frac = 0.78F;

  float height_school_senior_frac = 0.93F;

  /// WHERE ONE STEP OF CHILDHOOD ENDS AND THE NEXT BEGINS, in biological
  /// years (world_params.csv age_*_from_years; boss, 2026-09-13, drawn out of
  /// the design's prose). Below the first a person is an infant.
  ///
  /// THESE ARE THE BANDS OF THE FIGURE AND NOT OF WORK, and the two are
  /// different numbers on purpose (boss's caveat with the same message): the
  /// labour bands are 7-12 and 13-16, and folding either pair into the other
  /// would make one table row answer two questions — which is the defect this
  /// project has spent two days naming.
  float age_preschool_from_years = 3.0F;

  float age_school_junior_from_years = 7.0F;

  float age_school_senior_from_years = 11.0F;

  /// Adulthood, in the same units. IT IS ALSO IN life.csv as
  /// `adult_age_years`, and that is a second home for one fact: the two are
  /// 16 today and nothing keeps them equal. Named here rather than quietly
  /// preferred — the figure reads this one because the four bands must come
  /// from one row set or the steps can overlap.
  float age_adult_from_years = 16.0F;
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

/// @brief How tall this person is in metres, at any age.
///
/// AN ADULT IS THE BASE OF THEIR SEX AND A CHILD IS A FRACTION OF IT — the
/// fraction of the step they are at (BodyKnobs, world_params.csv). One
/// fraction for both sexes on purpose: the difference between a man and a
/// woman already sits in the number the fraction multiplies, and two rows per
/// step would be two places for one number to drift.
///
/// IT ANSWERED 0 FOR A CHILD UNTIL 2026-09-13, and said so in this header:
/// the world carried a base for a man and a woman and nothing for the steps
/// of childhood. The fractions arrived that night; the BANDS arrived an hour
/// later, and the hour in between is the whole lesson — a fraction with no
/// band is a name with no value, and nothing could be done with it.
///
/// The person's own deviation applies at every age: it is a fact about them
/// and not about their present size.
/// @param age_years Biological age. The bands live in `knobs`, so the caller
///        supplies the age and nothing else — it does not need the rule.
float HeightMeters(const ResidentRow& person, const BodyKnobs& knobs, float age_years);

}  // namespace core

#endif  // CORE_COMMON_BODY_H_
