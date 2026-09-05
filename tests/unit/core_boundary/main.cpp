// Unit test of core_boundary: the session over a simulation.
//
// Two stands, because the boundary has two kinds of promise to keep. The
// REAL simulation (core_world) proves the ones that only the engine can
// answer: that a staged order becomes a row before phase 1 and that the id
// promised at issue is the id the row carries. A SCRIPTED simulation proves
// the ones no wired subsystem produces yet — an outbox with events in it, an
// order that reached kActive — without inventing a consumer that task O3 has
// not written.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../common/fake_tables.h"
#include "core_boundary/session.h"
#include "core_common/calendar.h"
#include "core_common/day_window.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

/// A simulation the test drives by hand: it keeps one world, applies the
/// staged batch exactly as the engine does (buffer-law rule 2) and emits the
/// events the test asks for at the tick the test names. Nothing else — the
/// engine has its own unit test; this one is about the session.
class ScriptedSimulation final : public core::ISimulation {
 public:
  explicit ScriptedSimulation(const core::WorldState& initial) : world_(initial) {}

  /// One scripted emission: an event appended to the outbox at `tick`.
  void EmitAt(core::Tick tick, core::EventKind kind, core::EventSeverity severity) {
    core::SimEvent event;
    event.tick = tick;
    event.kind = kind;
    event.severity = severity;
    script_.push_back(event);
  }

  void AdvanceStep() override {
    world_.step_events.clear();
    for (const core::OrderRow& row : staged_issued_) {
      core::AppendRow(world_.orders, row);
    }
    for (const core::OrderId id : staged_cancelled_) {
      const std::uint32_t row = core::FindRow(world_.orders, id);
      if (row == core::kNoRow) {
        continue;
      }
      core::OrderStatus& status = world_.orders.rows[row].status;
      if (status == core::OrderStatus::kPending || status == core::OrderStatus::kAccepted) {
        status = core::OrderStatus::kCancelled;
      }
    }
    staged_issued_.clear();
    staged_cancelled_.clear();

    ++world_.calendar.tick;
    core::RefreshCalendarCaches(world_.calendar);
    for (const core::SimEvent& event : script_) {
      if (event.tick == world_.calendar.tick) {
        world_.step_events.push_back(event);
      }
    }
  }

  void StageOrders(std::span<const core::OrderRow> issued,
                   std::span<const core::OrderId> cancelled) override {
    staged_issued_.insert(staged_issued_.end(), issued.begin(), issued.end());
    staged_cancelled_.insert(staged_cancelled_.end(), cancelled.begin(), cancelled.end());
  }

  const core::WorldState& CompletedState() const override { return world_; }

  void ResetWorld(const core::WorldState& initial) override {
    world_ = initial;
    staged_issued_.clear();
    staged_cancelled_.clear();
  }

  /// The scripted simulation owns no subsystem, so the alarms it reports are
  /// the ones a test PUT there: `alarms` below is handed out verbatim. That
  /// is what lets the boundary test check the session's own half of the
  /// contract — the sort — without dragging in production's predicates.
  bool CanBeOrdered(core::ResidentId /*resident*/) const override { return false; }

  core::WorkforceCount Workforce() const override { return {}; }

  /// The scripted simulation owns no subsystem, so it forecasts nothing —
  /// which is exactly the case the session's kNoData filling is for.
  void CollectStockForecast(std::vector<core::StockForecast>& /*lights*/) const override {}

  /// No subsystem here either, and so no rate: kNoData.
  core::Deadline WearDeadline(core::UnitId /*unit*/) const override {
    return core::NoDeadline(core::DeadlineKind::kNoData);
  }

  void CollectAlarms(std::vector<core::Alarm>& alarms) const override {
    alarms.insert(alarms.end(), alarms_.begin(), alarms_.end());
  }

  /// The scripted double has no weather; the days ahead read from whatever
  /// the test set, so a session test can pin a forecast without a table set.
  void CollectWeatherForecast(std::span<core::DayForecast> into) const override {
    for (std::size_t ahead = 0; ahead < into.size(); ++ahead) {
      into[ahead] = ahead < forecast_.size() ? forecast_[ahead] : core::DayForecast{};
    }
  }

  std::vector<core::DayForecast> forecast_;

  /// What the next CollectAlarms will hand back, in the order given.
  std::vector<core::Alarm> alarms_;

 private:
  core::WorldState world_;

  std::vector<core::SimEvent> script_;

  std::vector<core::OrderRow> staged_issued_;

  std::vector<core::OrderId> staged_cancelled_;
};

core::OrderRow RotationOrder(std::uint32_t field_value) {
  core::OrderRow order;
  order.kind = core::OrderKind::kSetRotation;
  order.field = core::FieldId{field_value};
  return order;
}

core::OrderRow PauseOrder(std::uint32_t unit_value) {
  core::OrderRow order;
  order.kind = core::OrderKind::kPauseUnit;
  order.unit = core::UnitId{unit_value};
  return order;
}

std::unique_ptr<core::ISession> ScriptedSession(const core::ITableSet& tables,
                                                const core::WorldState& world,
                                                ScriptedSimulation** out) {
  auto simulation = std::make_unique<ScriptedSimulation>(world);
  *out = simulation.get();
  core::SessionConfig config;
  config.tables = &tables;
  config.simulation = std::move(simulation);
  return core::CreateSession(std::move(config));
}

// ---------------------------------------------------------------------------
// The shape check, and orders reaching the real engine
// ---------------------------------------------------------------------------

