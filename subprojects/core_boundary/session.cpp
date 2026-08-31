// Implementation of the boundary (include/core_boundary/session.h): the one
// session object through which a presentation drives a campaign.
//
// The whole module is thirty methods of bookkeeping around one idea: a
// command does nothing, it becomes a ROW of the order book, and the engine
// applies the staged batch before phase 1 of the next step (buffer-law rule
// 2, core_sim/step.h). Everything else here follows from that — the promised
// id, the journal's tick and sequence, the batch that ReplaceWorld drops.
//
// No rule of the game lives here. What an order MEANS is the consuming
// subsystem's, and the session checks only the shape of one: that is what
// keeps the boundary a leaf that no subsystem can reach through.

#include "core_boundary/session.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "boundary_config.h"
#include "core_common/calendar.h"
#include "core_common/day_window.h"
#include "core_common/ids.h"
#include "core_common/state_table_ops.h"
#include "core_log/log.h"
#include "signals.h"

namespace core {
namespace {

/// @brief Does this order name the entities its kind reads? SHAPE only —
/// whether the subject exists, is eligible or is already busy is the
/// consumer's verdict and comes back as an event after the step (session.h,
/// IssueOrder). The two questions look alike and must not be merged: the
/// answer to this one is known between steps and is the same for every
/// world, the answer to that one changes with the state.
bool ShapeIsValid(const OrderRow& order) {
  const bool has_resident = order.resident.value != kInvalidEntityIdValue;
  const bool has_unit = order.unit.value != kInvalidEntityIdValue;
  const bool has_field = order.field.value != kInvalidEntityIdValue;
  const bool has_herd = order.herd.value != kInvalidEntityIdValue;
  switch (order.kind) {
    case OrderKind::kNone:
      return false;
    case OrderKind::kAssignWork:
      // The kind of work decides which target is required: the four field
      // kinds name a field, barn work names a herd (labor_state.h).
      if (!has_resident || order.work == WorkKind::kNone) {
        return false;
      }
      return order.work == WorkKind::kHerdCare ? has_herd : has_field;
    case OrderKind::kReleaseWork:
      return has_resident;
    case OrderKind::kPauseUnit:
    case OrderKind::kResumeUnit:
    case OrderKind::kDemolishUnit:
    case OrderKind::kStartBuild:
    case OrderKind::kUpgradeUnit:
      return has_unit;
    case OrderKind::kSetRotation:
      // The three crops may all be invalid: that is three years of fallow,
      // a legal rotation and not an empty order.
      return has_field;
    case OrderKind::kBuildUnit:
      // The position is NOT checked against the map: what is a buildable
      // spot is construction's rule (task A2), and the start layout itself
      // still runs negative until the map editor's export lands.
      return order.unit_type.value != kInvalidDefIdValue;
  }
  return false;
}

/// @brief The session over an assembled simulation. Every method runs on the
/// sim thread (session.h, @file); nothing here is reentrant and nothing is
/// synchronized, deliberately.
class Session final : public ISession {
 public:
  Session(const BoundaryConfig& config, std::unique_ptr<ISimulation> simulation)
      : config_(config), simulation_(std::move(simulation)) {
    // The alarms describe the state that ActiveAlarms is asked about, so
    // they are computed for the starting world too, not only after a step.
    // Through the simulation directly: State() is virtual, and a virtual
    // call in a constructor is legal here and confusing everywhere.
    CollectAlarms(simulation_->CompletedState(), alarms_);
  }

  // -- time -----------------------------------------------------------------

  void AdvanceStep() override {
    RunOneStep();
    CollectAlarms(State(), alarms_);
  }

  FastForwardReport AdvanceUntil(const FastForwardTarget& target,
                                 std::uint32_t step_budget) override {
    FastForwardReport report;
    // A tick already behind us is reached without running anything; the
    // other targets are all "the NEXT one" and always cost a step.
    if (target.kind == FastForwardTargetKind::kTick && State().calendar.tick >= target.tick) {
      return report;
    }
    while (true) {
      if (step_budget != 0 && report.steps_run >= step_budget) {
        report.outcome = FastForwardOutcome::kBudgetSpent;
        break;
      }
      const std::size_t first_new = RunOneStep();
      ++report.steps_run;
      // The target is asked FIRST. A step that both reaches the target and
      // interrupts is finished either way, and answering kInterrupted would
      // send the caller back for a fast-forward it has already completed.
      if (TargetMet(target, first_new)) {
        report.outcome = FastForwardOutcome::kTargetReached;
        break;
      }
      if (HasInterrupting(first_new)) {
        report.outcome = FastForwardOutcome::kInterrupted;
        break;
      }
    }
    CollectAlarms(State(), alarms_);
    return report;
  }

