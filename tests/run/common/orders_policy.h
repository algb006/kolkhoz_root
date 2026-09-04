/// @file
/// @brief The two verbs a chairman says every day and the runs had never
/// said once: a standing work order, and stopping a unit.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THIS EXISTS. A verb the run does not say is not checked by the run,
/// however carefully it is implemented and however many unit tests stand
/// behind it (boss, 2026-09-04). Before this file the runs said four order
/// kinds out of eleven — kBuildUnit, kStartBuild, kUpgradeUnit, kAppoint —
/// and every one of them was said by a policy that wanted a BUILDING. The
/// day-to-day half of the chairman's book, the half he uses most, went
/// through thirty campaign years without being uttered.
///
/// WHAT IS MEASURED, AND WHAT IS DELIBERATELY NOT. Each of the two orders
/// is checked by its EFFECT ON THE WORLD, never by its acceptance:
///
///   kAssignWork — the man goes where he is sent and STAYS there. The
///     subject is deliberately a man the accountant had already placed on
///     something else, because that is the rule the order carries: "the
///     chairman's standing order outranks the accountant's morning
///     placement" (order_state.h, boss's decision of 2026-08-31). Herd care
///     on purpose: it is the one work kind a rest day does not take off the
///     list, so "he is still there" means every day and not most of them.
///
///   kPauseUnit — the unit stops wearing, and wears again the moment it is
///     started. That is the WHOLE of what a pause does in this slice, and
///     the run says so rather than implying more: the rest of unit rules §5
///     — production halted, workers released, supply stopped — needs unit
///     work cycles, and the core has none (unit_state.h). A run that
///     reported "the pause works" would be claiming the other three.
///
/// The subject is chosen by walking the world in row order and taking the
/// first that fits, so the choice is a function of the seed like everything
/// else here. A subject that dies or is demolished mid-measurement is not a
/// failure — the sequence is abandoned, said out loud, and started again on
/// a new subject.

#ifndef TESTS_RUN_COMMON_ORDERS_POLICY_H_
#define TESTS_RUN_COMMON_ORDERS_POLICY_H_

#include <cstdint>
#include <iostream>
#include <span>

#include "core_common/herd_state.h"
#include "core_common/labor_state.h"
#include "core_common/order_state.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_world/world.h"