int TestOrdersThroughTheEngine(const core::ITableSet& tables) {
  int failures = 0;
  core::StandardSimulationConfig sim_config;
  sim_config.tables = &tables;
  sim_config.worker_count = 1;

  core::SessionConfig config;
  config.tables = &tables;
  config.simulation = core::CreateStandardSimulation(sim_config);
  std::unique_ptr<core::ISession> session = core::CreateSession(std::move(config));
  if (!session) {
    std::cout << "FAIL: the session refused an empty table set\n";
    return 1;
  }

  failures += Expect(session->Stamp().tick == 0 && session->Stamp().serial == 0,
                     "a new session stamps tick 0, serial 0");

  // Shape: the kind must be named and the ids the kind reads must be there.
  failures += Expect(session->IssueOrder(core::OrderRow{}).value == 0,
                     "an order without a kind is refused");
  core::OrderRow no_target;
  no_target.kind = core::OrderKind::kSetRotation;
  failures +=
      Expect(session->IssueOrder(no_target).value == 0, "kSetRotation without a field is refused");
  core::OrderRow work;
  work.kind = core::OrderKind::kAssignWork;
  work.resident = core::ResidentId{7};
  work.work = core::WorkKind::kHerdCare;
  failures += Expect(session->IssueOrder(work).value == 0, "barn work without a herd is refused");

  // A post is a profession AT a unit, and all three parts are the SHAPE of
  // the order — whether that unit carries that post is core_labor's rule and
  // not the boundary's (task A7; manual/74-posts.md §3).
  core::OrderRow appoint;
  appoint.kind = core::OrderKind::kAppoint;
  appoint.resident = core::ResidentId{7};
  failures += Expect(session->IssueOrder(appoint).value == 0,
                     "an appointment without a unit or a post is refused");
  appoint.unit = core::UnitId{4};
  failures += Expect(session->IssueOrder(appoint).value == 0,
                     "and one without the post is still shapeless");
  core::OrderRow dismiss;
  dismiss.kind = core::OrderKind::kDismiss;
  failures += Expect(session->IssueOrder(dismiss).value == 0,
                     "a dismissal names the man, and nothing else will do");

  work.herd = core::HerdId{3};
  const core::OrderId first = session->IssueOrder(work);
  failures += Expect(first.value == 1, "the first order is promised id 1");
  const core::OrderId second = session->IssueOrder(RotationOrder(2));
  failures += Expect(second.value == 2, "the second order is promised id 2");
  failures +=
      Expect(session->State().orders.rows.empty(), "staging alone puts nothing in the book");

  session->AdvanceStep();
  failures +=
      Expect(session->Stamp().tick == 1 && session->Stamp().serial == 1, "one step, one serial");
  // The batch was applied before phase 1 and answered before the step ended,
  // and the two refusals are of DIFFERENT kinds on purpose. kAssignWork has
  // had a consumer since task A8, so it is core_labor that turns it down —
  // resident 7 is not in this empty world. kSetRotation still has none, and
  // the events slot refuses it with kNoConsumer rather than meeting it with
  // silence. Both rows are swept (order_state.h). The proof that the promised
  // ids named the rows the engine made is in the answers.
  failures += Expect(session->State().orders.rows.empty(),
                     "an order nobody consumes does not outlive its step");
  const std::span<const core::SimEvent> answered = session->Events();
  failures += Expect(answered.size() == 2, "both orders were answered");
  if (answered.size() == 2) {
    failures +=
        Expect(answered[0].order.value == first.value && answered[1].order.value == second.value,
               "the answers name the promised ids, in staging order");
    failures += Expect(
        answered[0].kind == core::EventKind::kOrderRefused &&
            answered[0].amount == static_cast<std::int64_t>(core::OrderRefusal::kNoSuchSubject),
        "work for a man who does not exist is refused by the consumer that has him");
    failures +=
        Expect(answered[1].kind == core::EventKind::kOrderRefused &&
                   answered[1].amount == static_cast<std::int64_t>(core::OrderRefusal::kNoConsumer),
               "an order kind with no consumer is refused, never met with silence");
  }
  session->AcknowledgeEvents(answered.size());

  // The journal recorded both, in one batch of tick 0.
  const std::vector<core::JournalEntry> journal = session->TakeJournal();
  failures += Expect(journal.size() == 2, "the journal holds both issues");
  if (journal.size() == 2) {
    failures += Expect(journal[0].tick == 0 && journal[1].tick == 0,
                       "both were issued after the completed tick 0");
    failures +=
        Expect(journal[0].sequence == 0 && journal[1].sequence == 1, "the batch order is total");
    failures += Expect(
        journal[0].verb == core::JournalVerb::kIssue && journal[0].order_id.value == first.value,
        "the entry carries the promised id");
    failures += Expect(journal[0].order.status == core::OrderStatus::kPending,
                       "the journal keeps the row as it was staged");
  }
  failures += Expect(session->TakeJournal().empty(), "taking the journal empties it");

  failures +=
      Expect(!session->CancelOrder(core::OrderId{999}), "an unknown id cannot be cancelled");
  failures += Expect(!session->CancelOrder(core::OrderId{}), "nor can the invalid id");

  // Issued and cancelled between the same two steps: the row is still born,
  // and it is born cancelled (session.h, CancelOrder) — which is exactly why
  // a cancel stages a cancellation instead of dropping the entry.
  const core::OrderId short_lived = session->IssueOrder(PauseOrder(5));
  failures += Expect(session->CancelOrder(short_lived), "a staged order can be cancelled");
  session->AdvanceStep();
  bool cancelled_answer = false;
  for (const core::SimEvent& event : session->Events()) {
    cancelled_answer = cancelled_answer || (event.kind == core::EventKind::kOrderCancelled &&
                                            event.order.value == short_lived.value);
  }
  failures +=
      Expect(cancelled_answer,
             "an order cancelled in its own batch is still born, and answered as cancelled");

  // The staged batch is what a save carries beside the world, and a load
  // hands back (session.h, StagedBatch / ReplaceWorld).
  const core::OrderId pending = session->IssueOrder(RotationOrder(3));
  failures +=
      Expect(session->StagedBatch().issued.size() == 1 &&
                 session->StagedBatch().issued.front().kind == core::OrderKind::kSetRotation,
             "what is staged is readable before the step that applies it");
  const core::WorldState saved = session->State();
  const core::StagedOrders saved_batch = session->StagedBatch();
  session->ReplaceWorld(saved, saved_batch);
  failures +=
      Expect(session->StagedBatch().issued.size() == 1, "a load puts the batch back where it was");
  session->AdvanceStep();
  bool resumed = false;
  for (const core::SimEvent& event : session->Events()) {
    resumed = resumed || event.order.value == pending.value;
  }
  failures += Expect(resumed, "and the resumed batch reaches the engine like any other");

  // The same call with the session's OWN batch as the argument: the natural
  // spelling of "reload the world and keep what is staged" (session.h, the
  // second ReplaceWorld). It used to lose the orders silently, because the
  // reset emptied the batch before the argument was read.
  const core::OrderId kept = session->IssueOrder(RotationOrder(4));
  const core::WorldState reloaded = session->State();
  session->ReplaceWorld(reloaded, session->StagedBatch());
  failures +=
      Expect(session->StagedBatch().issued.size() == 1 &&
                 session->StagedBatch().issued.front().kind == core::OrderKind::kSetRotation,
             "a reload given the session's own batch keeps it");
  session->AdvanceStep();
  bool kept_reached = false;
  for (const core::SimEvent& event : session->Events()) {
    kept_reached = kept_reached || event.order.value == kept.value;
  }
  failures += Expect(kept_reached, "and that batch reaches the engine too");
  return failures;
}

