// The groom's plan of the day (logistics_plan.h).

#include "logistics_plan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/cold_housing.h"
#include "core_common/day_window.h"
#include "core_common/geometry.h"
#include "core_common/haul.h"
#include "core_common/horse_yard_road.h"
#include "core_common/labor_state.h"
#include "core_common/logistics_rules.h"
#include "core_common/logistics_state.h"
#include "core_common/quantities.h"
#include "core_common/road_graph.h"
#include "core_common/road_route.h"
#include "core_common/site_supply_queue.h"
#include "core_common/spoilage.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"
#include "leg_loads.h"
#include "porter_pairs.h"
#include "trip_pricing.h"

namespace core {
namespace {

/// The plan's clock steps five game minutes: fine enough that two carts on
/// one load share it by the minute, coarse enough to stay cheap (a day is
/// some 150 steps of a few dozen carts).
constexpr float kStepsPerHour = 12.0F;
constexpr float kStepHours = 1.0F / kStepsPerHour;

/// An open task: its row, level and the day it has waited from.
struct OpenTask {
  std::uint32_t row = 0;
  std::uint8_t level = 0;
  /// A works' supply that will house a herd facing the cold (cold_housing.h):
  /// first within its level.
  bool cold_first = false;
  /// A works' supply: its place in the sites' queue (#595, core_common/
  /// site_supply_queue.h: raised first, then the oldest start) - the first
  /// site's loads are drawn before the next site's, inside the level.
  /// kNoSupplyRank for every other load.
  std::uint32_t site_rank = kNoSupplyRank;
  SimDay aged_from_day = 0;
  /// A field heap's shelf life on its field, game days (resources.csv
  /// `spoil_days` x `field_heap_keeping_factor`); infinite for every load that
  /// does not spoil and every other kind.
  float shelf_days = std::numeric_limits<float>::infinity();
  Vec2 place;
  bool logs = false;  ///< carted by the log cart (a stand's logs, the district's lot)
  /// A load priced ON FOOT - a milk leg whose walk holds the day's trips
  /// (HerdRow::leg_on_foot): no cart's chain takes it (#1117).
  bool walked = false;
};

/// A field heap's shelf life on its field (OpenTask::shelf_days).
float ShelfDaysOf(const LogisticsConfig& config,
                  const WorldState& world,
                  const LogisticsTaskRow& task) {
  if (task.load_kind != LogisticsLoadKind::kFieldHeap) {
    return std::numeric_limits<float>::infinity();
  }
  const std::uint32_t row = FindRow(world.fields, task.field);
  if (row == kNoRow) {
    return std::numeric_limits<float>::infinity();
  }
  const ResourceId resource = world.fields.rows[row].reaped_resource;
  const float days =
      resource.value < config.spoil_days.size() ? config.spoil_days[resource.value] : 0.0F;
  // One home with labour's weighing against the snow (#1099; spoilage.h).
  return FieldHeapShelfDays(days, config.field_heap_keeping_factor);
}

/// The open tasks — not paused, a seam left — by level, the longest waiting
/// first, then the row: the plan's order, repeatable.
std::vector<OpenTask> OpenTasks(const LogisticsConfig& config, const WorldState& world) {
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
    // A works' supply whose front leg is logs straight off a stand goes by
    // the log cart too (the direct leg; IsLogs below asks the same).
    bool logs = task.load_kind == LogisticsLoadKind::kStandLogs ||
                task.load_kind == LogisticsLoadKind::kDistrictLot;
    bool cold_first = false;
    std::uint32_t site_rank = kNoSupplyRank;
    if (task.load_kind == LogisticsLoadKind::kSiteSupply) {
      const std::uint32_t unit_row = FindRow(world.units, task.unit);
      logs = unit_row != kNoRow && world.units.rows[unit_row].construction.supply_log_way != 0;
      site_rank = unit_row == kNoRow ? kNoSupplyRank : SiteSupplyRank(world, unit_row);
      // THE SUPPLY OF THE WORKS THAT WILL HOUSE A HERD FACING THE COLD first
      // within its level (cold_housing.h; econ's form (a), boss 11 October
      // 2026): ahead of every other works' supply, still behind level 0 —
      // the harvest's carting.
      cold_first = unit_row != kNoRow && WorksHouseAColdHerd(config.cold_housing, world, unit_row);
    }
    bool walked = false;
    if (task.load_kind == LogisticsLoadKind::kMilkLeg) {
      const std::uint32_t herd_row = FindRow(world.herds, task.herd);
      walked = herd_row != kNoRow && world.herds.rows[herd_row].leg_on_foot != 0;
    }
    open.push_back(OpenTask{.row = row,
                            .level = static_cast<std::uint8_t>(task.level),
                            .cold_first = cold_first,
                            .site_rank = site_rank,
                            .aged_from_day = task.aged_from_day,
                            .shelf_days = ShelfDaysOf(config, world, task),
                            .place = place,
                            .logs = logs,
                            .walked = walked});
  }
  std::ranges::stable_sort(open, [](const OpenTask& left, const OpenTask& right) {
    if (left.level != right.level) {
      return left.level < right.level;
    }
    // WHAT SPOILS SOONEST FIRST, inside the level (#1056 C4.2 (a); econ's
    // contract §C4.2; boss, the #1056 thread after [23]): a field heap of the
    // shorter shelf life on its field goes ahead - the potatoes before the
    // grain on the days both stand - in the morning plan and every re-plan
    // alike, one order for both doors. Two heaps of one resource tie: the rot
    // is memoryless (spoilage.h), so their order cannot change what rots.
    if (left.shelf_days != right.shelf_days) {
      return left.shelf_days < right.shelf_days;
    }
    if (left.cold_first != right.cold_first) {
      return left.cold_first;
    }
    // THE SITES' QUEUE (#595): the first site's loads before the next site's,
    // inside the level; every other load keeps its place by age.
    if (left.site_rank != right.site_rank) {
      return left.site_rank < right.site_rank;
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

/// Whether the task's load goes by the log cart: a stand's logs, the
/// district's lot — and a works' supply whose front leg is logs straight off
/// a stand (the direct leg; ConstructionState::supply_log_way), which is a
/// stand's load on its way to another place.
bool IsLogs(const WorldState& world, const LogisticsTaskRow& task) {
  if (task.load_kind == LogisticsLoadKind::kSiteSupply) {
    const std::uint32_t row = FindRow(world.units, task.unit);
    return row != kNoRow && world.units.rows[row].construction.supply_log_way != 0;
  }
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
  // SL: the chain's reach at the day's own gear (the sledge on snow).
  const float hours_per_km =
      static_cast<float>(kClockScale) / CartGearToday(world, CartSpecsOf(config)).empty_kmh;
  for (std::uint32_t level = 0; level < kLogisticsLevelCount; ++level) {
    const std::vector<std::uint32_t>& ring = rings[level];
    if (ring.empty()) {
      continue;
    }
    const std::uint32_t start = turn[level] % static_cast<std::uint32_t>(ring.size());
    ++turn[level];
    for (std::uint32_t step = 0; step < ring.size() && chain.size() < kMaxChainLoads; ++step) {
      const OpenTask& next = open[ring[(start + step) % ring.size()]];
      // A LOAD PRICED ON FOOT IS NO CART'S (#1117; boss, the #1056 thread
      // [119]): the milk leg's cans are carried by the walkers its price
      // sent, and a cart chaining it drew a trip no milk rode - 3 290 cart
      // plan-hours on the canon's year 1 - and moved the timetable riders
      // and passengers read.
      if (next.row == first || next.walked) {
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
  float hours_per_km = 0.0F;         ///< Empty: the trot's pace, or the walk's.
  float loaded_hours_per_km = 0.0F;  ///< Loaded (#570, 2a): cart_loaded's pace, or the walk's.
  float drain = 0.0F;                ///< Seam-days one step of carting takes.
  /// A carrier on foot: `drain` by the load of each task of `chain` (#671; a
  /// bundle of hay or a sack by its form). Empty for a cart: one number.
  std::vector<float> chain_drain;
  float sets_out = 0.0F;  ///< Hours from midnight.
  Vec2 home;              ///< Where its day starts and ends.
  Vec2 at;                ///< The last place it reached.
  Phase phase = Phase::kNotOut;
  float ride_left = 0.0F;  ///< Hours of the way still ahead.
  float ride_home = 0.0F;  ///< Hours home from where it carts (its receiver, on trips).
  std::uint32_t task = kNoRow;
  // THE TRIPS (stage В, F3 (A)) of the load it carts: drawn when the load has
  // a receiver (WorkReceiverOf) away from it and the mover has a cart.
  bool trips = false;
  Vec2 load;                       ///< Where the load lies.
  Vec2 receiver;                   ///< Where its trips go.
  float trip_loaded_hours = 0.0F;  ///< The loaded half, load to receiver, at the loaded pace.
  float trip_empty_hours = 0.0F;   ///< The way back, empty, at the empty pace.
  float half_left = 0.0F;          ///< Hours of the current half-trip still ahead.
  bool loaded_half = false;        ///< On the loaded half (to the receiver).
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
float RideKm(
    const WorldState& world, const Mover& mover, const LogisticsTaskRow* task, Vec2 from, Vec2 to) {
  TravelMode mode = TravelMode::kCart;
  if (mover.on_foot) {
    mode = TravelMode::kWalk;
  } else if (task != nullptr && IsLogs(world, *task)) {
    mode = TravelMode::kLogCart;
  }
  return RoadKm(world, mode, from, to);
}

/// The way EMPTY, at the mover's empty pace.
float RideHours(
    const WorldState& world, const Mover& mover, const LogisticsTaskRow* task, Vec2 from, Vec2 to) {
  return RideKm(world, mover, task, from, to) * mover.hours_per_km;
}

/// THE PRICE'S FACTS (#1056 C3; core_common/trip_terms.h): the trip terms of a
/// task a cart prices at whole trips - written only under the planned model
/// (trip_terms_writer.h) - or nullptr: no terms, or a carrier on foot, whose
/// trips are his back's and keep the walk's drawing.
const TaskTripTerms* PlannedTerms(const Mover& mover, const LogisticsTaskRow& task) {
  return !mover.on_foot && task.trip_terms.trip_hours > 0.0F ? &task.trip_terms : nullptr;
}

/// Where the mover rides to cart `task`: the price's load point when the task
/// has one (a field heap's HEAP POINT, ruling 2 - not the field's centre),
/// else the work's place (work_seam.h, WorkPlaceOf). `place` is left as it
/// came when neither knows one.
void LoadPlaceOf(const WorldState& world,
                 const Mover& mover,
                 const LogisticsTaskRow& task,
                 Vec2& place) {
  if (const TaskTripTerms* const terms = PlannedTerms(mover, task)) {
    place = terms->load_point;
    return;
  }
  WorkPlaceOf(world, HaulingWorkOf(task), place);
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
      LoadPlaceOf(world, mover, candidate, place);
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
  if (mover.next < mover.chain_drain.size()) {
    mover.drain = mover.chain_drain[mover.next];
  }
  ++mover.next;
  const LogisticsTaskRow& task = world.logistics_tasks.rows[mover.task];
  Vec2 place = mover.at;
  LoadPlaceOf(world, mover, task, place);
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

/// THE TRIPS OF THE LOAD JUST REACHED (stage В, F3 (A)): true when they are
/// drawn — a cart (not a carrier on foot), a receiver written for the load
/// and away from it. Sets the mover's trip fields and its ride home from the
/// receiver, where its carting ends.
bool BeginTrips(const WorldState& world, Mover& mover) {
  mover.trips = false;
  // A CARRIER ON FOOT'S TRIPS ARE DRAWN TOO since 0.37.240 (#671, the second
  // delivery): the porter's record reads his loaded and empty legs
  // (porter_leg.h), and until then he stood at the load all day.
  if (mover.task == kNoRow) {
    return false;
  }
  const LogisticsTaskRow& task = world.logistics_tasks.rows[mover.task];
  Vec2 receiver;
  if (!WorkReceiverOf(world, HaulingWorkOf(task), receiver)) {
    return false;
  }
  const float one_way_km = RideKm(world, mover, &task, mover.at, receiver);
  if (!(one_way_km > 0.0F)) {
    return false;  // the receiver at the load: nothing to draw
  }
  mover.trips = true;
  mover.load = mover.at;
  mover.receiver = receiver;
  mover.ride_home = RideHours(world, mover, &task, receiver, mover.home);
  // THE PRICE's OWN TRIP (#1056 C3; trip_terms.h): under the planned model the
  // halves are the ones the seam was priced by - the road bed, the gear and the
  // heap point included (ruling 2) - so a whole trip drawn drains a whole trip
  // priced, and the drawn trips ARE the price.
  if (const TaskTripTerms* const terms = PlannedTerms(mover, task)) {
    mover.trip_loaded_hours = terms->trip_loaded_hours;
    const float empty = terms->trip_hours - terms->trip_loaded_hours;
    mover.trip_empty_hours = empty > 0.0F ? empty : 0.0F;
    return true;
  }
  // THE LOADED HALF AT THE LOADED PACE (#570, 2a), the way back at the
  // empty one — as the seam is priced (haul.h, RateOverKm). The loaded half
  // carries the loading at its start and the unloading at its end (#844):
  // the same two pauses RateOverKm adds, so the plan and the seam agree.
  mover.trip_loaded_hours = (one_way_km * mover.loaded_hours_per_km) + (2.0F * kLoadingPauseHours);
  mover.trip_empty_hours = one_way_km * mover.hours_per_km;
  return true;
}

/// One half of a trip from `hours`: loaded, the load to its receiver; or the
/// way back, empty.
void PushTripHalf(
    const WorldState& world, GroomPlan& plan, Mover& mover, SimDay day, float hours, bool loaded) {
  plan.carts[mover.cart].legs.push_back(CartLeg{
      .from = loaded ? mover.load : mover.receiver,
      .to = loaded ? mover.receiver : mover.load,
      .task = world.logistics_tasks.row_ids[mover.task],
      .loaded = static_cast<std::uint8_t>(loaded ? 1U : 0U),
      .depart = TickAt(day, hours),
      .arrive = TickAt(day, hours + (loaded ? mover.trip_loaded_hours : mover.trip_empty_hours)),
      .riders = {}});
  mover.loaded_half = loaded;
  mover.half_left = loaded ? mover.trip_loaded_hours : mover.trip_empty_hours;
}

/// THE LOADED HALF, OR A POOLED ONE (#1056 C3 body 5, ruling 3): a field
/// heap's LAST trip under the planned model, a part-load, takes a second
/// field's whole leftover when one fits the same load and lies on the way
/// (trip_pricing.h, PoolPartnerOf): loaded to the second heap, loaded on to the
/// receiver, both legs naming the second load (CartLeg::pooled_task), at the
/// first load's priced paces (PooledTripOf). The second load is the clock's
/// no more - no other cart rides to it today. Any other trip: PushTripHalf.
void PushLoadedHalf(const LogisticsConfig& config,
                    const WorldState& world,
                    std::vector<float>& remaining,
                    GroomPlan& plan,
                    Mover& mover,
                    SimDay day,
                    float hours) {
  const LogisticsTaskRow& task = world.logistics_tasks.rows[mover.task];
  const TaskTripTerms* const terms = PlannedTerms(mover, task);
  const bool last_trip =
      terms != nullptr && config.standard_day_hours > 0.0F &&
      remaining[mover.task] <= (terms->trip_hours / config.standard_day_hours) * 1.0001F;
  if (!last_trip || task.load_kind != LogisticsLoadKind::kFieldHeap) {
    PushTripHalf(world, plan, mover, day, hours, /*loaded=*/true);
    return;
  }
  const std::uint32_t trips = WholeTripsFor(terms->grams_left, terms->cart_load);
  const Grams leftover =
      trips == 0 ? 0 : terms->grams_left - (static_cast<Grams>(trips - 1U) * terms->cart_load);
  const std::uint32_t second =
      PoolPartnerOf(config, world, mover.task, leftover, mover.receiver, remaining);
  PooledTrip pooled;
  if (second == kNoRow || !PooledTripOf(world,
                                        *terms,
                                        world.logistics_tasks.rows[second].trip_terms.load_point,
                                        mover.receiver,
                                        pooled)) {
    PushTripHalf(world, plan, mover, day, hours, /*loaded=*/true);
    return;
  }
  const Vec2 second_load = world.logistics_tasks.rows[second].trip_terms.load_point;
  const LogisticsTaskId second_id = world.logistics_tasks.row_ids[second];
  std::vector<CartLeg>& legs = plan.carts[mover.cart].legs;
  legs.push_back(CartLeg{.from = mover.load,
                         .to = second_load,
                         .task = world.logistics_tasks.row_ids[mover.task],
                         .loaded = 1U,
                         .depart = TickAt(day, hours),
                         .arrive = TickAt(day, hours + pooled.to_second_hours),
                         .riders = {},
                         .pooled_task = second_id});
  legs.push_back(
      CartLeg{.from = second_load,
              .to = mover.receiver,
              .task = world.logistics_tasks.row_ids[mover.task],
              .loaded = 1U,
              .depart = TickAt(day, hours + pooled.to_second_hours),
              .arrive = TickAt(day, hours + pooled.to_second_hours + pooled.to_receiver_hours),
              .riders = {},
              .pooled_task = second_id});
  remaining[second] = 0.0F;
  mover.loaded_half = true;
  mover.half_left = pooled.to_second_hours + pooled.to_receiver_hours;
}

/// One step of a mover on its trips: the seam drains at the norm, as at the
/// load (the drawing changes, not the carting); a half that ends turns the
/// cart. At the receiver: the load carted, or the next round not fitting the
/// light — on from there (the next load, or home); else back. At the load:
/// the load carted — on from there; the next trip not fitting the light —
/// home from there; else loaded again.
void StepTrips(const LogisticsConfig& config,
               const WorldState& world,
               std::vector<float>& remaining,
               GroomPlan& plan,
               Mover& mover,
               SimDay day,
               float hours,
               float sunset) {
  remaining[mover.task] -= mover.drain;
  mover.half_left -= kStepHours;
  if (mover.half_left > 0.0F) {
    return;
  }
  const float now = hours + kStepHours;
  plan.carts[mover.cart].legs.back().arrive = TickAt(day, now);
  const bool carted = !(remaining[mover.task] > 0.0F);
  if (mover.loaded_half) {
    mover.at = mover.receiver;
    const bool round_fits =
        now + mover.trip_empty_hours + mover.trip_loaded_hours + mover.ride_home < sunset;
    if (carted || !round_fits) {
      mover.trips = false;
      GoOn(world, remaining, plan, mover, day, now, sunset, /*evening=*/!carted);
      return;
    }
    PushTripHalf(world, plan, mover, day, now, /*loaded=*/false);
    return;
  }
  mover.at = mover.load;
  if (carted || now + mover.trip_loaded_hours + mover.ride_home >= sunset) {
    mover.trips = false;
    GoOn(world, remaining, plan, mover, day, now, sunset, /*evening=*/!carted);
    return;
  }
  PushLoadedHalf(config, world, remaining, plan, mover, day, now);
}

/// THE CLOCK (B4b): every mover steps through the day together, so two carts
/// on one load share its seam by the minute. A cart out on its way arrives;
/// one carting drains its load's seam by its step; a load carted sends it on;
/// the evening — the ride home no longer fits the light — sends it home from
/// where it is. The seam itself is not touched: this is the estimate the legs'
/// ticks are written from (logistics_state.h).
void RunTheClock(const LogisticsConfig& config,
                 const WorldState& world,
                 std::vector<float> remaining,
                 GroomPlan& plan,
                 std::vector<Mover>& movers,
                 const DayWindow& window) {
  const SimDay day = world.calendar.day;
  // THE STEP IS COUNTED, NOT SUMMED (the Fable review of 11 October 2026,
  // item 12): a sum of some 150 twelfths rounded differently under another
  // compiler's float contraction could move a leg across an hour, and a save
  // re-planned on the host would part from the VM's. A quotient of a whole
  // count is the same everywhere (no multiply-add to fuse).
  for (std::uint32_t step = 0;; ++step) {
    const float hours = window.sunrise + (static_cast<float>(step) / kStepsPerHour);
    if (!(hours < window.sunset)) {
      break;
    }
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
            mover.phase = Mover::Phase::kCarting;
            if (BeginTrips(world, mover)) {
              // A TRIP THE LIGHT CANNOT FINISH IS NOT BEGUN: there, and home
              // from the receiver (econ's (A) gate, 0718109).
              if (hours + mover.trip_loaded_hours + mover.ride_home >= window.sunset) {
                GoOn(world, remaining, plan, mover, day, hours, window.sunset, /*evening=*/true);
                break;
              }
              PushLoadedHalf(config, world, remaining, plan, mover, day, hours);
              break;
            }
            legs.push_back(CartLeg{.from = mover.at,
                                   .to = mover.at,
                                   .task = world.logistics_tasks.row_ids[mover.task],
                                   .depart = TickAt(day, hours),
                                   .arrive = TickAt(day, hours),
                                   .riders = {}});
          }
          break;
        case Mover::Phase::kCarting:
          if (mover.trips) {
            StepTrips(config, world, remaining, plan, mover, day, hours, window.sunset);
            break;
          }
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
  const std::vector<OpenTask> open = OpenTasks(config, world);
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
  // THE CART OR THE SLEDGE (SL; cart_gear.h): the day's gear, chosen at the
  // stable for the whole day - the sledge's 8 km/h both ways on snow.
  const CartGear gear = CartGearToday(world, CartSpecsOf(config));
  const float harness_hours_per_km = static_cast<float>(kClockScale) / gear.empty_kmh;
  const float loaded_hours_per_km = static_cast<float>(kClockScale) / gear.loaded_kmh;
  const float walk_hours_per_km = static_cast<float>(kClockScale) / config.walk_speed_kmh;
  const float cart_drain = kStepHours / config.standard_day_hours;
  // A WALKER'S STEP BY HIS LOAD'S FORM (#671; porter_pairs.h).
  const PlanPaces paces{.cart_drain = cart_drain,
                        .walk_hours_per_km = walk_hours_per_km,
                        .harness_hours_per_km = harness_hours_per_km,
                        .loaded_hours_per_km = loaded_hours_per_km,
                        .has_carts = SettlementHasCarts(world, config.horse_kind)};
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
      // AT THE LOADED PACE (#570, 2b): loaded with its crew, 9 km/h, not the trot.
      const float ride = RoadKm(world, TravelMode::kCart, start, place) * loaded_hours_per_km;
      // ITS CREW RIDES BOTH LEGS (F3 (B); the Fable review of 11 October
      // 2026, item 7): the layer draws them out and back. STUB core — it
      // picks nobody up on the way («подбирает и высаживает по пути», §11):
      // the way out carries its crew, the way back the same crew.
      std::vector<ResidentId> crew;
      for (std::uint32_t other = 0; other < world.residents.rows.size(); ++other) {
        if (world.residents.rows[other].work.rides_cart_of.value == driver.value) {
          crew.push_back(world.residents.row_ids[other]);
        }
      }
      CartPlan cart{.driver = driver, .people_cart = true, .on_foot = false, .legs = {}};
      cart.legs.push_back(CartLeg{.from = start,
                                  .to = place,
                                  .task = LogisticsTaskId{},
                                  .depart = TickAt(plan.day, sets_out),
                                  .arrive = TickAt(plan.day, sets_out + ride),
                                  .riders = crew});
      cart.legs.push_back(CartLeg{.from = place,
                                  .to = start,
                                  .task = LogisticsTaskId{},
                                  .depart = TickAt(plan.day, window.sunset - ride),
                                  .arrive = TickAt(plan.day, window.sunset),
                                  .riders = std::move(crew)});
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
      mover.loaded_hours_per_km = loaded_hours_per_km;
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
      mover.loaded_hours_per_km = walk_hours_per_km;  // a carrier walks both halves
      for (const std::uint32_t task_row : mover.chain) {
        mover.chain_drain.push_back(WalkerDrainOf(config, world, task_row, paces));
      }
      mover.drain = mover.chain_drain.front();
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
  // THE PAIRS (#671; porter_pairs.h; CartPlan::partner): the carriers on foot
  // on a manure heap, and those under a works' log, go two by two in the
  // plan's order, each naming the other; the odd hand stays alone.
  {
    constexpr std::size_t kNobody = static_cast<std::size_t>(-1);
    std::vector<std::size_t> waiting(world.logistics_tasks.rows.size(), kNobody);
    for (std::size_t index = 0; index < movers.size(); ++index) {
      Mover& mover = movers[index];
      const std::uint32_t first = mover.on_foot && !mover.chain.empty() ? mover.chain.front() : 0;
      const PairTerms terms = mover.on_foot && !mover.chain.empty()
                                  ? PairTermsOf(config, world, first, paces)
                                  : PairTerms{};
      if (!terms.pairs) {
        continue;
      }
      if (terms.log) {
        mover.loaded_hours_per_km = terms.loaded_hours_per_km;
        mover.drain = terms.drain;
        mover.chain_drain.front() = mover.drain;
      }
      if (waiting[first] == kNobody) {
        waiting[first] = index;
        continue;
      }
      const Mover& other = movers[waiting[first]];
      plan.carts[mover.cart].partner = plan.carts[other.cart].driver;
      plan.carts[other.cart].partner = plan.carts[mover.cart].driver;
      waiting[first] = kNobody;
    }
  }
  RunTheClock(config, world, remaining, plan, movers, window);
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
  // WHAT EACH LEG CARRIES (#671; leg_loads.h): the trip's resource, and on a
  // walker's loaded leg the grams on his back.
  FillTripLoads(config, world, plan);
  for (const CartPlan& cart : plan.carts) {
    tally.legs += static_cast<std::uint32_t>(cart.legs.size());
    tally.carts += cart.on_foot ? 0U : 1U;
    for (const CartLeg& leg : cart.legs) {
      tally.loaded_trips += leg.loaded != 0 ? 1U : 0U;
      // A cart's load carted standing at the load — no receiver written, or
      // the receiver at the load — drawn as before F3 and counted here (the
      // shout logistics_plan.h promises).
      const bool stands = leg.from.x == leg.to.x && leg.from.y == leg.to.y;
      tally.legs_standing_at_load += !cart.on_foot && leg.task.value != kInvalidEntityIdValue &&
                                             stands && leg.depart != leg.arrive
                                         ? 1U
                                         : 0U;
    }
  }
  // THE SLEDGE ON THE LEG (SL; CartLeg::sledge, save 175): every horse cart of
  // a snow day, the people's cart too (one vehicle, transport §16); a carrier
  // on foot has none.
  for (CartPlan& cart : plan.carts) {
    for (CartLeg& leg : cart.legs) {
      leg.sledge = gear.sledge && !cart.on_foot ? 1U : 0U;
    }
  }
  return plan;
}

namespace {

/// A walk from the worker's work to an evening leg's start that still counts
/// as «the cart passes him»: game hours on foot. STUB core — the morning
/// samples the cart's way for the nearest point (cart_passengers.cpp); the
/// evening takes the leg's start alone, and this bounds how far a man goes
/// to reach it.
constexpr float kEveningPickupWalkHours = 1.0F;

/// A ride that saves less than this over walking is not taken (game hours):
/// the morning's own margin (cart_passengers.cpp, kLeastSavingHours).
constexpr float kEveningLeastSavingHours = 0.1F;

/// One worker wanting a ride home tonight.
struct EveningWalker {
  std::uint32_t row = 0;
  Vec2 work;
  Vec2 home;
  float walk_hours = 0.0F;
};

/// A point of an evening leg's way, with the hour the cart passes it.
struct EveningPoint {
  Vec2 position;
  float hours = 0.0F;
};

/// The way's points every kEveningSampleMetres (the morning's own spacing,
/// cart_passengers.cpp), the cart's hour at each.
constexpr float kEveningSampleMetres = 100.0F;

float Distance(Vec2 from, Vec2 to) {
  return std::hypot(to.x - from.x, to.y - from.y);
}

/// THE LEG'S WAY, SAMPLED (boss, 11 October 2026, the third ruling on F3's
/// pair: «Обратный путь… на тех же условиях» is the morning's pick-up at the
/// nearest point of the route): by the roads where the leg goes by them, the
/// straight line where it does not, from the leg's departure at the cart's
/// pace.
std::vector<EveningPoint> SampleEveningLeg(const WorldState& world,
                                           const RoadIndex& index,
                                           const CartLeg& leg,
                                           float departs,
                                           float ride_hours_per_km) {
  std::vector<EveningPoint> points;
  points.push_back(EveningPoint{.position = leg.from, .hours = departs});
  const Route way = index.Way(TravelMode::kCart, leg.from, leg.to);
  float hours = departs;
  for (const RouteLeg& piece : way.legs) {
    const float piece_hours = piece.effective_km * ride_hours_per_km;
    const auto steps = static_cast<std::uint32_t>(
        std::max(1.0F, std::ceil(piece.length_m / kEveningSampleMetres)));
    const std::vector<RoadPoint>* axis = nullptr;
    if (piece.on_road) {
      const std::uint32_t road_row = FindRow(world.roads, piece.road);
      axis = road_row == kNoRow ? nullptr : &world.roads.rows[road_row].axis;
    }
    for (std::uint32_t step = 1; step <= steps; ++step) {
      const float share = static_cast<float>(step) / static_cast<float>(steps);
      Vec2 position = piece.to;
      if (axis != nullptr) {
        position = PointAtChainage(
            *axis, piece.from_chainage_m + ((piece.to_chainage_m - piece.from_chainage_m) * share));
      } else if (!piece.on_road) {
        position = Vec2{.x = piece.from.x + ((piece.to.x - piece.from.x) * share),
                        .y = piece.from.y + ((piece.to.y - piece.from.y) * share)};
      }
      points.push_back(EveningPoint{.position = position, .hours = hours + (piece_hours * share)});
    }
    hours += piece_hours;
  }
  return points;
}

/// The cost to a walker's day of riding this leg home, or a negative number
/// when the leg passes nowhere within his pick-up walk: he boards at the
/// way's point nearest his work (within kEveningPickupWalkHours), leaving
/// work in time to be there when the cart passes — so a leg passing at noon
/// costs him the afternoon — and gets off at the later point that brings him
/// home soonest; never less than the way itself (walk, ride, walk).
float EveningRideCost(const std::vector<EveningPoint>& points,
                      const EveningWalker& walker,
                      float walk_hours_per_metre,
                      float sunset) {
  std::size_t board = points.size();
  float walk_to = 0.0F;
  for (std::size_t at = 0; at < points.size(); ++at) {
    const float walk = Distance(walker.work, points[at].position) * walk_hours_per_metre;
    if (walk <= kEveningPickupWalkHours && (board == points.size() || walk < walk_to)) {
      board = at;
      walk_to = walk;
    }
  }
  if (board == points.size()) {
    return -1.0F;
  }
  float best_way = -1.0F;
  for (std::size_t at = board; at < points.size(); ++at) {
    const float ride = points[at].hours - points[board].hours;
    const float walk_from = Distance(points[at].position, walker.home) * walk_hours_per_metre;
    const float way = walk_to + ride + walk_from;
    if (best_way < 0.0F || way < best_way) {
      best_way = way;
    }
  }
  return std::max(best_way, sunset - points[board].hours + walk_to);
}

}  // namespace

void SeatEveningRiders(const LogisticsConfig& config,
                       WorldState& current,
                       GroomPlan& plan,
                       LogisticsTally& tally) {
  // Every way home of the day is decided anew: a re-plan re-seats the evening.
  for (ResidentRow& resident : current.residents.rows) {
    resident.work.travel_home_hours = -1.0F;
    resident.work.rides_home_with = ResidentId{};
  }
  for (CartPlan& cart : plan.carts) {
    if (cart.people_cart || cart.on_foot || cart.legs.empty()) {
      continue;
    }
    // The evening's riders are written anew too; the morning's stay on the
    // first leg (a cart's first leg is never its evening one but on a day of
    // one leg, which seats the morning's alone).
    for (std::size_t index = 1; index < cart.legs.size(); ++index) {
      cart.legs[index].riders.clear();
    }
  }
  const float walk_hours_per_km = static_cast<float>(kClockScale) / config.walk_speed_kmh;
  // SL: the riders ride the day's gear (the sledge on snow).
  const CartGear gear = CartGearToday(current, CartSpecsOf(config));
  const float harness_hours_per_km = static_cast<float>(kClockScale) / gear.empty_kmh;
  // A loaded leg rides at the loaded pace (#570, 2a): the rider's ride home
  // on a loaded trip is that much longer.
  const float loaded_hours_per_km = static_cast<float>(kClockScale) / gear.loaded_kmh;
  const float walk_hours_per_metre =
      walk_hours_per_km * OffRoadWeightOf(current, TravelMode::kWalk) / 1000.0F;
  const std::shared_ptr<const RoadIndex> roads = RoadIndexOf(current);
  const DayWindow window = SolarWindow(current.weather.daylight_hours);
  // WHO WANTS A RIDE HOME: the morning's riders of a goods cart.
  std::vector<EveningWalker> walkers;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    const ResidentRow& resident = current.residents.rows[row];
    const ResidentId driver = resident.work.rides_cart_of;
    if (driver.value == kInvalidEntityIdValue || resident.work.kind == WorkKind::kNone) {
      continue;
    }
    bool goods_cart = false;
    for (const CartPlan& cart : plan.carts) {
      goods_cart = goods_cart || (cart.driver.value == driver.value && !cart.people_cart);
    }
    EveningWalker walker;
    if (!goods_cart || !WorkPlaceOf(current, resident.work, walker.work) ||
        !HomePositionOf(current, resident.family, walker.home)) {
      continue;
    }
    walker.row = row;
    walker.walk_hours =
        RoadKm(current, TravelMode::kWalk, walker.work, walker.home) * walk_hours_per_km;
    walkers.push_back(walker);
  }
  // SEATS TO WHOEVER WALKS FARTHEST (§11, «Кому место, если желающих
  // больше»), ties by row — the same order every run.
  std::stable_sort(
      walkers.begin(), walkers.end(), [](const EveningWalker& left, const EveningWalker& right) {
        return left.walk_hours > right.walk_hours;
      });
  tally.evening_wanting += static_cast<std::uint32_t>(walkers.size());
  for (const EveningWalker& walker : walkers) {
    // THE EVENING LEGS of each goods cart: its last loaded trip and its ride
    // home, the two that come back toward the village at the day's end.
    float best_hours = walker.walk_hours - kEveningLeastSavingHours;
    CartLeg* best_leg = nullptr;
    ResidentId best_driver;
    bool refused_by_form = false;
    bool full = false;
    for (CartPlan& cart : plan.carts) {
      if (cart.people_cart || cart.on_foot || cart.legs.empty()) {
        continue;
      }
      std::size_t last_loaded = cart.legs.size();
      for (std::size_t index = 0; index < cart.legs.size(); ++index) {
        last_loaded = cart.legs[index].loaded != 0 ? index : last_loaded;
      }
      const std::size_t home_leg = cart.legs.size() - 1;
      for (const std::size_t index : {last_loaded, home_leg}) {
        if (index >= cart.legs.size() || index == 0) {
          continue;
        }
        CartLeg& leg = cart.legs[index];
        if (leg.task.value == kInvalidEntityIdValue && index != home_leg) {
          continue;
        }
        // THE PICK-UP AT THE NEAREST POINT OF THE WAY, as the morning's (§11,
        // «на тех же условиях»); the cart does not turn aside and does not
        // wait for the day's end («Подвода с маршрута не сворачивает.
        // Попутчик подстраивается под неё»): a leg that passes him at noon
        // costs him the afternoon, and he walks (EveningRideCost).
        const float departs = static_cast<float>(HourFromTick(leg.depart));
        const std::vector<EveningPoint> points =
            SampleEveningLeg(current,
                             *roads,
                             leg,
                             departs,
                             leg.loaded != 0 ? loaded_hours_per_km : harness_hours_per_km);
        const float total = EveningRideCost(points, walker, walk_hours_per_metre, window.sunset);
        if (total < 0.0F || !(total < best_hours)) {
          continue;
        }
        // THE FORM DECIDES (CargoFormSeatsRiders): a loaded log trip, manure
        // or the live takes nobody; an empty way always.
        if (leg.loaded != 0 && !CargoFormSeatsRiders(LegCargoForm(config, current, leg))) {
          refused_by_form = true;
          continue;
        }
        if (leg.riders.size() >= config.cart_passenger_seats) {
          full = true;
          continue;
        }
        best_hours = total;
        best_leg = &leg;
        best_driver = cart.driver;
      }
    }
    WorkAssignment& work = current.residents.rows[walker.row].work;
    if (best_leg != nullptr) {
      best_leg->riders.push_back(current.residents.row_ids[walker.row]);
      work.travel_home_hours = best_hours;
      work.rides_home_with = best_driver;
      ++tally.evening_seated;
      continue;
    }
    // HE WALKS HOME, and his day pays for it (boss, 11 October 2026: «a rider
    // the evening trip cannot take walks home, and his day loses the
    // walk-minus-ride»).
    work.travel_home_hours = walker.walk_hours;
    tally.evening_refused_by_form += refused_by_form ? 1U : 0U;
    tally.evening_no_seat += !refused_by_form && full ? 1U : 0U;
  }
}

}  // namespace core
