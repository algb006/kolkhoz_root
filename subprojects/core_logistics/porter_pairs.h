// A carrier on foot in the plan's clock (#671; transport design §2, «Как
// носят»): what one step of his carrying drains of a load's seam, and whether
// his load is carried in PAIRS - manure on a stretcher, a works' log under
// two. Split out of logistics_plan.cpp, which the porter took past the
// thousand-line limit: the plan's clock decides where and when he goes; this
// file answers what his back does there.
// @threading PARALLEL_READONLY - pure reads of the world passed in.

#ifndef CORE_LOGISTICS_PORTER_PAIRS_H_
#define CORE_LOGISTICS_PORTER_PAIRS_H_

#include <cstdint>

#include "core_common/world_state.h"
#include "logistics_config.h"

namespace core {

/// @brief The day's paces the plan's clock steps by.
struct PlanPaces {
  /// Seam-days one step of a cart's carting takes.
  float cart_drain = 0.0F;
  /// Game hours a kilometre: on foot, in harness empty, in harness loaded.
  float walk_hours_per_km = 0.0F;
  float harness_hours_per_km = 0.0F;
  float loaded_hours_per_km = 0.0F;
  /// SettlementHasCarts: the seams are in cart-days, a back drains its share.
  bool has_carts = false;
};

/// @brief The seam-days one step of a LONE walker drains of the load of
///        `task_row`: the part of a cart-day his back does with this load's
///        resource while the seam is in cart-days (haul.h,
///        WalkerShareOfCartDay by WalkerLoadGrams - a bundle of hay is half a
///        sack), a whole step when the settlement has no cart and the seam is
///        his own.
float WalkerDrainOf(const LogisticsConfig& config,
                    const WorldState& world,
                    std::uint32_t task_row,
                    const PlanPaces& paces);

/// @brief Whether the load of a task is carried in pairs, and a pair head's
///        terms.
struct PairTerms {
  /// Manure (a stretcher) or a works' log from a pile (under two).
  bool pairs = false;
  /// The log's pair: `drain` and `loaded_hours_per_km` below replace the
  /// lone walker's. False for the stretcher - 40 kg a pair is 20 a head, and
  /// only the drawing changes.
  bool log = false;
  float drain = 0.0F;
  float loaded_hours_per_km = 0.0F;
};

/// @brief The pair's terms on the load of `task_row`: manure by its form; a
///        log when the task is a works' supply whose next walker trip is the
///        log (load_resource.h, WalkerLoadResource) - then a pair head's
///        share of a cart-day (haul.h, LogPairShareOfCartDay) and the loaded
///        half at labor.csv log_pair_loaded_pace of the walk.
PairTerms PairTermsOf(const LogisticsConfig& config,
                      const WorldState& world,
                      std::uint32_t task_row,
                      const PlanPaces& paces);

}  // namespace core

#endif  // CORE_LOGISTICS_PORTER_PAIRS_H_