// ---------------------------------------------------------------------------
// Readers of the event log: two consumers, two cursors, one log
// ---------------------------------------------------------------------------

/// @brief The case the reader cursors exist for (session.h, TWO CONSUMERS OF
/// EVENTS): readers acknowledge DIFFERENT parts of the window, out of order,
/// and none of them loses what it has not acknowledged itself. A single
/// reader draining everything would pass whatever the trimming did.
int TestEventReaders(const core::ITableSet& tables) {
  int failures = 0;
  core::WorldState world;
  ScriptedSimulation* script = nullptr;
  std::unique_ptr<core::ISession> session = ScriptedSession(tables, world, &script);
  if (!session) {
    std::cout << "FAIL: the scripted session was refused\n";
    return 1;
  }
  // One event per tick, each of its own kind, so every window can be named
  // by what is in it and not merely counted.
  script->EmitAt(1, core::EventKind::kResidentBorn, core::EventSeverity::kRoutine);
  script->EmitAt(2, core::EventKind::kResidentDied, core::EventSeverity::kRoutine);
  script->EmitAt(3, core::EventKind::kWedding, core::EventSeverity::kRoutine);
  script->EmitAt(4, core::EventKind::kFieldHarvested, core::EventSeverity::kRoutine);
  script->EmitAt(5, core::EventKind::kUnitBuilt, core::EventSeverity::kRoutine);

  const auto kinds = [](std::span<const core::SimEvent> window) {
    std::vector<core::EventKind> out;
    for (const core::SimEvent& event : window) {
      out.push_back(event.kind);
    }
    return out;
  };
  const std::vector<core::EventKind> born_died{core::EventKind::kResidentBorn,
                                               core::EventKind::kResidentDied};

  session->AdvanceStep();  // tick 1
  session->AdvanceStep();  // tick 2
  failures +=
      Expect(kinds(session->Events()) == born_died, "the default reader has the first two events");

  // A reader opened now starts at the END: the backlog belongs to whoever
  // was open when it accrued.
  const core::EventReaderId host = session->OpenEventReader();
  failures += Expect(host.value != 0, "an open reader has a real id");
  failures += Expect(session->Events(host).empty(), "a reader opened now inherits no backlog");
  failures +=
      Expect(kinds(session->Events()) == born_died, "and opening it moved nobody else's window");

  session->AdvanceStep();  // tick 3
  session->AdvanceStep();  // tick 4
  failures += Expect(session->Events().size() == 4 && session->Events(host).size() == 2,
                     "each cursor counts from where it stood");

  // Out of order, and different parts: the later reader acknowledges its
  // first event while the default reader has acknowledged nothing at all.
  session->AcknowledgeEvents(host, 1);
  failures += Expect(kinds(session->Events(host)) ==
                         std::vector<core::EventKind>{core::EventKind::kFieldHarvested},
                     "the reader that acknowledged sees the rest of its own window");
  failures +=
      Expect(session->Events().size() == 4, "and the reader that did not still sees everything");

  // Now the default reader overtakes it by three; the log may drop only
  // what BOTH have acknowledged.
  session->AcknowledgeEvents(3);
  failures += Expect(
      kinds(session->Events()) == std::vector<core::EventKind>{core::EventKind::kFieldHarvested},
      "the default reader keeps what it did not acknowledge");
  failures += Expect(kinds(session->Events(host)) ==
                         std::vector<core::EventKind>{core::EventKind::kFieldHarvested},
                     "and the other reader is untouched by that");

  // A third reader, and a step all three see differently.
  const core::EventReaderId watcher = session->OpenEventReader();
  failures += Expect(watcher.value != host.value, "ids are not reused while both are open");
  session->AdvanceStep();  // tick 5
  failures += Expect(session->Events().size() == 2 && session->Events(host).size() == 2 &&
                         kinds(session->Events(watcher)) ==
                             std::vector<core::EventKind>{core::EventKind::kUnitBuilt},
                     "one step, three windows, each starting at its own cursor");

  // The two newer readers drain themselves; the default one has not, so
  // nothing may be freed under it.
  session->AcknowledgeEvents(watcher, 1);
  session->AcknowledgeEvents(host, 99);  // above the size: the whole window
  failures += Expect(session->Events(watcher).empty() && session->Events(host).empty(),
                     "a count above the window acknowledges the window and no more");
  failures += Expect(session->Events().size() == 2,
                     "the slowest reader holds the log for as long as it lags");

  // Closing the laggard is not the same as acknowledging for it — here the
  // laggard is the default reader, which cannot be closed, so it drains.
  session->AcknowledgeEvents(session->Events().size());
  failures += Expect(session->Events().empty(), "and it drains when it is ready");

  session->CloseEventReader(watcher);
  session->AdvanceStep();  // tick 6, nothing scripted
  failures += Expect(session->Events().empty() && session->Events(host).empty(),
                     "a closed reader is gone and the rest keep working");

  // ReplaceWorld drops the log; the readers are subscriptions, not state.
  core::WorldState reloaded;
  session->ReplaceWorld(reloaded);
  failures += Expect(session->Events().empty() && session->Events(host).empty(),
                     "a load empties the log for every reader");
  session->AdvanceStep();  // tick 1 again — the script replays
  failures += Expect(
      kinds(session->Events()) == std::vector<core::EventKind>{core::EventKind::kResidentBorn} &&
          kinds(session->Events(host)) ==
              std::vector<core::EventKind>{core::EventKind::kResidentBorn},
      "and both cursors stand at the start of the new log");

  session->CloseEventReader(host);
  return failures;
}

// ---------------------------------------------------------------------------
// Events, the fast-forward and what only a scripted simulation can show
// ---------------------------------------------------------------------------

