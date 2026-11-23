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
#include "core_common/world_state.h"
#include "trail_lattice.h"

namespace core {

namespace {

using road_day::DayWeights;

/// THE NETWORK ON THIS DAY: the bare index shared, a copy of the trails and the day's weights
/// held - immutable like the index, so the double buffer still copies a pointer. Every query
/// is the bare index's own under the day's costs.
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
    distance_day_ = road_day::MakeDay(*base_, &trails_, RoadBeds{}, nullptr);
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
        placed_.emplace(key,
                        road_day::WalkRouteKm(*base_, pair.from, pair.to, day_.get(), nullptr));
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
    return road_day::WalkRouteKm(*base, from, to, day, edges);
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

}  // namespace core
