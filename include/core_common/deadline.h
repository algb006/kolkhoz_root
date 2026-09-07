/// @file
/// @brief A deadline: how many game days until something reaches its limit,
/// and the three different ways of having no number to give.
/// @threading PARALLEL_READONLY
/// Plain data with no state. Like alarms and the stock lights, a deadline is
/// DERIVED from a completed state between steps by the subsystem whose rule
/// it is, and nothing stores it.
///
/// THE THREE REFUSALS ARE NOT INTERCHANGEABLE, and telling them apart is the
/// whole reason this type exists rather than a bare integer with sentinels
/// (boss's summary, 2026-09-04; manual/technical/50-architecture.md).
///
/// | Answer | What it means | What the reader does |
/// |---|---|---|
/// | days | a real forecast | acts on it |
/// | never | will not reach the limit at the current rate | **asks again tomorrow** |
/// | not applicable | the question does not exist here | **drops it for good** |
/// | no data | a fair question, nothing to answer with | **shows nothing** |
///
/// "Never" and "not applicable" look alike and are not: a house that will
/// not wear out at today's rate gets a deadline the moment somebody moves
/// in, while a hay stack has no wear and never will. The story rechecks the
/// first and forgets the second.
///
/// The third is the dangerous one, because it is the easiest to counterfeit
/// with a zero. AN UNFILLED FIELD TAKES NO DEFAULT WHEN THE DEFAULT READS AS
/// WELL-BEING: "no deadline" must never be shown as "plenty of time".

#ifndef CORE_COMMON_DEADLINE_H_
#define CORE_COMMON_DEADLINE_H_

#include <cstdint>

namespace core {

/// @brief Which of the four answers a Deadline carries.
enum class DeadlineKind : std::uint8_t {
  /// `days` is a forecast in game days. 0 means the limit is reached now.
  kDays = 0,

  /// Not at the current rate — and the rate is what may change. A stopped
  /// unit does not wear; start it and a deadline appears.
  kNever,

  /// There is no such question here. A stack, a heap, a trench: nothing to
  /// wear, today or ever.
  kNotApplicable,

  /// The question is fair and there is nothing to answer it with — a rate
  /// the tables do not carry. Not a colour, not a zero, and above all not
  /// silence that reads as comfort.
  kNoData,

  kDeadlineKindCount,
};

/// @brief The answer, with its kind.
struct Deadline {
  DeadlineKind kind = DeadlineKind::kNoData;

  /// Game days until the limit. Meaningful only for kDays, and 0 otherwise.
  std::int32_t days = 0;
};

/// @brief How far ahead a wear forecast is willing to count: a hundred game
/// years. A unit whose term outlasts it reports exactly this, and a reader
/// takes it as "at least this" rather than as a count.
///
/// There is a horizon because the honest way to forecast an accumulating
/// share is to add it up day by day, and something has to stop the loop —
/// a share small enough to be denormal would otherwise run for ever.
/// `inline` because it lives in a header: a bare `constexpr` at namespace
/// scope has internal linkage, so every translation unit including this file
/// got its own copy at its own address. Nothing depended on that, but the
/// house style next door (ids.h, kInvalidDefIdValue) is `inline constexpr`,
/// and the standalone syntax gate is what noticed — a header compiled by
/// itself calls such a constant unused, which is exactly what it is there.
inline constexpr std::int32_t kWearForecastHorizonDays = 4800;

/// @brief A deadline in days, for the ordinary case.
constexpr Deadline DeadlineInDays(std::int32_t days) {
  return Deadline{.kind = DeadlineKind::kDays, .days = days < 0 ? 0 : days};
}

/// THE REFUSALS HAVE THREE NAMES AND NO PARAMETER, and that is the repair of
/// 0.17.81. There used to be one `NoDeadline(DeadlineKind why)`, and it would
/// take `kDays` without a murmur: the result was `{kDays, 0}`, which the
/// comment on the `kDays` enumerator declares to mean "the limit is reached
/// NOW" — named rather than counted in lines, because a line count is the
/// same ageing number as any other. So the function
/// whose name says "no deadline" was the one place in the core that could
/// forge the exact counterfeit the header above warns against — an unfilled
/// answer wearing the face of a measurement, and the most alarming
/// measurement there is.
///
/// The parameter never bought anything: every caller passed a literal, none
/// computed the reason, and the file's own thesis is that the three refusals
/// are NOT interchangeable. A selector among them says the opposite, and it
/// said it to `kDays` as readily as to the other three.
///
/// (No count of callers here. The first draft of this paragraph said
/// "eleven" and there were ten — written and wrong within the hour, which is
/// the third time in one day that a number in prose beside a rule has been
/// read as part of the rule and left to age.)
///
/// Named functions make the forgery UNSAYABLE rather than caught. There is
/// nothing to validate, no branch that could be wrong, and no argument for
/// a future caller to compute badly.

/// @brief Not at the current rate — the answer a reader RECHECKS tomorrow,
/// because the rate is what may change.
constexpr Deadline DeadlineNever() {
  return Deadline{.kind = DeadlineKind::kNever, .days = 0};
}

/// @brief The question does not exist here — the answer a reader DROPS for
/// good. A stack, a heap, a trench: nothing to wear, today or ever.
constexpr Deadline DeadlineNotApplicable() {
  return Deadline{.kind = DeadlineKind::kNotApplicable, .days = 0};
}

/// @brief A fair question with nothing to answer it — the reader SHOWS
/// NOTHING. Not a colour, not a zero, and above all not silence that reads
/// as comfort.
constexpr Deadline DeadlineNoData() {
  return Deadline{.kind = DeadlineKind::kNoData, .days = 0};
}

}  // namespace core

#endif  // CORE_COMMON_DEADLINE_H_
