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

#ifndef CORE_CATALOG_TABLE_VALUE_H_
#define CORE_CATALOG_TABLE_VALUE_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core_tables/tables.h"

namespace core {

/// @brief What a cell turned out to be. Three answers and not two: "there
/// is no such cell" is not "the cell says zero", and folding the first into
/// the second is how an empty `has_wear` came to mean "does not wear"
/// (task A5's caveat, closed here).
enum class CellState : std::uint8_t {
  /// No such row, no such column, or the cell is empty. The caller's value
  /// is left exactly as it was.
  kAbsent,

  /// Read and inside its range; the caller's value has been set.
  kRead,

  /// Present and wrong — not a number, or out of range. `error` says which.
  kBad,
};

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
