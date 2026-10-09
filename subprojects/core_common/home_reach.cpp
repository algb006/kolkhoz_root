#include "core_common/home_reach.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/world_state.h"

namespace core {
namespace {

/// THE LIVED-IN HOUSES, FOUND ON THE NETWORK ONCE (0.37.208). Finding a
/// place on the network is the costly half of a way (road_route.h,
/// NetworkPlace), and this file's question is asked of the same houses again
/// and again: by three ways for every marked stand each morning, in two
/// lamps, at each dawn's deadline, and for every candidate stand the day a
/// chairman looks for one to mark. Asked anew each time, a twenty-year run
/// of the canon went from 4-7 minutes to 21-29 (the first build of this
/// version; measured on its own pair). The houses are kept while the network
/// and the lived-in houses are the same ones — the key below is their whole
/// content, so a kept answer is never a stale one — and the place asked
/// about is found once a call.
///
/// A PURE CACHE: the same hours as RoadKm from each house, by construction
/// (RoadIndex::EffectiveKm of two places found for one mode), held to it by
/// a unit test. One a thread, so no reader waits on another.
struct HomesOnTheNetwork {
  std::shared_ptr<const RoadIndex> index;  ///< Kept alive: its address is the key.
  std::uint64_t houses = 0;                ///< The lived-in houses' content, hashed.
  std::size_t count = 0;
  std::array<std::vector<NetworkPlace>, kTravelModeCountValue> found;
  std::array<bool, kTravelModeCountValue> filled{};
};

bool LivedIn(const UnitRow& unit) {
  // A LIVED-IN house: the brigade sets out from where people sleep, and an
  // empty house far out is nobody's road.
  return unit.level != 0 && unit.household.value != kInvalidEntityIdValue;
}

/// The lived-in houses' rows and places, as one number (FNV-1a over the row
/// index and the position's bits) and their count.
std::uint64_t HousesKey(const WorldState& world, std::size_t& count) {
  constexpr std::uint64_t kOffset = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t key = kOffset;
  count = 0;
  const auto mix = [&key](std::uint64_t value) { key = (key ^ value) * kPrime; };
  for (std::size_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (!LivedIn(unit)) {
      continue;
    }
    ++count;
    mix(row);
    mix(std::bit_cast<std::uint32_t>(unit.position.x));
    mix(std::bit_cast<std::uint32_t>(unit.position.y));
  }
  return key;
}

}  // namespace

float NearestHomeTravelHours(const WorldState& world,
                             Vec2 place,
                             float speed_kmh,
                             TravelMode mode) {
  const auto way = static_cast<std::size_t>(mode);
  if (!(speed_kmh > 0.0F) || way >= kTravelModeCountValue) {
    return -1.0F;
  }
  // The labour model's own chronometer (labor_day.cpp, HoursPerKm): real
  // km/h divided by the clock's scale, so one number means one road for the
  // assignment and for the alarms.
  const float hours_per_km = static_cast<float>(kClockScale) / speed_kmh;
  thread_local HomesOnTheNetwork kept;
  std::shared_ptr<const RoadIndex> index = RoadIndexOf(world);
  std::size_t count = 0;
  const std::uint64_t houses = HousesKey(world, count);
  if (kept.index != index || kept.houses != houses || kept.count != count) {
    kept.index = std::move(index);
    kept.houses = houses;
    kept.count = count;
    kept.filled.fill(false);
  }
  std::vector<NetworkPlace>& homes = kept.found[way];
  if (!kept.filled[way]) {
    homes.clear();
    for (const UnitRow& unit : world.units.rows) {
      if (LivedIn(unit)) {
        homes.push_back(kept.index->Locate(mode, unit.position));
      }
    }
    kept.filled[way] = true;
  }
  // BY THE WAY THERE IS, not the crow's line (roads design §11-§13; 0.36.2).
  const NetworkPlace there = kept.index->Locate(mode, place);
  float best = -1.0F;
  for (const NetworkPlace& home : homes) {
    const float hours = kept.index->EffectiveKm(home, there) * hours_per_km;
    best = best < 0.0F || hours < best ? hours : best;
  }
  return best;
}

HandReach HandReachToday(const WorldState& world, Vec2 place, const ReachRule& rule) {
  const float daylight = world.weather.daylight_hours;
  const auto leaves_a_day = [&rule, daylight](float hours) {
    return hours >= 0.0F &&
           RoadLeavesAWorkingDay(hours, daylight, rule.travel_limit_hours, rule.min_usable_hours);
  };
  if (leaves_a_day(NearestHomeTravelHours(world, place, rule.walk_speed_kmh, TravelMode::kWalk))) {
    return HandReach::kOnFoot;
  }
  // The ride of a walking work is a team's (labor_system.cpp, MeasureRoads:
  // «every other ride a team»), and it is A SEAT'S: a passenger of the
  // people's cart rides from his house. The holder of the horse goes by the
  // horse yard (horse_yard_road.h), a longer way — the placement judges him
  // by it and, where it leaves a lone rider no day, seats a second hand and
  // sends a cart (people_cart.h). Measuring the reach by the holder's way
  // was this cure's second draft: it refused every winter felling a cart
  // serves (felling hand-days of years 10–20 fell by two thirds, 123 marks
  // released in nine villages).
  if (leaves_a_day(NearestHomeTravelHours(world, place, rule.ride_speed_kmh, TravelMode::kTeam))) {
    return HandReach::kByRide;
  }
  return HandReach::kNone;
}

bool FellingCanBeMannedToday(const WorldState& world,
                             Vec2 place,
                             const ReachRule& rule,
                             std::uint32_t draught_horses) {
  const float cart_hours =
      NearestHomeTravelHours(world, place, rule.ride_speed_kmh, TravelMode::kLogCart);
  if (cart_hours < 0.0F || cart_hours > rule.travel_limit_hours) {
    return false;
  }
  const HandReach reach = HandReachToday(world, place, rule);
  return reach == HandReach::kOnFoot || (reach == HandReach::kByRide && draught_horses > 0);
}

}  // namespace core
