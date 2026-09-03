// The float-to-mass conversions of quantities.h.
//
// They are out of line on purpose: the range constant below is arithmetic
// that belongs in one translation unit, and the header is included by nearly
// every file in the core.

#include "core_common/quantities.h"

#include <cmath>

namespace core {
namespace {

/// The largest mass these conversions will produce, in GRAMS. The hard
/// ceiling of int64 is 9.2e18; this stops three orders of magnitude short of
/// it, which is still a million times the whole map's yearly harvest
/// (quantities.h: under 4000 t = 4e9 g a year). A number above it is not a
/// big harvest, it is a broken cell or a damaged save — and stopping short
/// of the type's own limit leaves room for the sums the caller will do next.
///
/// It is a float constant, and the comparison happens in float, so the
/// bound is exact in the type the argument arrives in: comparing against
/// int64's maximum converted to float would be a bound nobody can name.
constexpr float kMaxGrams = 9.0e15F;

}  // namespace

Grams GramsFromFloat(float grams) {
  // Written as a POSITIVE test so that NaN fails it: NaN compares false
  // against everything, itself included, and `!(x > limit)` would let it
  // through. Every table reader in the core tests its cells the same way.
  if (!(grams >= 0.0F && grams <= kMaxGrams)) {
    return 0;
  }
  return static_cast<Grams>(grams);
}

Grams GramsFromKilograms(float kilograms) {
  return GramsFromFloat(kilograms * static_cast<float>(kGramsPerKilogram));
}

Grams GramsFromTonnes(float tonnes) {
  return GramsFromFloat(tonnes * static_cast<float>(kGramsPerTonne));
}

}  // namespace core
