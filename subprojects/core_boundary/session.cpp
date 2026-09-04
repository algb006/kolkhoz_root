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

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
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
      // The kind of work decides which target is required, and the list is
      // the whole of WorkKind, not the part of it that existed when this
      // check was written: the four field kinds and hauling name a FIELD
      // (the load lies on the ground it came off), barn work names a HERD,
      // and building names the UNIT that is the site (task A2 — a site is a
      // unit row). Construction was missing here, so a crew order shaped
      // the way the design shapes it was refused at the boundary and one
      // shaped to get past the boundary named a field the seam would never
      // read. Found by the delivery cycle of task A8.
      if (!has_resident || order.work == WorkKind::kNone) {
        return false;
      }
      if (order.work == WorkKind::kHerdCare) {
        return has_herd;
      }
      return order.work == WorkKind::kConstruction ? has_unit : has_field;
    case OrderKind::kReleaseWork:
      return has_resident;
    case OrderKind::kPauseUnit:
    case OrderKind::kResumeUnit:
    case OrderKind::kDemolishUnit:
    case OrderKind::kStartBuild:
    case OrderKind::kRepairUnit:
    case OrderKind::kUpgradeUnit:
      return has_unit;
    case OrderKind::kAppoint:
      // A post is a profession AT a unit: all three named, or the order says
      // nothing (manual/74-posts.md §3). Whether the unit carries that post
      // is the staff table's answer and core_labor's to give — the boundary
      // checks the shape, never the rules.
      return has_resident && has_unit && order.profession.value != kInvalidDefIdValue;
    case OrderKind::kDismiss:
      // The post he holds is on his row; naming it again would let the two
      // disagree.
      return has_resident;
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
    // they stand for the starting world too, not only after a step: a
    // session freshly created over a loaded world answers with the
    // conditions of that world at its very first call.
    RefreshAlarms();
  }

  // -- time -----------------------------------------------------------------

  void AdvanceStep() override {
    RunOneStep();
    RefreshAlarms();
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
    RefreshAlarms();
    return report;
  }

  // -- read -----------------------------------------------------------------

  StateStamp Stamp() const override {
    return StateStamp{.tick = State().calendar.tick, .serial = serial_};
  }

  const WorldState& State() const override { return simulation_->CompletedState(); }

  float MapSideMeters() const override { return config_.map_side_m; }

  UnitSignals SignalsOfUnit(UnitId unit) const override {
    return DeriveUnitSignals(config_, State(), unit);
  }

  FieldSignals SignalsOfField(FieldId field) const override {
    return DeriveFieldSignals(State(), field);
  }

  bool CanBeOrdered(ResidentId resident) const override {
    return simulation_->CanBeOrdered(resident);
  }

  WorkforceCount Workforce() const override { return simulation_->Workforce(); }

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
  //
  // The log is `events_`, and a reader is an ABSOLUTE position in the stream
  // it carries: `log_origin_` is the stream position of events_[0], so a
  // cursor minus the origin is an index. Absolute positions are what make
  // trimming safe — every cursor keeps meaning the same event after the
  // front is dropped, which relative indices would not.

  std::span<const SimEvent> Events() const override { return WindowOf(default_cursor_); }

  void AcknowledgeEvents(std::size_t count) override {
    default_cursor_ = AdvancedCursor(default_cursor_, count);
    TrimToSlowestReader();
  }

  EventReaderId OpenEventReader() override {
    // At the current end: a subscriber sees what happens from now on and
    // does not inherit another reader's unacknowledged backlog (session.h).
    const EventReaderId id{next_reader_id_};
    ++next_reader_id_;
    readers_.push_back(Reader{.id = id, .cursor = LogEnd()});
    return id;
  }

  std::span<const SimEvent> Events(EventReaderId reader) const override {
    const Reader* const open = FindReader(reader);
    assert(open != nullptr);  // an id that is not open: a caller error
    if (open == nullptr) {
      return {};
    }
    return WindowOf(open->cursor);
  }

  void AcknowledgeEvents(EventReaderId reader, std::size_t count) override {
    Reader* const open = FindReader(reader);
    assert(open != nullptr);
    if (open == nullptr) {
      return;  // nothing is acknowledged on anyone's behalf (session.h)
    }
    open->cursor = AdvancedCursor(open->cursor, count);
    TrimToSlowestReader();
  }

  void CloseEventReader(EventReaderId reader) override {
    const Reader* const open = FindReader(reader);
    assert(open != nullptr);
    if (open == nullptr) {
      return;
    }
    readers_.erase(readers_.begin() + (open - readers_.data()));
    // The events only this reader was holding are free now.
    TrimToSlowestReader();
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
    // The readers are the consumers' subscriptions, not the world's state:
    // they stay open, and every cursor stands at the start of the new,
    // empty log (session.h, OpenEventReader).
    log_origin_ = 0;
    default_cursor_ = 0;
    for (Reader& reader : readers_) {
      reader.cursor = 0;
    }
    batch_sequence_ = 0;
    serial_ = 0;
    simulation_->ResetWorld(initial);
    RefreshAlarms();
  }

  void ReplaceWorld(const WorldState& initial, const StagedOrders& staged) override {
    // The copy is taken BEFORE the reset, which empties the session's own
    // batch: that is what makes ReplaceWorld(world, StagedBatch()) — the
    // natural spelling of "reload the world and keep what is staged" —
    // correct instead of forbidden (session.h).
    StagedOrders carried = staged;
    ReplaceWorld(initial);
    // The batch a save carried beside the world: put back exactly as it was,
    // and NOT journaled — these were recorded when they were first issued,
    // in the journal that went with that save (session.h).
    assert(BatchBelongsTo(initial, carried));
    staged_ = std::move(carried);
  }

  const StagedOrders& StagedBatch() const override { return staged_; }

 private:
  /// @brief Does `staged` belong to `initial` — the @pre of the second
  /// ReplaceWorld, actually tested? A batch saved beside a world has its
  /// issued rows waiting for the ids [next_id_value, next_id_value + size),
  /// and every id it cancels is either a row of that world's book or one of
  /// those promises. A batch from ANOTHER world fails both, and applying it
  /// would resolve each id against whatever entity of the loaded world wears
  /// that number. Counted in 64 bits: the same test in 32 is where the
  /// previous version of this check became a tautology (MEM-001).
  /// @note Used only from the Debug assert; a caller error, not a refusal —
  ///       a wrong batch is still applied in Release, as the header says.
  static bool BatchBelongsTo(const WorldState& initial, const StagedOrders& staged) {
    const std::uint64_t base = initial.orders.next_id_value;
    const std::uint64_t promised_end = base + staged.issued.size();
    if (promised_end > std::numeric_limits<std::uint32_t>::max()) {
      return false;  // the promises would not fit the id space
    }
    for (const OrderId order : staged.cancelled) {
      const bool is_promise = order.value >= base && order.value < promised_end;
      if (!is_promise && FindRow(initial.orders, order) == kNoRow) {
        return false;
      }
    }
    return true;
  }

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

  /// @brief One open reader of the event log: its id and its position in
  /// the event stream (see the events section for what "absolute" means).
  struct Reader {
    EventReaderId id;
    std::uint64_t cursor = 0;
  };

  /// @brief Rebuilds the alarm list from the simulation and puts it in the
  /// order the contract promises: by kind, then by the subject id the kind
  /// names (session.h, ActiveAlarms; core_common/alarm_state.h).
  ///
  /// The sort is not decoration. Every predicate sweeps a state table in ROW
  /// order, and row order is not id order — removal is swap-with-last
  /// (state_table.h), so an unrelated death or demolition reshuffles the
  /// rows behind an alarm that did not change. A presentation diffing the
  /// list would see churn that means nothing. Sorting by (kind, subject)
  /// makes the list a function of the state alone.
  void RefreshAlarms() {
    alarms_.clear();
    simulation_->CollectAlarms(alarms_);
    std::sort(alarms_.begin(), alarms_.end(), [](const Alarm& left, const Alarm& right) {
      if (left.kind != right.kind) {
        return left.kind < right.kind;
      }
      return AlarmSubjectValue(left) < AlarmSubjectValue(right);
    });
  }

  /// @brief Stream position one past the last event held.
  std::uint64_t LogEnd() const { return log_origin_ + events_.size(); }

  /// @brief The window from `cursor` to the end of the log. A cursor is
  /// never behind the origin: trimming stops at the slowest one.
  std::span<const SimEvent> WindowOf(std::uint64_t cursor) const {
    // Every open cursor lies within the log by construction: the front is
    // trimmed to the SLOWEST of them and never further. A cursor behind the
    // origin would mean an event was freed while a reader still needed it.
    assert(cursor >= log_origin_ && cursor <= LogEnd());
    return std::span<const SimEvent>(events_).subspan(
        static_cast<std::size_t>(cursor - log_origin_));
  }

  /// @brief `cursor` moved past `count` events, clamped to the end of the
  /// log: a count above the window's size acknowledges the whole window.
  std::uint64_t AdvancedCursor(std::uint64_t cursor, std::size_t count) const {
    const std::uint64_t end = LogEnd();
    const std::uint64_t room = end - cursor;
    return cursor + (static_cast<std::uint64_t>(count) < room ? count : room);
  }

  const Reader* FindReader(EventReaderId reader) const {
    for (const Reader& open : readers_) {
      if (open.id == reader) {
        return &open;
      }
    }
    return nullptr;
  }

  Reader* FindReader(EventReaderId reader) {
    return const_cast<Reader*>(std::as_const(*this).FindReader(reader));
  }

  /// @brief Drops the front of the log up to the slowest cursor, and never
  /// further: an event is held until EVERY open reader has acknowledged it
  /// (session.h). The default reader counts among them and cannot be closed.
  void TrimToSlowestReader() {
    std::uint64_t slowest = default_cursor_;
    for (const Reader& open : readers_) {
      slowest = open.cursor < slowest ? open.cursor : slowest;
    }
    const std::size_t drop = static_cast<std::size_t>(slowest - log_origin_);
    if (drop == 0) {
      return;
    }
    events_.erase(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(drop));
    log_origin_ = slowest;
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

  /// Everything no open reader has acknowledged yet, oldest first.
  std::vector<SimEvent> events_;

  /// Stream position of events_[0] — what turns an absolute cursor into an
  /// index. Grows only in TrimToSlowestReader; reset by ReplaceWorld.
  std::uint64_t log_origin_ = 0;

  /// The default reader — the one behind the unqualified Events() and
  /// AcknowledgeEvents(count). Always open, never closed, has no id.
  std::uint64_t default_cursor_ = 0;

  /// The readers opened by OpenEventReader, in the order they were opened.
  /// Two consumers is the case this exists for (session.h), so a vector
  /// scanned linearly is the whole data structure the problem deserves.
  std::vector<Reader> readers_;

  /// The id the next OpenEventReader hands out. Never reused, so a stale
  /// id names a closed reader and not a new one; 0 stays "no reader".
  std::uint32_t next_reader_id_ = 1;

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
