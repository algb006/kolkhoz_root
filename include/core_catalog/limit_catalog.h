/// @file
/// @brief The district's limit catalogue: the lots of tables/limit_catalog.csv,
/// what each brings (tables/limit_lot_goods.csv), and the year's points
/// knobs out of world_params.csv (district design §1, §4; boss, parcels 208,
/// 211).
/// @threading PARALLEL_READONLY
/// Parsed once at assembly on the sim thread; read-only afterwards.
///
/// WHY IN THE CATALOGUE. Production consumes the order and grants the year's
/// points, and the runs' chairman reads the same prices to decide what to
/// buy: one reading, not two. The RULES — what may be ordered, the year's
/// grant — are production's (core_production/district_limit.h); the
/// catalogue holds definitions and no behaviour.

#ifndef CORE_CATALOG_LIMIT_CATALOG_H_
#define CORE_CATALOG_LIMIT_CATALOG_H_

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_tables/tables.h"

namespace core {

/// @brief What a lot is (limit_catalog.csv `kind`). Only goods are bought in
/// the core today; the others have their own windows (district design §1).
enum class LimitLotKind : std::uint8_t {
  kGoods = 0,
  kLivestock,
  kVehicle,
  kPerson,
  kChoice,
};

/// @brief The farm's status tier (core loop §3), which sets the base grant.
/// STUB: the core has no economic readiness index yet, so every farm is
/// kLagging (boss, parcel 211).
enum class FarmStatusTier : std::uint8_t {
  kLagging = 0,
  kAverage,
  kStrong,
  kLeading,
};

/// @brief One row of limit_catalog.csv with its goods.
struct LimitLotDef {
  /// Price in points; negative when the row names none (the lot is not
  /// ordered until it has one).
  std::int32_t points = -1;

  /// First epoch the lot is open in, 1..3.
  std::uint8_t era = 1;

  LimitLotKind kind = LimitLotKind::kGoods;

  /// Grams of each resource one lot brings, by ResourceId. A resource whose
  /// amount the table leaves empty brings nothing yet (boss, parcel 211);
  /// a lot all of whose amounts are empty is not ordered.
  ResourceAmounts goods;
};

/// @brief The whole catalogue and the year's points knobs. Defaults are the
/// design's figures, kept for a world with no tables.
struct LimitCatalog {
  /// Every lot, in table row order: LimitLotId indexes it.
  std::vector<LimitLotDef> lots;

  /// Base grant by FarmStatusTier: 350 / 320 / 285 / 250 — inversely to
  /// success (district design §1).
  std::array<std::int32_t, 4> base_points = {350, 320, 285, 250};

  /// For a plan delivered in full, 100 % of every position.
  std::int32_t plan_met_points = 150;

  /// Per percent over the plan, and the most that term gives.
  std::int32_t overfulfil_points_per_percent = 10;
  std::int32_t overfulfil_points_max = 200;

  /// Days the district's cart takes, and the most it may be late by.
  std::uint32_t delivery_days = 2;
  std::uint32_t delivery_delay_days_max = 2;
};

/// @brief The world_params.csv keys this catalogue reads, for the assembly's
/// declared-readers check (core_world/world.cpp).
std::span<const std::string_view> LimitWorldParamKeys();

/// @brief Reads the catalogue. Missing tables keep the defaults and an empty
/// lot list; a present one that cannot be understood refuses — an unknown
/// kind, a lot of goods naming a resource or lot the tables do not carry, a
/// non-numeric or non-positive amount.
/// @return false with `error` naming the table, row and column.
bool ParseLimitCatalog(const ITableSet& tables, LimitCatalog& catalog, std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_LIMIT_CATALOG_H_
