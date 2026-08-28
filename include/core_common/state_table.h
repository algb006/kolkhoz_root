/// @file
/// @brief StateTable — the one storage layout for every entity kind.
/// @threading PARALLEL_READONLY
/// A StateTable instance follows the double-buffer discipline of the step
/// cycle: during parallel phases the previous-step buffer is read-only for
/// everyone, and the current buffer is written with each worker owning a
/// disjoint range of rows. Structural changes — adding or removing rows —
/// happen only in single-threaded phases. Under those rules no member of this
/// struct is ever written concurrently.
///
/// Layout, chosen for the parallel data pass (architecture, §7е):
///
///     rows:       [ R0 ][ R1 ][ R2 ] ... [ Rn-1 ]      dense, no holes
///     row_ids:    [ id ][ id ][ id ] ... [ id   ]      parallel to rows
///     row_by_id:  id.value -> row index, or kNoRow     lookup, O(1)
///
/// Workers split [0, rows.size()) into contiguous chunks — rows of one array,
/// hot in cache, no pointer chasing. Rows are structs (arrays of structs, not
/// struct of arrays): at village scale — thousands of rows — readability wins
/// and the profiler has yet to object (plan, §12: no early optimization).
///
/// Cross-references between entities are EntityId values, never row indices
/// and never pointers: rows move on removal, buffers are copied, and a save
/// is just the tables written out. Ids are the only names that survive.
///
/// This header fixes the layout; Find/Append/Remove operations are free
/// functions of the core_common implementation (stage 1, task O1).

#ifndef CORE_COMMON_STATE_TABLE_H_
#define CORE_COMMON_STATE_TABLE_H_

#include <cstdint>
#include <vector>

namespace core {

/// @brief Value of row_by_id for an id with no live row (never issued, or dead).
inline constexpr std::uint32_t kNoRow = 0xFFFFFFFFu;

/// @brief Dense table of one entity kind's state.
/// @tparam IdT  The EntityId alias of this kind (ids.h).
/// @tparam RowT The per-entity state struct: plain data, copyable, no owning
///              pointers — copying the table must snapshot it completely.
///
/// Invariants, kept by the operations and assumed everywhere:
///   * rows.size() == row_ids.size(); row_ids[i] names the entity in rows[i].
///   * row_by_id[id.value] is the current row of that id, kNoRow otherwise.
///     Index 0 (the invalid id) is always kNoRow.
///   * Ids are issued from next_id_value, which only grows: an id is never
///     reused within a campaign, so a stale reference reads as "gone", not as
///     somebody else.
///   * Removal moves the last row into the vacated slot (swap-with-last) and
///     shrinks by one. Row order therefore depends on history — deterministic,
///     since history is — but is not meaningful; anything order-sensitive
///     must sort by id first.
///   * Iteration is by row index over the dense arrays; per-id lookup is the
///     exception, not the loop body.
///
/// Saves store rows and row_ids; row_by_id and next_id_value are rebuilt on
/// load (next_id_value = max stored id + 1), so the lookup array can never
/// disagree with the data it indexes.
template <typename IdT, typename RowT>
struct StateTable {
  /// Per-entity state, dense. The unit of parallel work.
  std::vector<RowT> rows;

  /// The id of each row, parallel to `rows`.
  std::vector<IdT> row_ids;

  /// id.value -> row index, or kNoRow. Grows with next_id_value; rebuilt on
  /// load. Sized so that every value below next_id_value is a valid index.
  std::vector<std::uint32_t> row_by_id;

  /// The raw value the next Append will issue. Starts at 1: value 0 is the
  /// invalid id (ids.h) and must never be issued.
  std::uint32_t next_id_value = 1;
};

}  // namespace core

#endif  // CORE_COMMON_STATE_TABLE_H_
