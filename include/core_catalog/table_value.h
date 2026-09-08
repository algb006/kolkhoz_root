/// @file
/// @brief The one way a number is read out of a balance table, with its
/// range — and the one place that decides what an empty cell means.
/// @threading SINGLE_THREADED
/// Factory-time code only: every config parser runs once, on the setup
/// thread, before the simulation exists.
///
/// WHY THIS MODULE EXISTS (boss, 2026-09-04). The policy below was written
/// three times: `CellOrDefault` in core_production, `CellOrDefault` in
/// core_construction, and `OptionalCell` in core_residents. The two that
/// share a name **do not share a signature** — one takes
/// `(fallback, low, high)`, the other `(low, high, fallback)` — so a call
/// written against one and compiled against the other silently swaps the
/// default with the lower bound, and the compiler has nothing to say about
/// three floats in a row.
///
/// A fourth reader, `RequiredValue` in core_residents, had **no range at
/// all**, which is where UB-001 and UB-002 of the delivery cycle came from:
/// `migration_per_year` and a migrant's age reach `static_cast<int32_t>`
/// straight off the disk.
///
/// **Three readers of one rule is not "several", it is "no owner."** The
/// place a value is checked and the place it is read have to be the same
/// place, or validation drifts away from reading exactly as the plot radius
/// drifted away from the plot rule.
///
/// THE POLICY, and it is the one every subsystem factory already meant:
/// a MISSING table, row, column or cell keeps the caller's default — a unit
/// test's world has no tables at all — while a PRESENT cell that cannot be
/// read or falls out of range REFUSES the configuration. A half-understood
/// balance is worse than none.
///
/// THE FIRST CLAUSE IS ABOUT THIS READER AND NOT ABOUT THE RUN. Since
/// 2026-09-08 a caller may declare that a set without the tables it reads is
/// broken, and then the factory refuses by name before any of these
/// functions is reached (core_tables/required_tables.h). What is written
/// here stays exactly true of the reader: it never invents a refusal out of
/// an absence, because absence is not its question.
///
/// AND THE POLICY HAS ONE NAMED EXCEPTION SINCE TASK A6, said here rather
/// than left to be discovered twenty lines below. A blank cell in a column
/// that EXISTS is not a missing cell in the sense above: the column is there
/// because somebody meant to fill it, so the blank is a hole in the data
/// rather than an absence of data. RequiredCell is the wrapper that says so,
/// and it is opt-in — the default for a blank stays "keep the caller's",
/// because most columns legitimately carry blanks for most rows.
///
/// Until A6 this paragraph and that rule could not both be true, because
/// nothing could tell a blank cell from a missing column: one CellState
/// stood for both. Two rules for one situation in one file is how a reader
/// comes away certain of the wrong one.

#ifndef CORE_CATALOG_TABLE_VALUE_H_
#define CORE_CATALOG_TABLE_VALUE_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core_tables/tables.h"

namespace core {

/// @brief What a cell turned out to be. FIVE answers, and the count is the
/// history: two were not enough because "there is no such cell" is not "the
/// cell says zero" — that fold is how an empty `has_wear` came to mean "does
/// not wear" (task A5). Three were not enough either, because the one answer
/// for absence covered a missing row, a missing column and a blank cell in a
/// column that exists, and those are three different pieces of news (task
/// A6). Whoever adds a sixth: the reason each of these is separate is that a
/// caller was seen to need the difference.
enum class CellState : std::uint8_t {
  /// The table has no such row. The caller's value is untouched.
  kNoRow,

  /// The table has no such COLUMN at all — nobody has said anything about
  /// this property, for any row. The caller's value is untouched.
  kNoColumn,

