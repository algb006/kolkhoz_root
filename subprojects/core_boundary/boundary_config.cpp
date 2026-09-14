// Parsing of the boundary's two knobs (boundary_config.h).
//
// The reader is written out here rather than shared with core_residents'
// table_read.h: that header is private to its module, and the boundary names
// no subsystem by design (manual/70-boundary.md §9). Two knobs are cheaper to
// read than a dependency that would let a subsystem's rules leak across the
// seam.

#include "boundary_config.h"

#include <cstdint>
#include <optional>
#include <string_view>

#include "core_catalog/definitions.h"
#include "core_catalog/table_value.h"
#include "core_tables/required_tables.h"
#include "core_tables/tables.h"

namespace core {
namespace {

constexpr std::uint32_t kMonthsPerBioYear = 12;

}  // namespace

bool ParseBoundaryConfig(const ITableSet& tables,
                         StubTables stubs,
                         BoundaryConfig& config,
                         std::string& error) {
  // The boundary reads one table of its own, and the catalogue's two behind
  // LoadDefinitions. Both refusals are the caller's word, carried in from
  // SessionConfig (core_tables/stub_tables.h).
  if (!RequireTables(tables, stubs, "boundary", {"life"}, &error)) {
    return false;
  }
  // The map side comes from the CATALOGUE, which is its one reader. This
  // module used to read the column itself and, alone among the three
  // readers, treated a zero as a REFUSAL rather than as "the table set
  // declares no map" — one cell with two meanings, and nothing that would
  // ever have made them disagree out loud (task A6).
  Definitions definitions;
  if (!LoadDefinitions(tables, stubs, definitions, error)) {
    return false;
  }
  config.map_side_m = definitions.map_side_m;
  // The posts' shifts (post_shift.h), dense by ProfessionId. The labor
  // module reads the same column for its own question — who is on the
  // accountant's list — and a profession key is data, not a dependency.
  if (const ITable* const professions = tables.FindTable("professions")) {
    const std::uint32_t shift_column = professions->FindColumn("shift");
    config.post_shift.assign(professions->RowCount(), PostShift::kWorkday);
    for (std::uint32_t row = 0; row < professions->RowCount() && shift_column != kNoTableColumn;
         ++row) {
      if (!ParsePostShift(professions->CellText(row, shift_column), config.post_shift[row])) {
        error = "professions: " + std::string(professions->CellText(row, 0)) + ": shift '" +
                std::string(professions->CellText(row, shift_column)) +
                "' is none of workday, evening, bath_day";
        return false;
      }
    }
  }
  const ITable* const life = tables.FindTable("life");
  if (life == nullptr) {
    return true;  // no table: the defaults above are the canonical values
  }
  // The shared reader states the fault; naming the table and the key is the
  // caller's job, because only the caller knows which table it was reading.
  // That prefix was lost for one commit when this moved off the module's own
  // copy of the reader — a message regression is still a regression.
  if (!OptionalValue(
          *life, "life_speedup", Range{.low = 0.1F, .high = 100.0F}, config.life_speedup, error)) {
    PrefixError("life", "life_speedup", error);
    return false;
  }
  // Stated in months by the design, so read in months and converted once.
  float infant_age_months = config.infant_age_bio_years * static_cast<float>(kMonthsPerBioYear);
  if (!OptionalValue(*life,
                     "infant_age_months",
                     Range{.low = 0.0F, .high = 240.0F},
                     infant_age_months,
                     error)) {
    PrefixError("life", "infant_age_months", error);
    return false;
  }
  config.infant_age_bio_years = infant_age_months / static_cast<float>(kMonthsPerBioYear);
  return true;
}

}  // namespace core
