/// @file
/// @brief The warm works' crew: the run's chairman puts standing builders on
/// every works that will house a freezing kolkhoz herd, while it is in its
/// labour phase, and takes them off when it ends.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY (the frozen cows of the tables commit, 3 November 2026; boss,
/// core-boss-1084-funeral-body [5]-[7]). warm_hold.h guards the warm's
/// MATERIALS until it is ordered; nothing guarded its HANDS after. A
/// construction job carries no window, so in the strada it ranks below every
/// harvest (PlacementTier; cold_housing's key lifts it only among the
/// windowless). On seed 2044 of the new-beach world the first cattle yard's
/// level 2 reached its labour phase on y1 day 31 - August - one day later
/// than in 357's world; its 4.3 man-days stood with nobody on them for eight
/// days, then with 1-3 builders, and it stood warm only on y2 day 4. Four
/// cows froze on days 1-4. The materials were never short.
///
/// THE RULE (a good bot's rule, the human's word of 2 October: the bots play
/// the layout and the queue well). Every morning, for each works in
/// kBuilding whose type is the home of a freezing kind (cold_housing_catalog.h)
/// with a kolkhoz herd of that kind standing: standing kAssignWork orders on
/// it up to the works' max crew (until #1374's pair: min(max crew, the
/// man-days left rounded up), which starved a winter works); when its works
/// end, every standing order on it is released. The player's own lever
/// (order_state.h kAssignWork), at the cost of the strada's hands for the
/// day or two the works take.

