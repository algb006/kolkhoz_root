// Unit test of core_sim: the step-cycle contract shape.
// The engine itself arrives with task O2; this test grows with it.

#include <type_traits>

#include "core_sim/step.h"

static_assert(core::kStepPhaseCount == 7, "the step cycle has seven fixed phases");
static_assert(static_cast<int>(core::StepPhase::kTimeAndWeather) == 0,
              "time-and-weather opens the step");
static_assert(static_cast<int>(core::StepPhase::kEvents) == core::kStepPhaseCount - 1,
              "events close the step");
static_assert(std::is_abstract_v<core::ISequentialPhase>, "ISequentialPhase is a contract");
static_assert(std::is_abstract_v<core::IParallelPhase>, "IParallelPhase is a contract");
static_assert(std::is_abstract_v<core::ISimulation>, "ISimulation is a contract");
static_assert(std::has_virtual_destructor_v<core::ISimulation>,
              "engine implementations are destroyed through the interface");

int main() {
  return 0;
}
