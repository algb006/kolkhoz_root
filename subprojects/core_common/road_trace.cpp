#include "core_common/road_trace.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <queue>
#include <utility>

#include "core_common/road_graph.h"

namespace core {
namespace {

/// Two consecutive points nearer than this are one spot: the draft has no
/// direction there (kBadPoints).
constexpr float kSameSpotMetres = 0.5F;

/// The second wave of the wander, as a share of the first's length: the
/// golden ratio, so the sum never repeats along a road.
constexpr float kSecondWaveShare = 0.618F;

/// The second wave's height against the first's; the sum is at most
/// 1 + kSecondWaveHeight, and is divided by it to stay within one.
constexpr float kSecondWaveHeight = 0.5F;

/// The most metres between two points of a waved line; a short wave takes
/// an eighth of its length.
constexpr float kWaveStepMetres = 10.0F;
constexpr float kWaveStepsPerWave = 8.0F;

/// Corner cutting (Chaikin): each corner is replaced by the points a
/// quarter of the way along its two sides.
constexpr float kCornerCut = 0.25F;

/// The half-bed is judged at five points across: the edges, the quarters and
/// the centre (the steps -2..2 of half a half-width).
constexpr int kCrossSteps = 2;

/// A heap entry older than the cost it was pushed with, by more than this,
/// is stale and skipped.
constexpr float kStaleSlack = 1e-3F;

/// A smooth periodic wave in [-1, 1] of `cycles`, one period a cycle, made
/// of multiplication and addition only: two parabolas, up then down. NOT
/// std::sin — the laid axis goes into the save (7c), and a libm may differ
/// from another in the last bit, so the same draft could lay a different
/// road under Clang and under MSVC (core_common/body.cpp says why this core
/// keeps its saved numbers out of transcendental functions).
float Wave(float cycles) {
  const float phase = cycles - std::floor(cycles);
  return phase < 0.5F ? 16.0F * phase * (0.5F - phase) : -16.0F * (phase - 0.5F) * (1.0F - phase);
}

Vec2 Plus(Vec2 a, Vec2 b) {
  return Vec2{.x = a.x + b.x, .y = a.y + b.y};
}

Vec2 Minus(Vec2 a, Vec2 b) {
  return Vec2{.x = a.x - b.x, .y = a.y - b.y};
}

Vec2 Times(Vec2 a, float k) {
  return Vec2{.x = a.x * k, .y = a.y * k};
}

float LengthOf(Vec2 a) {
  return std::sqrt((a.x * a.x) + (a.y * a.y));
}

Vec2 LeftOf(Vec2 direction) {
  return Vec2{.x = -direction.y, .y = direction.x};
}

float DistanceToSegment(Vec2 point, Vec2 from, Vec2 to) {
  const Vec2 span = Minus(to, from);
  const float length_squared = (span.x * span.x) + (span.y * span.y);
  float along = 0.0F;
  if (length_squared > 0.0F) {
    const Vec2 rel = Minus(point, from);
    along = std::clamp(((rel.x * span.x) + (rel.y * span.y)) / length_squared, 0.0F, 1.0F);
  }
  return LengthOf(Minus(Plus(from, Times(span, along)), point));
}

bool InsidePolygon(Vec2 point, const std::vector<Vec2>& outline) {
  bool inside = false;
  for (std::size_t index = 0, previous = outline.size() - 1; index < outline.size();
       previous = index++) {
    const Vec2 a = outline[index];
    const Vec2 b = outline[previous];
    if ((a.y > point.y) != (b.y > point.y) &&
        point.x < a.x + ((point.y - a.y) * (b.x - a.x) / (b.y - a.y))) {
      inside = !inside;
    }
  }
  return inside;
}

bool IsBuilt(RoadSurface surface) {
  return surface == RoadSurface::kGravel || surface == RoadSurface::kAsphalt ||
         surface == RoadSurface::kAsphaltWalks;
}

/// The units a corridor may not enter, sorted by x so a cross-section asks
/// only the ones within reach of its own x.
class UnitIndex {
 public:
  explicit UnitIndex(std::span<const RoadUnitDisc> units) : discs_(units.begin(), units.end()) {
    std::sort(discs_.begin(), discs_.end(), [](const RoadUnitDisc& a, const RoadUnitDisc& b) {
      return a.centre.x < b.centre.x;
    });
    for (const RoadUnitDisc& disc : discs_) {
      reach_m_ = std::max(reach_m_, disc.radius_m);
    }
  }

