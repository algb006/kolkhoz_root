/// @file
/// @brief The road index on a day (road_day.h; #2072's trails, roads.md §3's ground): the day
///        index that wraps the bare network, and the world-level doors that read it -
///        WithTrails, the day's BuildRoadIndex, GroundStateAt, WalkRouteKm, ReachMeasure.

#include "road_day.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core_common/road_rules.h"
#include "core_common/time_probe.h"
#include "core_common/world_state.h"
#include "trail_lattice.h"

namespace core {

namespace {

using road_day::DayWeights;

/// THE NETWORK ON THIS DAY: the bare index shared, a copy of the trails and the day's weights
/// held - immutable like the index, so the double buffer still copies a pointer. Every query
/// is the bare index's own under the day's costs.
float WalkOnTheDay(const RoadIndex& base,
                   Vec2 from,
                   Vec2 to,
                   const DayWeights* day,
                   std::vector<std::pair<TrailEnd, TrailEnd>>* edges);

class DayRoadIndex final : public RoadIndex {
 public:
  DayRoadIndex(std::shared_ptr<const RoadIndex> base,
               TrailTable trails,
               const RoadBeds& beds,
               const DayWeights* previous)
      : base_(std::move(base)), trails_(std::move(trails)) {
    day_ = road_day::MakeDay(*base_, &trails_, beds, previous);  // after trails_ is in place
    // THE DISTANCE, NOT THE TIME (boss 23 Nov; leisure.md §4: reach is metres of the way):
    // the same trails on dry ground - the ground's factors slow a walk, they do not move a
    // place farther. Neutral, so it computes no node table.
    distance_day_ = road_day::MakeDay(*base_, &trails_, RoadBeds{}, nullptr, day_->lookup);
  }

  float EffectiveKm(TravelMode mode, Vec2 from, Vec2 to) const override {
    return road_day::EffectiveKm(*base_, mode, from, to, day_.get());
  }

  NetworkPlace Locate(TravelMode mode, Vec2 position) const override {
    return base_->Locate(mode, position);
  }

  float EffectiveKm(const NetworkPlace& from, const NetworkPlace& to) const override {
    return road_day::EffectiveKm(*base_, from, to, day_.get());
  }

  RouteMeasure Measure(TravelMode mode, Vec2 from, Vec2 to) const override {
    return road_day::Measure(*base_, mode, from, to, day_.get());
  }

  Route Way(TravelMode mode, Vec2 from, Vec2 to) const override {
    return road_day::Way(*base_, mode, from, to, day_.get());
  }

  const RoadGraph& Graph() const override { return base_->Graph(); }

  float OffRoadWeight(TravelMode mode) const override { return base_->OffRoadWeight(mode); }

  const RoadIndex* Network() const override { return base_.get(); }

  const std::shared_ptr<const RoadIndex>& Base() const { return base_; }

  const DayWeights* Day() const { return day_.get(); }

  const DayWeights* DistanceDay() const { return distance_day_.get(); }

  /// Yesterday's walks priced on this day by the walker's route (option 1, 24 Nov). Called
  /// once, by WithTrails, before the index is shared.
  void PriceWalks(std::span<const WalkPair> walked) {
    for (const WalkPair& pair : walked) {
      const PairKey key = KeyOf(pair.from, pair.to);
      if (!placed_.contains(key)) {
        placed_.emplace(key, WalkOnTheDay(*base_, pair.from, pair.to, day_.get(), nullptr));
      }
    }
  }

  const float* PlacedKm(Vec2 from, Vec2 to) const {
    const auto found = placed_.find(KeyOf(from, to));
    return found != placed_.end() ? &found->second : nullptr;
  }

 private:
  /// A walk by its two positions' exact bits: the planner asks with the very floats the
  /// morning priced (homes and work places are fixed positions).
  using PairKey = std::array<std::uint32_t, 4>;

  struct PairHash {
    std::size_t operator()(const PairKey& key) const {
      std::uint64_t hash = 1469598103934665603ULL;
      for (const std::uint32_t word : key) {
        hash = (hash ^ word) * 1099511628211ULL;
      }
      return static_cast<std::size_t>(hash);
    }
  };

