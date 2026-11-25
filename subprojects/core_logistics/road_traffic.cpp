// The edge ledger (road_traffic.h; #2129, model B, boss [2]).

#include "road_traffic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "core_common/road_state.h"
#include "core_common/state_table_ops.h"

namespace core {
namespace {

constexpr float kSecondsPerHour = 3600.0F;
constexpr float kMetresPerKm = 1000.0F;
constexpr std::uint16_t kMinutesPerDayCap = 24U * 60U;

/// Whether `surface` lets a faster vehicle overtake a slower one moving ahead (roads.md
/// «Обгон только на асфальте»): asphalt only.
bool Overtakes(RoadSurface surface) {
  return surface == RoadSurface::kAsphalt || surface == RoadSurface::kAsphaltWalks;
}

std::uint16_t MinuteOf(std::int32_t second) {
  const std::int32_t minute = second / 60;
  return static_cast<std::uint16_t>(minute < 0 ? 0
                                               : std::min<std::int32_t>(minute, kMinutesPerDayCap));
}

}  // namespace

RoadTraffic::RoadTraffic(const WorldState& world,
                         std::span<const RoadObstacle> /*obstacles*/,
                         const RoadTrafficRules& rules)
    : rules_(rules), index_(RoadIndexOf(world)), roads_(&world.roads) {}

void RoadTraffic::BeginLeg(std::uint32_t cart,
                           std::uint32_t leg,
                           TravelMode mode,
                           float hours_per_km,
                           Vec2 from,
                           Vec2 to,
                           std::int32_t start_second) {
  if (!rules_.following_on || index_ == nullptr || !(hours_per_km > 0.0F)) {
    return;
  }
  const Route way = index_->Way(mode, from, to);
  Stream stream{.cart = cart, .leg = leg, .pieces = {}, .next = 0};
  stream.pieces.reserve(way.legs.size());
  bool any_edge = false;
  for (const RouteLeg& part : way.legs) {
    Piece piece;
    piece.free_seconds = static_cast<std::int32_t>(
        std::lround(part.length_m / kMetresPerKm * hours_per_km * kSecondsPerHour));
    if (part.on_road && part.edge != kNoRoadEdge) {
      piece.edge = part.edge;
      piece.forward = part.to_chainage_m >= part.from_chainage_m;
      piece.road = part.road;
      piece.chainage_m = part.from_chainage_m;
      const std::uint32_t row = FindRow(*roads_, part.road);
      piece.clamps = row != kNoRow && !Overtakes(roads_->rows[row].surface);
      any_edge = true;
    }
    stream.pieces.push_back(piece);
  }
  if (!any_edge) {
    return;  // open ground only: nothing to follow anybody on
  }
  streams_.push_back(std::move(stream));
  Push(static_cast<std::uint32_t>(streams_.size() - 1U), start_second);
}

void RoadTraffic::Push(std::uint32_t stream, std::int32_t second) {
  queue_.push(Entry{.second = second, .sequence = sequence_++, .stream = stream});
}

void RoadTraffic::AdvanceTo(std::int32_t now_second,
                            std::vector<RoadHold>& holds,
                            std::vector<LegDelay>& delays) {
  while (!queue_.empty() && queue_.top().second <= now_second) {
    const Entry entry = queue_.top();
    queue_.pop();
    Stream& stream = streams_[entry.stream];
    if (stream.next >= stream.pieces.size()) {
      continue;
    }
    const Piece& piece = stream.pieces[stream.next];
    ++stream.next;
    const std::int32_t own_out = entry.second + piece.free_seconds;
    std::int32_t out = own_out;
    if (piece.edge != kNoRoadEdge) {
      ++crossings_;
      const std::uint64_t key =
          (static_cast<std::uint64_t>(piece.edge) * 2U) + (piece.forward ? 1U : 0U);
      LastOut& last = last_out_[key];
      // FOLLOWING: on dirt and gravel nobody leaves the edge before the one ahead of him.
      if (piece.clamps && last.second > own_out && last.cart != stream.cart) {
        const std::int32_t held = last.second - own_out;
        if (held >= rules_.hold_min_seconds) {
          out = last.second;
          holds.push_back(RoadHold{.cart = stream.cart,
                                   .leg = stream.leg,
                                   .cause = RoadHoldCause::kFollowing,
                                   .road = piece.road,
                                   .chainage_m = piece.chainage_m,
                                   .from_minute = MinuteOf(own_out),
                                   .to_minute = MinuteOf(out),
                                   .behind_cart = last.cart,
                                   .rank = 1});
          delays.push_back(LegDelay{.cart = stream.cart, .leg = stream.leg, .seconds = held});
        }
      }
      if (out >= last.second) {
        last = LastOut{.second = out, .cart = stream.cart};
      }
    }
    if (stream.next < stream.pieces.size()) {
      Push(entry.stream, out);
    }
  }
}

}  // namespace core
