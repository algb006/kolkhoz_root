/// @file
/// @brief What the core says of a sown grass stand on a field — the answer
/// the layer shows BEFORE the chairman orders the stand renewed (0.37.212;
/// boss's ruling of 10 October 2026: «the loss is said before it is taken»).
/// @threading SINGLE_THREADED
/// A value handed out between steps on the sim thread; no behaviour.
///
/// A crop with crops.csv `stand_ages` (clover, timothy) lives by its stand's
/// summer: the table's yield × the summer's factor, fertility banked only in
/// the summers grass_stand.csv says. kSetRotation given to a field under such
/// a stand ENDS IT AT ONCE while it is growing and is refused
/// (kConflictsWithActive) while it is being cut.

#ifndef CORE_COMMON_GRASS_STAND_VIEW_H_
#define CORE_COMMON_GRASS_STAND_VIEW_H_

#include <cstdint>

#include "core_common/quantities.h"

namespace core {

/// @brief A field's sown grass stand, or `stands` false for a field that has
///        none — no such field, a meadow, bare ground, any other crop.
struct GrassStandView {
  bool stands = false;

  /// The summer the stand is in, 1 from the year it was sown.
  std::uint8_t summer = 0;

  /// The summers grass_stand.csv describes one by one; from the last of them
  /// on the stand gives that row's factor for ever. 0 — no table: no age.
  std::uint8_t summers_described = 0;

  /// The factor of the table's yield this summer, 0..1.
  float yield_factor = 0.0F;

  /// Whether this summer's cut adds the crop's fertility to the field.
  bool banks_fertility = false;

  /// The hay still standing uncut on the field at the stand's own estimate
  /// (fertility, weather, the summer's factor) — WHAT A RENEWAL ORDERED NOW
  /// WOULD LOSE. Grams of the crop's resource; 0 after this year's cut.
  Grams hay_standing = 0;

  /// The stand is being cut: kSetRotation is refused kConflictsWithActive
  /// until the mowing is through.
  bool being_cut = false;
};

}  // namespace core

#endif  // CORE_COMMON_GRASS_STAND_VIEW_H_