int TestEventsAndFastForward(const core::ITableSet& tables) {
  int failures = 0;
  core::WorldState world;
  world.weather.daylight_hours = 12.0F;  // sunrise at hour 6, sunset at 18

  ScriptedSimulation* script = nullptr;
  std::unique_ptr<core::ISession> session = ScriptedSession(tables, world, &script);
  if (!session) {
    std::cout << "FAIL: the scripted session was refused\n";
    return 1;
  }
  script->EmitAt(2, core::EventKind::kResidentBorn, core::EventSeverity::kNotable);
  script->EmitAt(4, core::EventKind::kResidentDied, core::EventSeverity::kInterrupting);
  script->EmitAt(9, core::EventKind::kFieldHarvested, core::EventSeverity::kRoutine);

  failures += Expect(session->Events().empty(), "a new session has drained nothing");
  session->AdvanceStep();
  failures += Expect(session->Events().empty(), "a step with an empty outbox adds nothing");
  session->AdvanceStep();
  failures += Expect(
      session->Events().size() == 1 && session->Events()[0].kind == core::EventKind::kResidentBorn,
      "the completed step's outbox lands in the log");
  session->AdvanceStep();
  failures += Expect(session->Events().size() == 1, "and is not drained twice");
  session->AcknowledgeEvents(1);
  failures += Expect(session->Events().empty(), "an acknowledgement drops what was shown");

  // The fast-forward stops on the interrupting event of tick 4.
  const core::FastForwardTarget far_away{.kind = core::FastForwardTargetKind::kTick,
                                         .tick = 100,
                                         .event_kind = core::EventKind::kNone};
  const core::FastForwardReport interrupted = session->AdvanceUntil(far_away, 0);
  failures += Expect(interrupted.outcome == core::FastForwardOutcome::kInterrupted,
                     "an interrupting event breaks the fast-forward");
  failures += Expect(interrupted.steps_run == 1 && session->Stamp().tick == 4,
                     "it stops in the step that emitted it");
  failures += Expect(!session->Events().empty() &&
                         session->Events().back().severity == core::EventSeverity::kInterrupting,
                     "the interrupting event is there to be found");

  // A budget slices the run; the same target continues it.
  const core::FastForwardReport spent = session->AdvanceUntil(far_away, 3);
  failures +=
      Expect(spent.outcome == core::FastForwardOutcome::kBudgetSpent && spent.steps_run == 3,
             "the budget stops the run and says so");
  failures += Expect(session->Stamp().tick == 7, "three steps ran");

  // Until a kind of event, counting only this call's steps.
  const core::FastForwardTarget harvest{.kind = core::FastForwardTargetKind::kFirstEventOf,
                                        .tick = 0,
                                        .event_kind = core::EventKind::kFieldHarvested};
  const core::FastForwardReport waited = session->AdvanceUntil(harvest, 0);
  failures += Expect(
      waited.outcome == core::FastForwardOutcome::kTargetReached && session->Stamp().tick == 9,
      "the run stops at the first event of the kind asked for");

  // A tick already behind costs no step at all.
  const core::FastForwardTarget behind{
      .kind = core::FastForwardTargetKind::kTick, .tick = 5, .event_kind = core::EventKind::kNone};
  const core::FastForwardReport nothing = session->AdvanceUntil(behind, 0);
  failures +=
      Expect(nothing.outcome == core::FastForwardOutcome::kTargetReached && nothing.steps_run == 0,
             "a target already met runs nothing");

  // Sunrise and sunset are the NEXT ones, never the current hour.
  const core::FastForwardTarget sunrise{.kind = core::FastForwardTargetKind::kNextSunrise,
                                        .tick = 0,
                                        .event_kind = core::EventKind::kNone};
  const core::FastForwardReport morning = session->AdvanceUntil(sunrise, 0);
  failures += Expect(morning.outcome == core::FastForwardOutcome::kTargetReached &&
                         core::HourFromTick(session->Stamp().tick) ==
                             core::SunriseHour(session->State().weather.daylight_hours),
                     "the fast-forward stops at the hour the sun rises in");
  const core::FastForwardTarget sunset{.kind = core::FastForwardTargetKind::kNextSunset,
                                       .tick = 0,
                                       .event_kind = core::EventKind::kNone};
  const core::FastForwardReport evening = session->AdvanceUntil(sunset, 0);
  failures += Expect(evening.outcome == core::FastForwardOutcome::kTargetReached &&
                         core::HourFromTick(session->Stamp().tick) ==
                             core::SunsetHour(session->State().weather.daylight_hours),
                     "and at the hour it sets in");
  const core::Tick before_next = session->Stamp().tick;
  const core::FastForwardReport next_morning = session->AdvanceUntil(sunrise, 0);
  failures += Expect(next_morning.steps_run > 0 && session->Stamp().tick > before_next,
                     "the next sunrise is tomorrow's, not this hour");

  // An order that reached kActive is no longer cancellable: work that has
  // begun is finished, not taken back (time design §11).
  core::WorldState with_active;
  core::OrderRow active = PauseOrder(4);
  active.status = core::OrderStatus::kActive;
  const core::OrderId active_id = core::AppendRow(with_active.orders, active);
  core::OrderRow accepted = PauseOrder(5);
  accepted.status = core::OrderStatus::kAccepted;
  const core::OrderId accepted_id = core::AppendRow(with_active.orders, accepted);

  session->ReplaceWorld(with_active);
  failures += Expect(session->Stamp().serial == 0, "a replaced world starts a new serial");
  failures += Expect(session->Events().empty(), "and drops the event log");
  failures += Expect(!session->CancelOrder(active_id), "an active order cannot be cancelled");
  failures += Expect(session->CancelOrder(accepted_id), "an accepted one still can");
  return failures;
}

// ---------------------------------------------------------------------------
// The derived signals
// ---------------------------------------------------------------------------