  static PairKey KeyOf(Vec2 from, Vec2 to) {
    return {std::bit_cast<std::uint32_t>(from.x),
            std::bit_cast<std::uint32_t>(from.y),
            std::bit_cast<std::uint32_t>(to.x),
            std::bit_cast<std::uint32_t>(to.y)};
  }

  std::shared_ptr<const RoadIndex> base_;
  TrailTable trails_;
  std::shared_ptr<const DayWeights> day_;
  std::shared_ptr<const DayWeights> distance_day_;
  std::unordered_map<PairKey, float, PairHash> placed_;
};

/// This thread's asks of PlacedWalkKm (the run's print; per thread, so no shared counter).
thread_local PlacedWalkCount g_placed_walk_count;

/// A whole walk's answer: its km and its lattice steps, in order.
struct WalkAnswer {
  float km = 0.0F;
  std::vector<std::pair<TrailEnd, TrailEnd>> steps;
};

/// Walks kept a thread before the memo starts over (forgetting only re-prices).
constexpr std::size_t kWalkMemoCap = std::size_t{1} << 16U;

/// THE WHOLE WALK, ONCE A DAY A THREAD (#2390 item 2, the second Fable review; exact): the
/// walker's door ran the way's choice (accesses, Choose, legs) before any leg's memo, for every
/// asker of the same pair - the morning's pricing, the timing door, the tread. Keyed by the
/// day's process-unique serial and the ends' bits; a neutral day's (serial 0) is not kept.
float WalkOnTheDay(const RoadIndex& base,
                   Vec2 from,
                   Vec2 to,
                   const DayWeights* day,
                   std::vector<std::pair<TrailEnd, TrailEnd>>* edges) {
  if (day == nullptr || day->serial == 0) {
    return road_day::WalkRouteKm(base, from, to, day, edges);
  }
  using WalkKey = std::array<std::uint32_t, 4>;

  struct WalkKeyHash {
    std::size_t operator()(const WalkKey& key) const {
      std::uint64_t hash = 1469598103934665603ULL;
      for (const std::uint32_t word : key) {
        hash = (hash ^ word) * 1099511628211ULL;
      }
      return static_cast<std::size_t>(hash);
    }
  };

  thread_local std::uint64_t memo_serial = 0;
  thread_local std::unordered_map<WalkKey, WalkAnswer, WalkKeyHash> memo;
  if (memo_serial != day->serial || memo.size() >= kWalkMemoCap) {
    memo.clear();
    memo_serial = day->serial;
  }
  const WalkKey key = {std::bit_cast<std::uint32_t>(from.x),
                       std::bit_cast<std::uint32_t>(from.y),
                       std::bit_cast<std::uint32_t>(to.x),
                       std::bit_cast<std::uint32_t>(to.y)};
  auto found = memo.find(key);
  if (found == memo.end()) {
    WalkAnswer answer;
    answer.km = road_day::WalkRouteKm(base, from, to, day, &answer.steps);
    found = memo.emplace(key, std::move(answer)).first;
  }
  if (edges != nullptr) {
    edges->insert(edges->end(), found->second.steps.begin(), found->second.steps.end());
  }
  return found->second.km;
}

/// The bare network under any index of ours, and the day it carried (or nullptr).
std::shared_ptr<const RoadIndex> BareOf(const std::shared_ptr<const RoadIndex>& index,
                                        const DayWeights** day) {
  *day = nullptr;
  if (index != nullptr && road_day::IsBare(*index)) {
    return index;
  }
  if (const auto dated = std::dynamic_pointer_cast<const DayRoadIndex>(index)) {
    *day = dated->Day();
    return dated->Base();
  }
  return nullptr;
}

/// True when the ground touches nobody today: every bed and open ground at factor 1.
bool GroundIsNeutral(const GroundFactors& factors, const RoadBeds& beds) {
  for (std::size_t bed = 0; bed < kRoadBedCountValue; ++bed) {
    const GroundState on_road =
        lattice::GroundOnRoad(beds.condition[bed], static_cast<RoadBed>(bed));
    for (const TravelMode mode : {TravelMode::kWalk, TravelMode::kTeam}) {
      if (lattice::GroundFactorOf(factors, mode, on_road) != 1.0F) {
        return false;
      }
    }
  }
  const GroundState open =
      lattice::GroundOffRoad(beds.condition[static_cast<std::size_t>(RoadBed::kDirt)]);
  return lattice::GroundFactorOf(factors, TravelMode::kWalk, open) == 1.0F &&
         lattice::GroundFactorOf(factors, TravelMode::kTeam, open) == 1.0F;
}

}  // namespace

std::shared_ptr<const RoadIndex> WithTrails(const std::shared_ptr<const RoadIndex>& index,
                                            const TrailTable& trails,
                                            const RoadBeds& beds,
                                            std::span<const WalkPair> walked) {
  const probe::Timer probe_timer(probe::Slot::kWithTrails);  // PROBE #2118
  const DayWeights* previous = nullptr;
  std::shared_ptr<const RoadIndex> base = BareOf(index, &previous);
  if (base == nullptr) {
    return index;  // a foreign index (a test's): nothing to wrap
  }
  if (trails.rows.empty() && GroundIsNeutral(trails.ground, beds)) {
    // No trail, no ground to slow anyone: the bare network - and no walk to price, its
    // route IS the straight door's answer (the normalisation).
    return base;
  }
  auto dated = std::make_shared<DayRoadIndex>(std::move(base), trails, beds, previous);
  dated->PriceWalks(walked);
  return dated;
}

std::shared_ptr<const RoadIndex> WithTrails(const std::shared_ptr<const RoadIndex>& index,
                                            const TrailTable& trails,
                                            const RoadBeds& beds) {
  return WithTrails(index, trails, beds, {});
}

std::shared_ptr<const RoadIndex> BuildRoadIndex(const RoadTable& roads,
                                                const TrailTable& trails,
                                                const RoadBeds& beds,
                                                std::span<const WalkPair> walked) {
  return WithTrails(BuildRoadIndex(roads), trails, beds, walked);
}

std::shared_ptr<const RoadIndex> BuildRoadIndex(const RoadTable& roads,
                                                const TrailTable& trails,
                                                const RoadBeds& beds) {
  return WithTrails(BuildRoadIndex(roads), trails, beds, {});
}

const float* PlacedWalkKm(const RoadIndex& index, Vec2 from, Vec2 to) {
  ++g_placed_walk_count.asked;
  const auto* const dated = dynamic_cast<const DayRoadIndex*>(&index);
  const float* const km = dated != nullptr ? dated->PlacedKm(from, to) : nullptr;
  g_placed_walk_count.found += km != nullptr ? 1U : 0U;
  return km;
}

PlacedWalkCount TakePlacedWalkCount() {
  const PlacedWalkCount taken = g_placed_walk_count;
  g_placed_walk_count = PlacedWalkCount{};
  return taken;
}

NetworkPlace LocateOnNetwork(const std::shared_ptr<const RoadIndex>& index,
                             TravelMode mode,
                             Vec2 position) {
  using LocateKey = std::array<std::uint32_t, 3>;

  struct LocateKeyHash {
    std::size_t operator()(const LocateKey& key) const {
      std::uint64_t hash = 1469598103934665603ULL;  // FNV-1a
      for (const std::uint32_t word : key) {
        hash = (hash ^ word) * 1099511628211ULL;
      }
      return static_cast<std::size_t>(hash);
    }
  };

  struct Kept {
    const RoadIndex* network = nullptr;
    std::shared_ptr<const RoadIndex> owner;  // keeps `network` alive while it is the key
    std::unordered_map<LocateKey, NetworkPlace, LocateKeyHash> places;
  };

  // A cap far above a village's homes x modes; past it the memo starts over (forgetting only
  // finds again).
  constexpr std::size_t kKeptPlaces = std::size_t{1} << 16U;
  thread_local Kept kept;
  const RoadIndex* const network = index->Network();
  if (kept.network != network || kept.places.size() >= kKeptPlaces) {
    kept.network = network;
    kept.owner = index;
    kept.places.clear();
  }
  const LocateKey key{static_cast<std::uint32_t>(mode),
                      std::bit_cast<std::uint32_t>(position.x),
                      std::bit_cast<std::uint32_t>(position.y)};
  const auto found = kept.places.find(key);
  if (found != kept.places.end()) {
    return found->second;
  }
  const NetworkPlace place = index->Locate(mode, position);
  kept.places.emplace(key, place);
  return place;
}

GroundState GroundStateAt(const WorldState& world, Vec2 point) {
  const DayWeights* day = nullptr;
  const std::shared_ptr<const RoadIndex> base = BareOf(RoadIndexOf(world), &day);
  if (base == nullptr) {
    return GroundState::kDry;
  }
  return road_day::GroundAt(*base, point, world.weather.road_beds);
}

float WalkRouteKm(const WorldState& world,
                  Vec2 from,
                  Vec2 to,
                  std::vector<std::pair<TrailEnd, TrailEnd>>* edges) {
  const DayWeights* day = nullptr;
  const std::shared_ptr<const RoadIndex> base = BareOf(RoadIndexOf(world), &day);
  if (base == nullptr) {
    return RoadKm(world, TravelMode::kWalk, from, to);
  }
  if (day != nullptr) {
    return WalkOnTheDay(*base, from, to, day, edges);
  }
  // A bare index (no trail in effect this morning, a neutral ground): the lattice still gives
  // the walker his steps, so the first trail can begin; its km is the straight line's by the
  // normalisation. NOT world.trails: today's tread lands there during the day, and the day's
  // costs are frozen at the morning's index (static review of 23 Nov 2026).
  static const TrailTable kNoTrails;
  DayWeights neutral;
  neutral.trails = &kNoTrails;
  return road_day::WalkRouteKm(*base, from, to, &neutral, edges);
}

RouteMeasure ReachMeasure(const WorldState& world, TravelMode mode, Vec2 from, Vec2 to) {
  const std::shared_ptr<const RoadIndex> index = RoadIndexOf(world);
  if (const auto dated = std::dynamic_pointer_cast<const DayRoadIndex>(index)) {
    return road_day::Measure(*dated->Base(), mode, from, to, dated->DistanceDay());
  }
  return index->Measure(mode, from, to);
}

float ReachKm(const WorldState& world, TravelMode mode, Vec2 from, Vec2 to) {
  return ReachMeasure(world, mode, from, to).effective_km;
}

std::uint64_t DayTableBuilds() {
  return road_day::TableBuilds();
}

// THE WORLD-LEVEL DOORS (moved from road_route.cpp, at its line limit).
std::shared_ptr<const RoadIndex> RoadIndexOf(const WorldState& world) {
  if (world.road_index != nullptr) {
    return world.road_index;
  }
  return BuildRoadIndex(world.roads);
}

float RoadKm(const WorldState& world, TravelMode mode, Vec2 from, Vec2 to) {
  return RoadIndexOf(world)->EffectiveKm(mode, from, to);
}

RouteMeasure RoadMeasure(const WorldState& world, TravelMode mode, Vec2 from, Vec2 to) {
  return RoadIndexOf(world)->Measure(mode, from, to);
}

float OffRoadWeightOf(const WorldState& world, TravelMode mode) {
  return RoadIndexOf(world)->OffRoadWeight(mode);
}

Vec2 DistrictExitPoint(const WorldState& world) {
  const std::shared_ptr<const RoadIndex> index = RoadIndexOf(world);
  bool found = false;
  Vec2 exit{};
  for (const RoadNode& node : index->Graph().nodes) {
    if (node.border && (!found || node.position.y > exit.y)) {
      exit = node.position;
      found = true;
    }
  }
  if (found) {
    return exit;
  }
  return world.units.rows.empty() ? Vec2{} : world.units.rows.front().position;
}

}  // namespace core