  /// Whether the cross-section from `from` to `to` enters a unit's circle.
  bool Touches(Vec2 from, Vec2 to) const {
    const float low = std::min(from.x, to.x) - reach_m_;
    const float high = std::max(from.x, to.x) + reach_m_;
    auto it =
        std::lower_bound(discs_.begin(), discs_.end(), low, [](const RoadUnitDisc& disc, float x) {
          return disc.centre.x < x;
        });
    for (; it != discs_.end() && it->centre.x <= high; ++it) {
      if (DistanceToSegment(it->centre, from, to) < it->radius_m) {
        return true;
      }
    }
    return false;
  }

 private:
  std::vector<RoadUnitDisc> discs_;
  float reach_m_ = 0.0F;
};

/// What stands in the corridor at one place: the refusal, in the order a
/// player would want to hear it, and whether trees are there to clear.
class Judge {
 public:
  Judge(const RoadTraceSite& site, const RoadDraft& draft, const RoadTraceConfig& config)
      : site_(site),
        units_(site.units),
        half_width_(draft.kind == RoadKind::kPath ? config.path_half_width_m
                                                  : config.road_half_width_m),
        built_(IsBuilt(draft.surface)),
        walks_(draft.surface == RoadSurface::kAsphaltWalks),
        raster_(site.raster != nullptr && site.raster->HasMap() ? site.raster : nullptr) {}

  float HalfWidth() const { return half_width_; }

  bool SeesMap() const { return raster_ != nullptr; }

  /// The cross-section centred on `centre`, across `left` (a unit normal).
  RoadDraftRefusal At(Vec2 centre, Vec2 left, bool* trees) const {
    std::uint8_t flags = 0;
    for (int step = -kCrossSteps; step <= kCrossSteps; ++step) {
      const Vec2 point = Plus(
          centre,
          Times(left, half_width_ * static_cast<float>(step) / static_cast<float>(kCrossSteps)));
      if (site_.map_side_m > 0.0F && (point.x < 0.0F || point.y < 0.0F ||
                                      point.x > site_.map_side_m || point.y > site_.map_side_m)) {
        return RoadDraftRefusal::kOutsideMap;
      }
      if (raster_ != nullptr) {
        flags |= raster_->FlagsAt(point);
      }
    }
    if (units_.Touches(Plus(centre, Times(left, -half_width_)),
                       Plus(centre, Times(left, half_width_)))) {
      return RoadDraftRefusal::kUnit;
    }
    if ((flags & kObstacleWater) != 0) {
      return RoadDraftRefusal::kWater;
    }
    if ((flags & kObstacleRiver) != 0 && !NearFord(centre)) {
      return RoadDraftRefusal::kRiverNoCrossing;
    }
    if ((flags & kObstacleForest) != 0) {
      return RoadDraftRefusal::kForest;
    }
    if ((flags & kObstacleReserve) != 0) {
      return RoadDraftRefusal::kReserve;
    }
    if ((flags & kObstacleRuins) != 0) {
      return RoadDraftRefusal::kRuins;
    }
    if ((flags & kObstacleTrees) != 0) {
      if (!built_) {
        return RoadDraftRefusal::kTrees;
      }
      if (trees != nullptr) {
        *trees = true;
      }
    }
    if (built_ && (flags & kObstacleFloodplain) != 0) {
      return RoadDraftRefusal::kFloodplain;
    }
    if (walks_ && !InVillage(centre)) {
      return RoadDraftRefusal::kOutsideVillage;
    }
    return RoadDraftRefusal::kNone;
  }

