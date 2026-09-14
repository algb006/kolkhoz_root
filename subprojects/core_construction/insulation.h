/// @file
/// @brief The straw insulation of Epoch I — kInsulateUnit's site on a
/// standing unit (unit rules §16, "Эпоха I числами"; boss, parcel 364).
/// @threading SINGLE_THREADED
/// Called only from inside the construction sub-step of the decisions slot
/// (phase 3), on the sim thread: it writes unit rows and emits events.
///
/// THE JOB, as unit_state.h's kInsulating tells it: at the order the straw
/// the kind asks for is frozen in the site's `reserved`, and what the site
/// already holds counts; each day the stores give what is still short; with
/// the straw all on site the kind's man-days open on the labour seam; at
/// zero the straw is spent, UnitRow::insulated set and kUnitInsulated said.
/// The unit works throughout.

#ifndef CORE_CONSTRUCTION_INSULATION_H_
#define CORE_CONSTRUCTION_INSULATION_H_

#include <cstdint>

#include "construction_config.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"

namespace core {

/// @brief Grams of straw an insulation job of `kind` takes; 0 for kNone.
Grams InsulationStrawGrams(const ConstructionConfig& config, InsulationKind kind);

/// @brief Game man-days an insulation job of `kind` takes; 0 for kNone.
float InsulationLaborDays(const ConstructionConfig& config, InsulationKind kind);

/// @brief kInsulateUnit on `unit`: checks it and opens the site.
/// @return kNone when the site was opened; otherwise the refusal written in
///         order_state.h (kNoSuchSubject, kRuleForbids, kMaterialsShort).
OrderRefusal StartInsulation(const ConstructionConfig& config, WorldState& current, UnitId unit);

/// @brief A day's delivery to the kInsulating site in `row`: the stores give
///        what its straw is still short of, and at the full straw the
///        man-days open. A site whose straw is already in full is left alone.
void DeliverInsulation(const ConstructionConfig& config, WorldState& current, std::uint32_t row);

/// @brief True when the kInsulating site in `row` has had its straw and its
///        labour — the job is ready to be finished.
bool InsulationDone(const ConstructionConfig& config, const WorldState& current, std::uint32_t row);

/// @brief Finishes the job in `row`: the straw is spent, the unit warm, the
///        site block cleared, kUnitInsulated said.
/// @pre InsulationDone(config, current, row).
void CompleteInsulation(const ConstructionConfig& config, WorldState& current, std::uint32_t row);

}  // namespace core

#endif  // CORE_CONSTRUCTION_INSULATION_H_
