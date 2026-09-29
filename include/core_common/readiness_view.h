/// @file
/// @brief «Куда я иду»: the village's readiness for the next era as the
///        office shows it from the first day — the two indices against their
///        thresholds, the years they held, the eight parts with their
///        weights, the transition's blocks and the answer an order to go on
///        would get now (epochs design §6; boss-core-early-build [1] p. 6).
/// @threading SINGLE_THREADED
/// Plain data, derived between steps from the completed world and the
/// core's thresholds; nothing here is state. ReadinessState in the world
/// holds the year's scoring; this view adds what only the core knows — the
/// thresholds, the weights, the standing blocks and the verdict — so no
/// consumer repeats the rule.
#ifndef CORE_COMMON_READINESS_VIEW_H_
#define CORE_COMMON_READINESS_VIEW_H_

#include <array>
#include <cstdint>

#include "core_common/order_state.h"
#include "core_common/readiness_state.h"

namespace core {

/// @brief The eight parts of the two indices, in ReadinessState's order.
/// Appended, never renumbered.
enum class ReadinessPart : std::uint8_t {
  kPlan = 0,       ///< Economic, weight 25.
  kWinterStocks,   ///< Economic, weight 20.
  kMechanisation,  ///< Economic, weight 20.
  kFunds,          ///< Economic, weight 15.
  kSatisfaction,   ///< Social, weight 35.
  kKolkhozEffort,  ///< Social, weight 25.
  kSocialObjects,  ///< Social, weight 20.
  kDemography,     ///< Social, weight 10.

  /// NOT A PART: the count, for a consumer's mirror and the array below.
  kReadinessPartCount,
};

/// @brief One part as the index weighs it.
struct ReadinessPartView {
  /// ReadinessComponent's own three (readiness_state.h): the score 0..100,
  /// whether this era has the part, whether the closed year could measure it.
  ReadinessComponent component;

  /// The part's weight in its index, points of 100.
  float weight = 0.0F;
};

/// @brief The whole answer to «куда я иду».
struct EraReadinessView {
  /// The year the indices were last scored at (the turn); 0 before the
  /// first year closed — then every index is nought and says so.
  std::uint16_t year_scored = 0;

  float economic_index = 0.0F;
  float economic_threshold = 0.0F;
  float social_index = 0.0F;
  float social_threshold = 0.0F;

  /// Years in a row both indices stood at or above their thresholds, and
  /// how many the transition asks.
  std::uint8_t years_both_above = 0;
  std::uint8_t years_required = 0;

  /// By ReadinessPart.
  std::array<ReadinessPartView, static_cast<std::size_t>(ReadinessPart::kReadinessPartCount)>
      parts{};

  /// The transition's blocks, each 1 when it holds: the accumulated ones
  /// (food variety, own traction, the wintering) as of the last turn, the
  /// standing ones (the office, the social objects, the units at level, the
  /// population) as of now — exactly as the order reads them.
  TransitionBlocks blocks;

  /// Residents now, and how many the transition asks.
  std::uint32_t population = 0;
  std::uint32_t population_required = 0;

  /// What an order to go into the next era would be answered NOW: kNone
  /// when it would pass, else the first unmet condition in the order's own
  /// order (kIndicesNotHeld, kNoOwnTraction, …; order_state.h).
  OrderRefusal verdict = OrderRefusal::kIndicesNotHeld;
};

}  // namespace core

#endif  // CORE_COMMON_READINESS_VIEW_H_
