/// @file
/// @brief The year's book of building materials: what was dug, what the carts
/// brought in off the pits, what went into walls and whose, and what lay on
/// the pits' piles when the year turned.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN PRINTS THIS (boss, core-boss-c2-site-supply-2026-10-09 [91],
/// [99]). «Where did the clay go» was answered twice in October 2026 by a
/// probe's patch on the site's prints, because the core booked what went
/// into walls without the unit type and the pits' carting summed into the
/// fields'. Save 150 put the three columns into the closed book
/// (YearLedger::dug, hauled_from_sites, built_in_by_type); this prints them.
///
/// WHAT THE NUMBERS ARE NOT. «Built in» is booked when a level, a repair or
/// an insulation CLOSES, not when the material reached the site: a site still
/// going up at the year's turn holds its stock in neither column. And the
/// unit type is the type's, not the policy's that ordered it — the order's
/// door is not in this print.

#ifndef TESTS_RUN_COMMON_YEAR_BOOK_H_
#define TESTS_RUN_COMMON_YEAR_BOOK_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core_common/extraction_state.h"
#include "core_common/ledger_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

/// @brief Keeps every closed year's material columns and prints them, a
/// line a year and material, at the run's end.
class YearBook {
 public:
  explicit YearBook(const core::ITableSet& tables) {
    ReadKeys(tables, "resources", resource_keys_);
    ReadKeys(tables, "unit_types", unit_type_keys_);
  }

  /// @brief Takes the book that has just closed. Call once per closed year,
  /// on the first tick after the rotation; the piles are read then.
  void CloseYear(const core::WorldState& world) {
    const core::YearLedger& book = world.ledger.closed;
    Year year;
    year.year = book.year;
    year.dug = book.dug;
    year.from_pits = book.hauled_from_sites;
    year.built_in = book.built_in;
    year.by_type = book.built_in_by_type;
    for (const core::ExtractionSiteRow& site : world.extraction_sites.rows) {
      core::AddLedgerAmount(year.on_piles, site.resource, site.load_grams);
    }
    years_.push_back(std::move(year));
  }

  /// @brief Prints a line a year for every material dug, carted off a pit,
  /// built in or lying on a pile that year. A year with none prints that it
  /// had none: an absent line would read as a year not run.
  void Report(std::string_view run_name) const {
    std::cout << run_name << ": the year's book (tonnes; built in = closed levels, repairs and "
              << "insulations; by unit type, not by the order's door) — " << years_.size()
              << " closed years\n";
    for (const Year& year : years_) {
      std::size_t lines = 0;
      for (std::size_t resource = 0; resource < resource_keys_.size(); ++resource) {
        const core::Grams dug = At(year.dug, resource);
        const core::Grams carted = At(year.from_pits, resource);
        const core::Grams built = At(year.built_in, resource);
        const core::Grams piled = At(year.on_piles, resource);
        if (dug == 0 && carted == 0 && built == 0 && piled == 0) {
          continue;
        }
        ++lines;
        std::cout << run_name << ":   book year " << year.year << " — " << resource_keys_[resource]
                  << ": dug " << Tonnes(dug) << ", carted off the pits " << Tonnes(carted)
                  << ", built in " << Tonnes(built);
        const char* separator = " (";
        for (const core::BuiltInRow& row : year.by_type) {
          if (row.resource.value != resource) {
            continue;
          }
          std::cout << separator << KeyOf(unit_type_keys_, row.type.value) << ' '
                    << Tonnes(row.grams);
          separator = ", ";
        }
        std::cout << (separator[0] == ',' ? ")" : "") << ", on the pits' piles at the turn "
                  << Tonnes(piled) << '\n';
      }
      if (lines == 0) {
        std::cout << run_name << ":   book year " << year.year
                  << " — nothing dug, carted off a pit or built in\n";
      }
    }
  }

 private:
  struct Year {
    std::uint16_t year = 0;
    core::ResourceAmounts dug;
    core::ResourceAmounts from_pits;
    core::ResourceAmounts built_in;
    core::BuiltInByType by_type;
    core::ResourceAmounts on_piles;
  };

  static void ReadKeys(const core::ITableSet& tables,
                       const char* table_name,
                       std::vector<std::string>& keys) {
    if (const core::ITable* table = tables.FindTable(table_name)) {
      const std::uint32_t key_col = table->FindColumn("key");
      for (std::uint32_t index = 0; index < table->RowCount(); ++index) {
        keys.emplace_back(table->CellText(index, key_col));
      }
    }
  }

  static core::Grams At(const core::ResourceAmounts& column, std::size_t index) {
    return index < column.size() ? column[index] : 0;
  }

  static std::string KeyOf(const std::vector<std::string>& keys, std::uint16_t index) {
    return index < keys.size() ? keys[index] : std::to_string(index);
  }

  static std::string Tonnes(core::Grams grams) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f t", static_cast<double>(grams) / 1.0e6);
    return text;
  }

  std::vector<std::string> resource_keys_;
  std::vector<std::string> unit_type_keys_;
  std::vector<Year> years_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_YEAR_BOOK_H_
