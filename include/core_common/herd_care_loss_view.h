/// @file
/// @brief What undone care costs at one unit today (#1848; core's proposal in the #1446 thread
///        [22]): the herd window's «уход: N % — молоко −M %» and the horse card's «тянет хуже:
///        недоуход», computed by the same curves the herd day applies, so the layer shows the
///        price and never computes it.
/// @threading SINGLE_THREADED
/// A value type, built between steps on the sim thread.

#ifndef CORE_COMMON_HERD_CARE_LOSS_VIEW_H_
#define CORE_COMMON_HERD_CARE_LOSS_VIEW_H_

#include <cstdint>

namespace core {

/// @brief One unit's care price (IProductionSystem::HerdCareLossOf). Kolkhoz herds only: a
///        private cow's care is her family's and has no coverage of the farm's.
struct HerdCareLossView {
  /// False for a unit with no kolkhoz herd standing at it: every number below is neutral.
  bool measured = false;

  /// The LOWEST HerdRow::care_coverage of the unit's kolkhoz herds, percent 0..100 (rounded):
  /// the window's «уход: N %».
  std::uint8_t coverage_pct = 100;

  /// The HIGHEST produce loss among those herds, percent 0..100 (rounded): 1 − the care factor
  /// on the kind's care_produce_floor, full at FullCareCoverage (the milk's own curve in the
  /// herd day). 0 where the floor is 1 (an empty floor in livestock.csv): no price, honestly.
  std::uint8_t produce_loss_pct = 0;

  /// True when a kolkhoz horse herd stands here; draught_pct is meaningful only then.
  bool horses_here = false;

  /// The settlement's draught under undone care, percent 0..100 (rounded; 100 = full):
  /// CareDraughtFactor, ONE number for every kolkhoz horse as the ration's and the cold's are
  /// - not this stable's horses alone. «Тянет хуже: недоуход» while under 100.
  std::uint8_t draught_pct = 100;
};

}  // namespace core

#endif  // CORE_COMMON_HERD_CARE_LOSS_VIEW_H_
