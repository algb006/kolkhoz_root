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

/// A line's cell transitions from its first cell: one move a byte, x and y each -1, 0 or +1
/// (a half-cell sample moves at most one cell an axis), encoded (x + 1) * 3 + (y + 1).
struct LineMoves {
  std::vector<std::uint8_t> moves;
};

std::int32_t MoveX(std::uint8_t move) {
  return static_cast<std::int32_t>(move / 3U) - 1;
}

std::int32_t MoveY(std::uint8_t move) {
  return static_cast<std::int32_t>(move % 3U) - 1;
}

/// The line's key: its canonical ends' and its length's bits.
using LineKey = std::array<std::uint32_t, 5>;

struct LineKeyHash {
  std::size_t operator()(const LineKey& key) const {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::uint32_t word : key) {
      hash = (hash ^ word) * 1099511628211ULL;
    }
    return static_cast<std::size_t>(hash);
  }
};

/// Lines kept a thread before the cache starts over (pure geometry: forgetting changes no
/// answer, it only samples again).
constexpr std::size_t kLineCacheCap = std::size_t{1} << 17U;

/// The transitions of the canonical line `from` -> `to`, sampled every half cell from `first`
/// (SnapTrailEnd(from)) - the very samples MeanCrossedCost took before the cache.
const LineMoves& MovesOf(Vec2 from, Vec2 to, float length, TrailEnd first) {
  thread_local std::unordered_map<LineKey, LineMoves, LineKeyHash> cache;
  const LineKey key = {std::bit_cast<std::uint32_t>(from.x),
                       std::bit_cast<std::uint32_t>(from.y),
                       std::bit_cast<std::uint32_t>(to.x),
                       std::bit_cast<std::uint32_t>(to.y),
                       std::bit_cast<std::uint32_t>(length)};
  if (const auto found = cache.find(key); found != cache.end()) {
    return found->second;
  }
  if (cache.size() >= kLineCacheCap) {
    cache.clear();
  }
  LineMoves line;
  const auto pieces =
      static_cast<std::uint32_t>(std::ceil(length / (kTrailSnapMetres / kSamplesPerCell)));
  TrailEnd here = first;
  for (std::uint32_t piece = 1; piece <= pieces; ++piece) {
    const float share = static_cast<float>(piece) / static_cast<float>(pieces);
    const TrailEnd next = SnapTrailEnd(
        {.x = from.x + ((to.x - from.x) * share), .y = from.y + ((to.y - from.y) * share)});
    if (next.x != here.x || next.y != here.y) {
      line.moves.push_back(
          static_cast<std::uint8_t>(((next.x - here.x + 1) * 3) + (next.y - here.y + 1)));
      here = next;
    }
  }
  return cache.emplace(key, std::move(line)).first->second;
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
  const float open = open_weight * open_cost;
  // THE LINE MISSES EVERY TRAIL (#2072 CPU, boss's (i) of 24 Nov): the box of its two end cells
  // holds the whole line, so a box clear of the trails' crosses no trodden edge - open ground,
  // the very answer the sampling below gives (its discount sums only trodden edges).
  const TrailEnd first = SnapTrailEnd(from);
  const TrailEnd last = SnapTrailEnd(to);
  if (!trails.grid.empty()) {
    const std::int32_t right = trails.origin.x + trails.columns - 1;
    const std::int32_t top = trails.origin.y + trails.rows_of_cells - 1;
    if (std::max(first.x, last.x) < trails.origin.x || std::min(first.x, last.x) > right ||
        std::max(first.y, last.y) < trails.origin.y || std::min(first.y, last.y) > top) {
      return open;
    }
  }
  // THE LINE'S CELLS ARE GEOMETRY (#2116, the Fable review's item 1; exact): its transitions,
  // sampled every half cell, read no trail - cached per thread by the line's bits, and each
  // day only REPLAYED against the day's trails by the same statements.
  const LineMoves& moves = MovesOf(from, to, length, first);
  // EACH EDGE BY ITS LENGTH (boss 24 Nov, LEGS2072's cases: counting every transition once
  // left the straight door cheaper than the route in 2.8 % of pairs; the route pays a diagonal
  // edge √2 of a straight one): a straight edge weighs 1, a diagonal √2. The answer is open
  // ground less the mean DISCOUNT, so an untrodden line answers open ground exactly.
  float discount = 0.0F;
  float crossed = 0.0F;
  TrailEnd here = first;
  for (const std::uint8_t move : moves.moves) {
    const TrailEnd next{.x = here.x + MoveX(move), .y = here.y + MoveY(move)};
    // STRAIGHT NEVER CHEAPER THAN THE ROUTE (boss 23 Nov, roads.md): the trodden discount the
    // straight door gives is the route's cut by the lattice's octile bound - a route along the
    // same trail pays up to that excess for its staircase.
    const float edge = next.x != here.x && next.y != here.y ? kSqrtTwo : 1.0F;
    if (trails.Find(here, next) != nullptr) {
      const float step = StepCost(trails, here, next, open_weight, open_cost, snow_trail_cost);
      discount += edge * (open - step) * kStraightDiscountShare;
    }
    crossed += edge;
    here = next;
  }
  return crossed > 0.0F ? open - (discount / crossed) : open;
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

namespace {

/// The grid's direction of a lesser-first neighbour step, or -1 for any other pair.
int GridDirection(TrailEnd lesser, TrailEnd greater) {
  const std::int32_t step_x = greater.x - lesser.x;
  const std::int32_t step_y = greater.y - lesser.y;
  if (step_x == 0 && step_y == 1) {
    return 0;
  }
  if (step_x == 1 && step_y >= -1 && step_y <= 1) {
    return 2 + step_y;  // (+1,-1) 1, (+1,0) 2, (+1,+1) 3
  }
  return -1;
}

}  // namespace

const TrailRow* TrailLookup::Find(TrailEnd a, TrailEnd b) const {
  if (std::tie(b.x, b.y) < std::tie(a.x, a.y)) {
    std::swap(a, b);
  }
  const int direction = GridDirection(a, b);
  if (direction >= 0 && !grid.empty()) {
    const std::int32_t column = a.x - origin.x;
    const std::int32_t row = a.y - origin.y;
    if (column < 0 || row < 0 || column >= columns || row >= rows_of_cells) {
      return nullptr;  // outside every trail's box: no trail
    }
    return grid[(static_cast<std::size_t>((row * columns) + column) * 4U) +
                static_cast<std::size_t>(direction)];
  }
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
  std::int32_t left = 0;
  std::int32_t bottom = 0;
  std::int32_t right = -1;
  std::int32_t top = -1;
  for (const TrailRow& row : table.rows) {
    lookup.rows.emplace(EdgeKey(row.a, row.b), &row);
    for (const TrailEnd end : {row.a, row.b}) {
      const bool first = right < left;
      left = first ? end.x : std::min(left, end.x);
      bottom = first ? end.y : std::min(bottom, end.y);
      right = first ? end.x : std::max(right, end.x);
      top = first ? end.y : std::max(top, end.y);
    }
  }
  if (right < left) {
    return lookup;
  }
  lookup.origin = {.x = left, .y = bottom};
  lookup.columns = right - left + 1;
  lookup.rows_of_cells = top - bottom + 1;
  lookup.grid.assign(static_cast<std::size_t>(lookup.columns) *
                         static_cast<std::size_t>(lookup.rows_of_cells) * 4U,
                     nullptr);
  for (const TrailRow& row : table.rows) {
    const int direction = GridDirection(row.a, row.b);  // rows are lesser-first
    if (direction < 0) {
      continue;  // a non-neighbour edge: the hash answers it
    }
    const std::int32_t column = row.a.x - left;
    const std::int32_t cell_row = row.a.y - bottom;
    lookup.grid[(static_cast<std::size_t>((cell_row * lookup.columns) + column) * 4U) +
                static_cast<std::size_t>(direction)] = &row;
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
