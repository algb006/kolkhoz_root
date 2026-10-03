/// @file
/// @brief The watchdog: no game agent's state machine hangs (architecture
///        §7ж³). The behaviour half of the contract — each wait kind's rules
///        (IWaitKindRules) and the dog that walks the waits (IWatchdog); the
///        state half is core_common/wait_state.h.
/// @threading SINGLE_THREADED
/// The dog runs in the single-threaded phase 6 (events) and nowhere else, once
/// every game hour; it walks the agents' rows in row order, so one thread and
/// many give the same firings in the same hour. The rules' poll interval and
/// inspection are pure reads (PARALLEL_READONLY in themselves) and are called
/// from that phase only; the emergency action, the nudge and the journal line
/// happen there too — phase code never logs (architecture §7ж³, «Пёс и
/// многопоточность»).
///
/// WHAT EACH KIND MUST HAVE, AND THE BUILD SAYS IT: a poll interval (no
/// default — «Умолчания нет: вид без интервала не собирается»), an inspection,
/// an emergency action, a list of whom to nudge, a journal line. The rules are
/// pure virtual: a kind's class that leaves one out does not compile, and the
/// table of rules is checked against kWaitKindCount at construction — a kind
/// with no rules object refuses the watchdog. A suite guard (like
/// scripts/event_sites.py) reddens when a kind has no fault test.
///
/// WHAT THE DOG DOES NOT DO (architecture §7ж³): no emergency for loads and
/// building sites — moving logs or finishing a house would be playing for the
/// player; their waits get a journal line and an alarm, not a rule here. And it
/// does not hide a defect: a firing is a FINDING, printed by kind in every run,
/// and a dog that fires every day says the automaton is wrong, not the term.
///
/// CONTRACT, NO IMPLEMENTATION (core rules §12a). The journal line becomes a
/// SimEvent of its own kind with the implementation — a new word of the seam,
/// named to boss then.

#ifndef CORE_WORLD_WATCHDOG_H_
#define CORE_WORLD_WATCHDOG_H_

#include <array>
#include <cstdint>
#include <memory>

#include "core_common/calendar.h"
#include "core_common/wait_state.h"

namespace core {

struct WorldState;

/// @brief What an inspection found.
enum class WaitVerdict : std::uint8_t {
  kWaiting = 0,       ///< An honest wait, within its term: nothing to do.
  kTermPassed,        ///< The term ran out: the wait hangs.
  kTargetGone,        ///< What it waited for is gone (the driver died, the unit fell).
  kNoWayThere,        ///< The agent's way to where it must be cannot be built.
  kWorkGone,          ///< It is busy with work that no longer exists.
  kWaitVerdictCount,  ///< NOT A VERDICT: the count.
};

/// @brief The journal line of one firing (architecture §7ж³, «Запись в журнал
///        — всегда»): who, in which wait, how long it stood, what it waited
///        for, what the dog did.
struct WatchdogLine {
  WaitAgent agent;
  WaitKind kind = WaitKind::kWaitKindCount;
  WaitVerdict verdict = WaitVerdict::kWaiting;

  /// Game hours the agent stood in the wait when the dog found it.
  std::uint32_t stood_hours = 0;

  WaitTarget target;

  /// How many waiting agents were nudged (IWaitKindRules::Nudge).
  std::uint32_t nudged = 0;

  /// The tick of the firing.
  Tick at = 0;
};

/// @brief One wait kind's rules. One object a kind, made by the subsystem
///        that owns the kind (the labour hour's passengers, the herd's horse
///        at the worker's yard) and handed to the watchdog at construction.
/// @threading PARALLEL_READONLY for PollIntervalHours and Inspect (pure, no
///        side effects); SINGLE_THREADED for Emergency and Nudge, which the dog
///        calls in phase 6 alone.
class IWaitKindRules {
 public:
  virtual ~IWaitKindRules() = default;

  /// @brief The kind these rules are for.
  virtual WaitKind Kind() const = 0;

  /// @brief How often the dog looks at this record, game hours, at least 1
  ///        (the dog walks hourly). FLEXIBLE, by the record and the world —
  ///        the human's word of 2 October 2026: «У каждого конечного автомата
  ///        интервал опроса может быть разный и гибкий» — a cart on its round
  ///        every hour, a specialist on the road once a day, nearer the term
  ///        more often.
  /// @return Game hours; 0 is read as 1.
  virtual std::uint32_t PollIntervalHours(const WorldState& world,
                                          const WaitRecord& record) const = 0;

  /// @brief Whether the wait is still honest. Called when the poll is due.
  /// @param now The current tick.
  virtual WaitVerdict Inspect(const WorldState& world,
                              const WaitAgent& agent,
                              const WaitRecord& record,
                              Tick now) const = 0;

  /// @brief The emergency, simple and unconditional (architecture §7ж³): the
  ///        agent is put where it belongs — a horse in the stable, a cart at
  ///        the horse yard, a resident at home or at his work — with no walk
  ///        and no path; the unfinished action is cancelled; what it carried
  ///        goes to the nearest store or lies where it stands. The agent's
  ///        wait is cleared and it is free for its dispatcher again.
  /// @post The agent's row holds no WaitRecord.
  virtual void Emergency(WorldState& current, const WaitAgent& agent) const = 0;

  /// @brief Tells those who waited for the hung agent that there is nobody to
  ///        wait for — the cart's passengers, the driver's crew, the load's
  ///        receiver, the next in the queue — so they plan again now rather
  ///        than sit out their own terms: one hang must not run on as a chain.
  /// @return How many agents were nudged.
  virtual std::uint32_t Nudge(WorldState& current,
                              const WaitAgent& agent,
                              const WaitRecord& record) const = 0;
};

/// @brief What one walk of the dog did, by kind and by verdict — printed in
///        every run (architecture §7ж³: «Счёт срабатываний по видам
///        печатается в прогонах»).
struct WatchdogTally {
  /// Records looked at (their poll was due).
  std::array<std::uint32_t, kWaitKindCount> polled = {};

  /// Firings, by kind (a verdict other than kWaiting).
  std::array<std::uint32_t, kWaitKindCount> fired = {};

  /// Agents nudged, by the kind of the wait that hung.
  std::array<std::uint32_t, kWaitKindCount> nudged = {};
};

/// @brief The dog.
class IWatchdog {
 public:
  virtual ~IWatchdog() = default;

  /// @brief One game hour's walk: every agent row in row order, each record
  ///        whose poll is due (now - last_polled >= the kind's interval) is
  ///        inspected; on a verdict other than kWaiting the kind's emergency
  ///        runs, those who waited for the agent are nudged, and a journal
  ///        line is written. A record looked at has last_polled = now.
  /// @pre Called once a game hour, in phase 6, on the sim thread.
  /// @post No record of the world has run past its term unseen by more than
  ///       its kind's poll interval.
  /// @return The hour's tally; the run adds it up.
  virtual WatchdogTally WalkHour(WorldState& current) = 0;
};

/// @brief Builds the dog over one rules object a kind.
/// @param rules Indexed by WaitKind: rules[k]->Kind() == k for every k below
///        kWaitKindCount, none null.
/// @return nullptr when a kind has no rules object, or one stands in another
///         kind's place — the watchdog does not run with a kind it cannot
///         handle (architecture §7ж³: «вид без интервала не собирается»).
std::unique_ptr<IWatchdog> CreateWatchdog(
    std::array<std::unique_ptr<IWaitKindRules>, kWaitKindCount> rules);

}  // namespace core

#endif  // CORE_WORLD_WATCHDOG_H_
