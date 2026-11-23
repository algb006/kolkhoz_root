/// @file
/// @brief The trails' lattice and the ground's states (trail_lattice.h; #2072 (A), roads.md §3).

#include "trail_lattice.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <queue>
#include <tuple>
#include <utility>
#include <vector>

namespace core::lattice {

namespace {

constexpr float kMetresPerKm = 1000.0F;
/// A diagonal step's length in cells.
constexpr float kSqrtTwo = 1.41421356F;
/// The octile distance's diagonal surcharge (√2 − 1).
constexpr float kOctileDiagonal = kSqrtTwo - 1.0F;
/// The octile bound: an 8-neighbour path's most excess over the line it follows (at 22.5°,
/// (1 + (√2 − 1) tan 22.5°) / sec 22.5° = 1.0824), and the share of the route's discount the
/// straight door keeps so it is never the cheaper one.
constexpr float kOctileBound = 1.0824F;
constexpr float kStraightDiscountShare = 1.0F / kOctileBound;
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
float MeanCrossedCost(const TrailLookup& trails,
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
      // STRAIGHT NEVER CHEAPER THAN THE ROUTE (boss 23 Nov, roads.md): the trodden discount the
      // straight door gives is the route's cut by the lattice's octile bound - a route along the
      // same trail pays up to that excess for its staircase.
      const float open = open_weight * open_cost;
      const float step = StepCost(trails, here, next, open_weight, open_cost, snow_trail_cost);
      sum += open - ((open - step) * kStraightDiscountShare);
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

namespace {

/// An edge's key, its lesser end first (the table's own order: x, then y).
std::uint64_t EdgeKey(TrailEnd a, TrailEnd b) {
  if (std::tie(b.x, b.y) < std::tie(a.x, a.y)) {
    std::swap(a, b);
  }
  const auto cell = [](TrailEnd end) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(end.x) & 0xFFFFU) << 16U) |
           (static_cast<std::uint32_t>(end.y) & 0xFFFFU);
  };
  return (cell(a) << 32U) | cell(b);
}

}  // namespace

const TrailRow* TrailLookup::Find(TrailEnd a, TrailEnd b) const {
  if (rows.empty()) {
    return table != nullptr ? FindTrailEdge(*table, a, b) : nullptr;
  }
  const auto found = rows.find(EdgeKey(a, b));
  return found != rows.end() ? found->second : nullptr;
}

TrailLookup MakeLookup(const TrailTable& table) {
  TrailLookup lookup;
  lookup.table = &table;
  lookup.rows.reserve(table.rows.size());
  for (const TrailRow& row : table.rows) {
    lookup.rows.emplace(EdgeKey(row.a, row.b), &row);
  }
  return lookup;
}

std::shared_ptr<const TrailLookup> LookupOf(const TrailTable* table) {
  if (table == nullptr || table->rows.empty()) {
    return nullptr;
  }
  return std::make_shared<const TrailLookup>(MakeLookup(*table));
}

const TrailLookup& NoLookup() {
  static const TrailLookup kNone;
  return kNone;
}

float StepCost(const TrailLookup& trails,
               TrailEnd from_cell,
               TrailEnd to_cell,
               float open_weight,
               float open_cost,
               float snow_trail_cost) {
  const TrailRow* const row = trails.Find(from_cell, to_cell);
  if (row == nullptr) {
    return open_weight * open_cost;
  }
  const float full = std::min(open_weight, trails.WeightFull());
  const float weight = open_weight - (TrailStrength(*row) * (open_weight - full));
  return weight * (row->snow > 0.0F ? snow_trail_cost : open_cost);
}

float StraightKm(const TrailLookup& trails,
                 Vec2 from,
                 Vec2 to,
                 float open_weight,
                 float open_cost,
                 float snow_trail_cost) {
  const float length = Distance(from, to);
  if (!(length > 0.0F) || trails.Empty()) {
    return length / kMetresPerKm * open_weight * open_cost;
  }
  return length / kMetresPerKm *
         MeanCrossedCost(trails, from, to, length, open_weight, open_cost, snow_trail_cost);
}