int TestSignals(const core::ITableSet& tables) {
  int failures = 0;
  core::WorldState world;
  world.calendar.tick = 8 * core::kTicksPerDay + 10;  // day 8, hour 10: daylight
  core::RefreshCalendarCaches(world.calendar);
  world.weather.daylight_hours = 12.0F;
  world.weather.air_temperature_celsius = -7.5F;

  core::FamilyRow family;
  const core::FamilyId household = core::AppendRow(world.families, family);

  core::UnitRow house;
  house.position = core::Vec2{.x = 100.0F, .y = 50.0F};
  house.household = household;
  const core::UnitId house_id = core::AppendRow(world.units, house);
  world.families.rows[core::FindRow(world.families, household)].house = house_id;

  core::UnitRow barn;
  barn.position = core::Vec2{.x = 400.0F, .y = 300.0F};
  barn.paused = 1;  // the chairman stopped it; the layer has to see that
  const core::UnitId barn_id = core::AppendRow(world.units, barn);

  core::HerdRow herd;
  herd.unit = barn_id;
  const core::HerdId herd_id = core::AppendRow(world.herds, herd);

  core::FieldRow field;
  field.center = core::Vec2{.x = -50.0F, .y = 900.0F};
  const core::FieldId field_id = core::AppendRow(world.fields, field);

  // Three of the household: a ploughman on the field, a milkmaid at the barn
  // and a baby born four days ago. Biology runs four times the calendar, so
  // four game days are about four biological months — well under eighteen.
  core::ResidentRow ploughman;
  ploughman.family = household;
  ploughman.birth_day = -30 * static_cast<std::int32_t>(core::kDaysPerYear);
  ploughman.work.kind = core::WorkKind::kPlowing;
  ploughman.work.field = field_id;
  const core::ResidentId ploughman_id = core::AppendRow(world.residents, ploughman);

  core::ResidentRow milkmaid;
  milkmaid.family = household;
  milkmaid.birth_day = -25 * static_cast<std::int32_t>(core::kDaysPerYear);
  milkmaid.work.kind = core::WorkKind::kHerdCare;
  milkmaid.work.herd = herd_id;
  const core::ResidentId milkmaid_id = core::AppendRow(world.residents, milkmaid);

  core::ResidentRow baby;
  baby.family = household;
  baby.birth_day = 4;
  const core::ResidentId baby_id = core::AppendRow(world.residents, baby);

  ScriptedSimulation* script = nullptr;
  std::unique_ptr<core::ISession> session = ScriptedSession(tables, world, &script);
  if (!session) {
    std::cout << "FAIL: the signals session was refused\n";
    return 1;
  }

  const core::UnitSignals house_signals = session->SignalsOfUnit(house_id);
  failures += Expect(house_signals.unit.value == house_id.value, "the signals name their unit");
  failures += Expect(house_signals.residents_living == 3, "the whole household lives here");
  failures += Expect(house_signals.infants == 1, "one of them is in diapers");
  failures += Expect(house_signals.residents_working == 0, "nobody works at a house");
  failures += Expect(house_signals.indoor_temperature_celsius == -7.5F,
                     "STUB: inside is as cold as outside");
  failures += Expect(house_signals.wear == 0.0F, "STUB: wear stays neutral for a house");
  failures += Expect(house_signals.paused == 0, "a unit nobody stopped is not stopped");

  const core::UnitSignals barn_signals = session->SignalsOfUnit(barn_id);
  failures +=
      Expect(barn_signals.residents_working == 1, "the barn crew is counted through the herd");
  failures += Expect(barn_signals.residents_living == 0, "nobody lives in the barn");
  // The pause reaches the layer through the SIGNALS, not through the row:
  // this is the only place the presentation can learn that a yard stands
  // still, so the byte core_production writes has to arrive here (task A8).
  failures += Expect(barn_signals.paused == 1, "and the stopped barn reports itself stopped");
  failures += Expect(session->SignalsOfUnit(core::UnitId{404}).unit.value == 0,
                     "a unit that does not exist gives a default");

  const core::FieldSignals field_signals = session->SignalsOfField(field_id);
  failures +=
      Expect(field_signals.field.value == field_id.value && field_signals.residents_working == 1,
             "one man is on the field");
  failures += Expect(session->SignalsOfField(core::FieldId{404}).field.value == 0,
                     "a field that does not exist gives a default");

  const core::ResidentWhereabouts on_field = session->WhereaboutsOf(ploughman_id);
  failures +=
      Expect(on_field.place == core::Whereabouts::kAtWork && on_field.field.value == field_id.value,
             "the ploughman is at work on his field");
  failures += Expect(on_field.to.x == -50.0F && on_field.to.y == 900.0F,
                     "and the field's centre is where he is");
  const core::ResidentWhereabouts at_barn = session->WhereaboutsOf(milkmaid_id);
  failures += Expect(at_barn.place == core::Whereabouts::kAtWork &&
                         at_barn.herd.value == herd_id.value && at_barn.unit.value == barn_id.value,
                     "the milkmaid is at the barn her herd stands in");
  const core::ResidentWhereabouts at_home = session->WhereaboutsOf(baby_id);
  failures +=
      Expect(at_home.place == core::Whereabouts::kAtHome && at_home.unit.value == house_id.value,
             "the unassigned are at home");
  failures +=
      Expect(session->WhereaboutsOf(core::ResidentId{404}).place == core::Whereabouts::kUnknown,
             "a resident that does not exist is nowhere");
  // The session's own half of the alarm contract is the SORT: the roster and
  // the predicates belong to the subsystems, and what the boundary promises
  // is an order — by kind, then by the subject id the kind names — so that a
  // panel can diff the list. The scripted simulation hands back exactly what
  // a test puts in it, deliberately out of order and with row order that a
  // real table would never produce.
  failures += Expect(session->ActiveAlarms().empty(), "a simulation with no alarms reports none");
  {
    core::Alarm store_high;
    store_high.kind = core::AlarmKind::kStoreFull;
    store_high.unit = core::UnitId{9};
    core::Alarm store_low;
    store_low.kind = core::AlarmKind::kStoreFull;
    store_low.unit = core::UnitId{2};
    core::Alarm hungry;
    hungry.kind = core::AlarmKind::kFamilyGoingHungry;
    hungry.family = core::FamilyId{5};
    core::Alarm waiting;
    waiting.kind = core::AlarmKind::kHarvestWaitingOnField;
    waiting.field = core::FieldId{1};
    // Deliberately backwards: family before store, and the high unit id
    // before the low one.
    script->alarms_ = {hungry, store_high, waiting, store_low};
    session->AdvanceStep();
    const std::span<const core::Alarm> sorted = session->ActiveAlarms();
    failures += Expect(sorted.size() == 4, "every alarm the simulation reports is passed on");
    if (sorted.size() == 4) {
      failures += Expect(sorted[0].kind == core::AlarmKind::kStoreFull &&
                             sorted[1].kind == core::AlarmKind::kStoreFull,
                         "kinds sort by their value, whatever order they arrived in");
      failures += Expect(sorted[0].unit.value == 2 && sorted[1].unit.value == 9,
                         "and within a kind the subject id decides — not the row order");
      failures += Expect(sorted[2].kind == core::AlarmKind::kHarvestWaitingOnField &&
                             sorted[3].kind == core::AlarmKind::kFamilyGoingHungry,
                         "the later kinds follow in roster order");
    }
    // A load re-asks: the alarms of the world just loaded stand at once,
    // rather than after the first step (session.h, ActiveAlarms).
    script->alarms_ = {store_low};
    core::WorldState reloaded;
    session->ReplaceWorld(reloaded);
    failures +=
        Expect(session->ActiveAlarms().size() == 1 && session->ActiveAlarms()[0].unit.value == 2,
               "a load stands the new world's alarms without waiting for a step");
    script->alarms_.clear();
    session->AdvanceStep();
    failures += Expect(session->ActiveAlarms().empty(), "and a condition that passed is gone");
  }

  // After dark everyone is home, assignment or not: the STUB whereabouts
  // follows the solar window (manual/70-boundary.md §10).
  core::WorldState night = world;
  night.calendar.tick = 8 * core::kTicksPerDay + 22;
  core::RefreshCalendarCaches(night.calendar);
  session->ReplaceWorld(night);
  failures += Expect(session->WhereaboutsOf(ploughman_id).place == core::Whereabouts::kAtHome,
                     "outside the daylight window the ploughman is home");
  return failures;
}

