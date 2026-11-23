/// @file
/// @brief The trodden paths' and the ground's world_params (trail_config.h).

#include "trail_config.h"

#include <array>
#include <cstddef>
#include <vector>

#include "core_catalog/table_value.h"
#include "core_tables/tables.h"

namespace core {

namespace {

constexpr std::array<std::string_view, 16> kTrailWorldParamKeys = {
    "trail_gain_per_pass",
    "trail_earth_decay_per_day",
    "trail_snow_decay_per_day",
    "trail_forest_decay_factor",
    "trail_weight_full",
    "trail_visible_strength",
    // roads.md §3: the ground factors (GroundFactors), by GroundState from wet on.
    "walk_wet",
    "walk_mud",
    "walk_snow",
    "walk_frozen",
    "walk_ice",
    "ride_wet",
    "ride_mud",
    "ride_snow",
    "ride_frozen",
    "ride_ice"};

/// Where the walk_* and ride_* keys begin: each runs GroundState kWet .. kIce.
constexpr std::size_t kWalkKeyFirst = 6;
constexpr std::size_t kRideKeyFirst = kWalkKeyFirst + (kGroundStateCountValue - 1);
static_assert(kRideKeyFirst + (kGroundStateCountValue - 1) == kTrailWorldParamKeys.size(),
              "one walk_ and one ride_ key per ground state from wet on");

}  // namespace

std::span<const std::string_view> TrailWorldParamKeys() {
  return kTrailWorldParamKeys;
}

std::span<const std::string_view> WithTrailWorldParamKeys(std::span<const std::string_view> labor) {
  static const std::vector<std::string_view> kAll = [labor] {
    std::vector<std::string_view> all(labor.begin(), labor.end());
    all.insert(all.end(), kTrailWorldParamKeys.begin(), kTrailWorldParamKeys.end());
    return all;
  }();
  return kAll;
}

bool ParseTrailRules(const ITable& world, TrailRules& rules, std::string& error) {
  const Range share{.low = 0.0F, .high = 1.0F};
  if (!OptionalValue(world, kTrailWorldParamKeys[0], share, rules.gain_per_pass, error) ||
      !OptionalValue(world, kTrailWorldParamKeys[1], share, rules.earth_decay_per_day, error) ||
      !OptionalValue(world, kTrailWorldParamKeys[2], share, rules.snow_decay_per_day, error) ||
      !OptionalValue(world,
                     kTrailWorldParamKeys[3],
                     Range{.low = 0.0F, .high = 10.0F},
                     rules.forest_decay_factor,
                     error) ||
      !OptionalValue(world,
                     kTrailWorldParamKeys[4],
                     Range{.low = 1.001F, .high = 10.0F},
                     rules.weight_full,
                     error) ||
      !OptionalValue(world, kTrailWorldParamKeys[5], share, rules.visible_strength, error)) {
    PrefixError("world_params", "trail_*", error);
    return false;
  }
  // A factor in [0.05, 2]: above 1 is faster than dry ground (none is today), 0 would stop
  // the way.
  const Range factor{.low = 0.05F, .high = 2.0F};
  for (std::size_t state = 1; state < kGroundStateCountValue; ++state) {
    if (!OptionalValue(world,
                       kTrailWorldParamKeys[kWalkKeyFirst + state - 1],
                       factor,
                       rules.ground.walk[state],
                       error) ||
        !OptionalValue(world,
                       kTrailWorldParamKeys[kRideKeyFirst + state - 1],
                       factor,
                       rules.ground.ride[state],
                       error)) {
      PrefixError("world_params", "walk_* / ride_*", error);
      return false;
    }
  }
  return true;
}

}  // namespace core
