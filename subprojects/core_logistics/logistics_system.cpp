// Implementation of the core_logistics boundary
// (include/core_logistics/logistics_system.h). THE PHASE HAS NO SUBJECT —
// the header says why at length, and the short version is that transport
// was delivered as WorkKind::kHauling rather than as a subsystem, so there
// is nothing in flight between units for this phase to advance.

#include "core_logistics/logistics_system.h"

#include <memory>

namespace core {
namespace {

/// Phase 5, over zero items, every tick. Not "not yet written": there is
/// nothing for it to write about (header).
class LogisticsStubPhase final : public IParallelPhase {
 public:
  std::uint32_t ParallelItemCount(const WorldState& /*current*/) const override { return 0; }

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
  return std::make_unique<LogisticsSystem>();
}

}  // namespace core
