// Implementation of the core_production boundary
// (include/core_production/production_system.h). Stage-1 STUB (task O0): no
// unit or field rows exist yet, so the phase iterates nothing and the
// decisions sub-step does nothing. The real system arrives at stage 4.

#include "core_production/production_system.h"

#include <memory>

namespace core {
namespace {

/// STUB slot for phase 4: no unit or field table exists until stage 4.
class ProductionStubPhase final : public IParallelPhase {
 public:
  std::uint32_t ParallelItemCount(const WorldState& /*current*/) const override { return 0; }

  void RunItemRange(const WorldState& /*previous*/,
                    WorldState& /*current*/,
                    std::uint32_t /*begin_item*/,
                    std::uint32_t /*end_item*/) override {}
};

class ProductionSystem final : public IProductionSystem {
 public:
  IParallelPhase& ProductionPhase() override { return phase_; }

  void RunProductionDecisions(const WorldState& /*previous*/, WorldState& /*current*/) override {
    // STUB: nomenclature, cycle starts and seasonal transitions arrive at
    // stage 4.
  }

 private:
  ProductionStubPhase phase_;
};

}  // namespace

std::unique_ptr<IProductionSystem> CreateProductionSystem(const ITableSet& /*tables*/) {
  // STUB: crop, unit-type and livestock tables are read at stage 4.
  return std::make_unique<ProductionSystem>();
}

}  // namespace core
