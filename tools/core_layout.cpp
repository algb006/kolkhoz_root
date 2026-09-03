/// @file
/// @brief core_layout — prints the byte layout of the state rows the boundary
/// hands over, as the compiler that built THIS library sees it.
/// @threading SINGLE_THREADED
/// A main() that prints seven numbers and exits. No simulation, no state, no
/// second thread — the label is here because every translation unit of this
/// project carries one, and a tool that publishes a check should not be the
/// file that skips one.
//
// Why an executable and not a header of constants: sizeof is a fact of the
// build, and the whole point is to catch a publication whose include/ and
// lib/ came from different builds. A number computed by the consumer's own
// compiler from our headers would agree with itself and prove nothing; this
// one is produced by the build that made the library, written beside it, and
// compared by whoever links (the graphics layer asked for it after task A3's
// FieldRow grew in the headers while the published archive stayed behind —
// the field table then arrived as confident nonsense).
//
// One line per row, "name size", plus the version. Deliberately dull: it is
// read by a script and by a person in a hurry.

#include <cstddef>
#include <iostream>

#include "core_common/family_state.h"
#include "core_common/herd_state.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/unit_state.h"
#include "core_common/version.h"
#include "core_common/world_state.h"

int main() {
  std::cout << "version " << core::kCoreVersionString << '\n';
  std::cout << "WorldState " << sizeof(core::WorldState) << '\n';
  std::cout << "ResidentRow " << sizeof(core::ResidentRow) << '\n';
  std::cout << "FamilyRow " << sizeof(core::FamilyRow) << '\n';
  std::cout << "FieldRow " << sizeof(core::FieldRow) << '\n';
  std::cout << "UnitRow " << sizeof(core::UnitRow) << '\n';
  std::cout << "HerdRow " << sizeof(core::HerdRow) << '\n';
  std::cout << "OrderRow " << sizeof(core::OrderRow) << '\n';
  return 0;
}