// ---------------------------------------------------------------------------
// The journal on disk
// ---------------------------------------------------------------------------

int TestJournalCodec() {
  int failures = 0;
  std::vector<core::JournalEntry> entries;

  core::JournalEntry issued;
  issued.tick = 4321;
  issued.sequence = 2;
  issued.verb = core::JournalVerb::kIssue;
  issued.order.kind = core::OrderKind::kBuildUnit;
  issued.order.status = core::OrderStatus::kPending;
  issued.order.unit_type = core::UnitTypeId{7};
  issued.order.position = core::Vec2{.x = -812.5F, .y = 1104.25F};
  issued.order.issued_tick = 4321;
  issued.order_id = core::OrderId{11};
  entries.push_back(issued);

  core::JournalEntry rotation;
  rotation.tick = 4321;
  rotation.sequence = 3;
  rotation.verb = core::JournalVerb::kIssue;
  rotation.order.kind = core::OrderKind::kSetRotation;
  rotation.order.field = core::FieldId{9};
  rotation.order.rotation_year0 = core::CropId{1};
  rotation.order.rotation_year1 = core::CropId{};
  rotation.order.rotation_year2 = core::CropId{4};
  rotation.order_id = core::OrderId{12};
  entries.push_back(rotation);

  core::JournalEntry cancelled;
  cancelled.tick = 4400;
  cancelled.sequence = 0;
  cancelled.verb = core::JournalVerb::kCancel;
  cancelled.order_id = core::OrderId{11};
  entries.push_back(cancelled);

  const std::vector<std::byte> bytes = core::EncodeJournal(entries);
  failures += Expect(bytes.size() > core::kJournalMagic.size(), "the journal encodes to bytes");
  failures += Expect(core::EncodeJournal(entries) == bytes, "the same entries give the same bytes");

  std::vector<core::JournalEntry> decoded;
  std::string error;
  failures += Expect(core::DecodeJournal(bytes, &decoded, &error), "and decode back");
  failures += Expect(decoded.size() == entries.size(), "with every entry");
  if (decoded.size() == entries.size()) {
    bool same = true;
    for (std::size_t index = 0; index < decoded.size(); ++index) {
      const core::JournalEntry& was = entries[index];
      const core::JournalEntry& now = decoded[index];
      same = same && now.tick == was.tick && now.sequence == was.sequence && now.verb == was.verb &&
             now.order_id.value == was.order_id.value && now.order.kind == was.order.kind &&
             now.order.status == was.order.status &&
             now.order.issued_tick == was.order.issued_tick &&
             now.order.field.value == was.order.field.value &&
             now.order.unit_type.value == was.order.unit_type.value &&
             now.order.rotation_year0.value == was.order.rotation_year0.value &&
             now.order.rotation_year1.value == was.order.rotation_year1.value &&
             now.order.rotation_year2.value == was.order.rotation_year2.value &&
             now.order.position.x == was.order.position.x &&
             now.order.position.y == was.order.position.y;
    }
    failures += Expect(same, "field for field, the exact bits included");
  }

  // All or nothing, and every refusal names itself.
  std::vector<core::JournalEntry> untouched = entries;
  std::vector<std::byte> broken = bytes;
  broken[0] = static_cast<std::byte>('X');
  failures += Expect(!core::DecodeJournal(broken, &untouched, &error), "a wrong magic is refused");
  failures += Expect(untouched.size() == entries.size(), "and the destination is untouched");

  broken = bytes;
  broken[core::kJournalMagic.size()] = static_cast<std::byte>(0xEE);
  failures +=
      Expect(!core::DecodeJournal(broken, &untouched, &error), "a foreign format is refused");

  broken = bytes;
  broken.pop_back();
  failures +=
      Expect(!core::DecodeJournal(broken, &untouched, &error), "a truncated file is refused");

  broken = bytes;
  broken.push_back(std::byte{0});
  failures += Expect(!core::DecodeJournal(broken, &untouched, &error),
                     "bytes glued after the last entry are refused");

  broken = bytes;
  // The first entry's order kind: past the 16-byte header and the entry's
  // own tick (8), sequence (4) and verb (1).
  broken[16 + 13] = static_cast<std::byte>(200);
  failures += Expect(!core::DecodeJournal(broken, &untouched, &error),
                     "an enum value this build does not know is refused");

  const std::vector<std::byte> empty_journal = core::EncodeJournal({});
  decoded.push_back(issued);
  failures += Expect(core::DecodeJournal(empty_journal, &decoded, &error) && decoded.empty(),
                     "an empty journal is a valid journal");
  return failures;
}

// ---------------------------------------------------------------------------
// One worker or many: the same book, bit for bit
// ---------------------------------------------------------------------------