namespace run {

/// @brief Issues one standing work order and one pause, watches what the
/// world does about them, and reports both as pass/fail lines.
class OrdersPolicy {
 public:
  /// @brief One day's worth of the chairman's attention, called after the
  /// day's steps like the other policies.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    switch (stage_) {
      case Stage::kLooking:
        Look(world, simulation);
        return;
      case Stage::kStopped:
        Watch(world, simulation);
        return;
      case Stage::kStarted:
        Settle(world);
        return;
      case Stage::kFinished:
        return;
    }
  }

  /// @brief Prints what the two orders did and returns the failure count.
  int Report() const {
    if (stage_ != Stage::kFinished) {
      std::cout << "FAIL: the run never got to say kAssignWork and kPauseUnit"
                << " (abandoned " << abandoned_ << " times: " << (why_ == nullptr ? "-" : why_)
                << ")\n";
      return 1;
    }
    std::cout << "orders: unit " << unit_.value << " stopped on day " << stopped_on_ << " for "
              << kHoldDays << " days at wear " << wear_when_stopped_ << ", started again at "
              << wear_when_started_ << "; resident " << worker_.value << " was taken off "
              << WorkName(work_before_) << " and held on herd care " << held_days_ << " days";
    if (abandoned_ != 0) {
      std::cout << " (after " << abandoned_ << " abandoned subjects)";
    }
    std::cout << '\n';

    int failures = 0;
    failures += Expect(pause_took_, "the chairman stopped a unit and the world stopped it");
    failures += Expect(grew_while_stopped_ == 0,
                       "and a stopped unit did not wear out on any of the days it stood");
    failures +=
        Expect(wear_when_started_ > wear_when_stopped_, "and it wore again once it was started");
    failures +=
        Expect(work_took_, "the chairman's work order outranked the accountant's placement");
    failures +=
        Expect(off_the_job_ == 0, "and it held the man there day after day, rest days included");
    failures += Expect(released_, "and kReleaseWork gave him back to the accountant");
    return failures;
  }

 private:
  /// Days the unit is left standing. Long enough that a wear rate of well
  /// under a percent a day would show, short enough to be a chairman's
  /// decision and not a mothballing.
  static constexpr std::uint32_t kHoldDays = 12;

  /// Days to let the world run after the pair of closing orders, so the
  /// resumed unit has wear to show and the release has been swept.
  static constexpr std::uint32_t kSettleDays = 4;

  /// A sequence abandoned this many times means the run cannot hold a
  /// subject long enough to measure one, and that is itself the finding.
  static constexpr std::uint32_t kMaxAbandoned = 8;

  enum class Stage : std::uint8_t { kLooking, kStopped, kStarted, kFinished };

  static int Expect(bool condition, const char* label) {
    if (condition) {
      return 0;
    }
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }

  static const char* WorkName(core::WorkKind kind) {
    switch (kind) {
      case core::WorkKind::kPlowing:
        return "plowing";
      case core::WorkKind::kHarrowing:
        return "harrowing";
      case core::WorkKind::kSowing:
        return "sowing";
      case core::WorkKind::kHarvest:
        return "the harvest";
      case core::WorkKind::kHerdCare:
        return "herd care";
      case core::WorkKind::kConstruction:
        return "building";
      case core::WorkKind::kHauling:
        return "hauling";
      case core::WorkKind::kNone:
        break;
    }
    return "nothing";
  }

  /// @brief The first standing unit that is actually wearing out. Wear
  /// above zero is the readable form of "this type has wear at all": the
  /// has_wear column lives in the build config, which a run does not see.
  static core::UnitId WearingUnit(const core::WorldState& world) {
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.level != 0 && unit.paused == 0 && unit.wear > 0.0F) {
        return world.units.row_ids[row];
      }
    }
    return core::UnitId{};
  }

  /// @brief The first kolkhoz herd standing anywhere. Its own household's
  /// cow is not the chairman's to staff (livestock design §5), so the
  /// order would be a chairman's order in name only.
  static core::HerdId KolkhozHerd(const core::WorldState& world) {
    for (std::uint32_t row = 0; row < world.herds.rows.size(); ++row) {
      if (world.herds.rows[row].household_owned == 0) {
        return world.herds.row_ids[row];
      }
    }
    return core::HerdId{};
  }

  /// @brief A man the ACCOUNTANT has already put on something that is not
  /// herd care, and who holds no post. Anyone at work is of working age by
  /// construction, so the age band is read where it is applied instead of
  /// being copied here — one home for the rule.
  static core::ResidentId PlacedElsewhere(const core::WorldState& world) {
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const core::ResidentRow& resident = world.residents.rows[row];
      if (resident.post.profession.value != core::kInvalidDefIdValue) {
        continue;
      }
      if (resident.work.kind == core::WorkKind::kNone ||
          resident.work.kind == core::WorkKind::kHerdCare) {
        continue;
      }
      return world.residents.row_ids[row];
    }
    return core::ResidentId{};
  }

  void Abandon(const char* why) {
    why_ = why;
    ++abandoned_;
    stage_ = abandoned_ >= kMaxAbandoned ? Stage::kFinished : Stage::kLooking;
    if (stage_ == Stage::kFinished) {
      // Give Report something honest to fail on rather than a half sequence.
      pause_took_ = false;
      work_took_ = false;
      released_ = false;
    }
  }

  void Look(const core::WorldState& world, core::ISimulation& simulation) {
    const core::UnitId unit = WearingUnit(world);
    const core::HerdId herd = KolkhozHerd(world);
    const core::ResidentId worker = PlacedElsewhere(world);
    if (unit.value == core::kInvalidEntityIdValue || herd.value == core::kInvalidEntityIdValue ||
        worker.value == core::kInvalidEntityIdValue) {
      return;  // the village has not grown into the question yet
    }
    unit_ = unit;
    herd_ = herd;
    worker_ = worker;
    work_before_ = world.residents.rows[core::FindRow(world.residents, worker)].work.kind;

    core::OrderRow orders[2];
    orders[0].kind = core::OrderKind::kPauseUnit;
    orders[0].unit = unit_;
    orders[1].kind = core::OrderKind::kAssignWork;
    orders[1].resident = worker_;
    orders[1].work = core::WorkKind::kHerdCare;
    orders[1].herd = herd_;
    simulation.StageOrders(std::span<const core::OrderRow>(orders, 2), {});
    stopped_on_ = world.calendar.day;
    since_ = 0;
    stage_ = Stage::kStopped;
  }

  void Watch(const core::WorldState& world, core::ISimulation& simulation) {
    const std::uint32_t unit_row = core::FindRow(world.units, unit_);
    const std::uint32_t worker_row = core::FindRow(world.residents, worker_);
    if (unit_row == core::kNoRow) {
      Abandon("the unit was taken down while it stood");
      return;
    }
    if (worker_row == core::kNoRow) {
      Abandon("the man died on the job");
      return;
    }
    const core::UnitRow& unit = world.units.rows[unit_row];
    const core::ResidentRow& worker = world.residents.rows[worker_row];
    ++since_;
    if (since_ == 1) {
      // The orders were staged at the close of yesterday and applied at the
      // top of today: this is the first day either could have shown.
      pause_took_ = unit.paused != 0;
      work_took_ = worker.work.kind == core::WorkKind::kHerdCare;
      wear_when_stopped_ = unit.wear;
      return;
    }
    if (unit.wear > wear_when_stopped_) {
      ++grew_while_stopped_;
    } else if (unit.wear < wear_when_stopped_) {
      Abandon("the unit was repaired or raised while it stood");
      return;
    }
    if (worker.work.kind == core::WorkKind::kHerdCare) {
      ++held_days_;
    } else {
      ++off_the_job_;
    }
    if (since_ < kHoldDays) {
      return;
    }
    core::OrderRow orders[2];
    orders[0].kind = core::OrderKind::kResumeUnit;
    orders[0].unit = unit_;
    orders[1].kind = core::OrderKind::kReleaseWork;
    orders[1].resident = worker_;
    simulation.StageOrders(std::span<const core::OrderRow>(orders, 2), {});
    since_ = 0;
    stage_ = Stage::kStarted;
  }

  void Settle(const core::WorldState& world) {
    ++since_;
    if (since_ < kSettleDays) {
      return;
    }
    const std::uint32_t unit_row = core::FindRow(world.units, unit_);
    wear_when_started_ =
        unit_row == core::kNoRow ? wear_when_stopped_ : world.units.rows[unit_row].wear;
    released_ = true;
    for (const core::OrderRow& order : world.orders.rows) {
      if (order.kind == core::OrderKind::kAssignWork &&
          order.status == core::OrderStatus::kAccepted && order.resident.value == worker_.value) {
        released_ = false;  // the standing order is still standing
      }
    }
    stage_ = Stage::kFinished;
  }

  Stage stage_ = Stage::kLooking;

  core::UnitId unit_;

  core::HerdId herd_;

  core::ResidentId worker_;

  core::WorkKind work_before_ = core::WorkKind::kNone;

  std::uint32_t stopped_on_ = 0;

  std::uint32_t since_ = 0;

  std::uint32_t held_days_ = 0;

  std::uint32_t off_the_job_ = 0;

  std::uint32_t grew_while_stopped_ = 0;

  std::uint32_t abandoned_ = 0;

  float wear_when_stopped_ = 0.0F;

  float wear_when_started_ = 0.0F;

  bool pause_took_ = false;

  bool work_took_ = false;

  bool released_ = false;

  const char* why_ = nullptr;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_ORDERS_POLICY_H_
