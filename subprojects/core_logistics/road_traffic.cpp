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
                           std::int32_t start_second,
                           bool replay) {
  if (!rules_.following_on || index_ == nullptr || !(hours_per_km > 0.0F)) {
    return;
  }
  const Route way = index_->Way(mode, from, to);
  Stream stream{.cart = cart, .leg = leg, .replay = replay, .pieces = {}, .next = 0};
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
      piece.to_chainage_m = part.to_chainage_m;
      const std::uint32_t row = FindRow(*roads_, part.road);
      piece.clamps = row != kNoRow && !Overtakes(roads_->rows[row].surface);
      const std::vector<RoadEdge>& edges = index_->Graph().edges;
      piece.no_pass = part.edge < edges.size() && edges[part.edge].bridge ? RoadNoPass::kBridge
                                                                          : RoadNoPass::kNone;
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

bool RoadTraffic::OncomingFree(
    const Piece& piece, float low, float high, double from_second, double to_second) const {
  const std::uint64_t key =
      (static_cast<std::uint64_t>(piece.edge) * 2U) + (piece.forward ? 0U : 1U);
  const auto found = recent_.find(key);
  if (found == recent_.end()) {
    return true;
  }
  const Recent& oncoming = found->second;
  for (std::size_t at = 0; at < oncoming.count; ++at) {
    const Passage& other = oncoming.passages[at];
    // Its positions run the other way: in this direction it covers [-to_m, -from_m].
    const float other_low = -other.to_m;
    const float other_high = -other.from_m;
    const bool meets_in_space = other_low < high && low < other_high;
    const bool meets_in_time = static_cast<double>(other.in_second) < to_second &&
                               from_second < static_cast<double>(other.out_second);
    if (meets_in_space && meets_in_time) {
      return false;
    }
  }
  return true;
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
    // A piece of no length (a way that touches the edge at a point) passes nobody.
    if (piece.edge != kNoRoadEdge && piece.to_chainage_m != piece.chainage_m) {
      ++crossings_;
      const std::uint64_t key =
          (static_cast<std::uint64_t>(piece.edge) * 2U) + (piece.forward ? 1U : 0U);
      Recent& recent = recent_[key];
      const float sign = piece.forward ? 1.0F : -1.0F;
      Passage own{.cart = stream.cart,
                  .from_m = sign * piece.chainage_m,
                  .to_m = sign * piece.to_chainage_m,
                  .in_second = entry.second,
                  .out_second = own_out};
      // FOLLOWING: on dirt and gravel nobody passes the one ahead of him. Ahead means ON THE
      // STRETCH BOTH TRAVEL: behind him where it begins, and reaching its end before him.
      // The wait is what it takes to reach that end with him; the rest of the piece is at the
      // own pace (named: he then follows at the leader's pace only on the shared stretch).
      std::int32_t held = 0;
      std::uint32_t ahead = kNoCartAhead;
      float shared_low = 0.0F;
      float shared_high = 0.0F;
      double leader_leaves = 0.0;
      for (std::size_t at = 0; piece.clamps && at < recent.count; ++at) {
        const Passage& other = recent.passages[at];
        const float low = std::max(own.from_m, other.from_m);
        const float high = std::min(own.to_m, other.to_m);
        if (other.cart == stream.cart || !(high > low)) {
          continue;
        }
        if (own.SecondAt(low) < other.SecondAt(low)) {
          continue;  // he is ahead where they meet: nobody to follow
        }
        const auto wait =
            static_cast<std::int32_t>(std::lround(other.SecondAt(high) - own.SecondAt(high)));
        if (wait > held) {
          held = wait;
          ahead = other.cart;
          shared_low = low;
          shared_high = high;
          leader_leaves = other.SecondAt(high);
        }
      }
      // OVERTAKING (roads §8, Alex #2443): where the road allows it, a cart that has been behind
      // the slower one overtake_after_seconds passes him if the oncoming lane on the shared
      // stretch is free while it is there; on a bridge it waits him out.
      if (held > rules_.overtake_after_seconds && piece.no_pass == RoadNoPass::kNone &&
          OncomingFree(piece, shared_low, shared_high, own.SecondAt(shared_low), leader_leaves)) {
        held = rules_.overtake_after_seconds;
      }
      {
        if (held >= rules_.hold_min_seconds) {
          out = own_out + held;
        }
        // A REPLAYED leg (begun before a re-plan) waits as it waited, but its hold is the
        // earlier plan's and its time is in its arrival already: nothing new is recorded.
        if (held >= rules_.hold_min_seconds && !stream.replay) {
          holds.push_back(RoadHold{.cart = stream.cart,
                                   .leg = stream.leg,
                                   .driver = {},  // the clock names it (RunTheClock)
                                   .cause = RoadHoldCause::kFollowing,
                                   .road = piece.road,
                                   .chainage_m = piece.chainage_m,
                                   .from_minute = MinuteOf(own_out),
                                   .to_minute = MinuteOf(out),
                                   .behind_cart = ahead,
                                   .rank = 1});
          delays.push_back(LegDelay{.cart = stream.cart, .leg = stream.leg, .seconds = held});
        }
      }
      own.out_second = out;
      recent.passages[recent.next] = own;
      recent.next = (recent.next + 1) % kRecentPassages;
      recent.count = std::min(recent.count + 1, kRecentPassages);
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