  /// A place of unknown heading, for the search grid: across and along.
  RoadDraftRefusal AtAnyHeading(Vec2 centre) const {
    const RoadDraftRefusal across = At(centre, Vec2{.x = 1.0F, .y = 0.0F}, nullptr);
    return across != RoadDraftRefusal::kNone ? across
                                             : At(centre, Vec2{.x = 0.0F, .y = 1.0F}, nullptr);
  }

 private:
  bool NearFord(Vec2 point) const {
    return std::any_of(site_.fords.begin(), site_.fords.end(), [point](const RoadFord& ford) {
      return LengthOf(Minus(point, ford.position)) <= ford.reach_m;
    });
  }

  bool InVillage(Vec2 point) const {
    return std::any_of(
        site_.village.begin(), site_.village.end(), [point](const std::vector<Vec2>& outline) {
          return outline.size() >= 3 && InsidePolygon(point, outline);
        });
  }

  const RoadTraceSite& site_;
  UnitIndex units_;
  float half_width_ = 0.0F;
  bool built_ = false;
  bool walks_ = false;
  const ObstacleRaster* raster_ = nullptr;
};

/// Whether the straight piece from `from` to `to` is clear, judged every
/// `step` metres.
bool PieceClear(const Judge& judge, Vec2 from, Vec2 to, float step) {
  const Vec2 span = Minus(to, from);
  const float length = LengthOf(span);
  if (length <= 0.0F) {
    return judge.At(from, Vec2{.x = 1.0F, .y = 0.0F}, nullptr) == RoadDraftRefusal::kNone;
  }
  const Vec2 left = LeftOf(Times(span, 1.0F / length));
  const auto count = static_cast<std::uint32_t>(std::ceil(length / step));
  for (std::uint32_t index = 0; index <= count; ++index) {
    const Vec2 point =
        Plus(from, Times(span, static_cast<float>(index) / static_cast<float>(count)));
    if (judge.At(point, left, nullptr) != RoadDraftRefusal::kNone) {
      return false;
    }
  }
  return true;
}

/// The same draft hashes the same, bit for bit: the wander's phases come
/// from it and from nothing else (FNV-1a over the draft's fields).
std::uint64_t DraftHash(const RoadDraft& draft) {
  std::uint64_t hash = 14695981039346656037ULL;
  const auto mix = [&hash](const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
      hash = (hash ^ bytes[index]) * 1099511628211ULL;
    }
  };
  mix(&draft.kind, sizeof(draft.kind));
  mix(&draft.surface, sizeof(draft.surface));
  mix(&draft.point_count, sizeof(draft.point_count));
  for (std::uint8_t index = 0; index < draft.point_count; ++index) {
    mix(&draft.points[index].x, sizeof(float));
    mix(&draft.points[index].y, sizeof(float));
  }
  return hash;
}

float Smoothstep(float edge, float value) {
  if (edge <= 0.0F) {
    return 1.0F;
  }
  const float t = std::clamp(value / edge, 0.0F, 1.0F);
  return t * t * (3.0F - (2.0F * t));
}

/// Two points: the chord with the surface's wave laid across it.
std::vector<Vec2> WanderedLine(
    Vec2 from, Vec2 to, const RoadWander& wander, std::uint64_t hash, float calm_m) {
  const Vec2 span = Minus(to, from);
  const float length = LengthOf(span);
  if (wander.amplitude_m <= 0.0F || wander.wavelength_m <= 0.0F || length <= 2.0F * calm_m) {
    return {from, to};
  }
  const Vec2 left = LeftOf(Times(span, 1.0F / length));
  // The phases, in cycles, from the draft's hash and nothing else.
  const float phase_one = static_cast<float>(hash & 0xFFFFU) / 65536.0F;
  const float phase_two = static_cast<float>((hash >> 16U) & 0xFFFFU) / 65536.0F;
  const float step = std::min(kWaveStepMetres, wander.wavelength_m / kWaveStepsPerWave);
  const auto count = static_cast<std::uint32_t>(std::ceil(length / step));
  std::vector<Vec2> points;
  points.reserve(count + 1);
  for (std::uint32_t index = 0; index <= count; ++index) {
    const float s = length * static_cast<float>(index) / static_cast<float>(count);
    const float wave =
        (Wave((s / wander.wavelength_m) + phase_one) +
         (kSecondWaveHeight * Wave((s / (kSecondWaveShare * wander.wavelength_m)) + phase_two))) /
        (1.0F + kSecondWaveHeight);
    const float calm = Smoothstep(calm_m, s) * Smoothstep(calm_m, length - s);
    points.push_back(
        Plus(Plus(from, Times(span, s / length)), Times(left, wander.amplitude_m * wave * calm)));
  }
  return points;
}

/// One stretch of a centripetal Catmull-Rom curve through `p1` and `p2`,
/// `p0` and `p3` its neighbours; `count` points from p1 up to, not
/// including, p2.
void AppendCurveStretch(
    Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, std::uint32_t count, std::vector<Vec2>& out) {
  const auto knot = [](float t, Vec2 a, Vec2 b) { return t + std::sqrt(LengthOf(Minus(b, a))); };
  const float t0 = 0.0F;
  const float t1 = knot(t0, p0, p1);
  const float t2 = knot(t1, p1, p2);
  const float t3 = knot(t2, p2, p3);
  const auto lerp = [](Vec2 a, Vec2 b, float ta, float tb, float t) {
    return Plus(Times(a, (tb - t) / (tb - ta)), Times(b, (t - ta) / (tb - ta)));
  };
  for (std::uint32_t index = 0; index < count; ++index) {
    const float t = t1 + ((t2 - t1) * static_cast<float>(index) / static_cast<float>(count));
    const Vec2 a1 = lerp(p0, p1, t0, t1, t);
    const Vec2 a2 = lerp(p1, p2, t1, t2, t);
    const Vec2 a3 = lerp(p2, p3, t2, t3, t);
    const Vec2 b1 = lerp(a1, a2, t0, t2, t);
    const Vec2 b2 = lerp(a2, a3, t1, t3, t);
    out.push_back(lerp(b1, b2, t1, t2, t));
  }
}

/// Points every `step` metres along a polyline, ends kept.
std::vector<Vec2> Densify(const std::vector<Vec2>& line, float step) {
  std::vector<Vec2> out;
  for (std::size_t index = 0; index + 1 < line.size(); ++index) {
    const Vec2 span = Minus(line[index + 1], line[index]);
    const auto count =
        std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::ceil(LengthOf(span) / step)));
    for (std::uint32_t part = 0; part < count; ++part) {
      out.push_back(
          Plus(line[index], Times(span, static_cast<float>(part) / static_cast<float>(count))));
    }
  }
  out.push_back(line.back());
  return out;
}

