/// @file
/// @brief Find/Append/Remove/RebuildLookup — the operations of StateTable.
/// @threading PARALLEL_READONLY
/// Same discipline as the tables these functions operate on (state_table.h):
/// FindRow is a pure read of row_by_id — safe from any worker against the
/// previous-step buffer or blocks finalized by earlier phases. The mutating
/// operations (Append, Remove, RebuildLookup) run only in single-threaded
/// phases: the step-cycle buffer law forbids shape changes in parallel code.
///
/// The operations keep the invariants documented on StateTable; nothing else
/// in the codebase may touch row_by_id or next_id_value directly.

#ifndef CORE_COMMON_STATE_TABLE_OPS_H_
#define CORE_COMMON_STATE_TABLE_OPS_H_

#include <cstdint>

#include "core_common/state_table.h"

namespace core {

/// @brief Row index of `id`, or kNoRow if the id was never issued or the
/// entity is gone. O(1).
template <typename IdT, typename RowT>
constexpr std::uint32_t FindRow(const StateTable<IdT, RowT>& table, IdT id) {
  if (id.value >= table.row_by_id.size()) {
    return kNoRow;
  }
  return table.row_by_id[id.value];
}

/// @brief Appends a new entity and issues its id (never reused within a
/// campaign). Returns the new id; the row lands at index rows.size() - 1.
/// @pre Fewer than 2^32 - 1 appends over the campaign — ids are not recycled.
template <typename IdT, typename RowT>
IdT AppendRow(StateTable<IdT, RowT>& table, const RowT& row) {
  const IdT id{table.next_id_value};
  ++table.next_id_value;
  table.row_by_id.resize(table.next_id_value, kNoRow);
  table.row_by_id[id.value] = static_cast<std::uint32_t>(table.rows.size());
  table.rows.push_back(row);
  table.row_ids.push_back(id);
  return id;
}

/// @brief Removes the entity `id` by swap-with-last; its id maps to kNoRow
/// from now on. Returns false if there is no such live entity.
/// @note The last row moves into the vacated slot: any row index obtained
/// before the call is stale after it. Ids remain valid — that is the point.
template <typename IdT, typename RowT>
bool RemoveRow(StateTable<IdT, RowT>& table, IdT id) {
  const std::uint32_t row = FindRow(table, id);
  if (row == kNoRow) {
    return false;
  }
  const auto last_row = static_cast<std::uint32_t>(table.rows.size() - 1);
  if (row != last_row) {
    table.rows[row] = table.rows[last_row];
    table.row_ids[row] = table.row_ids[last_row];
    table.row_by_id[table.row_ids[row].value] = row;
  }
  table.rows.pop_back();
  table.row_ids.pop_back();
  table.row_by_id[id.value] = kNoRow;
  return true;
}

/// @brief Rebuilds row_by_id from rows + row_ids after loading a save.
/// The save stores the dense arrays plus next_id_value (state_table.h);
/// the loader sets next_id_value first and this function rebuilds the lookup,
/// so it can never disagree with the data.
/// @note next_id_value is only ever raised here (to max stored id + 1 when
/// the stored counter is inconsistent or missing), never lowered: lowering it
/// would re-issue the ids of entities that died before the save — MEM-001,
/// stale references would resolve to somebody else instead of "gone".
template <typename IdT, typename RowT>
void RebuildLookup(StateTable<IdT, RowT>& table) {
  std::uint32_t max_id_value = 0;
  for (const IdT id : table.row_ids) {
    if (id.value > max_id_value) {
      max_id_value = id.value;
    }
  }
  if (table.next_id_value <= max_id_value) {
    table.next_id_value = max_id_value + 1;
  }
  table.row_by_id.assign(table.next_id_value, kNoRow);
  const auto row_count = static_cast<std::uint32_t>(table.row_ids.size());
  for (std::uint32_t row = 0; row < row_count; ++row) {
    table.row_by_id[table.row_ids[row].value] = row;
  }
}

}  // namespace core

#endif  // CORE_COMMON_STATE_TABLE_OPS_H_
