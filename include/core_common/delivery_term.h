/// @file
/// @brief DeliveryTerm — how many days the district's cart takes for a lot
/// ordered today, as the order window shows it before the points are spent.
/// @threading SINGLE_THREADED
/// Plain data, answered between steps off the completed state.
///
/// WHY IT IS ON THE BOUNDARY (boss seq 189, econ's mud-season audit): the
/// mud season's one decision is «order ahead», and a term the player cannot
/// see before ordering is a punishment for the unforeseeable. The core
/// computes it because the core owns the rule (LimitBaseDeliveryDays); a copy
/// of the arithmetic in the layer would part from it on the first edit.

#ifndef CORE_COMMON_DELIVERY_TERM_H_
#define CORE_COMMON_DELIVERY_TERM_H_

#include <cstdint>

namespace core {

/// @brief The cart's term for an order placed today, in game days, from the
/// order's day to the arrival day. A range because the district adds a delay
/// drawn at the order (0..limit_delivery_delay_days_max); both ends include
/// the mud's stretch when today is a mud day. Both 0 when the world has no
/// district tables to answer from.
struct DeliveryTerm {
  std::uint32_t days_min = 0;
  std::uint32_t days_max = 0;
};

}  // namespace core

#endif  // CORE_COMMON_DELIVERY_TERM_H_
