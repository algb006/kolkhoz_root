/// @file
/// @brief What went into the walls, by the unit type that took it — a row of
/// the year's book (YearLedger::built_in_by_type; boss, core-boss-c2-site-
/// supply-2026-10-09 [91], [99]).
/// @threading SINGLE_THREADED: written by construction's day close-out
/// (CompleteBuild, CompleteInsulation), read by the reports.
///
/// WHY A ROW AND NOT A COLUMN BY TYPE. `built_in` said how much clay went into
/// walls in a year and not whose walls: «where did the clay go» was answered
/// twice in October 2026 by a probe's patch on the site's prints. A dense
/// type × resource grid would carry eighty types by eighty resources into
/// every save to hold the dozen cells a year fills; a row per pair that
/// happened holds just those.

#ifndef CORE_COMMON_BUILT_IN_BY_TYPE_H_
#define CORE_COMMON_BUILT_IN_BY_TYPE_H_

#include <vector>

#include "core_common/ids.h"
#include "core_common/quantities.h"

namespace core {

/// @brief Grams of one resource gone into buildings of one unit type.
struct BuiltInRow {
  UnitTypeId type;

  ResourceId resource;

  Grams grams = 0;
};

/// @brief The year's rows, in the order their pair first took something.
using BuiltInByType = std::vector<BuiltInRow>;

/// @brief Adds grams under (type, resource), appending the pair's row the
/// first time it takes something. INVARIANT: the rows of one resource sum to
/// that resource's YearLedger::built_in — every writer of built_in calls this
/// beside it (construction_system.cpp, insulation.cpp).
/// @param type     An invalid id is a no-op, as is a non-positive amount.
/// @param resource An invalid id is a no-op.
inline void AddBuiltInByType(BuiltInByType& rows,
                             UnitTypeId type,
                             ResourceId resource,
                             Grams amount) {
  if (type.value == kInvalidDefIdValue || resource.value == kInvalidDefIdValue || amount <= 0) {
    return;
  }
  for (BuiltInRow& row : rows) {
    if (row.type == type && row.resource == resource) {
      row.grams += amount;
      return;
    }
  }
  rows.push_back(BuiltInRow{.type = type, .resource = resource, .grams = amount});
}

}  // namespace core

#endif  // CORE_COMMON_BUILT_IN_BY_TYPE_H_
