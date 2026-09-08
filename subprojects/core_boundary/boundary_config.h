/// @file
/// @brief Internal to core_boundary: the two balance knobs the derived
/// projections need, and their parser.
/// @threading SINGLE_THREADED
/// Factory-time code: parsed once by CreateSession, on the thread that
/// creates the session, before a single step runs. The parsed struct is
/// configuration and is read-only for the session's whole life.
///
/// The boundary itself knows no game rules — it moves rows in the order book
/// and reads the completed state (include/core_boundary/session.h). Two of the
/// signals it derives are nevertheless arithmetic over BALANCE numbers rather
/// than over the state alone: how fast a person ages, and how young an infant
/// is. That is the whole reason CreateSession takes a table set at all, and
/// the whole reason the projections are methods of a subsystem instead of free
/// functions (manual/70-boundary.md §3).
///
/// The policy is every factory's, and it has two halves since 2026-09-08: a
/// MISSING table keeps the canonical default ONLY for a caller that asked for
/// it (SessionConfig::stub_tables — a unit test's world has no tables at
/// all); under kRefused the session refuses and names the file
/// (core_tables/required_tables.h). A MISSING KEY inside a present table
/// still keeps its default. A PRESENT cell that cannot be read or falls out
/// of range refuses the session either way: a half-understood balance is
/// worse than none.

#ifndef CORE_BOUNDARY_BOUNDARY_CONFIG_H_
#define CORE_BOUNDARY_BOUNDARY_CONFIG_H_

#include <string>

#include "core_tables/stub_tables.h"

namespace core {

class ITableSet;  // core_tables/tables.h

/// The knobs, with the canonical values of tables/life.csv as defaults.
struct BoundaryConfig {
  /// How much faster biology runs than the calendar (demography design §2).
  /// Biological age = game years since birth x this.
  float life_speedup = 4.0F;

  /// Up to this BIOLOGICAL age a resident is an infant — the diapers on the
  /// line of UnitSignals::infants (life-cycle design §1: eighteen months).
  /// Kept in years because that is the unit every age comparison uses; the
  /// table states it in months, which is how the design says it.
  float infant_age_bio_years = 1.5F;

  /// Side of the square map in metres (tables/map.csv, `side_m`). ZERO when
  /// the table set declares no map — the presentation must then take the
  /// size from somewhere else rather than be handed a plausible lie.
  float map_side_m = 0.0F;
};

/// @brief Reads the knobs out of `tables`.
/// @param error Receives the reason on failure, table and key named.
/// @return false when a present cell is malformed or out of range, and —
///         under StubTables::kRefused — when a table this parse reads is
///         absent, `error` naming the file.
bool ParseBoundaryConfig(const ITableSet& tables,
                         StubTables stubs,
                         BoundaryConfig& config,
                         std::string& error);

}  // namespace core

#endif  // CORE_BOUNDARY_BOUNDARY_CONFIG_H_