/// A way round from `from` to `to` inside the band, or nothing. The grid is
/// laid along the chord (u along, v across), so the band is a strip of it
/// rather than a box round a diagonal.
std::optional<std::vector<Vec2>> SearchRound(const Judge& judge,
                                             Vec2 from,
                                             Vec2 to,
                                             const RoadTraceConfig& config) {
  const Vec2 span = Minus(to, from);
  const float length = LengthOf(span);
  const Vec2 along = Times(span, 1.0F / length);
  const Vec2 left = LeftOf(along);
  const float band = std::clamp(length * config.band_share, config.band_min_m, config.band_max_m);
  const float cell = config.search_cell_m;
  const auto half_v = static_cast<std::int32_t>(std::ceil(band / cell));
  const auto margin = half_v;
  const auto count_u = static_cast<std::int32_t>(std::ceil(length / cell)) + (2 * margin) + 1;
  const std::int32_t count_v = (2 * half_v) + 1;
  const auto at = [&](std::int32_t u, std::int32_t v) {
    return Plus(Plus(from, Times(along, static_cast<float>(u - margin) * cell)),
                Times(left, static_cast<float>(v - half_v) * cell));
  };
  const std::int32_t start_u = margin;
  const std::int32_t goal_u = margin + static_cast<std::int32_t>(std::lround(length / cell));
  const auto index_of = [count_v](std::int32_t u, std::int32_t v) { return (u * count_v) + v; };
  const std::int32_t start = index_of(start_u, half_v);
  const std::int32_t goal = index_of(goal_u, half_v);
  std::vector<std::int8_t> free_state(
      static_cast<std::size_t>(count_u) * static_cast<std::size_t>(count_v), -1);
  const auto is_free = [&](std::int32_t u, std::int32_t v) {
    if (u < 0 || v < 0 || u >= count_u || v >= count_v) {
      return false;
    }
    const std::int32_t index = index_of(u, v);
    if (index == start || index == goal) {
      return true;
    }
    std::int8_t& state = free_state[static_cast<std::size_t>(index)];
    if (state < 0) {
      const Vec2 point = at(u, v);
      state = DistanceToSegment(point, from, to) <= band &&
                      judge.AtAnyHeading(point) == RoadDraftRefusal::kNone
                  ? 1
                  : 0;
    }
    return state == 1;
  };
  std::vector<float> cost(free_state.size(), std::numeric_limits<float>::infinity());
  std::vector<std::int32_t> parent(free_state.size(), -1);
  using Entry = std::pair<float, std::int32_t>;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
  const auto estimate = [&](std::int32_t u, std::int32_t v) {
    const float du = static_cast<float>(std::abs(u - goal_u));
    const float dv = static_cast<float>(std::abs(v - half_v));
    return cell * (std::max(du, dv) + ((std::numbers::sqrt2_v<float> - 1.0F) * std::min(du, dv)));
  };
  cost[static_cast<std::size_t>(start)] = 0.0F;
  open.emplace(estimate(start_u, half_v), start);
  bool reached = false;
  while (!open.empty()) {
    const auto [priority, index] = open.top();
    open.pop();
    const std::int32_t u = index / count_v;
    const std::int32_t v = index % count_v;
    const float here = cost[static_cast<std::size_t>(index)];
    if (priority > here + estimate(u, v) + kStaleSlack) {
      continue;  // a stale entry
    }
    if (index == goal) {
      reached = true;
      break;
    }
    for (std::int32_t du = -1; du <= 1; ++du) {
      for (std::int32_t dv = -1; dv <= 1; ++dv) {
        if ((du == 0 && dv == 0) || !is_free(u + du, v + dv)) {
          continue;
        }
        // No cutting a corner between two blocked cells.
        if (du != 0 && dv != 0 && (!is_free(u + du, v) || !is_free(u, v + dv))) {
          continue;
        }
        const std::int32_t next = index_of(u + du, v + dv);
        const float step = (du != 0 && dv != 0) ? cell * std::numbers::sqrt2_v<float> : cell;
        if (here + step < cost[static_cast<std::size_t>(next)]) {
          cost[static_cast<std::size_t>(next)] = here + step;
          parent[static_cast<std::size_t>(next)] = index;
          open.emplace(here + step + estimate(u + du, v + dv), next);
        }
      }
    }
  }
  if (!reached) {
    return std::nullopt;
  }
  std::vector<Vec2> cells;
  for (std::int32_t index = goal; index >= 0; index = parent[static_cast<std::size_t>(index)]) {
    cells.push_back(at(index / count_v, index % count_v));
  }
  std::reverse(cells.begin(), cells.end());
  cells.front() = from;
  cells.back() = to;
  // Pulled taut: from each kept point, the farthest point still in plain
  // sight — found by galloping and then halving, so a long clear run costs a
  // handful of checks rather than one per cell.
  const float step = config.sample_step_m;
  std::vector<Vec2> taut{cells.front()};
  std::size_t kept = 0;
  while (kept + 1 < cells.size()) {
    std::size_t reach = kept + 1;
    std::size_t jump = 1;
    while (reach + jump < cells.size() &&
           PieceClear(judge, cells[kept], cells[reach + jump], step)) {
      reach += jump;
      jump *= 2;
    }
    for (jump /= 2; jump > 0; jump /= 2) {
      if (reach + jump < cells.size() &&
          PieceClear(judge, cells[kept], cells[reach + jump], step)) {
        reach += jump;
      }
    }
    taut.push_back(cells[reach]);
    kept = reach;
  }
  // Smoothed (two rounds of corner cutting), kept only if still clear.
  std::vector<Vec2> smooth = taut;
  for (int round = 0; round < 2 && smooth.size() > 2; ++round) {
    std::vector<Vec2> next{smooth.front()};
    for (std::size_t index = 0; index + 1 < smooth.size(); ++index) {
      const Vec2 a = smooth[index];
      const Vec2 b = smooth[index + 1];
      if (index > 0) {
        next.push_back(Plus(Times(a, 1.0F - kCornerCut), Times(b, kCornerCut)));
      }
      if (index + 2 < smooth.size()) {
        next.push_back(Plus(Times(a, kCornerCut), Times(b, 1.0F - kCornerCut)));
      }
    }
    next.push_back(smooth.back());
    smooth = std::move(next);
  }
  for (std::size_t index = 0; index + 1 < smooth.size(); ++index) {
    if (!PieceClear(judge, smooth[index], smooth[index + 1], step)) {
      return Densify(taut, config.curve_step_m);
    }
  }
  return Densify(smooth, config.curve_step_m);
}

