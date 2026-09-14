/// @file
/// @brief When the district's regular visits come, in numbers: the month of
/// each junior's visit and how far ahead it is announced (characters design
/// §2, "Эпоха I числами"; boss, parcel 324).
/// @threading PARALLEL_READONLY
/// Parsed once at assembly on the sim thread; read-only afterwards.
///
/// WHY IN THE CATALOGUE. Production raises the visits, and the runs' chairman
/// that answers them reads the same months: one reading, not two. The rules —
/// who comes on which cause, what a visit finds — are production's
/// (core_production/district_visit.h); the catalogue holds figures and no
/// behaviour.

#ifndef CORE_CATALOG_DISTRICT_VISIT_CATALOG_H_
#define CORE_CATALOG_DISTRICT_VISIT_CATALOG_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core_tables/tables.h"

namespace core {

/// @brief The regular visits' calendar. Defaults are boss's figures of parcel
/// 324, kept for a world with no tables.
struct DistrictVisitCatalog {
  /// The month Karasev's regular visit arrives in, 1..12
  /// (`district_visit_karasev_month`): June.
  std::uint8_t karasev_month = 6;

  /// The month Polushkina's regular visit arrives in, 1..12
  /// (`district_visit_polushkina_month`): December.
  std::uint8_t polushkina_month = 12;

  /// Game days between the announcement and the arrival
  /// (`district_visit_notice_days`), 0..kDaysPerMonth: a notice longer than a
  /// month would cross the visit of the other junior.
  std::uint32_t notice_days = 2;
};

/// @brief The world_params.csv keys this catalogue reads, for the assembly's
/// declared-readers check (core_world/world.cpp).
std::span<const std::string_view> DistrictVisitWorldParamKeys();

/// @brief Reads the three knobs of world_params.csv. A missing table or key
/// keeps the default.
/// @return false with `error` naming the key for a value that is not a whole
///         number or is out of range — a month outside 1..12, a notice longer
///         than a month.
bool ParseDistrictVisitCatalog(const ITableSet& tables,
                               DistrictVisitCatalog& catalog,
                               std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_DISTRICT_VISIT_CATALOG_H_
