/// @file
/// @brief Whether a subsystem may be built on its documented STUB defaults —
/// that is, without the balance tables it reads.
/// @threading SINGLE_THREADED
/// A compile-time choice carried into factory functions, which run once on
/// the setup thread before the simulation exists. No state, no cost.
///
/// WHY IT IS A PARAMETER AND NOT A CONVENTION. Every subsystem factory of
/// this core falls back to defaults when its tables are absent, and every one
/// of them did it in SILENCE until 2026-09-05. The fallback is legitimate —
/// a unit test builds subsystems with no tables at all, and the defaults
/// exist for exactly that — but it was happening to callers who had never
/// considered it.
///
/// THE DAY IT COST SOMETHING: `host` ran a table set with no weather.csv,
/// the core built on stub seasons without a word, and a whole day of
/// measurements described a climate the game does not have — 13 to 16 warm
/// days in the growing window against 20 to 23 on the real tables. Nothing
/// was broken from inside; the numbers were simply about another world.
///
/// A LOG LINE WOULD NOT HAVE FIXED IT. A message nobody reads is not a
/// guard, by this project's own rule that a check which cannot fail is
/// indistinguishable from a missing one (architecture §8ц). So the state is
/// UNDECLINABLE for whoever did not choose it: the caller says the word or
/// gets a refusal.
///
/// AND SILENCE MEANS THE STRICT THING. The factory functions take this
/// positionally, with no default, so a call site cannot omit it. Where it
/// lives in a config struct instead, its default is kRefused — a caller who
/// says nothing gets the answer that cannot quietly lie.

#ifndef CORE_TABLES_STUB_TABLES_H_
#define CORE_TABLES_STUB_TABLES_H_

#include <cstdint>

namespace core {

/// @brief The caller's answer to "may I run on defaults?".
enum class StubTables : std::uint8_t {
  /// A table set missing this subsystem's own tables is a BROKEN set:
  /// refuse, and say which table was missing. Everything that ships a game
  /// means this.
  kRefused = 0,

  /// The caller knows there may be no tables and wants the documented
  /// defaults. Unit tests and probes mean this, and now say so.
  kAllowed,
};

}  // namespace core

#endif  // CORE_TABLES_STUB_TABLES_H_
