// The groom's plan of the day (logistics_plan.h).

#include "logistics_plan.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/day_window.h"
#include "core_common/geometry.h"
#include "core_common/haul.h"
#include "core_common/horse_yard_road.h"
#include "core_common/labor_state.h"
#include "core_common/logistics_rules.h"
#include "core_common/logistics_state.h"
#include "core_common/quantities.h"
#include "core_common/road_route.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"

namespace core {
namespace {

/// The plan's clock steps five game minutes: fine enough that two carts on
/// one load share it by the minute, coarse enough to stay cheap (a day is
/// some 150 steps of a few dozen carts).
constexpr float kStepHours = 1.0F / 12.0F;

/// An open task: its row, level and the day it has waited from.
struct OpenTask {
  std::uint32_t row = 0;
  std::uint8_t level = 0;
  SimDay aged_from_day = 0;
  Vec2 place;
  bool logs = false;  ///< carted by the log cart (a stand's logs, the district's lot)
};

/// The open tasks — not paused, a seam left — by level, the longest waiting
/// first, then the row: the plan's order, repeatable.
std::vector<OpenTask> OpenTasks(const WorldState& world) {
  std::vector<OpenTask> open;
  for (std::uint32_t row = 0; row < world.logistics_tasks.rows.size(); ++row) {
    const LogisticsTaskRow& task = world.logistics_tasks.rows[row];
    if (task.paused) {
      continue;
    }
    const WorkAssignment work = HaulingWorkOf(task);
    const float* const seam = WorkSeamOf(world, work);
    Vec2 place;
    if (seam == nullptr || !(*seam > 0.0F) || !WorkPlaceOf(world, work, place)) {
      continue;
    }
    open.push_back(OpenTask{.row = row,
                            .level = static_cast<std::uint8_t>(task.level),
                            .aged_from_day = task.aged_from_day,
                            .place = place,
                            .logs = task.load_kind == LogisticsLoadKind::kStandLogs ||
                                    task.load_kind == LogisticsLoadKind::kDistrictLot});
  }
  std::ranges::stable_sort(open, [](const OpenTask& left, const OpenTask& right) {
    if (left.level != right.level) {
      return left.level < right.level;
    }
    if (left.aged_from_day != right.aged_from_day) {
      return left.aged_from_day < right.aged_from_day;
    }
    return left.row < right.row;
  });
  return open;
}

/// The task row whose load this work hauls, or kNoRow.
std::uint32_t TaskOfWork(const WorldState& world, const WorkAssignment& work) {
  for (std::uint32_t row = 0; row < world.logistics_tasks.rows.size(); ++row) {
    if (WorkServesTask(work, world.logistics_tasks.rows[row])) {
      return row;
    }
  }
  return kNoRow;
}

bool IsLogs(const LogisticsTaskRow& task) {
  return task.load_kind == LogisticsLoadKind::kStandLogs ||
         task.load_kind == LogisticsLoadKind::kDistrictLot;
}

/// A cart's chain (B3): its morning load, then the open tasks it can reach,
/// by level, each level's ring turned one for every cart before it.
std::vector<std::uint32_t> CartChain(
    const LogisticsConfig& config,
    const WorldState& world,
    const std::vector<OpenTask>& open,
    const std::array<std::vector<std::uint32_t>, kLogisticsLevelCount>& rings,
    std::array<std::uint32_t, kLogisticsLevelCount>& turn,
    std::uint32_t first,
    Vec2 origin) {
  std::vector<std::uint32_t> chain;
  if (first != kNoRow) {
    chain.push_back(first);
  }
  const float hours_per_km = static_cast<float>(kClockScale) / config.harness_speed_kmh;
  for (std::uint32_t level = 0; level < kLogisticsLevelCount; ++level) {
    const std::vector<std::uint32_t>& ring = rings[level];
    if (ring.empty()) {
      continue;
    }
    const std::uint32_t start = turn[level] % static_cast<std::uint32_t>(ring.size());
    ++turn[level];
    for (std::uint32_t step = 0; step < ring.size() && chain.size() < kMaxChainLoads; ++step) {
      const OpenTask& next = open[ring[(start + step) % ring.size()]];
      if (next.row == first) {
        continue;
      }
      // THE REACH: the ride from the cart's first load within the road
      // limit — the morning's own question (assignment.h, the road rule).
      const TravelMode mode = next.logs ? TravelMode::kLogCart : TravelMode::kCart;
      if (RoadKm(world, mode, origin, next.place) * hours_per_km > config.travel_limit_hours) {
        continue;
      }
      chain.push_back(next.row);
    }
  }
  return chain;
}

/// A carrier on foot's chain (B4b; boss [11], default 1): his morning load,
/// then the NEAREST open loads of its level within a walk of it — never a
/// log (a log is not carried on foot, haul.h) and never a far one.
std::vector<std::uint32_t> WalkerChain(const LogisticsConfig& config,
                                       const WorldState& world,
                                       const std::vector<OpenTask>& open,
                                       std::uint32_t first,
                                       Vec2 origin) {
  std::vector<std::uint32_t> chain = {first};
  const std::uint8_t level = static_cast<std::uint8_t>(world.logistics_tasks.rows[first].level);
  const float hours_per_km = static_cast<float>(kClockScale) / config.walk_speed_kmh;
  std::vector<std::pair<float, std::uint32_t>> near;
  for (const OpenTask& task : open) {
    if (task.row == first || task.level != level || task.logs) {
      continue;
    }
    const float hours = RoadKm(world, TravelMode::kWalk, origin, task.place) * hours_per_km;
    if (hours <= config.travel_limit_hours) {
      near.emplace_back(hours, task.row);
    }
  }
  std::ranges::stable_sort(
      near, [](const auto& left, const auto& right) { return left.first < right.first; });
  for (const auto& [hours, row] : near) {
    if (chain.size() >= kMaxChainLoads) {
      break;
    }
    chain.push_back(row);
  }
  return chain;
}

/// One mover of the plan's clock: a cart or a carrier on foot.
struct Mover {
  enum class Phase : std::uint8_t { kNotOut, kRiding, kCarting, kDone };