/// Where an end sticks: onto the nearest road axis within reach, and onto a
/// junction or a road's end near that.
RoadDraftEnd SnapEnd(const RoadTraceSite& site, Vec2 point, const RoadTraceConfig& config) {
  RoadDraftEnd end;
  end.point = point;
  if (site.roads == nullptr) {
    return end;
  }
  float best = config.snap_to_road_m;
  std::size_t best_row = site.roads->rows.size();
  float best_chainage = 0.0F;
  for (std::size_t row = 0; row < site.roads->rows.size(); ++row) {
    const std::vector<RoadPoint>& axis = site.roads->rows[row].axis;
    if (axis.size() < 2) {
      continue;
    }
    const AxisProjection projection = ProjectOntoAxis(axis, point);
    if (projection.distance_m <= best) {
      best = projection.distance_m;
      best_row = row;
      best_chainage = projection.chainage_m;
    }
  }
  if (best_row == site.roads->rows.size()) {
    return end;
  }
  const std::vector<RoadPoint>& axis = site.roads->rows[best_row].axis;
  float chainage = 0.0F;
  float nearest_gap = config.snap_to_junction_m;
  std::optional<float> junction;
  for (std::size_t index = 0; index < axis.size(); ++index) {
    if (index > 0) {
      chainage += LengthOf(Minus(axis[index].position, axis[index - 1].position));
    }
    const bool is_joint =
        axis[index].mark == RoadMark::kJunction || index == 0 || index + 1 == axis.size();
    const float gap = std::abs(chainage - best_chainage);
    if (is_joint && gap <= nearest_gap) {
      nearest_gap = gap;
      junction = chainage;
    }
  }
  end.road = site.roads->row_ids[best_row];
  end.snap = junction ? RoadEndSnap::kJunction : RoadEndSnap::kRoad;
  end.road_s_m = junction ? *junction : best_chainage;
  end.point = PointAtChainage(axis, end.road_s_m);
  return end;
}

