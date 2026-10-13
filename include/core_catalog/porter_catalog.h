/// @file
/// @brief The porter's rules as the tables give them (core_common/haul.h,
///        PorterRules): one reading of labor.csv's carry rows and of
///        resources.csv's `cargo_form`.
/// @threading PARALLEL_READONLY
/// Parsed once at assembly on the sim thread; read-only afterwards.
///
/// WHY IN THE CATALOGUE (#671, the second delivery; 0.37.240). A walker's
/// trip is counted by three modules - production writes a load's seam by it,
/// labour drains the seam and pays by it, logistics cuts the day into trips
/// by it - and until now each read labor.csv `carry_kg_adult` for itself. With
/// the carry depending on the load's form, three readings of two tables are
/// three places for a rule to part; they read it here.

#ifndef CORE_CATALOG_PORTER_CATALOG_H_
#define CORE_CATALOG_PORTER_CATALOG_H_

#include <string>
#include <vector>

#include "core_common/cargo_form.h"
#include "core_common/haul.h"
#include "core_tables/tables.h"

namespace core {

/// @brief Reads resources.csv `cargo_form` into `forms`, dense by ResourceId -
///        THE ONE HOME of the column's words (bulk, manure, packed, rick,
///        long, live).
/// @return false with `error` naming the row for an empty cell or a word that
///         is no form, in a column that exists. A table WITHOUT the column - a
///         test's hand-made set - leaves every resource kNone and is not an
///         error.
bool ReadCargoForms(const ITable& resources, std::vector<CargoForm>& forms, std::string& error);

/// @brief Fills `rules` from `tables`: labor.csv carry_kg_adult (20),
///        carry_kg_rick (10), carry_kg_stretcher_pair (40),
///        log_pair_max_leg_m (300), log_pair_loaded_pace (0.5); resources.csv
///        `cargo_form`, and the row `log` with its `kg_per_unit`.
/// @return false with `error` for a present-and-wrong cell. A missing table,
///         row or key keeps the default named above: a hand-made table set
///         carries every load as a 20-kg sack and no log at all.
/// @note carry_kg_rick ABSENT falls back to carry_kg_adult, not to 10: a
///       table set written before the key keeps its world.
bool ReadPorterRules(const ITableSet& tables, PorterRules& rules, std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_PORTER_CATALOG_H_
