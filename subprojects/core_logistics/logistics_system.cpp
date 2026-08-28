// Implementation of the core_logistics boundary
// (include/core_logistics/logistics_system.h). The whole subsystem is the
// phase-1 instant-delivery STUB (task O0): transport costs nothing and takes
// no time, so the phase has nothing to move. Real logistics is a phase-2
// project decision (plan, §11).

#include "core_logistics/logistics_system.h"

#include <memory>

namespace core {
namespace {

/// STUB slot for phase 5: instant delivery means no in-flight goods exist,
/// so there are no items to process.
class LogisticsStubPhase final : public IParallelPhase {
 public:
  std::uint32_t ParallelItemCount(const WorldState& /*previous*/) const override { return 0; }

  void RunItemRange(const WorldState& /*previous*/,
                    WorldState& /*current*/,
                    std::uint32_t /*begin_item*/,
                    std::uint32_t /*end_item*/) override {}
};

class LogisticsSystem final : public ILogisticsSystem {
 public:
  IParallelPhase& LogisticsPhase() override { return phase_; }

 private:
  LogisticsStubPhase phase_;
};

}  // namespace

std::unique_ptr<ILogisticsSystem> CreateLogisticsSystem(const ITableSet& /*tables*/) {
  // STUB: the phase-1 stub ignores the tables by contract.
  return std::make_unique<LogisticsSystem>();
}

}  // namespace core
