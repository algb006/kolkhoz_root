/// @file
/// @brief The run's chairman empties the church store and the clamps into
///        the guarded food yard (#1254; econ, core-boss-1250-distiller-rule
///        [12]; boss [11], [13]).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN DOES THIS. #1250's catch was undone every time: the church
/// store and the clamp hold raw material with no watchman BY DESIGN (crime
/// §7, «в церкви сторожа не ставят вовсе — там этот канал закрывается только
/// вывозом»; register 233 for the clamp), so the village's leak stood open in
/// every month of every village, and a taken distiller was replaced two
/// months later. The design's lever is the haul, kEmptyStore, and no run
/// ever ordered it.
///
/// THE RULE: once a food yard stands (level ≥ 1, the guarded store the
/// design names), each standing church store or clamp that holds raw
/// material and is not already emptying gets kEmptyStore (enable 1) - once
/// every food lying in it has another standing home (resource_stores.csv
/// `storage`: the grain the food yard's granary, sugar its food store, potato
/// a clamp or the vegetable store), as a good player waits for the granary
/// before he shuts the church to the harvest. The door decides the rest: it refuses with no store
/// that accepts food, and carries by its own order (start §5: the shortest shelf life first, then
/// what is stolen more readily) on the carts and hands of every haul. An
/// emptying store accepts no delivery, so the district's sugar lots (#1240)
/// land in the stores that accept it. A refused order is asked again after
/// kRetryDays. Once an order took, the store stays emptying: the clamps are
/// not refilled.
/// THE INSTRUMENT: the ES line a closed year - orders asked, taken (the unit's
/// `emptying` the next day), refused, and stores emptying at the year's end -
/// with «NOT MEASURED» when no food yard stood all year.
#ifndef TESTS_RUN_COMMON_STORE_EMPTYING_POLICY_H_
#define TESTS_RUN_COMMON_STORE_EMPTYING_POLICY_H_

