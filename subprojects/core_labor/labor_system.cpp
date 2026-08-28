// Implementation of the core_labor boundary
// (include/core_labor/labor_system.h). Stage-1 STUB (task O0): nobody works
// yet — assignments arrive at stage 5.

#include "core_labor/labor_system.h"

#include <memory>

namespace core {
namespace {

class LaborSystem final : public ILaborSystem {
 public:
  void RunAssignmentDecisions(const WorldState& /*previous*/, WorldState& /*current*/) override {
    // STUB: placement, deferred job changes and accrual close-out arrive at
    // stage 5.
  }
};

}  // namespace

std::unique_ptr<ILaborSystem> CreateLaborSystem(const ITableSet& /*tables*/) {
  // STUB: work rates and norms are read at stage 5.
  return std::make_unique<LaborSystem>();
}

}  // namespace core
