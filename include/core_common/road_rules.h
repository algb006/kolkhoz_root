/// @file
/// @brief The numbers of the roads' life: how a bed dries, how fast each
///        condition lets a cart go, what a trip does to the stretches it
///        crosses, and how a road left alone grows over (roads design §1, §3,
///        §4; delivery 3 of the roads work, boss core-boss-epoch1-6 [35]-[38]).
/// @threading SINGLE_THREADED
/// Read once when the simulation is assembled (core_catalog/road_rules_catalog.h);
/// plain data afterwards, read by any phase.
///
/// EVERY NUMBER HERE IS world_params.csv's, `road_` keys, and the defaults
/// below equal the export of 25 September 2026. Most are STUB with a reason
/// each (the reason and its address in the roads design live in the design
/// base's note of the row); the wear bands of speed are the unit rules' §15.

#ifndef CORE_COMMON_ROAD_RULES_H_
#define CORE_COMMON_ROAD_RULES_H_

#include <array>
#include <cstdint>

#include "core_common/road_state.h"

namespace core {

/// @brief What the weather has made of a bed today (roads design §1).
enum class RoadCondition : std::uint8_t {
  kDry = 0,  ///< Сухая: full speed.
  kWet,      ///< Сырая: rain has passed; dries in days.
  kMud,      ///< Раскисшая: the mud season (WeatherState::mud); dirt only.
  kFrozen,   ///< Замёрзшая, зимник: frost without snow; faster than dry.
  kSnow,     ///< Заснеженная: snow lies; a wheel barely goes.
  kRoadConditionCount,
};

/// @brief The beds the weather treats differently (roads design §3). A path
///        has no bed; the two asphalts are one bed.
enum class RoadBed : std::uint8_t {
  kDirt = 0,
  kGravel,
  kAsphalt,
  kRoadBedCount,
};

inline constexpr std::size_t kRoadBedCountValue = static_cast<std::size_t>(RoadBed::kRoadBedCount);

/// @brief The bed a surface is: a path and dirt are both earth.
constexpr RoadBed BedOf(RoadSurface surface) {
  switch (surface) {
    case RoadSurface::kGravel:
      return RoadBed::kGravel;
    case RoadSurface::kAsphalt:
    case RoadSurface::kAsphaltWalks:
      return RoadBed::kAsphalt;
    default:
      return RoadBed::kDirt;
  }
}

/// @brief The roads' numbers (world_params.csv, `road_` keys).
struct RoadRules {
  /// Game days a bed stays wet after the last rainy day, by RoadBed.
  std::array<float, kRoadBedCountValue> dry_days = {1.0F, 0.0F, 0.0F};

  /// Below this daily mean, °C, a wet bed takes `dry_cold_extra_days` more.
  float dry_cold_below_celsius = 5.0F;
  float dry_cold_extra_days = 1.0F;

  /// Speed multipliers of a wet bed, by RoadBed.
  std::array<float, kRoadBedCountValue> wet_factor = {0.8F, 0.95F, 1.0F};

  /// A gravel bed in the mud season; dirt's is world_params
  /// `mud_speed_factor`, read beside these (boss [36] item 2).
  float mud_factor_gravel = 0.95F;

  /// A frozen bed: faster than a dry one (roads design §1).
  float frozen_factor = 1.15F;

  /// A snowed bed, for a WHEEL. STUB (boss [36] item 3): a horse-drawn haul
  /// goes on sleighs in the snow and at the dry pace, so no Epoch I traveller
  /// meets this number yet.
  float snow_wheel_factor = 0.3F;

  /// Trip-equivalents one passage counts for, by who passes (an empty cart
  /// is one).
  float traffic_weight_walk = 0.05F;
  float traffic_weight_horse = 0.3F;
  float traffic_weight_cart = 1.0F;
  float traffic_weight_cart_loaded = 2.0F;

  /// Game days of the traffic counter's memory (an exponential average).
  float traffic_memory_days = 4.0F;

  /// Trip-equivalents a day from which a stretch reads as driven regularly,
  /// rarely, almost never; below the last — not at all.
  float traffic_regular_from = 4.0F;
  float traffic_rare_from = 0.8F;
  float traffic_almost_none_from = 0.1F;

  /// The start's counter, seeded from start_layout.csv `traffic`; `none` is 0.
  float traffic_start_regular = 8.0F;
  float traffic_start_rare = 1.6F;
  float traffic_start_almost_none = 0.2F;

  /// Wear points one trip-equivalent adds to a dirt stretch; × the wet factor
  /// on a wet or muddy day.
  float wear_pct_per_trip = 0.02F;
  float wear_wet_factor = 1.5F;

  /// Speed multipliers from wear: 51..75 % and 76..100 % (unit rules §15).
  float wear_speed_from_51 = 0.9F;
  float wear_speed_from_76 = 0.8F;

  /// A dirt stretch driven almost never or never loses this much wear a day,
  /// down to the floor (roads design §4, register 253).
  float overgrow_pct_per_day = 0.08F;
  float overgrow_floor_pct = 10.0F;

  /// A removed stretch's wear, kept by the land, fades this much a day.
  float strip_decay_pct_per_day = 0.08F;
};

/// @brief The beds' condition today, and how long each stays wet after the
///        last rain (WeatherState::road_beds). History, like the snow cover:
///        a bed is wet because it rained and has not dried, so it is carried
///        from yesterday by the weather phase and saved.
struct RoadBeds {
  std::array<RoadCondition, kRoadBedCountValue> condition{};

  /// Game days a bed will still be wet after today, by RoadBed; 0 when dry.
  std::array<float, kRoadBedCountValue> wet_days_left{};
};

/// @brief What today's weather makes of each bed (roads design §1, §3).
///
/// A rainy day wets every bed and sets how long it stays wet: `dry_days` of
/// its bed, and `dry_cold_extra_days` more below `dry_cold_below_celsius`.
/// A dry day spends one of those days. On top, in this order: snow lying
/// makes every bed snowed; the mud season makes dirt and gravel muddy (an
/// asphalt bed stays wet — §3, «на асфальте почти нет»); a mean at or below
/// freezing without snow is the frozen winter road; otherwise wet or dry.
/// @param rules The roads' numbers.
/// @param yesterday The beds as yesterday left them.
/// @param rain_today Today's precipitation is rain.
/// @param mean_celsius Today's mean air temperature.
/// @param mud_season WeatherState::mud today.
/// @param snow_lies WeatherState::snow_cover_days > 0 today.
RoadBeds RoadBedsAfter(const RoadRules& rules,
                       const RoadBeds& yesterday,
                       bool rain_today,
                       float mean_celsius,
                       bool mud_season,
                       bool snow_lies);

/// @brief The speed multiplier of a bed in a condition.
/// @param mud_speed_factor world_params.csv `mud_speed_factor` — the muddy
///        DIRT bed's, kept under its own key (boss [36] item 2).
/// @param on_runners A horse-drawn load on sleighs, or a person on foot: the
///        snowed bed's wheel factor does not apply. STUB (boss [36] item 3):
///        in the snow a horse-drawn haul goes at the dry pace — the sleigh
///        replaces the cart, and the sleigh itself is not modelled yet.
float RoadBedFactor(const RoadRules& rules,
                    float mud_speed_factor,
                    RoadCondition condition,
                    RoadBed bed,
                    bool on_runners);

}  // namespace core

#endif  // CORE_COMMON_ROAD_RULES_H_
