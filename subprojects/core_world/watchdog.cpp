// The watchdog (core_world/watchdog.h).

#include "core_world/watchdog.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "core_common/emit_event.h"
#include "core_common/event_state.h"
#include "core_common/herd_state.h"
#include "core_common/resident_state.h"
#include "core_common/world_state.h"
#include "wait_rules.h"

namespace core {
namespace {

class Watchdog final : public IWatchdog {
 public:
  explicit Watchdog(std::array<std::unique_ptr<IWaitKindRules>, kWaitKindCount> rules)
      : rules_(std::move(rules)) {}

  WatchdogTally WalkHour(WorldState& current) override {
    WatchdogTally tally;
    const Tick now = current.calendar.tick;
    // Residents, then herds, each in row order: one thread and many give the
    // same firings in the same hour (architecture §7ж³).
    for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
      WalkOne(
          current,
          WaitAgent{.table = WaitAgentTable::kResident, .id = current.residents.row_ids[row].value},
          current.residents.rows[row].wait,
          now,
          tally);
    }
    for (std::uint32_t row = 0; row < current.herds.rows.size(); ++row) {
      WalkOne(current,
              WaitAgent{.table = WaitAgentTable::kHerd, .id = current.herds.row_ids[row].value},
              current.herds.rows[row].wait,
              now,
              tally);
    }
    return tally;
  }

 private:
  /// One agent's wait: due by its kind's interval, inspected, and on a
  /// verdict other than an honest wait — the emergency, the nudge and the
  /// journal line, in that order. A wait not begun yet (since in the
  /// future: a passenger who reaches his point later today) is not looked
  /// at.
  void WalkOne(WorldState& current,
               const WaitAgent& agent,
               std::optional<WaitRecord>& wait,
               Tick now,
               WatchdogTally& tally) {
    if (!wait.has_value() || wait->since > now) {
      return;
    }
    const auto kind = static_cast<std::size_t>(wait->kind);
    if (kind >= kWaitKindCount) {
      return;
    }
    const IWaitKindRules& rules = *rules_[kind];
    std::uint32_t interval = rules.PollIntervalHours(current, *wait);
    interval = interval == 0 ? 1U : interval;
    if (now - wait->last_polled < interval && now != wait->since) {
      return;
    }
    ++tally.polled[kind];
    wait->last_polled = now;
    const WaitVerdict verdict = rules.Inspect(current, agent, *wait, now);
    if (verdict == WaitVerdict::kWaiting) {
      return;
    }
    // The record is copied before the emergency clears it: the nudge and the
    // journal line are about the wait that hung.
    const WaitRecord hung = *wait;
    rules.Emergency(current, agent);
    const std::uint32_t nudged = rules.Nudge(current, agent, hung);
    ++tally.fired[kind];
    tally.nudged[kind] += nudged;
    const WatchdogLine line{.agent = agent,
                            .kind = hung.kind,
                            .verdict = verdict,
                            .stood_hours = static_cast<std::uint32_t>(now - hung.since),
                            .target = hung.target,
                            .nudged = nudged,
                            .at = now};
    SimEvent& event = EmitEvent(current, EventKind::kWatchdogFired);
    if (agent.table == WaitAgentTable::kResident) {
      event.resident = ResidentId{agent.id};
    } else {
      event.herd = HerdId{agent.id};
    }
    event.amount = PackWatchdogAmount(line);
  }

  std::array<std::unique_ptr<IWaitKindRules>, kWaitKindCount> rules_;
};

}  // namespace

std::unique_ptr<IWatchdog> CreateWatchdog(
    std::array<std::unique_ptr<IWaitKindRules>, kWaitKindCount> rules) {
  for (std::uint32_t kind = 0; kind < kWaitKindCount; ++kind) {
    if (rules[kind] == nullptr || static_cast<std::uint32_t>(rules[kind]->Kind()) != kind) {
      return nullptr;
    }
  }
  return std::make_unique<Watchdog>(std::move(rules));
}

}  // namespace core
