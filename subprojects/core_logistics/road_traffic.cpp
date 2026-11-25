// The edge ledger (road_traffic.h; #2129, model B, boss [2]).

#include "road_traffic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
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
                           std::int32_t start_second,
                           bool replay) {
  if (!rules_.following_on || index_ == nullptr || !(hours_per_km > 0.0F)) {
    return;
  }
  const Route way = index_->Way(mode, from, to);
  Stream stream{
      .cart = cart, .leg = leg, .replay = replay, .pace = hours_per_km, .pieces = {}, .next = 0};
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
        }
        // A REPLAYED leg (begun before a re-plan) waits as it waited, but its hold is the
        // earlier plan's and its time is in its arrival already: nothing new is recorded.
        if (held >= rules_.hold_min_seconds && !stream.replay) {
          // PROBE (wip/probe-2129-pairs, not for release): the pair behind each hold.
          thread_local std::uint32_t printed = 0;
          if (printed < 6000) {
            ++printed;
            std::cerr << "PAIR2129 own_pace " << stream.pace << " lead_pace " << last.pace
                      << " own_in " << entry.second << " lead_in " << last.in_second << " own_free "
                      << piece.free_seconds << " lead_free " << last.free_seconds << " held "
                      << held << " edge " << piece.edge << " fwd " << piece.forward << " own "
                      << stream.cart << '/' << stream.leg << " lead " << last.cart << '/'
                      << last.leg << " piece " << stream.next << '/' << stream.pieces.size()
                      << '\n';
          }
          holds.push_back(RoadHold{.cart = stream.cart,
                                   .leg = stream.leg,
                                   .driver = {},  // the clock names it (RunTheClock)
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
        last = LastOut{.second = out,
                       .cart = stream.cart,
                       .in_second = entry.second,
                       .free_seconds = piece.free_seconds,
                       .pace = stream.pace,
                       .leg = stream.leg};
      }
    }
    if (stream.next < stream.pieces.size()) {
      Push(entry.stream, out);
    }
  }
}

void RoadTraffic::CarryHolds(const GroomPlan& earlier,
                             const std::vector<std::uint32_t>& kept_legs,
                             std::uint16_t now_minute,
                             GroomPlan& plan) {
  // By driver: a re-plan renumbers the carts, and drops those with no work left.
  std::unordered_map<std::uint32_t, std::uint32_t> cart_of_driver;
  for (std::uint32_t cart = 0; cart < plan.carts.size(); ++cart) {
    if (!plan.carts[cart].people_cart) {
      cart_of_driver.emplace(plan.carts[cart].driver.value, cart);
    }
  }
  const auto now_index = [&](std::uint32_t before) -> std::uint32_t {
    if (before >= earlier.carts.size()) {
      return kHoldOffThePlan;
    }
    const auto found = cart_of_driver.find(earlier.carts[before].driver.value);
    return found != cart_of_driver.end() ? found->second : kHoldOffThePlan;
  };
  std::vector<RoadHold> carried;
  for (const RoadHold& hold : earlier.holds) {
    RoadHold moved = hold;
    moved.cart = hold.cart == kHoldOffThePlan ? kHoldOffThePlan : now_index(hold.cart);
    if (moved.cart != kHoldOffThePlan) {
      const std::uint32_t kept = moved.cart < kept_legs.size() ? kept_legs[moved.cart] : 0;
      if (hold.leg >= kept) {
        continue;  // a leg not yet begun: the re-plan drew it anew, and its holds
      }
    } else if (hold.cart != kHoldOffThePlan) {
      // The driver's cart left the plan; his begun legs went with it, so only a hold that
      // began before the re-plan stood - the plan's own clock is past nothing else.
      if (hold.from_minute > now_minute) {
        continue;
      }
      moved.leg = kHoldOffThePlan;
    }
    if (hold.behind_cart != kNoCartAhead) {
      moved.behind_cart = now_index(hold.behind_cart);
    }
    carried.push_back(moved);
  }
  carried.insert(carried.end(), plan.holds.begin(), plan.holds.end());
  plan.holds = std::move(carried);
}

}  // namespace core
