/// @file
/// @brief The trails' lattice and the ground's states (trail_lattice.h; #2072 (A), roads.md §3).

#include "trail_lattice.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <queue>
#include <tuple>
#include <utility>
#include <vector>

#include "core_common/time_probe.h"

namespace core::lattice {

namespace {

constexpr float kMetresPerKm = 1000.0F;
/// A diagonal step's length in cells.
constexpr float kSqrtTwo = 1.41421356F;
/// The octile distance's diagonal surcharge (√2 − 1).
constexpr float kOctileDiagonal = kSqrtTwo - 1.0F;
/// Cells around the from-to box the walker's search may use (200 m at 25 m).
constexpr std::int32_t kSearchMarginCells = 8;
/// The query door samples the line every half cell.
constexpr float kSamplesPerCell = 2.0F;
/// A queue entry is stale when its estimate exceeds the cell's settled cost by more than this.
constexpr float kStaleSlack = 1e-6F;
constexpr std::uint32_t kNoCell = 0xFFFFFFFFU;

float Distance(Vec2 a, Vec2 b) {
  return std::hypot(a.x - b.x, a.y - b.y);
}

/// The mean cost a km of the lattice edges the line crosses, sampled every half cell from the
/// lesser end (x, then y) - one canonical direction, so a pair answers the same either way
/// (static review of 23 Nov 2026). A line inside one cell crosses no edge: open ground.
float MeanCrossedCost(const TrailTable& trails,
                      Vec2 from,
                      Vec2 to,
                      float length,
                      float open_weight,
                      float open_cost,
                      float snow_trail_cost) {
  if (std::tie(to.x, to.y) < std::tie(from.x, from.y)) {
    std::swap(from, to);
  }
  const auto pieces =
      static_cast<std::uint32_t>(std::ceil(length / (kTrailSnapMetres / kSamplesPerCell)));
  float sum = 0.0F;
  std::uint32_t crossed = 0;
  TrailEnd here = SnapTrailEnd(from);
  for (std::uint32_t piece = 1; piece <= pieces; ++piece) {
    const float share = static_cast<float>(piece) / static_cast<float>(pieces);
    const TrailEnd next = SnapTrailEnd(
        {.x = from.x + ((to.x - from.x) * share), .y = from.y + ((to.y - from.y) * share)});
    if (next.x != here.x || next.y != here.y) {
      sum += StepCost(trails, here, next, open_weight, open_cost, snow_trail_cost);
      ++crossed;
      here = next;
    }
  }
  return crossed > 0 ? sum / static_cast<float>(crossed) : open_weight * open_cost;
}

}  // namespace

GroundState GroundOnRoad(RoadCondition condition, RoadBed bed) {
  switch (condition) {
    case RoadCondition::kWet:
      return GroundState::kWet;
    case RoadCondition::kMud:
      return bed == RoadBed::kDirt ? GroundState::kMud : GroundState::kWet;
    case RoadCondition::kFrozen:
      return GroundState::kFrozen;
    case RoadCondition::kSnow:
      return GroundState::kWet;
    default:
      return GroundState::kDry;
  }
}

GroundState GroundOffRoad(RoadCondition dirt) {
  switch (dirt) {
    case RoadCondition::kWet:
      return GroundState::kWet;
    case RoadCondition::kMud:
      return GroundState::kMud;
    case RoadCondition::kFrozen:
      return GroundState::kFrozen;
    case RoadCondition::kSnow:
      return GroundState::kSnow;
    default:
      return GroundState::kDry;
  }
}

float GroundFactorOf(const GroundFactors& factors, TravelMode mode, GroundState state) {
  const auto index = static_cast<std::size_t>(state);
  if (index >= kGroundStateCountValue) {
    return 1.0F;
  }
  if (mode == TravelMode::kWalk) {
    return factors.walk[index];
  }
  if (mode == TravelMode::kTeam) {
    return factors.ride[index];
  }
  return 1.0F;
}

float CostOf(float factor) {
  return factor > 0.0F ? 1.0F / factor : 1.0F;
}

float StepCost(const TrailTable& trails,
               TrailEnd from_cell,
               TrailEnd to_cell,
               float open_weight,
               float open_cost,
               float snow_trail_cost) {
  const TrailRow* const row = FindTrailEdge(trails, from_cell, to_cell);
  if (row == nullptr) {
    return open_weight * open_cost;
  }
  const float full = std::min(open_weight, trails.weight_full);
  const float weight = open_weight - (TrailStrength(*row) * (open_weight - full));
  return weight * (row->snow > 0.0F ? snow_trail_cost : open_cost);
}

float StraightKm(const TrailTable& trails,
                 Vec2 from,
                 Vec2 to,
                 float open_weight,
                 float open_cost,
                 float snow_trail_cost) {
  const probe::Timer timer(probe::Slot::kStraight);
  const float length = Distance(from, to);
  if (!(length > 0.0F) || trails.rows.empty()) {
    return length / kMetresPerKm * open_weight * open_cost;
  }
  return length / kMetresPerKm *
         MeanCrossedCost(trails, from, to, length, open_weight, open_cost, snow_trail_cost);
}

float RouteKm(const TrailTable& trails,
              Vec2 from,
              Vec2 to,
              float open_weight,
              float open_cost,
              float snow_trail_cost,
              std::vector<std::pair<TrailEnd, TrailEnd>>* edges) {
  const probe::Timer timer(probe::Slot::kRoute);
  const TrailEnd start = SnapTrailEnd(from);
  const TrailEnd goal = SnapTrailEnd(to);
  const float straight = Distance(from, to) / kMetresPerKm * open_weight * open_cost;
  if (start.x == goal.x && start.y == goal.y) {
    return straight;
  }
  const std::int32_t left = std::min(start.x, goal.x) - kSearchMarginCells;
  const std::int32_t bottom = std::min(start.y, goal.y) - kSearchMarginCells;
  const std::int32_t width = std::max(start.x, goal.x) + kSearchMarginCells - left + 1;
  const std::int32_t height = std::max(start.y, goal.y) + kSearchMarginCells - bottom + 1;
  const auto index_of = [&](std::int32_t x, std::int32_t y) {
    return static_cast<std::uint32_t>(((y - bottom) * width) + (x - left));
  };
  const auto cell_of = [&](std::uint32_t at) {
    const auto columns = static_cast<std::uint32_t>(width);
    return TrailEnd{.x = left + static_cast<std::int32_t>(at % columns),
                    .y = bottom + static_cast<std::int32_t>(at / columns)};
  };
  const float cell_km = kTrailSnapMetres / kMetresPerKm;
  // The heuristic's cheapest km: a full trail on the cheaper ground - never over the truth.
  const float cheapest =
      std::min(open_weight, trails.weight_full) * std::min(open_cost, snow_trail_cost);
  const auto octile_km = [&](std::int32_t x, std::int32_t y) {
    const auto across = static_cast<float>(std::abs(x - goal.x));
    const auto up = static_cast<float>(std::abs(y - goal.y));
    return cell_km * (std::max(across, up) + (kOctileDiagonal * std::min(across, up)));
  };
  std::vector<float> settled(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                             std::numeric_limits<float>::infinity());
  std::vector<std::uint32_t> came_from(settled.size(), kNoCell);
  using Entry = std::pair<float, std::uint32_t>;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<>> frontier;
  const std::uint32_t start_index = index_of(start.x, start.y);
  const std::uint32_t goal_index = index_of(goal.x, goal.y);
  settled[start_index] = 0.0F;
  frontier.emplace(octile_km(start.x, start.y) * cheapest, start_index);
  while (!frontier.empty()) {
    const auto [estimate, node] = frontier.top();
    frontier.pop();
    if (node == goal_index) {
      break;
    }
    const TrailEnd here = cell_of(node);
    const float cost_here = settled[node];
    if (estimate > cost_here + (octile_km(here.x, here.y) * cheapest) + kStaleSlack) {
      continue;  // a stale entry
    }
    for (std::int32_t step_y = -1; step_y <= 1; ++step_y) {
      for (std::int32_t step_x = -1; step_x <= 1; ++step_x) {
        const std::int32_t next_x = here.x + step_x;
        const std::int32_t next_y = here.y + step_y;
        if ((step_x == 0 && step_y == 0) || next_x < left || next_y < bottom ||
            next_x >= left + width || next_y >= bottom + height) {
          continue;
        }
        const float step_km = (step_x != 0 && step_y != 0) ? cell_km * kSqrtTwo : cell_km;
        const float cost_next =
            cost_here +
            (step_km *
             StepCost(trails, here, {next_x, next_y}, open_weight, open_cost, snow_trail_cost));
        const std::uint32_t next = index_of(next_x, next_y);
        if (cost_next < settled[next]) {
          settled[next] = cost_next;
          came_from[next] = node;
          frontier.emplace(cost_next + (octile_km(next_x, next_y) * cheapest), next);
        }
      }
    }
  }
  const float routed = settled[goal_index];
  if (!(routed < std::numeric_limits<float>::infinity())) {
    return straight;
  }
  if (edges != nullptr) {
    std::vector<std::pair<TrailEnd, TrailEnd>> backwards;
    for (std::uint32_t node = goal_index; came_from[node] != kNoCell; node = came_from[node]) {
      backwards.emplace_back(cell_of(came_from[node]), cell_of(node));
    }
    edges->insert(edges->end(), backwards.rbegin(), backwards.rend());
  }
  // The normalisation: the untrodden lattice route between the same cells is octile x open.
  const float untrodden = octile_km(start.x, start.y) * open_weight * open_cost;
  return untrodden > 0.0F ? straight * (routed / untrodden) : straight;
}

}  // namespace core::lattice
