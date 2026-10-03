/// @file
/// @brief WaitRecord — one game agent's wait, with its term: the state half of
///        the watchdog contract (architecture §7ж³; core_world/watchdog.h is
///        the behaviour half).
/// @threading PARALLEL_READONLY
/// Plain data. A record lives IN ITS AGENT'S ROW (ResidentRow, HerdRow) — there
/// is no shared list of waits, so a parallel phase that writes its own rows
/// writes its own waits and no other (architecture §7ж³, «Пёс и
/// многопоточность»). Only the single-threaded phase 6 changes a record that
/// is not the writer's own: the watchdog (core_world/watchdog.h).
///
/// THE HUMAN'S RULE (2 October 2026, architecture §7ж³): «Не должно быть
/// зависаний при каких то нештатных ситуациях, должен быть сторожевой пес.
/// Если что то зависло, пишется в лог и срабатывает аварийный режим». A state
/// left only by somebody else's action — waiting for a cart, for a crew, for a
/// load — has a TERM in game hours, and a record cannot be made without one:
/// there is no default constructor, and an absent wait is an absent optional
/// in the row, not a record of a kind «none».
///
/// GAME TIME ONLY (ticks; calendar.h, one tick a game hour): by real time the
/// dog would fire differently at x1 and x12 and the run would not repeat.
///
/// CONTRACT, NO IMPLEMENTATION (core rules §12a): the kinds below are the ones
/// a design names; the waits of the hang inventory (claude/reviews/core-hang-
/// inventory-outside-reader-2026-10-02.md, 23 of them) are moved onto this
/// record by their own task, each kind with the term boss accepted.

#ifndef CORE_COMMON_WAIT_STATE_H_
#define CORE_COMMON_WAIT_STATE_H_

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/ids.h"

namespace core {

/// @brief What an agent is waiting for, by kind. Each kind has its own rules
///        object (core_world/watchdog.h, IWaitKindRules): a poll interval, an
///        inspection, an emergency action, whom to nudge and a journal line.
///        Values are appended BEFORE kWaitKindCount and never renumbered: the
///        save stores the number.
enum class WaitKind : std::uint8_t {
  /// A resident waits at a point of a cart's way for the cart to pick him up
  /// (transport design §11, «Как человек попадает на подводу»; routing stage
  /// B, the groom's plan). Term: transport.csv cart_loaded `wait_limit_hours`
  /// (1 game hour, STUB core, 0.37.166). The target is the cart's driver.
  kPassengerAwaitsCart = 0,

  /// A riding horse spends the night at its worker's yard and waits for his
  /// next shift (transport design §1, «Верховая лошадь ночует у работника»;
  /// the first case of architecture §7ж³). Term: to the worker's next working
  /// day's first hour. The agent is the kolkhoz horse herd's head; the target
  /// is the worker. Emergency: the horse stands in the stable again, «просто
  /// телепортируется в конюшню» (the human, 2 October 2026).
  kHorseAtWorkersYard,

  /// NOT A KIND: the count, for the rules table and the codecs' range check.
  kWaitKindCount,
};

inline constexpr std::uint32_t kWaitKindCount =
    static_cast<std::uint32_t>(WaitKind::kWaitKindCount);

/// @brief Whose wait it is: the table and the row id of the waiting agent. In
///        the core a cart is not a row of its own — it is its driver (a
///        resident) and a horse (a head of a herd) — so the agents are
///        residents and herds until the groom's plan gives the cart a row
///        (stage B, the plan's builder).
enum class WaitAgentTable : std::uint8_t {
  kResident = 0,
  kHerd,
};

/// @brief The waiting agent's address.
struct WaitAgent {
  WaitAgentTable table = WaitAgentTable::kResident;

  /// ResidentId::value or HerdId::value, by `table`.
  std::uint32_t id = kInvalidEntityIdValue;
};

/// @brief What the wait is FOR — the target whose disappearance ends it (a
///        driver who died, a unit taken down, a worker gone): at most one id
///        of each kind is set; which one a kind reads is that kind's rules'.
struct WaitTarget {
  ResidentId resident;
  UnitId unit;
  FieldId field;
};

/// @brief One wait, with its term. Lives in the agent's row as an optional.
///        Made only with a kind, a start and a term (no default constructor:
///        a wait without a term does not compile — architecture §7ж³).
struct WaitRecord {
  /// @param kind What is waited for.
  /// @param since The tick the wait began.
  /// @param term_hours How long an honest wait of this kind may last, game
  ///        hours (= ticks); the kind's rules name where the number comes
  ///        from. At least one: a term of nought is a wait already over.
  /// @param target What the wait is for (WaitTarget).
  WaitRecord(WaitKind kind, Tick since, std::uint32_t term_hours, WaitTarget target);

  WaitKind kind;

  /// The tick the agent entered the waiting state.
  Tick since;

  /// The term, game hours. The wait has run out when the current tick is past
  /// since + term_hours.
  std::uint32_t term_hours;

  /// The tick the watchdog last looked at this record; `since` until it has.
  /// The next look is due when the kind's poll interval has passed since it.
  Tick last_polled;

  WaitTarget target;
};

}  // namespace core

#endif  // CORE_COMMON_WAIT_STATE_H_
