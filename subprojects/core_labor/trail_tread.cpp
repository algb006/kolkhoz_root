/// @file
/// @brief The morning's trodden paths (trail_tread.h; #2072).

#include "trail_tread.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <tuple>
#include <utility>
#include <vector>

#include "assignment.h"
#include "core_common/horse_yard_road.h"
#include "core_common/land_state.h"
#include "core_common/road_route.h"
#include "core_common/work_seam.h"

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
  // YESTERDAY'S TREAD LANDS NOW (save 229): trodden as it was, then decayed as the morning
  // always decayed it - the same trails as treading it yesterday would have left.
  for (const TrailPass& pass : current.walks.pending) {
    for (std::uint32_t at = 0; at < pass.passes; ++at) {
      TreadTrailEdge(
          current.trails, pass.a, pass.b, current.walks.pending_over_snow, rules.gain_per_pass);
    }
  }
  current.walks.pending.clear();
  current.walks.pending_over_snow = false;
  current.trails.weight_full = rules.weight_full;
  current.trails.visible_strength = rules.visible_strength;
  current.trails.ground = rules.ground;
  DecayTrails(current.trails,
              rules,
              current.weather.snow_cover_days > 0,
              current.weather.precipitation == Precipitation::kSnow);
  current.walks.yesterday = std::move(current.walks.today);
  current.walks.today.clear();
  // EVERY MORNING, trails or none: today's ground re-dates the index (roads.md §3), and
  // yesterday's walks are priced on it by the walker's route (option 1, 24 Nov); the node
  // tables are reused while the beds are unchanged.
  current.road_index = WithTrails(
      RoadIndexOf(current), current.trails, current.weather.road_beds, current.walks.yesterday);
}

std::vector<WalkPair> PlacedWalksOf(const WorldState& current, LivestockKindId horse_kind) {
  std::vector<WalkPair> walks;
  for (const ResidentRow& person : current.residents.rows) {
    const WorkAssignment& work = person.work;
    Vec2 home{};
    if (work.kind == WorkKind::kNone || !HomePositionOf(current, person.family, home)) {
      continue;
    }
    // WHO WALKS, BY THE WORLD'S OWN PREDICATES - the timing door's and the tread's.
    if (!WorkRidesOut(current, work)) {
      Vec2 place{};
      if (WorkPlaceOf(current, work, place)) {
        walks.push_back(WalkPair{.from = home, .to = place});
      }
      continue;
    }
    Vec2 yard{};
    if (DayStartsAtHorseYard(current, work) && HorseYardPositionOf(current, horse_kind, yard)) {
      walks.push_back(WalkPair{.from = home, .to = yard});
    }
  }
  return walks;
}

void TreadPlacedWalkers(const TrailRules& rules,
                        WorldState& current,
                        std::span<const AssignmentCandidate> candidates,
                        std::span<const std::uint32_t> plan,
                        LivestockKindId horse_kind) {
  if (!(rules.gain_per_pass > 0.0F)) {
    return;
  }
  const bool over_snow = current.weather.snow_cover_days > 0;
  std::vector<std::pair<TrailEnd, TrailEnd>> steps;
  for (std::size_t slot = 0; slot < candidates.size() && slot < plan.size(); ++slot) {
    if (plan[slot] == kNoJobAssigned) {
      continue;
    }
    const ResidentRow& person = current.residents.rows[candidates[slot].resident_row];
    const WorkAssignment& work = person.work;
    const Vec2 home = candidates[slot].home;
    // WHO WALKS, BY THE WORLD'S OWN PREDICATES (static review of 23 Nov 2026): the timing door
    // (WorkRoadHours) walks him when WorkRidesOut is false, and walks him to the horse yard
    // when his day starts there - so he treads exactly those walks (rule 8 for the rest: the
    // brigade cart's riders, the people's cart, the horsemen ride).
    if (!WorkRidesOut(current, work)) {
      Vec2 place{};
      if (WorkPlaceOf(current, work, place)) {
        // His lattice route, on the index as it stood this morning; trodden only after every
        // route is taken, and the index is not re-dated today (the day's costs are frozen).
        WalkRouteKm(current, home, place, &steps);
      }
      continue;
    }
    Vec2 yard{};
    if (DayStartsAtHorseYard(current, work) && HorseYardPositionOf(current, horse_kind, yard)) {
      WalkRouteKm(current, home, yard, &steps);
    }
  }
  // PENDING TILL TOMORROW'S MORNING (save 229): the trails stay the morning's all day, so an
  // index rebuilt mid-day - the loader's, a laid road's - is the morning's.
  std::vector<TrailPass>& pending = current.walks.pending;
  current.walks.pending_over_snow = over_snow;
  const auto key = [](const TrailPass& pass) {
    return std::tie(pass.a.x, pass.a.y, pass.b.x, pass.b.y);
  };
  for (auto [a, b] : steps) {
    const Vec2 from = TrailEndPosition(a);
    const Vec2 to = TrailEndPosition(b);
    if (CrossesAStandingCrop(current, from, to)) {
      continue;  // rule 9
    }
    if (std::tie(b.x, b.y) < std::tie(a.x, a.y)) {
      std::swap(a, b);
    }
    const TrailPass probe{.a = a, .b = b, .passes = 0};
    const auto at = std::lower_bound(
        pending.begin(),
        pending.end(),
        probe,
        [&key](const TrailPass& left, const TrailPass& right) { return key(left) < key(right); });
    if (at != pending.end() && key(*at) == key(probe)) {
      ++at->passes;
    } else {
      pending.insert(at, TrailPass{.a = a, .b = b, .passes = 1});
    }
  }
}

}  // namespace core
