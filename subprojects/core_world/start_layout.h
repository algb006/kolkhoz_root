// Internal to core_world: the parser of tables/start_layout.csv, defined in
// start_layout.cpp and consumed by genesis.cpp.
//
// THE LAYOUT IS DATA, AND DATA IS PARSED BEFORE THE WORLD IS BUILT.
// Until 2026-09-06 genesis read the cells of this table one at a time, in
// the middle of placing what they described, and it had no way to refuse:
// it builds a world and returns it. So a table that had been hand-edited
// wrong was answered with a WARNING and a zero — a field of nought hectares
// standing at the map's south-west corner — and the world was founded on it.
// Worse, a header the export renamed made genesis return "people-only" in
// silence: eighty residents, no houses, no fields, and no line saying why.
//
// The parser turns that into one refusal that NAMES THE ROW AND THE COLUMN,
// taken before a single row is placed. Genesis then walks a structure whose
// every number has already been checked, and has no cell reads left in it.
//
// What is a refusal and what is a warning is decided by ONE question: is the
// table wrong ON ITS OWN? A blank rotation slot is a fallow year, not an
// error. A coordinate outside the map is a disagreement between two tables
// and belongs to the placer that knows the map's side, not here.

#ifndef CORE_WORLD_START_LAYOUT_H_
#define CORE_WORLD_START_LAYOUT_H_

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core_common/geometry.h"

namespace core {

class ITable;

/// @brief What one layout row describes. The four values the shipped table
/// uses; anything else is a refusal, because the old code read an unknown
/// kind as "field" and a misspelt "unit" became a nought-hectare field.
enum class LayoutKind : std::uint8_t {
  kUnit,          ///< A unit of the registry; unit_type names its type.
  kField,         ///< Arable land, with a three-year rotation.
  kReserveField,  ///< Arable land held back to be built on (start canon §2).
  kMeadow,        ///< Hay meadow; upland or floodplain.
};

/// @brief One row of start_layout.csv, checked and converted.
///
/// Keys and crop names are std::string rather than std::string_view: the
/// parsed layout outlives no table today, but it is a value the caller may
/// keep, and a view into a table set that has been dropped is exactly the
/// kind of debt that is invisible until it is a crash.
struct StartLayoutRow {
  std::string key;
  LayoutKind kind = LayoutKind::kUnit;
  std::string unit_type;                ///< Registry key; empty unless kUnit.
  Vec2 place{};                         ///< Metres from the map's south-west corner.
  float area_ha = 0.0F;                 ///< Hectares; 0 on a unit row.
  std::array<std::string, 3> rotation;  ///< Crop keys; an empty slot is a fallow year.
  bool derelict = false;                ///< Land that has rested and waits to be raised.
  bool floodplain = false;              ///< Meadow kind; false means upland.
};

/// @brief The whole hand-designed start scene, in table order.
struct StartLayout {
  std::vector<StartLayoutRow> rows;
};

/// @brief Reads and checks tables/start_layout.csv.
/// @param table The loaded table; the parse keeps no reference to it.
/// @param out   Filled with one entry per table row, in table order, only
///              when the function returns true; untouched otherwise.
/// @param error On refusal, a sentence naming WHICH ROW and WHICH COLUMN —
///              the row by its key when it has one and by its 1-based
///              position when it does not, the column by its header name.
///              Untouched on success.
/// @return false when the table cannot be read as a layout. The caller then
///         has no scene: refusing is the whole point, and a half-read scene
///         is what this parser exists to stop.
/// @note Called at setup on the sim thread. Reads the table, nothing else.
bool ParseStartLayout(const ITable& table, StartLayout& out, std::string& error);

}  // namespace core

#endif  // CORE_WORLD_START_LAYOUT_H_
