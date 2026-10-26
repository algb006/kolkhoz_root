/// @file
/// @brief The herd window's plan line (#1289; livestock.md «План стада —
///        "держать не больше N голов"»; registry 319): «сена на 39 · мест 60 ·
///        держим 39» and its warnings, one line per kolkhoz herd.
/// @threading SINGLE_THREADED
/// Plain data, built on the sim thread between steps and copied out.
///
/// THE PLAN IS A NEW GATE ON TOP OF THE ROOF SHARE: since 1 October 2026 a
/// herd calves by its share under the roof (herd_life.cpp CalvingRoofShare);
/// the plan adds «a birth only while the heads are under N». The old «приплод
/// только на свободное место» is not the core's rule and must not be read
/// back from this line.

#ifndef CORE_COMMON_HERD_PLAN_VIEW_H_
#define CORE_COMMON_HERD_PLAN_VIEW_H_

#include <cstdint>

#include "core_common/ids.h"

namespace core {

/// @brief One kolkhoz herd's plan line.
struct HerdPlanLine {
  HerdId herd;
  LivestockKindId kind;
  /// Newborns, juveniles and adults: what N measures (boss, core-boss-1289 [2] A).
  std::uint16_t heads = 0;
  /// «мест»: the herd's places, HerdPlaces (its unit type's room less the
  /// other herds' heads there) - the plan's ceiling.
  std::uint16_t places = 0;
  /// «держим»: the chairman's N, or `places` while no plan is set.
  std::uint16_t plan = 0;
  /// Whether the chairman has set N (else the game runs as before: no gate).
  bool plan_set = false;
  /// «сена на»: the heads this herd's hay feeds to the hay lamp's horizon,
  /// every other herd at its own plan. STUB until #1289's body: 0 and
  /// `hay_for_known` false.
  std::uint16_t hay_for = 0;
  bool hay_for_known = false;
  /// «Сена на» with the other milk-kind and horse herds at their FLOOR (every
  /// other kind at its plan): econ's order of the hay, «to the floors of both
  /// herds first, then to the horses» (core-boss-1289 [10] (2)). The run's
  /// bot sizes the horses on it; the window shows `hay_for`.
  std::uint16_t hay_for_others_at_floor = 0;
  /// «по норме»: `hay_for` counted on the mowing norm, not the village's own
  /// cut (the first year before the mowing). STUB until the body.
  bool hay_for_by_norm = false;
  /// The window's three warnings (the words are rpg's). STUB until the body:
  /// false. N under the milk plan's cows; N under the spring's teams (horses);
  /// N under two - no birth at all.
  bool below_milk_floor = false;
  bool below_teams = false;
  bool no_births = false;
  /// The floor's own number, for the window's words («меньше, чем нужно плану
  /// молока (26)», «весной не хватит упряжек (нужно 18)»): the milk plan's
  /// cows for the milk kind, the spring's horses for the horse, 0 for every
  /// other kind (HerdFloors).
  std::uint16_t floor_heads = 0;
};

}  // namespace core

#endif  // CORE_COMMON_HERD_PLAN_VIEW_H_