float RouteKm(const TrailLookup& trails,
              Vec2 from,
              Vec2 to,
              float open_weight,
              float open_cost,
              float snow_trail_cost,
              std::vector<std::pair<TrailEnd, TrailEnd>>* edges) {
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
      std::min(open_weight, trails.WeightFull()) * std::min(open_cost, snow_trail_cost);
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

namespace {

/// A memo's key: the day and every input's bits - a hit is the same question, exactly.
using MemoKey = std::array<std::uint32_t, 9>;

struct MemoKeyHash {
  std::size_t operator()(const MemoKey& key) const {
    std::uint64_t hash = 14695981039346656037ULL;  // FNV-1a
    for (const std::uint32_t word : key) {
      hash = (hash ^ word) * 1099511628211ULL;
    }
    return static_cast<std::size_t>(hash);
  }
};

MemoKey KeyOf(std::uint64_t serial,
              Vec2 from,
              Vec2 to,
              float open_weight,
              float open_cost,
              float snow_trail_cost) {
  return {static_cast<std::uint32_t>(serial),
          static_cast<std::uint32_t>(serial >> 32U),
          std::bit_cast<std::uint32_t>(from.x),
          std::bit_cast<std::uint32_t>(from.y),
          std::bit_cast<std::uint32_t>(to.x),
          std::bit_cast<std::uint32_t>(to.y),
          std::bit_cast<std::uint32_t>(open_weight),
          std::bit_cast<std::uint32_t>(open_cost),
          std::bit_cast<std::uint32_t>(snow_trail_cost)};
}

/// A memo bounded in size: cleared when the day changes or it grows past kMemoCap.
template <typename Value>
struct DayMemo {
  static constexpr std::size_t kMemoCap = 1U << 18U;
  std::uint64_t serial = 0;
  std::unordered_map<MemoKey, Value, MemoKeyHash> answers;

  void Enter(std::uint64_t day) {
    if (day != serial || answers.size() > kMemoCap) {
      answers.clear();
      serial = day;
    }
  }
};

struct RouteAnswer {
  float km = 0.0F;
  std::vector<std::pair<TrailEnd, TrailEnd>> edges;
};

std::atomic<std::uint64_t> g_day_serial{0};

}  // namespace

std::uint64_t NextDaySerial() {
  return g_day_serial.fetch_add(1, std::memory_order_relaxed) + 1U;
}

float StraightKmMemo(std::uint64_t serial,
                     const TrailLookup& trails,
                     Vec2 from,
                     Vec2 to,
                     float open_weight,
                     float open_cost,
                     float snow_trail_cost) {
  thread_local DayMemo<float> memo;
  memo.Enter(serial);
  const MemoKey key = KeyOf(serial, from, to, open_weight, open_cost, snow_trail_cost);
  const auto found = memo.answers.find(key);
  if (found != memo.answers.end()) {
    return found->second;
  }
  const float km = StraightKm(trails, from, to, open_weight, open_cost, snow_trail_cost);
  memo.answers.emplace(key, km);
  return km;
}

float RouteKmMemo(std::uint64_t serial,
                  const TrailLookup& trails,
                  Vec2 from,
                  Vec2 to,
                  float open_weight,
                  float open_cost,
                  float snow_trail_cost,
                  std::vector<std::pair<TrailEnd, TrailEnd>>* edges) {
  thread_local DayMemo<RouteAnswer> memo;
  memo.Enter(serial);
  const MemoKey key = KeyOf(serial, from, to, open_weight, open_cost, snow_trail_cost);
  auto found = memo.answers.find(key);
  if (found == memo.answers.end()) {
    RouteAnswer answer;
    answer.km = RouteKm(trails, from, to, open_weight, open_cost, snow_trail_cost, &answer.edges);
    found = memo.answers.emplace(key, std::move(answer)).first;
  }
  if (edges != nullptr) {
    edges->insert(edges->end(), found->second.edges.begin(), found->second.edges.end());
  }
  return found->second.km;
}

}  // namespace core::lattice
