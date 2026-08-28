// Implementation of the core_residents boundary
// (include/core_residents/residents_system.h). Stage-1 STUB (task O0): the
// world has no resident or family rows yet, so every slot is a no-op with
// zero parallel items. The real system arrives at stage 3.

#include "core_residents/residents_system.h"

#include <memory>

namespace core {
namespace {

/// STUB slot for phases 2 (needs) and 6 (metrics): no family table exists
/// until stage 3, so there is nothing to iterate.
class ResidentsStubPhase final : public IParallelPhase {
 public:
  std::uint32_t ParallelItemCount(const WorldState& /*previous*/) const override { return 0; }

  void RunItemRange(const WorldState& /*previous*/,
                    WorldState& /*current*/,
                    std::uint32_t /*begin_item*/,
                    std::uint32_t /*end_item*/) override {}
};

class ResidentsSystem final : public IResidentsSystem {
 public:
  IParallelPhase& NeedsPhase() override { return needs_phase_; }

  IParallelPhase& MetricsPhase() override { return metrics_phase_; }

  void RunDemographyDecisions(const WorldState& /*previous*/, WorldState& /*current*/) override {
    // STUB: births, deaths, marriages and migration arrive at stage 3.
  }

 private:
  ResidentsStubPhase needs_phase_;

  ResidentsStubPhase metrics_phase_;
};

}  // namespace

std::unique_ptr<IResidentsSystem> CreateResidentsSystem(const ITableSet& /*tables*/) {
  // STUB: consumption norms and demography rates are read at stage 3.
  return std::make_unique<ResidentsSystem>();
}

}  // namespace core
