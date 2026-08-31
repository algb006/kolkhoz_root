/// @file
/// @brief core_report boundary: the yearly run ledger as CSV — the sheet the
/// core's numbers are reconciled against the balance calculations on
/// (stage 7, task F2; manual/68-run-ledger.md).
/// @threading SINGLE_THREADED
/// Pure functions of a completed state and the table set, called between
/// steps by whoever drives the simulation — the thirty-year run, later the
/// ImGui application. No phase code, no state of its own, no I/O: the
/// caller owns the file. Reads WorldState::ledger.closed and the state's
/// own tables; never touches the simulation.
///
/// ONE ROW PER YEAR, one column per number, the header derived from the
/// tables. The reconciliation (manual/balance/49-simulations.md) speaks in
/// tonnes per resource and heads per kind, so a fixed scalar vocabulary is
/// not enough: the per-resource and per-kind columns are generated from
/// the live resources and livestock tables, one column per table row, in
/// table order, named `<block>_<key>[_unit]`. Two runs over the same tables
/// produce the same header; a run over different tables produces a
/// different one, and a sheet that mixes them is caught by the header,
/// not by a silent misalignment. The dialect is the balance tables' own
/// (manual/61-balance-tables.md §2): comma, decimal point, LF, no quoting
/// needed because keys are snake_case and numbers are numbers.
///
/// WHEN A ROW IS TAKEN. The ledger closes its book at the events slot of
/// the FIRST tick of the next calendar year (ledger_state.h). The driver
/// watches `ledger.closed.year` advance and writes the row then; the
/// state columns therefore describe the world one tick into the new year —
/// the stores after the plan shipped, the people after the turn's
/// bookkeeping. That is the year-end the calculations mean by "at the end
/// of the year", and it is the same instant for every run.
///
/// COLUMN VOCABULARY, in this fixed order:
///
///   year                              ledger.closed.year
///   -- state at the turn --
///   population, families, epoch, life_expectancy_years,
///   fertility_mean                    AREA-WEIGHTED over every field, which
///                                     is the mean the balance calculations
///                                     use ("плодородие по участкам —
///                                     считается среднее"); 0 with no land
///   -- people flows --
///   births, deaths, arrivals, departures, weddings
///   -- satiety --
///   satiety_mean                      day-mean sum / days
///   satiety_day_min, hungry_at_once_max
///   -- land --
///   area_sown_ha, area_harvested_ha, area_lost_ha, area_manured_ha,
///   manure_plowed_in_kg
///   -- herds, totals --
///   herd_births, herd_deaths_age, herd_deaths_hunger, herd_culled,
///   herd_hungry_head_days
///   -- labor --
///   work_<kind>_days                  one per WorkKind except kNone, in
///                                     enum order: plowing, harrowing,
///                                     sowing, harvest, herd_care
///   trudodni_accrued, trudodni_burned  in trudodni (hundredths / 100)
///   walk_offs
///   -- per resource, kilograms; one column per row of resources.csv --
///   harvest_<key>_kg, seed_<key>_kg, delivered_<key>_kg,
///   herd_produce_<key>_kg, feed_<key>_kg,
///   issued_<key>_kg, ration_<key>_kg, nets_<key>_kg,
///   yard_produce_<key>_kg, plot_harvest_<key>_kg, eaten_<key>_kg,
///   store_<key>_kg                    sum of every unit's stock now
///   pantry_<key>_kg                   sum of every family's pantry now
///   plan_due_<key>_kg                 plan.due now (the NEW year's plan)
///   -- per livestock kind; one group per row of livestock.csv --
///   herd_<key>_adult, herd_<key>_male, herd_<key>_juvenile,
///   herd_<key>_newborn, herd_<key>_billeted
///                                     kolkhoz-owned herds only; the
///                                     yards' own animals are
///                                     yard_<key>_head, all rungs summed
///
/// Numbers are printed so that they read back exactly: integers as
/// integers, kilograms as grams / 1000 with three decimals, floats with
/// enough digits to round-trip (std::to_chars shortest form). A missing
/// table (no `livestock`, no `resources`) leaves its group out of the
/// header and the row alike, in both places — the two functions always
/// agree on the column count.
///
/// They agree BY CONSTRUCTION, not by discipline: both are one pass over
/// one list of column sites, run in name mode or in value mode. A column
/// cannot be added to one and forgotten in the other, because there is only
/// one of it.
///
/// The sheet is wide — a dozen blocks over the whole resource registry, and
/// the registry carries the later epochs' goods too, so many columns are
/// structurally zero in phase 1. That is deliberate: a column set that
/// depended on which resources happened to move would give two runs two
/// different headers, and the sheet is read by a spreadsheet, not by eye.

#ifndef CORE_REPORT_LEDGER_CSV_H_
#define CORE_REPORT_LEDGER_CSV_H_

#include <string>

#include "core_common/world_state.h"

namespace core {

class ITableSet;  // core_tables/tables.h — the live definition tables.

/// @brief The header line of the ledger sheet, LF-terminated.
/// A pure function of the tables: the fixed vocabulary above with the
/// per-resource and per-kind groups expanded in table order.
std::string LedgerCsvHeader(const ITableSet& tables);

/// @brief One row of the ledger sheet, LF-terminated, for the year in
/// `state.ledger.closed`; the state columns are taken from `state` as it
/// is. Column order and count match LedgerCsvHeader over the same tables.
/// @pre state.ledger.closed.year > 0 — a book was closed. Calling it on a
/// world whose first year has not ended is a driver error (asserted in
/// Debug; an all-zero flows row otherwise).
std::string LedgerCsvRow(const WorldState& state, const ITableSet& tables);

}  // namespace core

#endif  // CORE_REPORT_LEDGER_CSV_H_
