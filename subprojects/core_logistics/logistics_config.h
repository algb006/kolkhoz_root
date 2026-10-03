/// @file
/// @brief LogisticsConfig — the groom's logistics' numbers: the default level
///        of a load's task by its kind, the ageing, the self-raise to level 0
///        by a spoiling load (tables/logistics.csv; routing stage B, B2), and
///        what spoils and what is feed (resources.csv, world_params.csv —
///        the same cells production reads, by the same keys).
/// @threading SINGLE_THREADED
/// Built once at load; read by the logistics sub-step in the decisions phase.

#ifndef CORE_LOGISTICS_LOGISTICS_CONFIG_H_
#define CORE_LOGISTICS_LOGISTICS_CONFIG_H_

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core_common/logistics_state.h"
#include "core_tables/tables.h"

namespace core {

inline constexpr std::uint32_t kLogisticsLoadKindCount =
    static_cast<std::uint32_t>(LogisticsLoadKind::kLogisticsLoadKindCount);

struct LogisticsConfig {
  /// The level a task of each load kind is made at (logistics.csv `level_*`;
  /// boss, the logistics thread [9], default 1).
  std::array<LogisticsLevel, kLogisticsLoadKindCount> default_level = {
      LogisticsLevel::kTerm,        // a field's heap
      LogisticsLevel::kOrdinary,    // a stand's logs
      LogisticsLevel::kOrdinary,    // a dig's load
      LogisticsLevel::kTerm,        // the district's lot
      LogisticsLevel::kBackground,  // a store's transfer
  };

  /// Days from the last trip after which a background task is ordinary, and
  /// an ordinary one is due by a date (logistics.csv; econ [5], STUB econ).
  float age_background_days = 6.0F;
  float age_ordinary_days = 4.0F;

  /// A heap to lose at least this share of itself, or this many tonnes, by
  /// tomorrow's spoilage is urgent by itself (logistics.csv; econ [5]).
  float urgent_spoil_share = 0.10F;
  float urgent_spoil_tonnes = 1.0F;

  /// resources.csv `spoil_days`, by ResourceId; 0 keeps for ever — the cells
  /// production reads (production_config.cpp), by the same rule.
  std::vector<float> spoil_days;

  /// 1 for a resource with feed_value above nought (resources.csv): a heap of
  /// it feeds a herd, and a hungry herd raises it to level 0.
  std::vector<std::uint8_t> feed;

  /// world_params.csv `field_heap_keeping_factor` — a heap on its field keeps
  /// this share of a store's term (production_config.h).
  float field_heap_keeping_factor = 0.33F;

  /// THE PLAN'S REACH (B3): the harness pace, real km/h (transport.csv
  /// horse_trot `speed_kmh`), and the accountant's road limit, game hours
  /// (labor.csv travel_limit_hours) — the cells labour reads, by the same
  /// keys and ranges: a cart is given no load in its chain that it could not
  /// be sent to in the morning.
  float harness_speed_kmh = 12.0F;
  float travel_limit_hours = 6.0F;
};

/// @brief Reads the config from the table set.
/// @return false with `error` naming the table and the cell; a table set with
///         no logistics.csv keeps the defaults above (StubTables decides).
bool ParseLogisticsConfig(const ITableSet& tables, LogisticsConfig& config, std::string& error);

}  // namespace core

#endif  // CORE_LOGISTICS_LOGISTICS_CONFIG_H_
