// The start-layout parser (start_layout.h): one pass over
// tables/start_layout.csv that either yields a checked scene or refuses,
// naming the row and the column.

#include "start_layout.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core_catalog/table_value.h"
#include "core_common/quantities.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// @brief Names the row the way a person reading the CSV can find it: by its
/// key when it has one, and by its position among the data rows when it does
/// not — the position is what is left when the key is the broken thing.
std::string RowName(std::string_view key, std::uint32_t row) {
  std::string name = "row " + std::to_string(row + 1);
  if (!key.empty()) {
    name += " ('" + std::string(key) + "')";
  }
  return name;
}

std::string Refuse(std::string_view key,
                   std::uint32_t row,
                   std::string_view column,
                   std::string_view what) {
  return "start_layout: " + RowName(key, row) + ", column '" + std::string(column) +
         "': " + std::string(what);
}

/// @brief One number of the layout, read by the catalogue's reader and no
/// second copy of its rules.
///
/// Genesis used to read these cells with a band of its own, and said so:
/// "written here and not taken from core_catalog because genesis cannot
/// refuse". THE PARSER CAN, so the reason is gone — and with it the second
/// ceiling. The bands were not even the same number (1e9 here, Range's 1e7
/// there), which is what a rule with two homes looks like from the outside.
///
/// @param range What this column may hold. NonNegative for the ones the
///        table's own header calls never-negative: metres from the
///        south-west corner, and an area.
/// @return An error sentence naming the row and the column, empty when the
///         cell is fine or simply absent.
std::string ReadLayoutNumber(const ITable& table,
                             std::uint32_t row,
                             std::uint32_t column,
                             Range range,
                             std::string_view column_name,
                             std::string_view key,
                             float* value) {
  std::string trouble;
  if (OptionalCell(table, row, column, range, *value, trouble)) {
    return {};
  }
  return Refuse(key, row, column_name, trouble);
}

}  // namespace

