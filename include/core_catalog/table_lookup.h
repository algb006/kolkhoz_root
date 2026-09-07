/// @file
/// @brief The one way a NAME written in a table becomes a dense id.
/// @threading SINGLE_THREADED
/// Parse time only, from the loading thread, before any phase runs. Nothing
/// here touches world state.
///
/// A sibling of table_value.h and deliberately not part of it: that file
/// reads a NUMBER out of a cell, this one resolves a KEY against a roster.
/// Same shape of answer, different subject, and a header carrying both would
/// be a bag.
///
/// WHY THIS EXISTS. Three parses turned "a key the roster does not have"
/// into an empty id and said nothing: crops.csv's resource column, the
/// feed links, and the food config's own copy of the lookup. An empty id is
/// then carried all the way to AddToStock, which refuses to write it —
/// silently, because that is what an unnamed resource deserves — while
/// DeliverToStores counts the load as delivered and the caller destroys it
/// against a number that never landed (0.17.79, UB-001).
///
/// The cure is at the load, not at the door: a key nobody can resolve is a
/// TYPO, and a typo belongs in the loader's error, next to its row.

#ifndef CORE_CATALOG_TABLE_LOOKUP_H_
#define CORE_CATALOG_TABLE_LOOKUP_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "core_common/ids.h"

namespace core {

class ITable;

/// @brief What a named key turned out to be. FOUR answers, and the count is
/// the point: three of them used to arrive as one empty id, and the three
/// mean opposite things.
enum class KeyState : std::uint8_t {
  /// The roster table is not in this table set at all. NOBODY TO ASK — a
  /// legitimate state, not a fault: stub table sets (StubTables::kAllowed)
  /// run through this same parse and are meant to. Refusing here would
  /// break the thing the stubs exist for.
  kNoRoster,
  /// The cell names nothing — a blank cell, OR a column this table has not
  /// got at all: ITable::CellText answers an empty view to both, and this
  /// does not tell them apart. Its sibling CellState does have a kNoColumn,
  /// so the difference is deliberate here rather than overlooked: a parse
  /// asks FindColumn before it asks anything else, and a column that is
  /// missing is its own refusal, made once, not once per row. Whether a
  /// blank is allowed is the reading parse's business and differs by column.
  kEmpty,
  /// The cell NAMES SOMETHING THE ROSTER DOES NOT HAVE. This is the one the
  /// three parses were losing: a misspelt key, and no reading of it is
  /// benign. It is not "absent" — absent is honest, this is a claim that
  /// turned out to be false.
  kNotFound,
  /// Resolved.
  kFound,
};

/// @brief Was there anything to resolve? True for the two states that carry
/// no id and no accusation; kNotFound is deliberately NOT one of them.
///
/// It shares its name with table_value.h's IsAbsent ON PURPOSE, and the two
/// are an overload set, not a collision: both answer "was there anything
/// there at all" about their own kind of answer, and both take a scoped enum,
/// so nothing converts silently between them.
constexpr bool IsAbsent(KeyState state) {
  return state == KeyState::kNoRoster || state == KeyState::kEmpty;
}

/// @brief Resolves the text of one cell against a roster table's keys.
/// @param roster The table whose `key` column names the entities; may be
///        null, which answers kNoRoster rather than kNotFound.
/// @param table The table being parsed, whose cell holds the name.
/// @param row,column Where the name sits in `table`.
/// @param row_index Written only when the answer is kFound; untouched
///        otherwise, so a caller may seed it with its own default.
/// @return Which of the four cases this was.
KeyState LookupRow(const ITable* roster,
                   const ITable& table,
                   std::uint32_t row,
                   std::uint32_t column,
                   std::uint32_t& row_index);

/// @brief Refuses a name the roster does not have, and says WHICH name.
///
/// The message carries the row and the key as written, because that is the
/// only part a person can act on: "row 7: resource 'potatos' is unknown"
/// sends the reader to the typo, while "invalid resource" sends him to the
/// code. It does NOT name the table — the caller prefixes that, the way it
/// already prefixes CellOrDefault's errors, so the two read alike.
///
/// @param column_name What to call the cell in the message ("resource").
/// @param allow_empty Whether an unnamed cell is legal here. A parameter and
///        not a rule because both answers are right somewhere: a crop with
///        no resource has nowhere to put its harvest, while an optional link
///        column may legitimately name nothing.
/// @param row_index Written only when the key resolved.
/// @return False with `error` set. True when the key resolved, when the cell
///         was legitimately empty, or when THERE IS NO ROSTER TO ASK — the
///         last is what keeps stub table sets loading.
bool RequiredRow(const ITable* roster,
                 const ITable& table,
                 std::uint32_t row,
                 std::uint32_t column,
                 std::string_view column_name,
                 bool allow_empty,
                 std::uint32_t& row_index,
                 std::string& error);

/// @brief RequiredRow, handing back the dense resource id instead of the row.
/// @param id Written only when the key resolved.
bool RequiredResource(const ITable* roster,
                      const ITable& table,
                      std::uint32_t row,
                      std::uint32_t column,
                      std::string_view column_name,
                      bool allow_empty,
                      ResourceId& id,
                      std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_TABLE_LOOKUP_H_
