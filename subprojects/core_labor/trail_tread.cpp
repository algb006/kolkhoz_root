/// @file
/// @brief The morning's trodden paths (trail_tread.h; #2072).

#include "trail_tread.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>

#include "assignment.h"
#include "core_common/land_state.h"
#include "core_common/road_route.h"

namespace core {

namespace {

/// Square metres in a hectare (FieldRow::area_ga is hectares).
constexpr float kSquareMetresPerHectare = 10000.0F;

/// Rule 9: the leg's midpoint on a field with a standing crop - the field as its disc
/// (centre, area), the core keeping no outline.
bool CrossesAStandingCrop(const WorldState& world, Vec2 from, Vec2 to) {
  const Vec2 middle{.x = (from.x + to.x) / 2.0F, .y = (from.y + to.y) / 2.0F};
  for (const FieldRow& field : world.fields.rows) {
    if (field.kind != LandKind::kArable ||
        (field.phase != FieldPhase::kGrowing && field.phase != FieldPhase::kHarvest)) {
      continue;
    }
    const float radius =
        std::sqrt(field.area_ga * kSquareMetresPerHectare / std::numbers::pi_v<float>);
    const float dx = middle.x - field.center.x;
    const float dy = middle.y - field.center.y;
    if ((dx * dx) + (dy * dy) <= radius * radius) {
      return true;
    }
  }
  return false;
}

}  // namespace

void DecayTrailsThisMorning(const TrailRules& rules, WorldState& current) {
  current.trails.weight_full = rules.weight_full;
  current.trails.visible_strength = rules.visible_strength;
  current.trails.ground = rules.ground;
  DecayTrails(current.trails,
              rules,
              current.weather.snow_cover_days > 0,
              current.weather.precipitation == Precipitation::kSnow);
  // EVERY MORNING, trails or none: today's ground re-dates the index (roads.md §3); the node
  // tables are reused while the beds are unchanged.
  current.road_index = WithTrails(RoadIndexOf(current), current.trails, current.weather.road_beds);
}

void TreadPlacedWalkers(const TrailRules& rules,
                        WorldState& current,
                        std::span<const AssignmentJob> jobs,
                        std::span<const AssignmentCandidate> candidates,
                        std::span<const std::uint32_t> plan,
                        std::span<const std::uint8_t> rides_horse,
                        std::span<const std::uint32_t> rides_cart_with) {
  if (!(rules.gain_per_pass > 0.0F)) {
    return;
  }
  // The way each walks is measured on the index as it stood this morning (yesterday's trails):
  // today's tread changes tomorrow's ways, not today's.
  const std::shared_ptr<const RoadIndex> index = RoadIndexOf(current);
  const bool over_snow = current.weather.snow_cover_days > 0;
  bool trodden = false;
  for (std::size_t slot = 0; slot < candidates.size() && slot < plan.size(); ++slot) {
    const std::uint32_t job = plan[slot];
    if (job == kNoJobAssigned || job >= jobs.size()) {
      continue;
    }
    const bool on_horse = slot < rides_horse.size() && rides_horse[slot] != 0;
    const bool in_cart = slot < rides_cart_with.size() && rides_cart_with[slot] != kNoJobAssigned;
    if (on_horse || in_cart) {
      continue;  // rule 8: people on foot only
    }
    const Route way = index->Way(TravelMode::kWalk, candidates[slot].home, jobs[job].position);
    for (const RouteLeg& leg : way.legs) {
      if (leg.on_road || CrossesAStandingCrop(current, leg.from, leg.to)) {
        continue;
      }
      TreadTrail(current.trails, leg.from, leg.to, over_snow, rules.gain_per_pass);
      trodden = true;
    }
  }
  if (trodden) {
    current.road_index = WithTrails(index, current.trails, current.weather.road_beds);
  }
}

}  // namespace core
