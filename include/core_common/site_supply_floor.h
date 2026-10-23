/// @file
/// @brief THE FLOOR UNDER A WORKS' SUPPLY (#734; transport design «Пол
///        подвоза»): the rule's numbers and its two questions, one home for
///        the three who ask - labour's morning placement, the lamp
///        kSiteSupplyWaits and the run's prints.
/// @threading PARALLEL_READONLY - pure functions of their arguments.
///
/// WHY (the trace of #734, nine canon villages, 0.37.238): a works' supply
/// stands on the works' own tier, the windowless one (#561) - and from the
/// day the spring's field work opens, every draught horse is taken by a job
/// with a window before the queue reaches it. The first barrack's supply, a
/// cart-day or so, got no carrier for 21-37 days in a row; the house stood
/// «delivering» with its materials at the store, and the year's settlers
/// were turned away (arrived 6 of 32 where the supply was cured by hand).
///
/// THE RULE: a works whose supply has brought nothing for `after_days`
/// working days in a row (ConstructionState::supply_idle_days) is ON THE
/// FLOOR: its supply job is offered ahead of every job with a window - and
/// behind the loads at level 0 - for carters WITH HORSES, up to `share` of
/// the day's draught horses for all the works on the floor together, never
/// fewer than one while the village has a draught horse, dealt in the sites'
/// queue order (site_supply_queue.h). What the supply needs beyond that it
/// asks in its old place. ONCE ON THE FLOOR IT STAYS THERE TILL ITS RECIPE IS
/// ON THE SITE: a delivery does not take the count below the rule's days
/// (supply_haul.cpp) - a floor that let go at the first cart gave a works a
/// cart a week.
#ifndef CORE_COMMON_SITE_SUPPLY_FLOOR_H_
#define CORE_COMMON_SITE_SUPPLY_FLOOR_H_

#include <cstdint>

#include "core_common/unit_state.h"

namespace core {

/// @brief logistics.csv `site_supply_floor_after_days` and
///        `site_supply_floor_share` (core_catalog/supply_floor_catalog.h).
///        Both STUB, econ's.
struct SupplyFloorRules {
  /// Working days in a row with nothing delivered after which the works'
  /// supply is on the floor. 0 switches the floor off.
  std::uint32_t after_days = 4;

  /// The share of the day's draught horses the works on the floor may take
  /// together, 0..1.
  float share = 0.25F;
};

/// @brief Whether this works' supply is on the floor today: something awaits
///        and it has stood `after_days` working days with nothing delivered.
inline bool OnSupplyFloor(const SupplyFloorRules& rules, const ConstructionState& works) {
  return rules.after_days > 0 && works.supply_idle_days >= rules.after_days &&
         AnythingAwaits(works);
}

/// @brief THE LAMP'S DAYS (save 165; stage В6 part 3): the idle working days
///        less those a mowing held (ConstructionState::supply_mowing_days) -
///        what kSiteSupplyWaits reads and says. The floor reads the idle days
///        whole (OnSupplyFloor).
inline std::uint16_t SupplyLampDays(const ConstructionState& works) {
  return works.supply_mowing_days < works.supply_idle_days
             ? static_cast<std::uint16_t>(works.supply_idle_days - works.supply_mowing_days)
             : std::uint16_t{0};
}

/// @brief Whether «подвоз на стройку ждёт» is lit for this works: on the floor
///        (OnSupplyFloor) AND its lamp days (SupplyLampDays) at the floor's
///        own days - a June works the mowing held is on the floor and dark.
inline bool SupplyWaitLampLit(const SupplyFloorRules& rules, const ConstructionState& works) {
  return OnSupplyFloor(rules, works) && SupplyLampDays(works) >= rules.after_days;
}

/// @brief The carters with horses the works on the floor may take together
///        this morning: `share` of the draught horses, rounded down, and at
///        least one while there is a horse at all.
/// @param draught_horses The day's draught horses (the placement's pool).
inline std::uint32_t SupplyFloorCarters(const SupplyFloorRules& rules,
                                        std::uint32_t draught_horses) {
  if (draught_horses == 0 || rules.after_days == 0) {
    return 0;
  }
  const auto by_share =
      static_cast<std::uint32_t>(static_cast<float>(draught_horses) * rules.share);
  return by_share > 0 ? by_share : 1U;
}

}  // namespace core

#endif  // CORE_COMMON_SITE_SUPPLY_FLOOR_H_