int TestWorkerIndependence(const core::ITableSet& tables) {
  int failures = 0;

  struct RunResult {
    std::vector<core::SimEvent> events;
    std::uint32_t next_order_id = 0;
  };

  const auto run = [&tables](std::uint32_t worker_count) {
    core::StandardSimulationConfig sim_config;
    sim_config.tables = &tables;
    sim_config.world_seed = 99;
    sim_config.worker_count = worker_count;
    core::SessionConfig config;
    config.tables = &tables;
    config.simulation = core::CreateStandardSimulation(sim_config);
    std::unique_ptr<core::ISession> session = core::CreateSession(std::move(config));
    RunResult result;
    if (!session) {
      return result;
    }
    for (std::uint32_t step = 0; step < 6; ++step) {
      session->IssueOrder(RotationOrder(step + 1));
      const core::OrderId doomed = session->IssueOrder(PauseOrder(step + 1));
      if (step % 2 == 0) {
        session->CancelOrder(doomed);
      }
      session->AdvanceStep();
    }
    const std::span<const core::SimEvent> events = session->Events();
    result.events.assign(events.begin(), events.end());
    result.next_order_id = session->State().orders.next_id_value;
    return result;
  };

  // The book itself is swept empty every step — an order nobody consumes is
  // refused and removed in the step it was applied (order_state.h). What
  // survives is the ANSWER, and that is what has to be worker-independent.
  const RunResult single = run(1);
  const RunResult parallel = run(4);
  bool same = single.events.size() == parallel.events.size() &&
              single.next_order_id == parallel.next_order_id;
  for (std::size_t index = 0; same && index < single.events.size(); ++index) {
    same = single.events[index].tick == parallel.events[index].tick &&
           single.events[index].kind == parallel.events[index].kind &&
           single.events[index].order.value == parallel.events[index].order.value &&
           single.events[index].amount == parallel.events[index].amount;
  }
  failures += Expect(!single.events.empty(), "the run answered anything at all");
  failures += Expect(same, "one worker and four answer identically, in the same order");
  return failures;
}

/// The shape of a CREW order: building names the UNIT that is the site, not
/// a field (task A2 — a site is a unit row). This case was missing from the
/// boundary's shape check, so the order the design describes was refused
/// here, while the one shaped to get past named a field the labor seam
/// would never read. Its own session, because issuing orders moves the id
/// counter and the promised-id assertions above count on it.
int TestCrewOrderShape(const core::ITableSet& tables) {
  int failures = 0;
  core::StandardSimulationConfig sim_config;
  sim_config.tables = &tables;
  sim_config.worker_count = 1;
  core::SessionConfig config;
  config.tables = &tables;
  config.simulation = core::CreateStandardSimulation(sim_config);
  std::unique_ptr<core::ISession> session = core::CreateSession(std::move(config));
  if (!session) {
    std::cout << "FAIL: the crew-shape session was refused\n";
    return 1;
  }
  core::OrderRow crew;
  crew.kind = core::OrderKind::kAssignWork;
  crew.resident = core::ResidentId{7};
  crew.work = core::WorkKind::kConstruction;
  crew.field = core::FieldId{5};
  failures += Expect(session->IssueOrder(crew).value == 0,
                     "building work named by a field is not a shape the book knows");
  crew.field = core::FieldId{};
  crew.unit = core::UnitId{9};
  failures += Expect(session->IssueOrder(crew).value != 0,
                     "and named by its site it is a shape the book takes");
  // Hauling stays with the fields: the load lies on the ground it came off.
  core::OrderRow haul;
  haul.kind = core::OrderKind::kAssignWork;
  haul.resident = core::ResidentId{7};
  haul.work = core::WorkKind::kHauling;
  haul.unit = core::UnitId{9};
  failures += Expect(session->IssueOrder(haul).value == 0, "carrying is not named by a unit");
  haul.unit = core::UnitId{};
  haul.field = core::FieldId{5};
  failures += Expect(session->IssueOrder(haul).value != 0, "it is named by the field it lies on");
  return failures;
}

/// The stock lights at the boundary. What is measured here is the SESSION's
/// half of the contract — four lights, always, in kind order, with the gaps
/// filled and named — because the forecasts themselves belong to the
/// subsystems and have their own tests.
///
/// The scripted simulation answers nothing at all, which is exactly the case
/// worth measuring: a light nobody computes must come back dark WITH A
/// REASON, and above all not green.
int TestStockLights(const core::ITableSet& tables) {
  int failures = 0;
  core::WorldState world;
  ScriptedSimulation* script = nullptr;
  std::unique_ptr<core::ISession> session = ScriptedSession(tables, world, &script);
  if (!session) {
    std::cout << "FAIL: the stock-light session was refused\n";
    return 1;
  }
  const std::span<const core::StockForecast> lights = session->StockLights();
  failures += Expect(lights.size() == static_cast<std::size_t>(core::StockKind::kStockKindCount),
                     "there are always four lights, however many were answered");
  bool in_kind_order = true;
  for (std::size_t index = 0; index < lights.size(); ++index) {
    in_kind_order = in_kind_order && lights[index].kind == static_cast<core::StockKind>(index);
  }
  failures += Expect(in_kind_order, "and they come in kind order, so a panel can diff the list");

  // THE TERMINATOR IS NOT A COLOUR. It exists so the layer can put a
  // static_assert on the number of lights it draws (boss, 2026-09-04), and
  // the core's half of that bargain is never to hand one over: a light of
  // value kStockLightCount would pass every switch the layer writes and
  // land in its default, which draws grey — that is, "no data".
  bool in_range = true;
  for (const core::StockForecast& light : lights) {
    in_range = in_range && light.light < core::StockLight::kStockLightCount &&
               light.no_data_reason < core::NoDataReason::kNoDataReasonCount;
  }
  failures += Expect(in_range, "no light or reason is handed over as the terminator itself");

  // -- the three-day forecast, on the same session ------------------------
  //
  // The length is a CONTRACT and not a measurement: the design spends three
  // days, and a panel that draws three icons must never be handed two. The
  // order is the other half of it — a strip that reads "tomorrow, the day
  // after, the third" cannot be given them in any other order and notice.
  //
  // AND THE WIND TRAVELS BESIDE THE NAME, not inside it: the middle day here
  // is clear AND blowing hard, which is precisely the day that could not be
  // expressed while the forecast was one field wide.
  script->forecast_ = {core::DayForecast{.phenomenon = core::WeatherPhenomenon::kThunderstorm,
                                         .wind = core::WindBand::kSquall},
                       core::DayForecast{.phenomenon = core::WeatherPhenomenon::kClear,
                                         .wind = core::WindBand::kStrongWind},
                       core::DayForecast{.phenomenon = core::WeatherPhenomenon::kBlizzard,
                                         .wind = core::WindBand::kStrongWind}};
  session->AdvanceStep();
  const std::span<const core::DayForecast> forecast = session->WeatherForecast();
  failures += Expect(forecast.size() == 3, "the forecast is three days, always");
  failures += Expect(forecast.size() == 3 &&
                         forecast[0].phenomenon == core::WeatherPhenomenon::kThunderstorm &&
                         forecast[1].phenomenon == core::WeatherPhenomenon::kClear &&
                         forecast[2].phenomenon == core::WeatherPhenomenon::kBlizzard,
                     "and they arrive as tomorrow, the day after, the third");
  failures += Expect(forecast.size() == 3 && forecast[0].wind == core::WindBand::kSquall &&
                         forecast[1].wind == core::WindBand::kStrongWind,
                     "a clear day that blows hard is a day the forecast can say");
  bool forecast_in_range = true;
  for (const core::DayForecast& day : forecast) {
    forecast_in_range = forecast_in_range &&
                        day.phenomenon < core::WeatherPhenomenon::kWeatherPhenomenonCount &&
                        day.wind < core::WindBand::kWindBandCount;
  }
  failures += Expect(forecast_in_range, "and no day is handed over as an enum's terminator");

  bool any_green = false;
  for (const core::StockForecast& light : lights) {
    any_green = any_green || light.light == core::StockLight::kGreen;
  }
  failures += Expect(!any_green,
                     "a light nobody computed is never green: green would teach the player "
                     "to trust it");
  for (const core::StockForecast& light : lights) {
    failures +=
        Expect(light.light == core::StockLight::kNoData, "an unanswered light says so plainly");
    failures += Expect(light.no_data_reason != core::NoDataReason::kNone,
                       "and every dark light carries a reason");
  }
  // The two reasons are not interchangeable: one is a hole in the design and
  // the other a hole in the wiring, and different people fix them.
  const auto& firewood = lights[static_cast<std::size_t>(core::StockKind::kFirewood)];
  failures += Expect(firewood.no_data_reason == core::NoDataReason::kRateNotInDesign,
                     "firewood is dark because the design has given no rate");
  const auto& food = lights[static_cast<std::size_t>(core::StockKind::kFood)];
  failures += Expect(food.no_data_reason == core::NoDataReason::kNoSubsystemAnswered,
                     "and food, here, because this simulation wires no subsystem — "
                     "the two complaints are not the same complaint");
  return failures;
}