  /// The column is there and this row's cell is BLANK. The caller's value is
  /// untouched.
  ///
  /// THIS IS THE ONE THAT USED TO BE INDISTINGUISHABLE, and it is the whole
  /// reason the enumerator was split (task A6, 2026-09-07). All three used to
  /// return one `kAbsent`, so "nobody said anything about wear" and "this row
  /// was left blank in a column that means to be filled" arrived at every
  /// caller as the same value — and a caller that wanted to tell them apart
  /// had to ask the table a second question of its own, which exactly one
  /// caller did and the other ninety did not.
  ///
  /// A column that EXISTS is a column the export means to fill. A blank in it
  /// is a hole in the data, not a value, and the danger is that it reads as
  /// the answer costing nothing to notice: zero.
  kEmpty,

  /// Read and inside its range; the caller's value has been set.
  kRead,

  /// Present and wrong — not a number, or out of range. `error` says which.
  kBad,
};

/// @brief Was there anything to read at all? True for all three kinds of
/// nothing.
///
/// It exists so that a caller who genuinely does not care which kind of
/// nothing it was can say so IN ONE WORD, instead of writing out three cases
/// and thereby pretending to have considered them. The wrappers below are its
/// first users: `CellOrDefault` writes its fallback for any nothing, and that
/// is right — a fallback is an answer to "there is no value here", whatever
/// the reason.
constexpr bool IsAbsent(CellState state) {
  return state == CellState::kNoRow || state == CellState::kNoColumn || state == CellState::kEmpty;
}

/// @brief The inclusive bounds a value must fall inside.
///
/// There is no default, and that is deliberate: a range nobody wrote down
/// is exactly the hole this module was made to close. A value that really
/// has no bounds says so with `Range::Any()`, which is a sentence in the
/// code rather than an omission.
struct Range {
  float low = 0.0F;

  float high = 0.0F;

  /// @brief A knob with no bounds worth naming. Still rejects NaN and
  /// infinity: those are not "any value", they are "not a value".
  static constexpr Range Any() { return Range{.low = -kUnbounded, .high = kUnbounded}; }

  /// @brief Zero and up, to the same practical ceiling.
  static constexpr Range NonNegative() { return Range{.low = 0.0F, .high = kUnbounded}; }

  /// @brief A SHARE: zero to one inclusive. Named because "0..1" written out
  /// at each call site is a range that drifts one call at a time, and
  /// because a share is the commonest knob in the balance tables.
  static constexpr Range Unit() { return Range{.low = 0.0F, .high = 1.0F}; }

  /// The largest magnitude any balance number may carry. Not
  /// `float`'s maximum: every one of these ends up in a `float` sum, an
  /// `int32` cast or a gram count, and a number this size already means the
  /// table is wrong. Ten million metres is most of a map's diagonal, ten
  /// million years outlives the campaign by five orders of magnitude.
  static constexpr float kUnbounded = 1.0e7F;
};

/// @brief One scalar knob of a key/value table: where it lands and what it
/// may be.
struct ScalarKnob {
  std::string_view key;

  float* value;

