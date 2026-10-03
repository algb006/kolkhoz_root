// The groom's plan of the day (logistics_plan.h).

#include "logistics_plan.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/labor_state.h"
#include "core_common/logistics_rules.h"
#include "core_common/logistics_state.h"
#include "core_common/road_route.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"

namespace core {
namespace {

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

}  // namespace

GroomPlan BuildGroomPlan(const LogisticsConfig& config,
                         const WorldState& world,
                         LogisticsTally& tally) {
  GroomPlan plan;
  plan.day = world.calendar.day;
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
  const float hours_per_km = config.harness_speed_kmh > 0.0F
                                 ? static_cast<float>(kClockScale) / config.harness_speed_kmh
                                 : 0.0F;
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const WorkAssignment& work = world.residents.rows[row].work;
    if (work.kind != WorkKind::kHauling || work.rides_horse == 0) {
      continue;  // a cart is a carter on a horse; the carriers on foot are not in the plan
    }
    Vec2 origin;
    if (!WorkPlaceOf(world, work, origin)) {
      continue;
    }
    CartPlan cart;
    cart.driver = world.residents.row_ids[row];
    const std::uint32_t first = TaskOfWork(world, work);
    if (first != kNoRow) {
      cart.legs.push_back(CartLeg{.from = origin,
                                  .to = origin,
                                  .task = world.logistics_tasks.row_ids[first],
                                  .depart = 0,
                                  .arrive = 0,
                                  .riders = {}});
    }
    Vec2 at = origin;
    for (std::uint32_t level = 0; level < kLogisticsLevelCount; ++level) {
      const std::vector<std::uint32_t>& ring = rings[level];
      if (ring.empty()) {
        continue;
      }
      const std::uint32_t start = turn[level] % static_cast<std::uint32_t>(ring.size());
      ++turn[level];
      for (std::uint32_t step = 0; step < ring.size() && cart.legs.size() < kMaxChainLoads;
           ++step) {
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
        cart.legs.push_back(CartLeg{.from = at,
                                    .to = next.place,
                                    .task = world.logistics_tasks.row_ids[next.row],
                                    .depart = 0,
                                    .arrive = 0,
                                    .riders = {}});
        at = next.place;
      }
    }
    tally.legs += static_cast<std::uint32_t>(cart.legs.size());
    ++tally.carts;
    plan.carts.push_back(std::move(cart));
  }
  return plan;
}

}  // namespace core
