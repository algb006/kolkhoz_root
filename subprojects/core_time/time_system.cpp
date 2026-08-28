// Implementation of the core_time boundary (include/core_time/time_system.h).
// Stage-1 STUB (task O0): advances the clock and refreshes the calendar
// caches; weather arrives at stage 2.

#include "core_time/time_system.h"

#include <memory>

#include "core_common/calendar.h"

namespace core {
namespace {

/// Phase 1 slot: one tick forward, caches refreshed.
/// STUB: weather fields pass through unchanged until stage 2, and the
/// day-zero weekday — a campaign setup parameter from the tables — is fixed
/// to Monday until the setup table exists.
class CalendarAdvancePhase final : public ISequentialPhase {
 public:
  void RunSequential(const WorldState& previous, WorldState& current) override {
    current.calendar.tick = previous.calendar.tick + 1;
    RefreshCalendarCaches(current.calendar, Weekday::kMonday);
  }
};

class TimeSystem final : public ITimeSystem {
 public:
  ISequentialPhase& TimeAndWeatherPhase() override { return phase_; }

 private:
  CalendarAdvancePhase phase_;
};

}  // namespace

std::unique_ptr<ITimeSystem> CreateTimeSystem(const ITableSet& /*tables*/) {
  // STUB: the weather and campaign-setup tables are read at stage 2.
  return std::make_unique<TimeSystem>();
}

}  // namespace core
