// A SECOND translation unit, and its only job is to have a different address
// than the first one for the same constant.
//
// The version pin (core_common/version_pin.h) rests on one property of
// core_common/version.h: the version constants have INTERNAL linkage, so the
// library keeps the number it was built from and a consumer keeps its own.
// Write `inline constexpr` instead and they become one entity per program;
// MSVC then folds the library's copy away and the pin passes on a mismatched
// pair — measured on the host, at every optimisation level.
//
// A single-publish test cannot see that: everything agrees with itself. What
// it CAN see is the linkage, and this is how. Two translation units take the
// address of the same constant; internal linkage gives two objects and two
// addresses, external linkage gives one of each.

#include "core_common/version.h"

namespace core_test {

const char* const* VersionStringAddressFromOtherTu() {
  return &core::kCoreVersionString;
}

}  // namespace core_test
