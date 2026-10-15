/// @file
/// @brief «Нет ресурса — нет транспорта»: whether the chairman's horse stands
///        at the office's hitching post, and why (camera-and-visuals.md;
///        task 957, boss core-boss-957-transport-doors-2026-10-16 [2]).
/// @threading SINGLE_THREADED
/// Plain data, built from the completed world between steps. The chairman's
/// horse has no head in any herd: it is «вечная» and eats from the common
/// stable (the human, 15 October 2026), so it stands while the kolkhoz
/// horses at a stable got any feed today. The answer changes at most once a
/// game day, at the day's first tick, when the herd day writes the ration.
/// Whether he is in the saddle is the layer's: it applies a change on
/// dismount, never under the rider.
#ifndef CORE_COMMON_CHAIRMAN_TRANSPORT_VIEW_H_
#define CORE_COMMON_CHAIRMAN_TRANSPORT_VIEW_H_

#include <cstdint>

namespace core {

/// @brief The ground the horse's presence was read on. Appended, never
/// renumbered.
enum class HorsePostGround : std::uint8_t {
  /// Some kolkhoz horse herd at a unit got a share of today's ration
  /// (HerdRow::fed_share > 0): present.
  kFed = 0,

  /// Every kolkhoz horse herd at a unit got nothing today — «корм кончился»,
  /// not a short ration (boss [2] Q1): absent.
  kNoFeedToday,

  /// No kolkhoz horse herd stands at a unit yet — the start's horses live in
  /// the private yards until the horse yard stands and a groom is appointed
  /// (herd_state.h). Present (boss [2] Q2): the prologue shows the horse, and
  /// the rule does not punish the first days.
  kNoStableYet,
};

/// @brief The chairman's motor transport. Appended, never renumbered.
enum class ChairmanVehicle : std::uint8_t {
  /// STUB: the only value. Epoch I has no motorcycle and no fuel stock
  /// (farm-office.md §4: «Лошадь, с Эпохи II мотоцикл»); the fuel half of the
  /// rule is built with Epoch II's vehicles (boss [2] Q4), and this enum grows
  /// then.
  kNone = 0,
};

/// @brief The answer to «стоит ли транспорт председателя на месте».
///
/// No water half: the core has no livestock water — a farm unit's well is
/// inside its plot and «механики жажды у нас нет» (water.md §1; boss [2] Q3).
struct ChairmanTransportView {
  /// The layer shows the horse at the hitching post when 1, an empty post
  /// when 0.
  bool horse_at_post = true;

  /// Why `horse_at_post` reads as it does.
  HorsePostGround horse_ground = HorsePostGround::kNoStableYet;

  ChairmanVehicle vehicle = ChairmanVehicle::kNone;
};

}  // namespace core

#endif  // CORE_COMMON_CHAIRMAN_TRANSPORT_VIEW_H_
