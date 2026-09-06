/// @file
/// @brief The vocabulary of the stink field: how strongly a place smells,
/// and when a source emits at all.
/// @threading PARALLEL_READONLY
/// Plain values and pure functions over them. Read from anywhere; the field
/// itself is walked between steps on the sim thread, through the
/// construction subsystem that owns the source table.
///
/// WHY THE BAND AND NOT A NUMBER. The design gives this field three readers
/// and every one of them is a LIVE SIGNAL, not a gauge: the chairman coughs
/// in a strong zone, winces in a medium one and notices nothing in a weak
/// one; the flies buzz where the air is bad; a house may not be built where
/// it stinks (water design §4). None of the three has anything to do with a
/// percentage, and publishing one would invite the layer to pick its own
/// thresholds — which is the same mechanic-with-two-answers this core has
/// been closing all week. So the seam carries the band the design speaks in.
///
/// AND THE WIND IS NOT MISSING FROM IT. Direction does not enter this field
/// at all, and that is a decision and not a gap (water design §4, "Ветер не
/// учитываем"): a wind rose would be a whole system bought for a
/// plausibility the player cannot follow. Written here so that nobody
/// finds the absence in a month and files it as unfinished work.

#ifndef CORE_COMMON_STINK_H_
#define CORE_COMMON_STINK_H_

#include <cstdint>

namespace core {

/// @brief How badly a place smells, or how badly a source smells at its
/// core. The four values the design names (water design §4).
///
/// ORDERED, and the order is the whole point: kNone < kWeak < kMedium <
/// kStrong, so "the worse of two" is a comparison and not a table.
enum class StinkStrength : std::uint8_t {
  kNone = 0,  ///< Clean air. Silence, for the flies that read it.
  kWeak,      ///< The background of a village. The chairman notices nothing.
  kMedium,    ///< He winces and waves a hand.
  kStrong,    ///< He coughs, gags, covers his face with a sleeve.

  /// NOT A VALUE: the number of them, for a consumer's mirror. Values are
  /// appended BEFORE it.
  kStinkStrengthCount,
};

/// @brief When a source emits (water design §4, and it is a MECHANIC and
/// not a turn of phrase).
enum class StinkWhen : std::uint8_t {
  /// The contents smell, not the work: a manure heap, a sty, the poultry
  /// farms, a silage trench. A farm cannot be switched off — the animals
  /// smell on an idle day too.
  kAlways = 0,

  /// The work smells: a tannery, a slaughterhouse, a forge, a dye house, a
  /// smokehouse. "No stock, nobody to work it, the unit is stopped — the
  /// source goes out."
  kWorking,
};

/// @brief The worse of two bands.
///
/// WHERE TWO ZONES OVERLAP, THE WORSE ONE WINS — and this is an ASSUMPTION,
/// named as one because the design does not say. It says the opposite for
/// two sources it defers: street latrines are "weak, but they ADD UP in
/// dense building", and stove smoke "adds up by the density of the
/// building". Neither is in the source table today, and both are their own
/// delivery; when they arrive, THEY bring the adding rule with them, for
/// themselves, and this one stays what it is for the thirteen named
/// sources. Taking the worse of two is the reading that matches how the
/// design speaks everywhere else — of a PLACE being in a strong or a medium
/// zone, never of two mediums making a strong.
constexpr StinkStrength WorseStink(StinkStrength left, StinkStrength right) {
  return left > right ? left : right;
}

}  // namespace core

#endif  // CORE_COMMON_STINK_H_
