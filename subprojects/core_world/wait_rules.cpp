// The rules of every wait kind (wait_rules.h).

#include "wait_rules.h"

#include <cstdint>
#include <memory>

#include "core_common/herd_state.h"
#include "core_common/labor_state.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace core {
namespace {

/// The poll of both kinds: every game hour. A passenger's wait is an hour
/// long at most (transport.csv wait_limit_hours, STUB core), and a horse's
/// night ends with its worker's first hour — the dog must see either within
/// the hour it hangs (architecture §7ж³: «Сутки может быть очень много»).
constexpr std::uint32_t kEveryHour = 1;

/// A PASSENGER WAITING FOR A CART at a point of its way (wait_state.h,
/// kPassengerAwaitsCart; the labour hour's seating, cart_passengers.h).
class PassengerWaitRules final : public IWaitKindRules {
 public:
  WaitKind Kind() const override { return WaitKind::kPassengerAwaitsCart; }

  std::uint32_t PollIntervalHours(const WorldState& /*world*/,
                                  const WaitRecord& /*record*/) const override {
    return kEveryHour;
  }

  /// The cart is gone when its driver is (dead, left) or no longer holds a
  /// horse; the wait's own work is gone when the passenger stands on none or
  /// no longer rides that driver's cart; and the term ran out after its
  /// hours.
  WaitVerdict Inspect(const WorldState& world,
                      const WaitAgent& agent,
                      const WaitRecord& record,
                      Tick now) const override {
    const std::uint32_t row = FindRow(world.residents, ResidentId{agent.id});
    if (row == kNoRow) {
      return WaitVerdict::kTargetGone;
    }
    const WorkAssignment& work = world.residents.rows[row].work;
    if (work.kind == WorkKind::kNone || work.rides_cart_of.value != record.target.resident.value) {
      return WaitVerdict::kWorkGone;
    }
    const std::uint32_t driver = FindRow(world.residents, record.target.resident);
    if (driver == kNoRow || world.residents.rows[driver].work.rides_horse == 0) {
      return WaitVerdict::kTargetGone;
    }
    return now > record.since + record.term_hours ? WaitVerdict::kTermPassed
                                                  : WaitVerdict::kWaiting;
  }

  /// He walks: the cart is struck off his work, and his road is measured
  /// again on foot by the labour hour (WorkAssignment::travel_hours < 0 is
  /// «not measured», cart_passengers.cpp).
  void Emergency(WorldState& current, const WaitAgent& agent) const override {
    const std::uint32_t row = FindRow(current.residents, ResidentId{agent.id});
    if (row == kNoRow) {
      return;
    }
    ResidentRow& person = current.residents.rows[row];
    person.work.rides_cart_of = ResidentId{};
    person.work.travel_hours = -1.0F;
    person.wait.reset();
  }

  /// Nobody waits for a passenger.
  std::uint32_t Nudge(WorldState& /*current*/,
                      const WaitAgent& /*agent*/,
                      const WaitRecord& /*record*/) const override {
    return 0;
  }
};

/// A RIDING HORSE AT ITS WORKER'S YARD for the night (wait_state.h,
/// kHorseAtWorkersYard). NO WRITER IN THE WORLD YET (boss, the logistics
/// thread [39], 2): the rider kept overnight is queued after stage B; only
/// the fault test makes this record.
class HorseAtWorkersYardRules final : public IWaitKindRules {
 public:
  WaitKind Kind() const override { return WaitKind::kHorseAtWorkersYard; }

  std::uint32_t PollIntervalHours(const WorldState& /*world*/,
                                  const WaitRecord& /*record*/) const override {
    return kEveryHour;
  }

  /// Its worker gone, or the night past its term (the worker's next working
  /// day's first hour, set when the record is made).
  WaitVerdict Inspect(const WorldState& world,
                      const WaitAgent& /*agent*/,
                      const WaitRecord& record,
                      Tick now) const override {
    if (FindRow(world.residents, record.target.resident) == kNoRow) {
      return WaitVerdict::kTargetGone;
    }
    return now > record.since + record.term_hours ? WaitVerdict::kTermPassed
                                                  : WaitVerdict::kWaiting;
  }

  /// «Просто телепортируется в конюшню» (the human, 2 October 2026): the
  /// core keeps a horse as a head of the team, with no place of its own, so
  /// standing in the stable again is the wait struck off.
  void Emergency(WorldState& current, const WaitAgent& agent) const override {
    const std::uint32_t row = FindRow(current.herds, HerdId{agent.id});
    if (row != kNoRow) {
      current.herds.rows[row].wait.reset();
    }
  }

  /// Nobody waits for the horse but its worker, and he takes it in the
  /// morning's placement from the stable as from any day.
  std::uint32_t Nudge(WorldState& /*current*/,
                      const WaitAgent& /*agent*/,
                      const WaitRecord& /*record*/) const override {
    return 0;
  }
};

}  // namespace

std::array<std::unique_ptr<IWaitKindRules>, kWaitKindCount> CreateWaitRules() {
  return {std::make_unique<PassengerWaitRules>(), std::make_unique<HorseAtWorkersYardRules>()};
}

std::int64_t PackWatchdogAmount(const WatchdogLine& line) {
  const auto stood = static_cast<std::uint64_t>(line.stood_hours);
  const auto verdict = static_cast<std::uint64_t>(line.verdict) & 0xFFU;
  const auto kind = static_cast<std::uint64_t>(line.kind) & 0xFFU;
  const auto nudged = static_cast<std::uint64_t>(line.nudged) & 0xFFFFU;
  return static_cast<std::int64_t>(stood | (verdict << 32U) | (kind << 40U) | (nudged << 48U));
}

}  // namespace core
