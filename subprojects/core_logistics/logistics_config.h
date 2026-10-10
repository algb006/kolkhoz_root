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

  /// resources.csv `log`: what a stand's load is (kDayOffHeldUrgentLoad names
  /// it; task 427). Invalid with no such row.
  ResourceId log_resource;

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

  /// THE PLAN'S CLOCK (B4b): the cells a leg's time is estimated from, read
  /// by labour's keys and ranges (labor_config.cpp): the hours behind a norm
  /// man-day (labor.csv standard_day_hours) — a seam of one cart-day is that
  /// many hours of one cart; the walker's pace (transport.csv pedestrian
  /// `speed_kmh`); and the two a carrier on foot's share of a cart-day is
  /// made of (core_common/haul.h, WalkerShareOfCartDay): what a man carries
  /// (labor.csv carry_kg_adult) and the cart's load (transport.csv
  /// cart_loaded `load_tonnes` x `load_scale`).
  float standard_day_hours = 10.0F;
  float walk_speed_kmh = 5.0F;
  float carry_kg_adult = 20.0F;
  float cart_load_kg = 750.0F;

  /// The horse's livestock row (livestock.csv key `horse`), for the horse
  /// yard a cart's day starts at (horse_yard_road.h); invalid without one.
  LivestockKindId horse_kind;
};

/// @brief Reads the config from the table set.
/// @return false with `error` naming the table and the cell; a table set with
///         no logistics.csv keeps the defaults above (StubTables decides).
bool ParseLogisticsConfig(const ITableSet& tables, LogisticsConfig& config, std::string& error);

}  // namespace core

#endif  // CORE_LOGISTICS_LOGISTICS_CONFIG_H_
