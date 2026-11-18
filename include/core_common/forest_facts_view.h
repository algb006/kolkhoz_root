/// @file
/// @brief The forest's facts by stand (#1788 (b); layers §9 «Лес»; #1789's
/// forest plots read the same stands): what the core holds of each timber
/// stand - where, what kind, how much timber, how much of it marked, whether it
/// is being felled, and a planting's species, area and day.
/// @threading PARALLEL_READONLY
/// Plain data, built between steps by a pure read (ISession::ForestFacts).
///
/// THE CORE HAS NO TREES, ONLY STANDS. «Old», «dead with nowhere to fall» and
/// «fallen» exist nowhere in the core - no state, no design behind them; a STUB
/// for the first Epoch I build (boss, #1788: layers §9 says the layer is of
/// little use before Epoch II, «упавшее исчезает само»).

#ifndef CORE_COMMON_FOREST_FACTS_VIEW_H_
#define CORE_COMMON_FOREST_FACTS_VIEW_H_

#include <cstdint>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/timber_state.h"

namespace core {

/// @brief One stand's facts.
struct ForestStandFact {
  TimberStandId stand;
  TimberStandKind kind = TimberStandKind::kGrove;
  Vec2 position;
  float stock_m3 = 0.0F;                      ///< Timber standing.
  float marked_m3 = 0.0F;                     ///< Of it, marked for felling.
  bool being_felled = false;                  ///< A felling's work is under way on it.
  TreeSpeciesId species;                      ///< A planting's; invalid otherwise.
  float planted_area_ha = 0.0F;               ///< A planting's; 0 otherwise.
  std::uint32_t planted_day = kNeverPlanted;  ///< A planting's day; kNeverPlanted otherwise.
};

/// @brief Every stand of the world, by table row.
struct ForestFactsView {
  /// False from a bare engine.
  bool measured = false;
  std::vector<ForestStandFact> stands;
};

}  // namespace core

#endif  // CORE_COMMON_FOREST_FACTS_VIEW_H_