  Range range;
};

/// @brief Turns "a cell is not a number" into "food: ration_auto: a cell is
/// not a number", building the message with appends only.
void PrefixError(std::string_view table, std::string_view key, std::string& error);

/// @brief Reads one cell as a real number, bounded.
/// @param value Written only when the answer is kRead; untouched otherwise.
/// @note The range test is written POSITIVELY (`>= low && <= high`) so that
///       NaN fails it: NaN compares false against everything, including
///       itself, and a negated test would let it through. Infinity fails
///       the same test for any finite bound.
CellState ReadCell(const ITable& table,
                   std::uint32_t row,
                   std::uint32_t column,
                   Range range,
                   float& value,
                   std::string& error);

/// @brief Reads one cell, leaving the caller's value alone when it is
/// absent. The difference from CellOrDefault is the whole point of
/// CellState: here "no such cell" keeps whatever the caller already had,
/// there it writes a named fallback. Both are honest; folding them into one
/// is not.
/// @return false only for a present-and-wrong cell.
bool OptionalCell(const ITable& table,
                  std::uint32_t row,
                  std::uint32_t column,
                  Range range,
                  float& value,
                  std::string& error);

/// @brief Reads one cell and REFUSES a blank one in a column that exists.
/// @return false for a present-and-wrong cell, for an empty cell in a present
///         column, and for a row that is not there. `error` names the table,
///         the column and the row for the last two; for a cell that is
///         present and wrong it carries the reader's own words ("a cell is
///         not a number", "a cell is out of range") unprefixed, which is
///         what every other wrapper here does with them.
///
/// The fourth shape, and the one that was missing until 2026-09-07. The three
/// that were here already answer "what do I do when there is no value":
/// OptionalCell keeps the caller's, CellOrDefault writes a named fallback,
/// ReadCell hands the question back. None of them could say THERE MUST BE
/// ONE — so a column whose every row is meant to be filled had no way to say
/// so, and a hole in an export read as a number.
///
/// @note A missing COLUMN is not a refusal here, and that is deliberate: a
///       column absent for every row is a table this build was not given,
///       which is a different conversation (stub_tables.h) from a row left
///       blank in a column its neighbours fill. A missing ROW is refused,
///       because that is neither — it is a caller asking about something
///       that does not exist, and the first draft of this wrapper let it
///       pass in silence.
bool RequiredCell(const ITable& table,
                  std::string_view table_name,
                  std::string_view column_name,
                  std::uint32_t row,
                  std::uint32_t column,
                  Range range,
                  float& value,
                  std::string& error);

/// @brief Reads one cell, falling back to `fallback` when it is absent.
/// @return false only for a present-and-wrong cell.
bool CellOrDefault(const ITable& table,
                   std::uint32_t row,
                   std::uint32_t column,
                   Range range,
                   float fallback,
                   float& value,
                   std::string& error);

/// @brief Reads one key's `value` cell of a key/value table, leaving the
/// caller's value alone when the key is not there.
bool OptionalValue(
    const ITable& table, std::string_view key, Range range, float& value, std::string& error);

/// @brief Reads one key's `value` cell of a key/value table, REFUSING when
/// the key is not there.
/// @note This is the one that had no range until 2026-09-04. A required
///       value is required because something depends on it, and something
///       that depends on a number depends on its size too.
bool RequiredValue(const ITable& table,
                   std::string_view table_name,
                   std::string_view key,
                   Range range,
                   float& value,
                   std::string& error);

/// @brief Refuses a key/value table whose rows do not declare who reads them,
/// or which declares a key for the core that no core module reads.
///
/// WHY IT LIVES HERE AND NOT IN THE MODULE THAT READS THE TABLE. It was
/// written in core_time on 2026-09-06 with the debt named in place: `knows`
/// is one MODULE's key set, and "the core" is many modules. Exact for a
/// table only one module reads; wrong the moment a second one does, because
/// a key of module B, honestly declared `core`, is refused by module A.
///
/// THAT DAY CAME THE SAME AFTERNOON — world_params.csv gained five body rows
/// read by genesis while the check sat in core_time — so the rule moved to
/// the one module both a reader and the ASSEMBLY may depend on. The union of
/// keys is gathered where every module is known, and no module judges the
/// core any more.
///
/// @param known The keys the core reads, gathered from the readers
///        THEMSELVES rather than written out a second time beside them. A
///        hand-kept twin of this list would age exactly as the mirror it
///        replaces: silently, and only on the day the two disagree.
/// @return false with `error` naming the row and what is wrong with it.
bool CheckDeclaredReaders(const ITable& table,
                          std::string_view table_name,
                          std::span<const std::string_view> known,
                          std::string& error);

/// @brief Reads a whole run of scalar knobs, naming the offender on failure.
/// @pre Every knob's `value` points at a real float. A knob array declared
///      larger than its initialiser list leaves the tail value-initialised
///      with a null `value`, and the dereference would then be UB on every
///      call — the defect UB-001 found in food_config.cpp. The assert names
///      it at the first run; `std::to_array` at the declaration prevents it.
bool ReadKnobs(const ITable& table,
               std::string_view table_name,
               std::span<const ScalarKnob> knobs,
               std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_TABLE_VALUE_H_