  std::size_t cart = 0;  ///< Its index in the plan's carts.
  std::vector<std::uint32_t> chain;
  std::size_t next = 0;  ///< The chain's next load to go to.
  bool on_foot = false;
  float hours_per_km = 0.0F;
  float drain = 0.0F;     ///< Seam-days one step of carting takes.
  float sets_out = 0.0F;  ///< Hours from midnight.
  Vec2 home;              ///< Where its day starts and ends.
  Vec2 at;                ///< The last place it reached.
  Phase phase = Phase::kNotOut;
  float ride_left = 0.0F;  ///< Hours of the way still ahead.
  float ride_home = 0.0F;  ///< Hours home from where it carts.
  std::uint32_t task = kNoRow;
};

/// The tick that contains the moment `hours` from today's midnight.
Tick TickAt(SimDay day, float hours) {
  return (static_cast<Tick>(day) * kTicksPerDay) + HourOfDayMoment(hours);
}

/// The hour of its day a tick begins at, as hours from midnight.
float HoursOfTick(Tick tick) {
  return static_cast<float>(tick % kTicksPerDay);
}

/// The hours of the way between two places for this mover.
float RideHours(
    const WorldState& world, const Mover& mover, const LogisticsTaskRow* task, Vec2 from, Vec2 to) {
  TravelMode mode = TravelMode::kCart;
  if (mover.on_foot) {
    mode = TravelMode::kWalk;
  } else if (task != nullptr && IsLogs(*task)) {
    mode = TravelMode::kLogCart;
  }
  return RoadKm(world, mode, from, to) * mover.hours_per_km;
}

/// A load leg of no length at `hours`: a load of the chain the clock did not
/// send the mover to — carted by others before it came, or past the evening.
/// It stays in the legs IN ITS PLACE OF THE CHAIN, because the clock is an
/// estimate and the labour hour follows the chain by the seams (B4): a load
/// the estimate gave to another cart may still lie when this one is free.
void KeepInTheChain(const WorldState& world,
                    GroomPlan& plan,
                    const Mover& mover,
                    std::uint32_t task_row,
                    SimDay day,
                    float hours) {
  const LogisticsTaskRow& task = world.logistics_tasks.rows[task_row];
  Vec2 place = mover.at;
  WorkPlaceOf(world, HaulingWorkOf(task), place);
  plan.carts[mover.cart].legs.push_back(CartLeg{.from = place,
                                                .to = place,
                                                .task = world.logistics_tasks.row_ids[task_row],
                                                .depart = TickAt(day, hours),
                                                .arrive = TickAt(day, hours),
                                                .riders = {}});
}

/// Sends the mover on to the next load of its chain with carting left, or
/// home when there is none or `evening`; `hours` is now.
void GoOn(const WorldState& world,
          const std::vector<float>& remaining,
          GroomPlan& plan,
          Mover& mover,
          SimDay day,
          float hours,
          float sunset,
          bool evening = false) {
  std::vector<CartLeg>& legs = plan.carts[mover.cart].legs;
  while (mover.next < mover.chain.size()) {
    const std::uint32_t next = mover.chain[mover.next];
    if (!evening && remaining[next] > 0.0F) {
      // THE WAY THERE AND HOME MUST FIT THE LIGHT, with a step of carting
      // between (0.37.181): until then the clock sent a cart to its next load
      // whatever the hour, and its leg home came after sunset in every
      // village of 0.37.180's canon (core-legsprobe2, late_h).
      const LogisticsTaskRow& candidate = world.logistics_tasks.rows[next];
      Vec2 place = mover.at;
      WorkPlaceOf(world, HaulingWorkOf(candidate), place);
      const float there = RideHours(world, mover, &candidate, mover.at, place);
      const float back = RideHours(world, mover, &candidate, place, mover.home);
      if (hours + there + kStepHours + back < sunset) {
        break;
      }
      evening = true;
    }
    KeepInTheChain(world, plan, mover, next, day, hours);
    ++mover.next;
  }
  if (mover.next >= mover.chain.size()) {
    const float ride = RideHours(world, mover, nullptr, mover.at, mover.home);
    legs.push_back(CartLeg{.from = mover.at,
                           .to = mover.home,
                           .task = LogisticsTaskId{},
                           .depart = TickAt(day, hours),
                           .arrive = TickAt(day, hours + ride),
                           .riders = {}});
    mover.phase = Mover::Phase::kDone;
    return;
  }
  mover.task = mover.chain[mover.next];
  ++mover.next;
  const LogisticsTaskRow& task = world.logistics_tasks.rows[mover.task];
  Vec2 place = mover.at;
  WorkPlaceOf(world, HaulingWorkOf(task), place);
  mover.ride_left = RideHours(world, mover, &task, mover.at, place);
  mover.ride_home = RideHours(world, mover, &task, place, mover.home);
  legs.push_back(CartLeg{.from = mover.at,
                         .to = place,
                         .task = LogisticsTaskId{},
                         .depart = TickAt(day, hours),
                         .arrive = TickAt(day, hours + mover.ride_left),
                         .riders = {}});
  mover.at = place;
  mover.phase = Mover::Phase::kRiding;
}

/// THE CLOCK (B4b): every mover steps through the day together, so two carts
/// on one load share its seam by the minute. A cart out on its way arrives;
/// one carting drains its load's seam by its step; a load carted sends it on;
/// the evening — the ride home no longer fits the light — sends it home from
/// where it is. The seam itself is not touched: this is the estimate the legs'
/// ticks are written from (logistics_state.h).
void RunTheClock(const WorldState& world,
                 std::vector<float> remaining,
                 GroomPlan& plan,
                 std::vector<Mover>& movers,
                 const DayWindow& window) {
  const SimDay day = world.calendar.day;
  for (float hours = window.sunrise; hours < window.sunset; hours += kStepHours) {
    for (Mover& mover : movers) {
      std::vector<CartLeg>& legs = plan.carts[mover.cart].legs;
      switch (mover.phase) {
        case Mover::Phase::kNotOut:
          if (hours >= mover.sets_out) {
            GoOn(world, remaining, plan, mover, day, hours, window.sunset);
          }
          break;
        case Mover::Phase::kRiding:
          mover.ride_left -= kStepHours;
          if (mover.ride_left <= 0.0F) {
            legs.back().arrive = TickAt(day, hours);
            legs.push_back(CartLeg{.from = mover.at,
                                   .to = mover.at,
                                   .task = world.logistics_tasks.row_ids[mover.task],
                                   .depart = TickAt(day, hours),
                                   .arrive = TickAt(day, hours),
                                   .riders = {}});
            mover.phase = Mover::Phase::kCarting;
          }
          break;
        case Mover::Phase::kCarting:
          if (hours + kStepHours + mover.ride_home >= window.sunset) {
            legs.back().arrive = TickAt(day, hours);
            GoOn(world, remaining, plan, mover, day, hours, window.sunset, /*evening=*/true);
            break;
          }
          remaining[mover.task] -= mover.drain;
          legs.back().arrive = TickAt(day, hours + kStepHours);
          if (!(remaining[mover.task] > 0.0F)) {
            GoOn(world, remaining, plan, mover, day, hours + kStepHours, window.sunset);
          }
          break;
        case Mover::Phase::kDone:
          break;
      }
    }
  }
  // THE NIGHT: one still on its way when the light ends goes home from where
  // the way was to take it — it does not set out again (assignment.h, the
  // road rule: no work after sunset).
  for (Mover& mover : movers) {
    if (mover.phase == Mover::Phase::kRiding || mover.phase == Mover::Phase::kCarting) {
      if (mover.phase == Mover::Phase::kRiding) {
        // The load it was riding to: in the chain, not reached; the way is
        // cut short at sunset.
        plan.carts[mover.cart].legs.back().arrive = TickAt(day, window.sunset);
        KeepInTheChain(world, plan, mover, mover.task, day, window.sunset);
      }
      GoOn(world, remaining, plan, mover, day, window.sunset, window.sunset, /*evening=*/true);
    }
  }
}

}  // namespace

GroomPlan BuildGroomPlan(const LogisticsConfig& config,
                         const WorldState& world,
                         LogisticsTally& tally,
                         const GroomPlan* earlier) {
  GroomPlan plan;
  plan.day = world.calendar.day;
  const Tick now = world.calendar.tick;
  for (const LogisticsTaskRow& task : world.logistics_tasks.rows) {
    const auto level = static_cast<std::size_t>(task.level);
    if (level < tally.tasks.size()) {
      ++tally.tasks[level];
    }
  }
  const std::vector<OpenTask> open = OpenTasks(world);
  // Each level's ring: the open tasks of the level, in the plan's order, and
  // where the next cart starts on it.
  std::array<std::vector<std::uint32_t>, kLogisticsLevelCount> rings;
  for (std::uint32_t index = 0; index < open.size(); ++index) {
    if (open[index].level < kLogisticsLevelCount) {
      rings[open[index].level].push_back(index);
    }
  }
  std::array<std::uint32_t, kLogisticsLevelCount> turn = {};
  // The seams as the morning left them, in seam-days, by task row.
  std::vector<float> remaining(world.logistics_tasks.rows.size(), 0.0F);
  for (const OpenTask& task : open) {
    remaining[task.row] = *WorkSeamOf(world, HaulingWorkOf(world.logistics_tasks.rows[task.row]));
  }
  const DayWindow window = SolarWindow(world.weather.daylight_hours);
  const float harness_hours_per_km = static_cast<float>(kClockScale) / config.harness_speed_kmh;
  const float walk_hours_per_km = static_cast<float>(kClockScale) / config.walk_speed_kmh;
  const float cart_drain = kStepHours / config.standard_day_hours;
  const float walker_drain =
      cart_drain * (SettlementHasCarts(world, config.horse_kind)
                        ? WalkerShareOfCartDay(GramsFromKilograms(config.carry_kg_adult),
                                               GramsFromKilograms(config.cart_load_kg),
                                               walk_hours_per_km,
                                               harness_hours_per_km)
                        : 1.0F);
  Vec2 yard;
  const bool stabled = HorseYardPositionOf(world, config.horse_kind, yard);

  std::vector<Mover> movers;
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const ResidentRow& person = world.residents.rows[row];
    const WorkAssignment& work = person.work;
    Vec2 house;
    Vec2 place;
    if (!HomePositionOf(world, person.family, house) || !WorkPlaceOf(world, work, place)) {
      continue;
    }
    const ResidentId driver = world.residents.row_ids[row];
    // THE PEOPLE'S CART (B4b; 0.37.168): out to the work with its riders,
    // back in the evening — not in the clock: it carries no load.
    if (TakesThePeoplesCart(work.kind) && work.rides_horse != 0) {
      const Vec2 start = stabled ? yard : house;
      const float sets_out =
          window.sunrise +
          (stabled ? RoadKm(world, TravelMode::kWalk, house, yard) * walk_hours_per_km : 0.0F);
      const float ride = RoadKm(world, TravelMode::kCart, start, place) * harness_hours_per_km;
      CartPlan cart{.driver = driver, .people_cart = true, .on_foot = false, .legs = {}};
      cart.legs.push_back(CartLeg{.from = start,
                                  .to = place,
                                  .task = LogisticsTaskId{},
                                  .depart = TickAt(plan.day, sets_out),
                                  .arrive = TickAt(plan.day, sets_out + ride),
                                  .riders = {}});
      cart.legs.push_back(CartLeg{.from = place,
                                  .to = start,
                                  .task = LogisticsTaskId{},
                                  .depart = TickAt(plan.day, window.sunset - ride),
                                  .arrive = TickAt(plan.day, window.sunset),
                                  .riders = {}});
      plan.carts.push_back(std::move(cart));
      continue;
    }
    if (work.kind != WorkKind::kHauling) {
      continue;
    }
    const std::uint32_t first = TaskOfWork(world, work);
    Mover mover;
    mover.cart = plan.carts.size();
    if (work.rides_horse != 0) {
      mover.chain = CartChain(config, world, open, rings, turn, first, place);
      mover.hours_per_km = harness_hours_per_km;
      mover.drain = cart_drain;
      mover.home = stabled ? yard : house;
      mover.sets_out =
          window.sunrise +
          (stabled ? RoadKm(world, TravelMode::kWalk, house, yard) * walk_hours_per_km : 0.0F);
    } else {
      if (first == kNoRow) {
        continue;  // a carrier on a load with no task is the placement's alone
      }
      mover.on_foot = true;
      mover.chain = WalkerChain(config, world, open, first, place);
      mover.hours_per_km = walk_hours_per_km;
      mover.drain = walker_drain;
      mover.home = house;
      mover.sets_out = window.sunrise;
    }
    mover.at = mover.home;
    CartPlan cart{.driver = driver, .people_cart = false, .on_foot = mover.on_foot, .legs = {}};
    // THE RE-PLAN (B5): the rest of the day from where the cart is. Every leg
    // the earlier plan had begun by now is kept as it was — a begun leg is
    // never broken (boss [11], default 3) — and the clock sets the cart out
    // again where and when the last of them ends, but not before this hour.
    if (earlier != nullptr) {
      for (const CartPlan& before : earlier->carts) {
        if (before.driver.value != driver.value || before.people_cart) {
          continue;
        }
        for (const CartLeg& leg : before.legs) {
          if (leg.depart > now) {
            break;
          }
          cart.legs.push_back(leg);
        }
        break;
      }
      const float start = std::max(mover.sets_out, HoursOfTick(now) + 1.0F);
      mover.sets_out = start;
      if (!cart.legs.empty()) {
        mover.at = cart.legs.back().to;
        mover.sets_out = std::max(start, HoursOfTick(cart.legs.back().arrive));
      }
    }
    plan.carts.push_back(std::move(cart));
    movers.push_back(std::move(mover));
  }
  RunTheClock(world, remaining, plan, movers, window);
  // THE RIDERS (B4b): whoever the labour hour seated this morning on a
  // driver's cart rides its first leg. ONCE (0.37.186): a re-plan keeps a
  // begun first leg with its riders (above), and until then this loop wrote
  // them onto it a second time — found writing the passenger's strike, which
  // reads them.
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const ResidentId of = world.residents.rows[row].work.rides_cart_of;
    if (of.value == kInvalidEntityIdValue) {
      continue;
    }
    const ResidentId rider = world.residents.row_ids[row];
    for (CartPlan& cart : plan.carts) {
      if (cart.driver.value == of.value && !cart.on_foot && !cart.legs.empty()) {
        std::vector<ResidentId>& riders = cart.legs.front().riders;
        if (std::ranges::none_of(
                riders, [rider](ResidentId seated) { return seated.value == rider.value; })) {
          riders.push_back(rider);
          ++tally.riders;
        }
        break;
      }
    }
  }
  for (const CartPlan& cart : plan.carts) {
    tally.legs += static_cast<std::uint32_t>(cart.legs.size());
    tally.carts += cart.on_foot ? 0U : 1U;
  }
  return plan;
}

}  // namespace core