  // -- read -----------------------------------------------------------------

  StateStamp Stamp() const override {
    return StateStamp{.tick = State().calendar.tick, .serial = serial_};
  }

  const WorldState& State() const override { return simulation_->CompletedState(); }

  UnitSignals SignalsOfUnit(UnitId unit) const override {
    return DeriveUnitSignals(config_, State(), unit);
  }

  FieldSignals SignalsOfField(FieldId field) const override {
    return DeriveFieldSignals(State(), field);
  }

  ResidentWhereabouts WhereaboutsOf(ResidentId resident) const override {
    return DeriveWhereabouts(State(), resident);
  }

  std::span<const Alarm> ActiveAlarms() const override { return alarms_; }

  // -- orders ---------------------------------------------------------------

  OrderId IssueOrder(const OrderRow& order) override {
    if (!ShapeIsValid(order)) {
      return OrderId{};
    }
    OrderRow staged = order;
    staged.status = OrderStatus::kPending;
    staged.refusal = OrderRefusal::kNone;
    staged.issued_tick = State().calendar.tick;

    // The id the row WILL carry: the engine is the book's only appender and
    // appends the batch in staging order, so the counter can be read ahead
    // (order_state.h, THE ONE APPENDER). This is why a cancel never removes
    // an entry from the batch — every id promised after it would move.
    const OrderId promised{StagedIdBase() + static_cast<std::uint32_t>(staged_.issued.size())};
    staged_.issued.push_back(staged);
    Record(JournalVerb::kIssue, staged, promised);
    return promised;
  }

  bool CancelOrder(OrderId order) override {
    if (order.value == kInvalidEntityIdValue) {
      return false;
    }
    const OrderTable& book = State().orders;
    const std::uint32_t row = FindRow(book, order);
    if (row != kNoRow) {
      const OrderStatus status = book.rows[row].status;
      // Work that has begun is finished, not taken back (time design §11),
      // and a terminal row is already answered for.
      if (status != OrderStatus::kPending && status != OrderStatus::kAccepted) {
        return false;
      }
    } else if (!IsStagedIssue(order)) {
      return false;  // an id this session never promised and the book never had
    }
    staged_.cancelled.push_back(order);
    Record(JournalVerb::kCancel, OrderRow{}, order);
    return true;
  }

  // -- events ---------------------------------------------------------------

  std::span<const SimEvent> Events() const override { return events_; }

  void AcknowledgeEvents(std::size_t count) override {
    if (count >= events_.size()) {
      events_.clear();
      return;
    }
    events_.erase(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(count));
  }

  // -- record ---------------------------------------------------------------

  std::vector<JournalEntry> TakeJournal() override {
    std::vector<JournalEntry> taken;
    taken.swap(journal_);
    return taken;
  }

  void ReplaceWorld(const WorldState& initial) override {
    // The staged batch named entities of the world being replaced, so it
    // goes; so does everything the old world said about itself. The journal
    // stays: a replay that spans a load is the caller's to cut (session.h).
    staged_.issued.clear();
    staged_.cancelled.clear();
    events_.clear();
    batch_sequence_ = 0;
    serial_ = 0;
    simulation_->ResetWorld(initial);
    CollectAlarms(State(), alarms_);
  }

  void ReplaceWorld(const WorldState& initial, const StagedOrders& staged) override {
    ReplaceWorld(initial);
    // The batch a save carried beside the world: put back exactly as it was,
    // and NOT journaled — these were recorded when they were first issued,
    // in the journal that went with that save (session.h).
    assert(staged.issued.empty() ||
           initial.orders.next_id_value + staged.issued.size() >= initial.orders.next_id_value);
    staged_ = staged;
  }

  const StagedOrders& StagedBatch() const override { return staged_; }

 private:
  /// @brief The id the next staged row would be given if the batch were
  /// applied now: the book's counter, which nothing but the engine moves.
  std::uint32_t StagedIdBase() const { return State().orders.next_id_value; }

