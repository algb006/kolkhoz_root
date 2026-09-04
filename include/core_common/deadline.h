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
constexpr std::int32_t kWearForecastHorizonDays = 4800;

/// @brief A deadline in days, for the ordinary case.
constexpr Deadline DeadlineInDays(std::int32_t days) {
  return Deadline{.kind = DeadlineKind::kDays, .days = days < 0 ? 0 : days};
}

/// @brief One of the three refusals, said out loud.
constexpr Deadline NoDeadline(DeadlineKind why) {
  return Deadline{.kind = why, .days = 0};
}

}  // namespace core

#endif  // CORE_COMMON_DEADLINE_H_