// ---------------------------------------------------------------------------
// The two workforce questions (task A8)
// ---------------------------------------------------------------------------

/// Who may be given an order, and how many there are. The answers are asked
/// of the REAL simulation on purpose: the boundary owns no rule here, it owns
/// the fan-out — session to ISimulation to core_labor — and a break anywhere
/// along it would leave the layer with a plausible zero. The rule itself is
/// core_labor's unit test; what is measured here is that the answer comes
/// from there and not from a default.
///
/// The corner is the third man: of working age and with no roof. The layer
/// hard-codes sixteen and would count him; the core counts a day that can
/// START somewhere, and does not.
int TestWorkforceQuestions(const core::ITableSet& tables) {
  int failures = 0;
  core::StandardSimulationConfig sim_config;
  sim_config.tables = &tables;
  sim_config.worker_count = 1;

  core::SessionConfig config;
  config.tables = &tables;
  config.simulation = core::CreateStandardSimulation(sim_config);
  std::unique_ptr<core::ISession> session = core::CreateSession(std::move(config));
  if (!session) {
    std::cout << "FAIL: the workforce session was refused\n";
    return 1;
  }

  core::WorldState world;
  core::FamilyRow family;
  const core::FamilyId household = core::AppendRow(world.families, family);
  core::UnitRow house;
  house.household = household;
  const core::UnitId house_id = core::AppendRow(world.units, house);
  world.families.rows[core::FindRow(world.families, household)].house = house_id;

  // Twenty biological years: life runs four times the calendar, so five
  // calendar years are twenty of his own, well clear of the sixteen.
  core::ResidentRow grown;
  grown.family = household;
  grown.birth_day = -5 * static_cast<std::int32_t>(core::kDaysPerYear);
  const core::ResidentId grown_id = core::AppendRow(world.residents, grown);

  core::ResidentRow child;
  child.family = household;
  child.birth_day = 0;
  const core::ResidentId child_id = core::AppendRow(world.residents, child);

  // A family without a house: nobody's day starts anywhere.
  core::FamilyRow homeless_family;
  const core::FamilyId no_roof = core::AppendRow(world.families, homeless_family);
  core::ResidentRow homeless;
  homeless.family = no_roof;
  homeless.birth_day = -5 * static_cast<std::int32_t>(core::kDaysPerYear);
  const core::ResidentId homeless_id = core::AppendRow(world.residents, homeless);

  session->ReplaceWorld(world);
  failures += Expect(session->CanBeOrdered(grown_id), "a grown man with a roof takes orders");
  failures += Expect(!session->CanBeOrdered(child_id), "a child does not");
  failures += Expect(!session->CanBeOrdered(homeless_id), "and neither does a man with no roof");
  failures += Expect(!session->CanBeOrdered(core::ResidentId{404}),
                     "a resident that does not exist takes no orders either");

  const core::WorkforceCount empty_day = session->Workforce();
  failures += Expect(empty_day.employable == 1, "one of the three can be put to work");
  failures += Expect(empty_day.idle == 1, "and today he stands idle");

  // The same world with the man at work: employable does not move, idle does.
  // Two numbers that moved together would be one number with two names.
  world.residents.rows[core::FindRow(world.residents, grown_id)].work.kind =
      core::WorkKind::kSowing;
  session->ReplaceWorld(world);
  const core::WorkforceCount working_day = session->Workforce();
  failures += Expect(working_day.employable == 1, "putting him to work does not change the pool");
  failures += Expect(working_day.idle == 0, "but it empties the idle count");

  return failures;
}

}  // namespace

int main() {
  int failures = 0;
  const test::FakeTableSet tables;

  failures += Expect(core::kJournalMagic.size() == 8, "the journal magic is eight bytes");
  failures += TestOrdersThroughTheEngine(tables);
  failures += TestEventsAndFastForward(tables);
  failures += TestEventReaders(tables);
  failures += TestSignals(tables);
  failures += TestJournalCodec();
  failures += TestWorkerIndependence(tables);
  failures += TestCrewOrderShape(tables);
  failures += TestStockLights(tables);
  failures += TestWorkforceQuestions(tables);

  if (failures == 0) {
    std::cout << "unit_core_boundary: orders, events, readers, signals, journal, worker "
                 "independence and the workforce questions\n";
  }
  return failures;
}
