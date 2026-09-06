// The figure rule of core_common/body.h.

#include "core_common/body.h"

#include <cstdint>

namespace core {
namespace {

/// The salts of the height's run and the build's run are spread far enough
/// apart that the two runs cannot overlap: an overlap would tie a person's
/// width to their height, which is a correlation nobody asked for and which
/// would show as a village of identical silhouettes.
constexpr std::uint64_t kSaltStride = 0x100;

/// How many uniforms are summed to make one normal.
///
/// TWELVE, AND NOT BOX-MULLER, and the reason is the one hard requirement
/// this core has: the same input must give the same answer under Clang and
/// under MSVC. Box-Muller needs `std::log` and `std::cos`, and neither is
/// required to be correctly rounded — two libms may differ in the last bit.
/// That is tolerable for a number nobody keeps and NOT tolerable for these
/// two, because they are stored in the state and written into the save.
///
/// The state model holds cross-compiler convergence by keeping everything
/// that must converge an integer behind an integer gate. A float out of a
/// transcendental was the first thing in the SAVED state that did not fit
/// that argument, and the static-analysis cycle named it before it shipped
/// (RACE-001, 2026-09-06).
///
/// Summing twelve uniforms and subtracting six gives mean 0 and variance 1
/// exactly, out of ADDITION ALONE — and IEEE-754 addition is correctly
/// rounded by the standard, so the sum is bit-identical wherever it runs.
/// The tails past six sigma that a true normal has are missing, and that
/// costs nothing here: the draw is cut at 2.5 sigma two lines later.
constexpr std::uint32_t kUniformsPerNormal = 12;

/// THE WIDEST A FIGURE MAY EVER BE, whatever the knobs say: a person is
/// nine tenths off their base at the very most.
///
/// TWO NUMBERS EACH INSIDE ITS OWN BAND ARE NOT A BAND. `height_sigma_frac`
/// is read against 0..0.5 and `clamp_sigma` against 0.5..6, and both bands
/// are sane — but it is their PRODUCT that reaches the person, and a legal
/// pair multiplies out to 3. `base * (1 + deviation)` is then NEGATIVE, and
/// the seam hands the layer a man of minus five metres. Found by the
/// static-analysis cycle (UB-001, 2026-09-06), which also pointed out that
/// genesis already does the right thing seventy lines from the same place:
/// the old-house wear band is checked AS A PAIR after both cells are read.
///
/// It is a cap and not a refusal because these are figures and not
/// mechanics: a village of very odd silhouettes is a table somebody should
/// look at, while a village of negative people is a crash three systems
/// away.
constexpr float kWidestDeviation = 0.9F;

/// @brief How far from the base a person may stand, in fractions.
float DeviationBand(float sigma_frac, float clamp_sigma) {
  const float band = sigma_frac * clamp_sigma;
  return band < kWidestDeviation ? band : kWidestDeviation;
}

/// @brief Holds a deviation inside the band, whichever way it got there.
float HoldInsideTheBand(float deviation, float band) {
  return deviation < -band ? -band : (deviation > band ? band : deviation);
}

}  // namespace

float DrawBodyDeviation(std::uint64_t world_seed,
                        std::uint64_t person,
                        std::uint64_t salt,
                        float sigma_frac,
                        float clamp_sigma) {
  // Written positively so a nonsense knob falls back to "no deviation"
  // rather than through: a sigma of nan would poison every figure in the
  // village, and a village of NaN-tall people shows up three systems away
  // from its cause.
  if (!(sigma_frac > 0.0F) || !(clamp_sigma > 0.0F)) {
    return 0.0F;
  }
  // The tick is 0 for every one of these: a person's figure is fixed for
  // life, so the position must not move with the calendar.
  float sum = 0.0F;
  for (std::uint32_t index = 0; index < kUniformsPerNormal; ++index) {
    sum += CounterHashUnitFloat(world_seed, 0, person, salt + index);
  }
  const float normal = sum - (static_cast<float>(kUniformsPerNormal) * 0.5F);
  const float clamped =
      normal < -clamp_sigma ? -clamp_sigma : (normal > clamp_sigma ? clamp_sigma : normal);
  return HoldInsideTheBand(clamped * sigma_frac, DeviationBand(sigma_frac, clamp_sigma));
}

void RollBody(std::uint64_t world_seed,
              std::uint64_t person,
              const BodyKnobs& knobs,
              ResidentRow& row) {
  row.height_deviation =
      DrawBodyDeviation(world_seed, person, 0, knobs.height_sigma_frac, knobs.clamp_sigma);
  row.build_deviation =
      DrawBodyDeviation(world_seed, person, kSaltStride, knobs.build_sigma_frac, knobs.clamp_sigma);
}

void RollBodyFromParents(std::uint64_t world_seed,
                         std::uint64_t person,
                         const BodyKnobs& knobs,
                         const ResidentRow& mother,
                         const ResidentRow& father,
                         ResidentRow& child) {
  // HALF THE SPREAD ON TOP OF THE PARENTS' MEAN (boss, 2026-09-06): a lanky
  // father and his lanky sons read as kin from a hundred metres with no
  // label on anybody, which is what a live signal is.
  const float height = DrawBodyDeviation(
      world_seed, person, kSaltStride * 2U, knobs.height_sigma_frac * 0.5F, knobs.clamp_sigma);
  const float build = DrawBodyDeviation(
      world_seed, person, kSaltStride * 3U, knobs.build_sigma_frac * 0.5F, knobs.clamp_sigma);
  // AND THE SUM IS HELD INSIDE THE BAND TOO, which the first version did not
  // do. Half a spread added on top of an already-clamped parental mean
  // reaches past the cut in ONE generation — 13.9 % against the 9 % the
  // header promises — and the reachable bound then creeps outward by about
  // 0.046 every generation after. The population does not run away (the
  // recurrence contracts in variance), but the PROMISE did, and a bound
  // that only the comment believes in is worse than none.
  const float height_band = DeviationBand(knobs.height_sigma_frac, knobs.clamp_sigma);
  const float build_band = DeviationBand(knobs.build_sigma_frac, knobs.clamp_sigma);
  child.height_deviation = HoldInsideTheBand(
      ((mother.height_deviation + father.height_deviation) * 0.5F) + height, height_band);
  child.build_deviation = HoldInsideTheBand(
      ((mother.build_deviation + father.build_deviation) * 0.5F) + build, build_band);
}

float HeightMeters(const ResidentRow& person, const BodyKnobs& knobs, bool adult) {
  if (!adult) {
    return 0.0F;  // no base for the steps of childhood; see the header
  }
  const float base = person.sex == Sex::kMale ? knobs.height_male_m : knobs.height_female_m;
  return base * (1.0F + person.height_deviation);
}

}  // namespace core
