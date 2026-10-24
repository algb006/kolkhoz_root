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

#include "core_catalog/avatar_catalog.h"
#include "core_common/cargo_form.h"
#include "core_common/cart_gear.h"
#include "core_common/cold_housing.h"
#include "core_common/haul.h"
#include "core_common/logistics_state.h"
#include "core_common/site_supply_floor.h"
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
      LogisticsLevel::kTerm,        // a works' supply (transport §12, level 1)
      LogisticsLevel::kUrgent,      // the milk leg (#1056 C1.1)
      LogisticsLevel::kOrdinary,    // a rick's supply (#1056 C1.2, level 2)
      LogisticsLevel::kBackground,  // a field's winter dose (#1056 C2, level 3)
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

  /// resources.csv `hay`, and farming.csv's hay_weathering_summer, _autumn,
  /// _winter, _spring (the month's share of loose hay lost; the cells
  /// production weathers it by): the third lever of #1056 C3 orders the field
  /// heaps of a level by their nightly loss, hay's from these (ShelfDaysOf).
  ResourceId hay_resource;
  std::array<float, 4> hay_weathering_by_season{};

  /// resources.csv `manure`: what a field's winter dose is (#1056 C2).
  ResourceId manure_resource;

  /// resources.csv `spoil_days`, by ResourceId; 0 keeps for ever — the cells
  /// production reads (production_config.cpp), by the same rule.
  std::vector<float> spoil_days;

  /// 1 for a resource with feed_value above nought (resources.csv): a heap of
  /// it feeds a herd, and a hungry herd raises it to level 0.
  std::vector<std::uint8_t> feed;

  /// resources.csv `cargo_form`, by ResourceId (cargo_form.h; transport §11):
  /// kNone for every resource of a set with no such column.
  std::vector<CargoForm> cargo_form;

  /// resources.csv `is_fuel`, by ResourceId: 1 for a fuel, which rides in no
  /// body with food (CargoFormsMix).
  std::vector<std::uint8_t> is_fuel;

  /// world_params.csv `field_heap_keeping_factor` — a heap on its field keeps
  /// this share of a store's term (production_config.h).
  float field_heap_keeping_factor = 0.33F;

  /// THE PLAN'S REACH (B3): the harness pace, real km/h (transport.csv
  /// horse_trot `speed_kmh`), and the accountant's road limit, game hours
  /// (labor.csv travel_limit_hours) — the cells labour reads, by the same
  /// keys and ranges: a cart is given no load in its chain that it could not
  /// be sent to in the morning.
  float harness_speed_kmh = 12.0F;
  /// The LOADED cart's pace (transport.csv cart_loaded `speed_kmh`; #570,
  /// 2a): a trip's loaded half in the plan, and the evening rider's ride on
  /// a loaded leg.
  float loaded_speed_kmh = 9.0F;
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

  /// transport.csv `seats` of `cart_loaded`: the goods cart's bench, which
  /// the evening return seats (F3 (B); logistics_plan.h, SeatEveningRiders).
  /// 0 seats nobody, as labour reads the same cell for the morning.
  std::uint32_t cart_passenger_seats = 0;
  float cart_load_kg = 750.0F;

  /// THE CART TRIP's MODEL (#1056 C3; transport.csv `cart_loaded`
  /// `trip_model`, production's cell): 3 (CartTripModel::kPlanned in
  /// production's cart_trips.h) - the plan draws whole trips from each task's
  /// TaskTripTerms and writes its seam (trip_pricing.h). Every other value:
  /// the plan draws as before and writes no price.
  std::uint8_t cart_trip_model = 0;

  /// THE POOLED TRIP's DETOUR (#1056 C3, ruling 3; logistics.csv
  /// `pool_detour_share`, STUB econ 0.25 by her §C3 estimate - the second
  /// field's ~0.3-0.5 km against a field-to-store run of 1.6-2.1 km): a field
  /// heap's last part-load takes another field's leftover when the way first
  /// heap -> second heap -> store is at most this share longer than first heap
  /// -> store. 0 pools nothing (C3-f's «lift off» sub-run). Return condition
  /// (econ): pooled trips under 5 % of field-heap trips in the median village
  /// - the share is re-read from C3-h's printed detour distribution.
  float pool_detour_share = 0.25F;

  /// THE SLEDGE (SL; core_common/cart_gear.h): transport.csv `sledge`; the
  /// groom's plan draws a goods cart's trips at CartGearToday's paces with
  /// CartSpecsOf(config).
  SledgeSpecs sledge;

  /// What a back carries by the load's form, and the terms of logs by two
  /// (core_common/haul.h, PorterRules; core_catalog/porter_catalog.h) - the
  /// same reading labour and production hold. Until 0.37.240 a float here,
  /// labor.csv carry_kg_adult alone.
  PorterRules porter;

  /// The floor under a works' supply (#734; core_common/site_supply_floor.h;
  /// logistics.csv `site_supply_floor_*`) - the same reading labour's
  /// placement holds; here for the lamp kSiteSupplyWaits and its door.
  SupplyFloorRules supply_floor;

  /// The horse's livestock row (livestock.csv key `horse`), for the horse
  /// yard a cart's day starts at (horse_yard_road.h); invalid without one.
  LivestockKindId horse_kind;

  /// The cold-housing rule's data (core_common/cold_housing.h): a works that
  /// will house a herd facing the cold goes first among the supply.
  ColdHousing cold_housing;

  /// The avatars' rows (avatars.csv; #635): the «declare the day working»
  /// advice asks the door's own question, the avatar's closed years in it.
  std::vector<AvatarDef> avatars;
};

/// @brief The cart's and the sledge's numbers of this config (SL; cart_gear.h).
inline CartSpecs CartSpecsOf(const LogisticsConfig& config) {
  return CartSpecs{.cart_empty_kmh = config.harness_speed_kmh,
                   .cart_loaded_kmh = config.loaded_speed_kmh,
                   .cart_load_kg = config.cart_load_kg,
                   .sledge_kmh = config.sledge.kmh,
                   .sledge_load_kg = config.sledge.load_kg};
}

/// @brief Reads the config from the table set.
/// @return false with `error` naming the table and the cell; a table set with
///         no logistics.csv keeps the defaults above (StubTables decides).
bool ParseLogisticsConfig(const ITableSet& tables, LogisticsConfig& config, std::string& error);

}  // namespace core

#endif  // CORE_LOGISTICS_LOGISTICS_CONFIG_H_