/// One judged sample of the finished axis.
struct Sample {
  float s_m = 0.0F;
  Vec2 point;
  RoadDraftRefusal refusal = RoadDraftRefusal::kNone;
  bool trees = false;
};

}  // namespace

RoadDraftResult TraceRoad(const RoadTraceSite& site,
                          const RoadDraft& draft,
                          const RoadTraceConfig& config) {
  RoadDraftResult result;
  const bool has_map = site.raster != nullptr && site.raster->HasMap();
  result.gaps.obstacles_unread = !has_map;
  // Not a draft at all: a count out of range, a surface the kind has not,
  // two consecutive points on one spot.
  const bool count_fits = draft.point_count >= 2 && draft.point_count <= kRoadDraftMaxPoints;
  const bool surface_fits =
      draft.kind == RoadKind::kPath
          ? draft.surface == RoadSurface::kNone
          : draft.surface != RoadSurface::kNone && draft.surface < RoadSurface::kRoadSurfaceCount;
  bool points_apart = count_fits;
  for (std::uint8_t index = 1; points_apart && index < draft.point_count; ++index) {
    points_apart = LengthOf(Minus(draft.points[index], draft.points[index - 1])) >= kSameSpotMetres;
  }
  if (!count_fits || !surface_fits || !points_apart || draft.kind >= RoadKind::kRoadKindCount) {
    result.blocks.push_back(
        RoadDraftBlock{.refusal = RoadDraftRefusal::kBadPoints, .at = draft.points[0]});
    return result;
  }

  const Judge judge(site, draft, config);
  std::vector<Vec2> controls(draft.points.begin(), draft.points.begin() + draft.point_count);
  result.start = SnapEnd(site, controls.front(), config);
  result.end = SnapEnd(site, controls.back(), config);
  controls.front() = result.start.point;
  controls.back() = result.end.point;
  // SNAPPING MOVES THE ENDS, so the points are asked again (static review
  // of 0.36.26): a short spur beside a junction snapped both ends to the one
  // junction and came back as a clean road of nought metres, and an end
  // snapped onto the next point made the curve divide nought by nought.
  for (std::size_t index = 1; index < controls.size(); ++index) {
    if (LengthOf(Minus(controls[index], controls[index - 1])) < kSameSpotMetres) {
      result.blocks.push_back(
          RoadDraftBlock{.refusal = RoadDraftRefusal::kBadPoints, .at = controls[index]});
      return result;
    }
  }

  // The axis, stretch by stretch between control points; a stretch that
  // could not be taken round stays as drawn and is remembered.
  std::vector<Vec2> axis;
  std::vector<std::pair<std::size_t, std::size_t>> stranded;  // axis index ranges
  if (controls.size() == 2) {
    axis = WanderedLine(controls[0],
                        controls[1],
                        config.wander[static_cast<std::size_t>(draft.surface)],
                        DraftHash(draft),
                        config.wander_calm_m);
    axis = Densify(axis, config.curve_step_m);
  } else {
    for (std::size_t stretch = 0; stretch + 1 < controls.size(); ++stretch) {
      const Vec2 p1 = controls[stretch];
      const Vec2 p2 = controls[stretch + 1];
      const Vec2 p0 = stretch == 0 ? Minus(Times(p1, 2.0F), p2) : controls[stretch - 1];
      const Vec2 p3 =
          stretch + 2 < controls.size() ? controls[stretch + 2] : Minus(Times(p2, 2.0F), p1);
      std::vector<Vec2> piece;
      const auto count = std::max<std::uint32_t>(
          2, static_cast<std::uint32_t>(std::ceil(LengthOf(Minus(p2, p1)) / config.curve_step_m)));
      AppendCurveStretch(p0, p1, p2, p3, count, piece);
      piece.push_back(p2);
      bool clear = true;
      for (std::size_t index = 0; clear && index + 1 < piece.size(); ++index) {
        clear = PieceClear(judge, piece[index], piece[index + 1], config.sample_step_m);
      }
      if (!clear) {
        if (std::optional<std::vector<Vec2>> round = SearchRound(judge, p1, p2, config)) {
          piece = std::move(*round);
        } else {
          const std::size_t first = axis.empty() ? 0 : axis.size() - 1;
          stranded.emplace_back(first, first + piece.size() - 1);
        }
      }
      if (!axis.empty()) {
        axis.pop_back();  // the stretch starts on the last one's end
      }
      axis.insert(axis.end(), piece.begin(), piece.end());
    }
  }

  // Running lengths, and the axis the layer reads.
  std::vector<float> s_of(axis.size(), 0.0F);
  for (std::size_t index = 1; index < axis.size(); ++index) {
    s_of[index] = s_of[index - 1] + LengthOf(Minus(axis[index], axis[index - 1]));
  }
  result.axis.reserve(axis.size());
  for (std::size_t index = 0; index < axis.size(); ++index) {
    result.axis.push_back(RoadAxisPoint{.position = axis[index], .s_m = s_of[index]});
  }
  if (result.start.snap != RoadEndSnap::kFree) {
    result.axis.front().mark = RoadMark::kJunction;
  }
  if (result.end.snap != RoadEndSnap::kFree) {
    result.axis.back().mark = RoadMark::kJunction;
  }
  result.length_m = s_of.back();

  // Judged every sample_step_m along the finished axis.
  std::vector<Sample> samples;
  float clearing_m = 0.0F;
  for (std::size_t index = 0; index + 1 < axis.size(); ++index) {
    const Vec2 span = Minus(axis[index + 1], axis[index]);
    const float length = LengthOf(span);
    if (length <= 0.0F) {
      continue;
    }
    const Vec2 left = LeftOf(Times(span, 1.0F / length));
    const auto count = std::max<std::uint32_t>(
        1, static_cast<std::uint32_t>(std::ceil(length / config.sample_step_m)));
    const bool last = index + 2 == axis.size();
    for (std::uint32_t part = 0; part < count + (last ? 1U : 0U); ++part) {
      const float share = static_cast<float>(part) / static_cast<float>(count);
      Sample sample{.s_m = s_of[index] + (length * share),
                    .point = Plus(axis[index], Times(span, share))};
      sample.refusal = judge.At(sample.point, left, &sample.trees);
      if (sample.trees && sample.refusal == RoadDraftRefusal::kNone && part < count) {
        clearing_m += length / static_cast<float>(count);
      }
      samples.push_back(sample);
    }
  }
  const auto add_block = [&result](RoadDraftRefusal refusal, float from, float to, Vec2 at) {
    result.blocks.push_back(
        RoadDraftBlock{.refusal = refusal, .s_from_m = from, .s_to_m = to, .at = at});
  };
  const RoadSurfaceCost& cost = site.costs[static_cast<std::size_t>(draft.surface)];
  if (!cost.open) {
    add_block(RoadDraftRefusal::kClosedByEpoch, 0.0F, result.length_m, axis.front());
  }
  for (const auto& [first, last] : stranded) {
    add_block(RoadDraftRefusal::kNoWayRound, s_of[first], s_of[last], axis[(first + last) / 2]);
  }
  for (std::size_t index = 0; index < samples.size();) {
    const RoadDraftRefusal refusal = samples[index].refusal;
    std::size_t run_end = index;
    while (run_end + 1 < samples.size() && samples[run_end + 1].refusal == refusal) {
      ++run_end;
    }
    if (refusal != RoadDraftRefusal::kNone) {
      add_block(
          refusal, samples[index].s_m, samples[run_end].s_m, samples[(index + run_end) / 2].point);
    }
    index = run_end + 1;
  }

  result.carriageway_m = 2.0F * judge.HalfWidth();
  const bool built = IsBuilt(draft.surface);
  result.clearing_m = built ? config.clearing_width_m : 0.0F;
  if (built) {
    const float hundreds = result.length_m / 100.0F;
    result.estimate.man_days = cost.man_days_per_100m * hundreds;
    result.estimate.materials.resize(cost.materials_per_100m.size(), 0);
    for (std::size_t resource = 0; resource < cost.materials_per_100m.size(); ++resource) {
      result.estimate.materials[resource] = static_cast<std::int64_t>(std::llround(
          static_cast<double>(cost.materials_per_100m[resource]) * static_cast<double>(hundreds)));
    }
    result.estimate.clearing_ha = clearing_m * config.clearing_width_m / 10000.0F;
    result.estimate.timber_m3 = result.estimate.clearing_ha * site.timber_m3_per_ha;
  }
  return result;
}

}  // namespace core