  /// @brief Is `order` one of the ids this session has promised in the batch
  /// that has not been applied yet? The batch's ids are consecutive from the
  /// counter, so the question is a range test and needs no second list.
  bool IsStagedIssue(OrderId order) const {
    const std::uint32_t base = StagedIdBase();
    return order.value >= base &&
           order.value < base + static_cast<std::uint32_t>(staged_.issued.size());
  }

  void Record(JournalVerb verb, const OrderRow& order, OrderId id) {
    JournalEntry entry;
    entry.tick = State().calendar.tick;
    entry.sequence = batch_sequence_;
    entry.verb = verb;
    entry.order = order;
    entry.order_id = id;
    journal_.push_back(entry);
    ++batch_sequence_;
  }

  /// @brief Stages what is waiting, runs one step, drains the completed
  /// step's outbox into the log.
  /// @return Where this step's events start in the log — what the interrupt
  ///         and event-target checks look at, and the reason they never
  ///         re-examine events of earlier steps.
  std::size_t RunOneStep() {
    if (!staged_.issued.empty() || !staged_.cancelled.empty()) {
      simulation_->StageOrders(staged_.issued, staged_.cancelled);
      staged_.issued.clear();
      staged_.cancelled.clear();
    }
    simulation_->AdvanceStep();
    ++serial_;
    batch_sequence_ = 0;  // a new tick opens a new batch

    const std::size_t first_new = events_.size();
    const StepEventLog& outbox = State().step_events;
    events_.insert(events_.end(), outbox.begin(), outbox.end());
    return first_new;
  }

  bool TargetMet(const FastForwardTarget& target, std::size_t first_new) const {
    const WorldState& world = State();
    switch (target.kind) {
      case FastForwardTargetKind::kTick:
        return world.calendar.tick >= target.tick;
      case FastForwardTargetKind::kNextSunrise:
        return HourFromTick(world.calendar.tick) == SunriseHour(world.weather.daylight_hours);
      case FastForwardTargetKind::kNextSunset:
        return HourFromTick(world.calendar.tick) == SunsetHour(world.weather.daylight_hours);
      case FastForwardTargetKind::kFirstEventOf:
        // Only this call's events count. A kind that has already been in the
        // log since before the fast-forward is not what "until the first
        // event of" asks about; kNone matches nothing at all.
        if (target.event_kind == EventKind::kNone) {
          return false;
        }
        for (std::size_t index = first_new; index < events_.size(); ++index) {
          if (events_[index].kind == target.event_kind) {
            return true;
          }
        }
        return false;
    }
    return false;
  }

  bool HasInterrupting(std::size_t first_new) const {
    for (std::size_t index = first_new; index < events_.size(); ++index) {
      if (events_[index].severity == EventSeverity::kInterrupting) {
        return true;
      }
    }
    return false;
  }

  BoundaryConfig config_;

  std::unique_ptr<ISimulation> simulation_;

  /// Steps completed since creation or the last ReplaceWorld (session.h).
  std::uint64_t serial_ = 0;

  /// Position of the next journal entry within this tick's batch.
  std::uint32_t batch_sequence_ = 0;

  /// What is staged and not yet applied — handed to the engine at the next
  /// step, saved beside the world, restored by the second ReplaceWorld.
  StagedOrders staged_;

  /// Everything emitted since the last acknowledgement, oldest first.
  std::vector<SimEvent> events_;

  std::vector<JournalEntry> journal_;

  /// The conditions standing in the completed state; rebuilt after every
  /// step, so it never describes a world other than State()'s.
  std::vector<Alarm> alarms_;
};

}  // namespace

std::unique_ptr<ISession> CreateSession(SessionConfig config) {
  assert(config.tables != nullptr);
  assert(config.simulation != nullptr);
  if (config.tables == nullptr || config.simulation == nullptr) {
    LogError("boundary: a session needs both a table set and a simulation");
    return nullptr;
  }
  BoundaryConfig knobs;
  std::string error;
  if (!ParseBoundaryConfig(*config.tables, knobs, error)) {
    LogError("boundary: " + error);
    return nullptr;
  }
  return std::make_unique<Session>(knobs, std::move(config.simulation));
}

}  // namespace core