bool ParseStartLayout(const ITable& table, StartLayout& out, std::string& error) {
  // FOUR COLUMNS ARE THE TABLE. Without a key nothing can be found by name,
  // without a kind nothing knows what it is, and without coordinates there
  // is no scene — only a list. A missing one of these used to be answered
  // with a people-only world and no line at all, which is the same silence
  // the whole delta of these two days has been closing.
  struct RequiredColumn {
    const char* name;
    std::uint32_t index;
  };

  RequiredColumn required[] = {
      {"key", table.FindColumn("key")},
      {"kind", table.FindColumn("kind")},
      {"x_m", table.FindColumn("x_m")},
      {"y_m", table.FindColumn("y_m")},
      {"area_ha", table.FindColumn("area_ha")},
  };
  for (const RequiredColumn& column : required) {
    if (column.index == kNoTableColumn) {
      error = std::string("start_layout: no column '") + column.name + "' — the table cannot be" +
              " read as a layout";
      return false;
    }
  }
  const std::uint32_t key_col = required[0].index;
  const std::uint32_t kind_col = required[1].index;
  const std::uint32_t x_col = required[2].index;
  const std::uint32_t y_col = required[3].index;
  const std::uint32_t area_col = required[4].index;

  // The optional ones: a layout that carries no rotation is a layout of
  // fallow fields, not a broken table.
  //
  // 'unit_type' and 'meadow_kind' are optional as COLUMNS and required by
  // the ROWS that need them — a layout with no meadows in it owes nothing
  // to meadow_kind. So their refusals below say which of the two happened:
  // a missing column is not a defective row, and reporting it as one is the
  // very diagnosis this parser exists to stop (UB-006 of the 2026-09-06
  // cycle, found in the parser written that same morning).
  const std::uint32_t type_col = table.FindColumn("unit_type");
  const std::uint32_t derelict_col = table.FindColumn("is_derelict");
  // The two columns of the first morning's condition. Optional as columns
  // for the same reason as the rotation: a table exported before they
  // existed describes a start where nothing is inherited, which is a
  // poorer scene and not a broken one.
  const std::uint32_t wear_col = table.FindColumn("start_wear_pct");
  const std::uint32_t dead_col = table.FindColumn("start_dead");
  const std::uint32_t meadow_kind_col = table.FindColumn("meadow_kind");
  const std::array<std::uint32_t, 3> rotation_cols = {table.FindColumn("rotation_year0"),
                                                      table.FindColumn("rotation_year1"),
                                                      table.FindColumn("rotation_year2")};

  StartLayout parsed;
  parsed.rows.reserve(table.RowCount());
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    StartLayoutRow entry;
    entry.key = std::string(table.CellText(row, key_col));
    if (entry.key.empty()) {
      error = Refuse({}, row, "key", "a layout row must have a key");
      return false;
    }
    // A duplicate key is not a cosmetic defect: the stock table finds a unit
    // by this key, and with two rows carrying it the stores would go to
    // whichever the search reached first.
    for (const StartLayoutRow& earlier : parsed.rows) {
      if (earlier.key == entry.key) {
        error = Refuse(entry.key, row, "key", "this key is already used by an earlier row");
        return false;
      }
    }

    const std::string_view kind = table.CellText(row, kind_col);
    if (kind == "unit") {
      entry.kind = LayoutKind::kUnit;
    } else if (kind == "field") {
      entry.kind = LayoutKind::kField;
    } else if (kind == "reserve_field") {
      entry.kind = LayoutKind::kReserveField;
    } else if (kind == "meadow") {
      entry.kind = LayoutKind::kMeadow;
    } else {
      error = Refuse(entry.key,
                     row,
                     "kind",
                     "'" + std::string(kind) + "' is not a layout kind (unit, field, " +
                         "reserve_field, meadow)");
      return false;
    }

    // The table's own header says metres from the south-west corner, NEVER
    // negative, and an area is an area. The band genesis read them with was
    // symmetric, so until today a minus sign in front of a coordinate
    // passed every check on the way in and laid a yard outside the world.
    std::string trouble =
        ReadLayoutNumber(table, row, x_col, Range::NonNegative(), "x_m", entry.key, &entry.place.x);
    if (trouble.empty()) {
      trouble = ReadLayoutNumber(
          table, row, y_col, Range::NonNegative(), "y_m", entry.key, &entry.place.y);
    }
    if (trouble.empty()) {
      trouble = ReadLayoutNumber(
          table, row, area_col, Range::NonNegative(), "area_ha", entry.key, &entry.area_ha);
    }
    float derelict = 0.0F;
    if (trouble.empty()) {
      trouble = ReadLayoutNumber(
          table, row, derelict_col, Range::Unit(), "is_derelict", entry.key, &derelict);
    }
    // THE BAND STARTS AT MINUS ONE ON PURPOSE, and it is the only range in
    // this parser that admits a negative: -1 is the column's own word for
    // "as built", so it has to pass the same door the real wears come
    // through. Anything BELOW it is a typo, and -1 is exactly what a typo
    // of -10 would otherwise be silently rounded into if the band were
    // written as "negative means absent".
    //
    // AND THE BAND IS ONE NOTCH LOOSER THAN THAT SENTENCE, which is said
    // here rather than left to be discovered: it also admits the open
    // interval (-1, 0), and every reader downstream asks `>= 0`, so a
    // mistyped -0.5 would read as "nothing was said" instead of refusing.
    // Unreachable from the shipped table, which carries only -1 and
    // 0..100; open as UB-001 of the 0.17.83 cycle, to be closed by a
    // predicate rather than by a range. A comment claiming a guard is
    // stricter than the guard is exactly the described door this project
    // keeps finding, so the claim is corrected before the code is.
    float wear = -1.0F;
    if (trouble.empty()) {
      trouble = ReadLayoutNumber(table,
                                 row,
                                 wear_col,
                                 Range{.low = -1.0F, .high = kWearScale},
                                 "start_wear_pct",
                                 entry.key,
                                 &wear);
    }
    float dead = 0.0F;
    if (trouble.empty()) {
      trouble =
          ReadLayoutNumber(table, row, dead_col, Range::Unit(), "start_dead", entry.key, &dead);
    }
    if (!trouble.empty()) {
      error = trouble;
      return false;
    }
    entry.derelict = derelict > 0.5F;
    entry.start_wear_pct = wear;
    entry.start_dead = dead > 0.5F;

    // A DEAD FIELD IS NOT A THING, and neither is a worn meadow: both
    // columns describe a BUILDING, and a value on a land row would be read
    // by nobody — the silent kind of wrong table this parser exists to
    // stop. Said as a refusal rather than ignored, because ignoring it is
    // what "the core has no rule for that cell" looked like every previous
    // time it cost a day.
    if (entry.kind != LayoutKind::kUnit) {
      if (wear >= 0.0F) {
        error = Refuse(entry.key, row, "start_wear_pct", "only a unit row can be worn");
        return false;
      }
      if (entry.start_dead) {
        error = Refuse(entry.key, row, "start_dead", "only a unit row can start dead");
        return false;
      }
    }

    if (entry.kind == LayoutKind::kUnit) {
      entry.unit_type = std::string(table.CellText(row, type_col));
      if (entry.unit_type.empty()) {
        error =
            Refuse(entry.key,
                   row,
                   "unit_type",
                   type_col == kNoTableColumn ? "there is no such column, and this row is a unit"
                                              : "a unit row must name its type");
        return false;
      }
    }

    // The meadow kind decides the yield (meadow_kinds.csv), so a meadow that
    // does not say which it is would silently become the upland one — the
    // cheaper of the two, and the wrong one for every floodplain row.
    if (entry.kind == LayoutKind::kMeadow) {
      const std::string_view meadow_kind = table.CellText(row, meadow_kind_col);
      if (meadow_kind == "floodplain") {
        entry.floodplain = true;
      } else if (meadow_kind != "upland") {
        error =
            Refuse(entry.key,
                   row,
                   "meadow_kind",
                   meadow_kind_col == kNoTableColumn
                       ? "there is no such column, and this row is a meadow"
                       : "'" + std::string(meadow_kind) + "' is neither 'upland' nor 'floodplain'");
        return false;
      }
    }

    for (std::size_t slot = 0; slot < entry.rotation.size(); ++slot) {
      if (rotation_cols[slot] != kNoTableColumn) {
        entry.rotation[slot] = std::string(table.CellText(row, rotation_cols[slot]));
      }
    }
    parsed.rows.push_back(std::move(entry));
  }

  out = std::move(parsed);
  return true;
}

}  // namespace core