#ifndef TESTS_RUN_COMMON_WARM_CREW_H_
#define TESTS_RUN_COMMON_WARM_CREW_H_

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core_catalog/cold_housing_catalog.h"
#include "core_common/calendar.h"
#include "core_common/cold_housing.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class WarmCrew {
 public:
  explicit WarmCrew(const core::ITableSet& tables) {
    std::string error;
    ready_ = core::ReadColdHousing(tables, cold_, error);
    if (const core::ITable* const world = tables.FindTable("world_params")) {
      const std::uint32_t row = world->FindRowByKey("livestock_freezing_counter");
      const std::uint32_t column = world->FindColumn("value");
      if (row != core::kNoTableRow && column != core::kNoTableColumn) {
        const float value =
            world->CellReal(row, column).value_or(static_cast<float>(freezing_counter_));
        freezing_counter_ = value >= 1.0F ? static_cast<std::uint32_t>(value) : 1U;
      }
    }
  }

  /// @brief #1084's base arm of the cure: the crew never stands.
  void SetOff() { off_ = true; }

  /// @brief The morning's orders: builders up to the need on every warm
  ///        works in its labour phase, and their release when it ends.
  void RunDay(core::ISimulation& simulation) {
    if (!ready_) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    ObserveLate(world);
    if (off_) {
      return;
    }
    std::vector<core::OrderRow> orders;
    // RELEASE: a standing builder whose works is no longer a warm works in
    // its labour phase.
    for (const core::OrderRow& standing : world.orders.rows) {
      if (standing.kind != core::OrderKind::kAssignWork ||
          standing.status != core::OrderStatus::kAccepted ||
          standing.work != core::WorkKind::kConstruction ||
          std::ranges::find(sent_, standing.resident) == sent_.end()) {
        continue;
      }
      const std::uint32_t row = core::FindRow(world.units, standing.unit);
      if (row == core::kNoRow || !WarmWorksInLabour(world, row)) {
        core::OrderRow release;
        release.kind = core::OrderKind::kReleaseWork;
        release.resident = standing.resident;
        orders.push_back(release);
        std::erase(sent_, standing.resident);
      }
    }
    // SEND: up to the need on each warm works in its labour phase.
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      if (!WarmWorksInLabour(world, row)) {
        continue;
      }
      const core::UnitRow& unit = world.units.rows[row];
      // THE WORKS' WHOLE CREW, NOT THE MAN-DAYS LEFT (#1374's pair, seed 2035
      // pen 55; boss, core-boss-1374 [9]): the need was min(max crew, the
      // man-days left rounded up), which reads a head as a whole man-day a
      // day. In winter a head closes a fraction of one: pen 55's level 2
      // reached its labour phase on y1 day 39 (November), its own ten closed
      // 1.98 of 4.29, the crew then stood at the five sent for the 4.29, and
      // they closed 0.15-0.27 a day in December - it stood warm on y2 day 2,
      // and two cows froze on y2 days 1-2. The works' own limit is the cap.
      const std::uint32_t need = unit.construction.max_crew;
      std::uint32_t standing = 0;
      for (const core::OrderRow& order : world.orders.rows) {
        standing +=
            (order.kind == core::OrderKind::kAssignWork &&
             (order.status == core::OrderStatus::kAccepted ||
              order.status == core::OrderStatus::kPending) &&
             order.work == core::WorkKind::kConstruction && order.unit == world.units.row_ids[row])
                ? 1U
                : 0U;
      }
      for (std::uint32_t r = 0; r < world.residents.rows.size() && standing < need; ++r) {
        const core::ResidentRow& person = world.residents.rows[r];
        const core::ResidentId id = world.residents.row_ids[r];
        if (!FreeAdult(world, person) || std::ranges::find(sent_, id) != sent_.end() ||
            HasStandingOrder(world, id)) {
          continue;
        }
        core::OrderRow send;
        send.kind = core::OrderKind::kAssignWork;
        send.resident = id;
        send.work = core::WorkKind::kConstruction;
        send.unit = world.units.row_ids[row];
        orders.push_back(send);
        sent_.push_back(id);
        ++standing;
        ++sent_total_;
      }
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
    }
  }

  /// @brief The builders this crew has sent in all (the run's own line).
  std::uint32_t SentTotal() const { return sent_total_; }

  /// @brief The coarse count: the warm works (any home of a freezing kind,
  ///        under works) still unfinished on a day their kind's kolkhoz herd
  ///        counted a cold night («мёрзнет», cold_nights 1+) - each works once.
  ///        Says yes in nearly every village: the first autumn's level 2
  ///        waits for its materials while the cows count cold nights.
  std::uint32_t LateWorks() const { return static_cast<std::uint32_t>(late_.size()); }

  /// @brief Boss's question of the whole guard, on the freezing stage
  ///        (core-boss-1084-funeral-body [11]): the warm works still
  ///        unfinished on a day their kind's herd stood at «замерзает»
  ///        (cold_nights >= world_params livestock_freezing_counter) - each
  ///        works once. The gap to LateWorks is what «мёрзнет» costs with no
  ///        death.
  std::uint32_t FreezingWorks() const { return static_cast<std::uint32_t>(freezing_.size()); }

  /// @brief Every day, off or on: marks the late and the freezing warm works.
  void ObserveLate(const core::WorldState& world) {
    ObserveAt(world, 1U, late_);
    ObserveAt(world, freezing_counter_, freezing_);
  }

 private:
  /// Marks into `marked` each warm works under works on a day its kind's
  /// kolkhoz herd's cold_nights reach `threshold`.
  void ObserveAt(const core::WorldState& world,
                 std::uint32_t threshold,
                 std::vector<std::uint32_t>& marked) const {
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.dead != 0 || (unit.construction.phase != core::ConstructionPhase::kDelivering &&
                             unit.construction.phase != core::ConstructionPhase::kBuilding)) {
        continue;
      }
      const std::uint32_t id = world.units.row_ids[row].value;
      if (std::ranges::find(marked, id) != marked.end()) {
        continue;
      }
      for (const core::HerdRow& herd : world.herds.rows) {
        const std::size_t kind = herd.kind.value;
        if (herd.household_owned == 0 && kind < cold_.home_by_kind.size() &&
            kind < cold_.freezes_by_kind.size() && cold_.freezes_by_kind[kind] != 0 &&
            cold_.home_by_kind[kind].value == unit.type.value && herd.cold_nights >= threshold) {
          marked.push_back(id);
          break;
        }
      }
    }
  }

  /// @brief Whether `row` is a warm works in its labour phase: kBuilding,
  ///        man-days left, the home type of a freezing kind with a kolkhoz
  ///        herd of it standing.
  bool WarmWorksInLabour(const core::WorldState& world, std::uint32_t row) const {
    const core::UnitRow& unit = world.units.rows[row];
    if (unit.dead != 0 || unit.construction.phase != core::ConstructionPhase::kBuilding ||
        !(unit.construction.labor_days_remaining > 0.0F)) {
      return false;
    }
    for (const core::HerdRow& herd : world.herds.rows) {
      const std::size_t kind = herd.kind.value;
      if (herd.household_owned == 0 && kind < cold_.home_by_kind.size() &&
          kind < cold_.freezes_by_kind.size() && cold_.freezes_by_kind[kind] != 0 &&
          cold_.home_by_kind[kind].value == unit.type.value &&
          herd.adult_count + herd.juvenile_count + herd.newborn_count > 0) {
        return true;
      }
    }
    return false;
  }

 private:
  /// An adult (16 biological years, the life table's own speed-up 4) with no
  /// post: the builders' pool (as sawmill_policy.h's).
  static bool FreeAdult(const core::WorldState& world, const core::ResidentRow& person) {
    if (person.post.profession.value != core::kInvalidDefIdValue) {
      return false;
    }
    const auto lived =
        static_cast<std::int64_t>(world.calendar.day) - static_cast<std::int64_t>(person.birth_day);
    const float age =
        static_cast<float>(lived) * kLifeSpeedup / static_cast<float>(core::kDaysPerYear);
    return age >= kAdultYears && age < kOldYears;
  }

  static bool HasStandingOrder(const core::WorldState& world, core::ResidentId id) {
    return std::ranges::any_of(world.orders.rows, [id](const core::OrderRow& order) {
      return order.kind == core::OrderKind::kAssignWork && order.resident == id &&
             (order.status == core::OrderStatus::kAccepted ||
              order.status == core::OrderStatus::kPending);
    });
  }

  static constexpr float kLifeSpeedup = 4.0F;  // life.csv life_speedup (STUB copy, run side)
  static constexpr float kAdultYears = 18.0F;
  static constexpr float kOldYears = 55.0F;

  core::ColdHousing cold_;
  bool ready_ = false;
  bool off_ = false;
  std::vector<core::ResidentId> sent_;
  std::uint32_t sent_total_ = 0;
  std::vector<std::uint32_t> late_;
  std::vector<std::uint32_t> freezing_;
  std::uint32_t freezing_counter_ = 6;  // world_params livestock_freezing_counter
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_WARM_CREW_H_
