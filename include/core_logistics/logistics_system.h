/// @file
/// @brief ILogisticsSystem — the boundary of the logistics subsystem.
/// @threading PARALLEL_WRITE
/// The logistics phase (slot 5) writes world state from many workers, each
/// over its own range of unit rows, strictly under the buffer law
/// (core_sim/step.h). Accessor and factory are wiring-time, sim thread only.
///
/// Subsystem law (state model, manual/52-state-model.md): implementations
/// hold configuration only — every fact about the simulated world lives in
/// WorldState.
///
/// @note STUB for the whole of project phase 1 (plan, §11а): delivery is
/// instant — whatever a unit's output buffer offers appears where it is
/// needed within the same step, with no carts, routes or travel time. Real
/// logistics (routing, transport, spoilage in transit) is project phase 2;
/// it replaces the implementation behind this same interface.

#ifndef CORE_LOGISTICS_LOGISTICS_SYSTEM_H_
#define CORE_LOGISTICS_LOGISTICS_SYSTEM_H_

#include <memory>

#include "core_sim/step.h"

namespace core {

class ITableSet;  // Defined in core_tables (stage 1, task F5).

/// @brief The logistics subsystem: owner of step slot 5.
/// @brief The logistics slot of the step, and IT HAS NO SUBJECT.
///
/// THIS IS NOT A STUB AWAITING ITS IMPLEMENTATION, and the difference is
/// the whole of this note. A stub is work not yet done; this is a phase
/// whose work was decided to belong somewhere else and was done there.
///
/// It was written as the phase-1 instant-delivery stub — "transport costs
/// nothing and takes no time, so the phase has nothing to move" — against
/// a phase-2 promise that a real implementation would follow. Phase 2 came.
/// TRANSPORT DID NOT BECOME A SUBSYSTEM: it became the eighth kind of work,
/// WorkKind::kHauling, carried by a resident under an order from the
/// chairman with a cart the groom hands out (phase-2 plan A4, delivered
/// 2026-09-04, on decision 155). Goods do not travel between units on their
/// own, so there is nothing in flight for a parallel phase to advance.
///
/// SO THE HONEST STATE IS: this phase runs every tick, over zero items, for
/// ever, and there is no known future subject for it. Left standing rather
/// than removed because the seven-phase cycle is named in the project's own
/// rules and its shape is not this module's to change — reported to boss on
/// 2026-09-05, and the question is his: remove the phase and make the cycle
/// six, or keep it as the place a future in-flight model would go.
///
/// WHY IT IS WRITTEN DOWN AT ALL. A phase that executes and does nothing is
/// indistinguishable from one that works, from outside, by any means except
/// reading it. That is the same shape as a capacity column copied from
/// level 1, a warning compared against today's room, and a roster entry no
/// predicate could ever reach — all found in this project in one week, each
/// by measurement, after each had cost something. This one is named before
/// it costs anything.
class ILogisticsSystem {
 public:
  virtual ~ILogisticsSystem() = default;

  /// @brief The logistics slot implementation (phase 5, parallel by unit).
  /// Valid for the lifetime of the system; wired into
  /// StepPhaseSet::logistics.
  virtual IParallelPhase& LogisticsPhase() = 0;
};

/// @brief Creates the logistics subsystem.
/// @param tables Balance tables; non-owning, must outlive the returned
///               object. NOTHING HERE READS THEM, and nothing is expected
///               to — see the note above the class.
std::unique_ptr<ILogisticsSystem> CreateLogisticsSystem(const ITableSet& tables);

}  // namespace core

#endif  // CORE_LOGISTICS_LOGISTICS_SYSTEM_H_