#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief Empties the unguarded stores into the guarded food yard.
class StoreEmptyingPolicy {
 public:
  explicit StoreEmptyingPolicy(const core::ITableSet& tables) {
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      church_ = types->FindRowByKey("church_store");
      clamp_ = types->FindRowByKey("clamp");
      food_yard_ = types->FindRowByKey("food_yard");
    }
    const core::ITable* const resources = tables.FindTable("resources");
    if (resources != nullptr) {
      for (const std::string_view key : kRawMaterial) {
        const std::uint32_t row = resources->FindRowByKey(key);
        if (row != core::kNoTableRow) {
          raw_.push_back(core::ResourceId{static_cast<std::uint16_t>(row)});
        }
      }
      const std::uint32_t kcal = resources->FindColumn("kcal_per_gram");
      food_.assign(resources->RowCount(), 0);
      for (std::uint32_t row = 0; row < resources->RowCount() && kcal != core::kNoTableColumn;
           ++row) {
        food_[row] = resources->CellReal(row, kcal).value_or(0.0F) > 0.0F ? 1 : 0;
      }
    }
    // THE FOOD'S OTHER HOMES, by the core's own rule (production_config.cpp:
    // a resource_stores row makes BOTH its `unit` and its `storage` a home):
    // the grain the food yard and its granary, sugar, meat and salt the food
    // yard and its food store, potato a clamp or the vegetable store. The
    // first cut read `storage` alone, so meat and sugar waited for a food
    // store module no bot builds while the food yard itself stood their home,
    // and the church was never emptied (#1254, 0.37.333's pair).
    const core::ITable* const types = tables.FindTable("unit_types");
    const core::ITable* const homes = tables.FindTable("resource_stores");
    if (resources != nullptr && types != nullptr && homes != nullptr) {
      homes_.resize(resources->RowCount());
      const std::uint32_t resource_col = homes->FindColumn("resource");
      for (std::uint32_t row = 0; row < homes->RowCount(); ++row) {
        if (resource_col == core::kNoTableColumn) {
          break;
        }
        const std::uint32_t resource = resources->FindRowByKey(homes->CellText(row, resource_col));
        for (const std::string_view column :
             {std::string_view("unit"), std::string_view("storage")}) {
          const std::uint32_t col = homes->FindColumn(column);
          const std::uint32_t type = col == core::kNoTableColumn
                                         ? core::kNoTableRow
                                         : types->FindRowByKey(homes->CellText(row, col));
          if (resource < homes_.size() && type != core::kNoTableRow && type != church_) {
            homes_[resource].push_back(type);
          }
        }
      }
    }
  }

  /// @brief The control arm (--no-empty-stores): the rule does nothing.
  void Enable(bool on) { on_ = on; }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    if (!on_) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    const core::SimDay today = world.calendar.day;
    ReadYesterday(world);
    CloseYear(world, today);
    if (!FoodYardStands(world)) {
      return;
    }
    yard_seen_year_ = true;
    std::vector<core::OrderRow> orders;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      const bool eligible = unit.type.value == church_ || unit.type.value == clamp_;
      if (!eligible || unit.level == 0 || unit.emptying != 0 || !HoldsRaw(unit)) {
        continue;
      }
      // NOT BEFORE EVERY FOOD IN IT HAS ANOTHER HOME (#1254's trace, seed
      // 1940): the design lets the order go early at its price («урожай,
      // которому некуда лечь, стоит на току»), and a bot that gave it with a
      // food yard but no granary left the rye and oat harvest homeless - the
      // seed went to nought, the seed loan was refused kNowhereToStore, and
      // with no grain there was no straw to build with for 28 years.
      if (!EveryFoodHasAnotherHome(world, row)) {
        ++held_no_home_year_;
        continue;
      }
      const std::uint32_t id = world.units.row_ids[row].value;
      const auto retry = retry_after_.find(id);
      if (retry != retry_after_.end() && today < retry->second) {
        continue;
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kEmptyStore;
      order.unit = world.units.row_ids[row];
      order.enable = 1;
      orders.push_back(order);
      asked_.push_back(id);
      retry_after_[id] = today + kRetryDays;
      ++asked_year_;
    }
    if (!orders.empty()) {
      simulation.StageOrders(orders, {});
    }
  }

  /// @brief The fixture difference, in words, BEFORE the run measures.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — once a food yard stands, the run's "
                 "chairman orders «Освободить склад» (kEmptyStore) on every church store and "
                 "clamp holding raw material, so their grain, potato and sugar go to the stores "
                 "that accept it and the leak can close (#1254; econ, core-boss-1250-distiller-"
                 "rule [12]); --no-empty-stores is the control\n";
  }

 private:
  static constexpr std::array<std::string_view, 6> kRawMaterial = {
      "rye", "wheat", "barley", "oat", "potato", "sugar"};
  static constexpr core::SimDay kRetryDays = core::kDaysPerMonth;

  bool FoodYardStands(const core::WorldState& world) const {
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.type.value == food_yard_ && unit.level > 0 &&
          unit.construction.phase == core::ConstructionPhase::kNone) {
        return true;
      }
    }
    return false;
  }

  /// Whether each food lying in the store at `row` has a standing store of
  /// one of its home types elsewhere, not being emptied itself.
  bool EveryFoodHasAnotherHome(const core::WorldState& world, std::uint32_t row) const {
    const core::UnitRow& store = world.units.rows[row];
    for (std::uint32_t resource = 0; resource < store.stock.size(); ++resource) {
      if (store.stock[resource] <= 0 || resource >= food_.size() || food_[resource] == 0) {
        continue;
      }
      if (resource >= homes_.size()) {
        return false;  // no home named anywhere: nowhere to carry it
      }
      bool housed = false;
      for (std::uint32_t other = 0; other < world.units.rows.size() && !housed; ++other) {
        const core::UnitRow& unit = world.units.rows[other];
        if (other == row || unit.level == 0 || unit.emptying != 0 ||
            unit.construction.phase != core::ConstructionPhase::kNone) {
          continue;
        }
        for (const std::uint32_t type : homes_[resource]) {
          housed = housed || unit.type.value == type;
        }
      }
      if (!housed) {
        return false;
      }
    }
    return true;
  }

  bool HoldsRaw(const core::UnitRow& unit) const {
    for (const core::ResourceId resource : raw_) {
      if (core::UnreservedOf(unit, resource) > 0) {
        return true;
      }
    }
    return false;
  }

  /// Yesterday's orders, off the state: the unit emptying now, or refused.
  void ReadYesterday(const core::WorldState& world) {
    for (const std::uint32_t id : asked_) {
      const std::uint32_t row = core::FindRow(world.units, core::UnitId{id});
      const bool took = row != core::kNoRow && world.units.rows[row].emptying != 0;
      took_year_ += took ? 1U : 0U;
      refused_year_ += took ? 0U : 1U;
    }
    asked_.clear();
  }

  void CloseYear(const core::WorldState& world, core::SimDay today) {
    if (today == 0 || today % core::kDaysPerYear != 0 || today == closed_day_) {
      return;
    }
    closed_day_ = today;
    std::uint32_t emptying = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      emptying += unit.emptying != 0 ? 1U : 0U;
    }
    std::printf("ES,y=%u,asked=%u,took=%u,refused=%u,held_no_home_days=%u,emptying=%u%s\n",
                today / core::kDaysPerYear,
                asked_year_,
                took_year_,
                refused_year_,
                held_no_home_year_,
                emptying,
                yard_seen_year_ ? "" : ",NOT MEASURED: no food yard stood");
    asked_year_ = took_year_ = refused_year_ = held_no_home_year_ = 0;
    yard_seen_year_ = false;
  }

  std::uint32_t church_ = core::kNoTableRow;
  std::uint32_t clamp_ = core::kNoTableRow;
  std::uint32_t food_yard_ = core::kNoTableRow;
  std::vector<core::ResourceId> raw_;
  bool on_ = true;
  std::vector<std::uint32_t> asked_;
  std::unordered_map<std::uint32_t, core::SimDay> retry_after_;
  core::SimDay closed_day_ = 0;
  bool yard_seen_year_ = false;
  std::uint32_t asked_year_ = 0;
  std::uint32_t took_year_ = 0;
  std::uint32_t refused_year_ = 0;
  std::uint32_t held_no_home_year_ = 0;
  std::vector<std::uint8_t> food_;
  std::vector<std::vector<std::uint32_t>> homes_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_STORE_EMPTYING_POLICY_H_
