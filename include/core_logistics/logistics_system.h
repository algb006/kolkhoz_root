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
///               object. The phase-1 stub ignores them, the phase-2
///               implementation will not.
/// Implemented in core_logistics (instant-delivery STUB — task O0).
std::unique_ptr<ILogisticsSystem> CreateLogisticsSystem(const ITableSet& tables);

}  // namespace core

#endif  // CORE_LOGISTICS_LOGISTICS_SYSTEM_H_
