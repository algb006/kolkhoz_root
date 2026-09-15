/// @file
/// @brief The run's chairman buys on the district's limit what a building
/// site waits for and only the limit brings (district design §1; boss,
/// parcel 208).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN BUYS. With logs felled and boards sawn, the late store years
/// of the sawmill measurement waited for GLASS: the start's sixty panes built
/// seven granaries, and the core had no channel for more. The limit is that
/// channel (boss: "второго канала не заводим"), and a run has no chairman to
/// spend the points. So the run plays him.
///
/// ONE SIGNAL, ONE ANSWER (boss, parcel 208): a site of the queue lacks a
/// material the stores cannot cover, the material comes in a goods lot of the
/// catalogue, and the points cover that lot → buy the cheapest such lot. No
/// cart already carrying that material → no second one while it is on the
/// road. LOGS AND BOARDS ARE NOT BOUGHT: they have channels of their own in
/// the core (felling, the sawmill) and their own prostheses; buying them too
/// would measure the limit and the timber together.

#ifndef TESTS_RUN_COMMON_LIMIT_POLICY_H_
#define TESTS_RUN_COMMON_LIMIT_POLICY_H_

#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/limit_catalog.h"
#include "core_common/order_state.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"
#include "rise_watch.h"

namespace run {

class LimitPolicy {
 public:
  explicit LimitPolicy(const core::ITableSet& tables) {
    std::string error;
    ready_ = core::ParseLimitCatalog(tables, catalog_, error) && !catalog_.lots.empty();
    ReadCosts(tables);
    const core::ITable* const resources = tables.FindTable("resources");
    if (resources != nullptr) {
      log_ = resources->FindRowByKey("log");
      board_ = resources->FindRowByKey("board");
    }
  }

  /// @brief The fixture difference, in words, before anything is measured.
  static void Declare(const char* run) {
    std::cout << run
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman BUYS on the "
                 "district's limit the cheapest goods lot carrying a material a building site "
                 "waits for and the stores cannot cover, logs and boards excepted, when the "
                 "year's points cover it and no cart with that material is on the road "
                 "(district design §1; boss, 2026-09-13)\n";
  }

  /// @brief Counts the step the chairman's yard waits to take (rise_watch.h).
  void SetRiseWatch(RiseWatch watch) { rise_watch_ = std::move(watch); }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    Book(world);
    if (!ready_) {
      return;
    }
    if (cooldown_ > 0) {
      --cooldown_;
      return;
    }
    core::OrderRow order;
    if (!NextOrder(world, order)) {
      return;
    }
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    ++bought_;
    cooldown_ = kCooldownDays;
  }

  /// @brief The points of every closed year, and what the policy bought.
  void Report(const char* run) const {
    std::cout << run << ": the run's chairman bought " << bought_ << " limit lots\n";
    for (const Year& year : years_) {
      std::cout << run << ":   limit year " << year.year << " — granted " << year.granted
                << ", spent " << year.spent << ", burnt " << year.burned << "\n";
    }
  }

 private:
  static constexpr std::uint32_t kCooldownDays = 1;

  struct Cost {
    core::UnitTypeId type;
    std::uint8_t level = 0;
    std::uint32_t resource = 0;
    core::Grams grams = 0;
  };

  struct Year {
    std::uint16_t year = 0;
    std::int32_t granted = 0;
    std::int32_t spent = 0;
    std::int32_t burned = 0;
  };

  /// Every recipe line of unit_level_cost.csv, in grams (kg_per_unit × amount).
  void ReadCosts(const core::ITableSet& tables) {
    const core::ITable* const costs = tables.FindTable("unit_level_cost");
    const core::ITable* const types = tables.FindTable("unit_types");
    const core::ITable* const resources = tables.FindTable("resources");
    if (costs == nullptr || types == nullptr || resources == nullptr) {
      return;
    }
    const std::uint32_t mass_column = resources->FindColumn("kg_per_unit");
    for (std::uint32_t row = 0; row < costs->RowCount(); ++row) {
      const std::uint32_t type_row =
          types->FindRowByKey(costs->CellText(row, costs->FindColumn("unit")));
      const std::uint32_t resource_row =
          resources->FindRowByKey(costs->CellText(row, costs->FindColumn("resource")));
      const std::optional<std::int64_t> level = costs->CellInteger(row, costs->FindColumn("level"));
      const std::optional<float> amount = costs->CellReal(row, costs->FindColumn("amount"));
      const std::optional<float> kilograms = resources->CellReal(resource_row, mass_column);
      if (type_row == core::kNoTableRow || resource_row == core::kNoTableRow || !level || !amount ||
          !kilograms) {
        continue;
      }
      costs_.push_back(
          Cost{.type = core::UnitTypeId{static_cast<std::uint16_t>(type_row)},
               .level = static_cast<std::uint8_t>(*level),
               .resource = resource_row,
               .grams = static_cast<core::Grams>(static_cast<double>(*amount) *
                                                 static_cast<double>(*kilograms) * 1000.0)});
    }
  }

  /// A closed year is read once, the day after the books turned.
  void Book(const core::WorldState& world) {
    const core::YearLedger& closed = world.ledger.closed;
    if (closed.year == 0 || closed.year == last_booked_year_) {
      return;
    }
    last_booked_year_ = closed.year;
    years_.push_back(Year{.year = closed.year,
                          .granted = closed.limit_points_granted,
                          .spent = closed.limit_points_spent,
                          .burned = closed.limit_points_burned});
  }

  static core::Grams HeldInStores(const core::WorldState& world, std::uint32_t resource) {
    core::Grams held = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.level > 0 && resource < unit.stock.size()) {
        held += unit.stock[resource];
      }
    }
    return held;
  }

  static bool OnTheRoad(const core::WorldState& world, std::uint32_t resource) {
    for (const core::LimitDeliveryRow& cart : world.limit_deliveries.rows) {
      if (resource < cart.goods.size() && cart.goods[resource] > 0) {
        return true;
      }
    }
    return false;
  }

  /// The cheapest goods lot of an open epoch that carries `resource`;
  /// kNoTableRow when none does.
  std::uint32_t CheapestLotCarrying(const core::WorldState& world, std::uint32_t resource) const {
    std::uint32_t best = core::kNoTableRow;
    for (std::uint32_t lot = 0; lot < catalog_.lots.size(); ++lot) {
      const core::LimitLotDef& def = catalog_.lots[lot];
      const bool usable = def.kind == core::LimitLotKind::kGoods && def.points > 0 &&
                          def.era <= static_cast<std::uint8_t>(world.epoch) &&
                          resource < def.goods.size() && def.goods[resource] > 0;
      if (usable && (best == core::kNoTableRow || def.points < catalog_.lots[best].points)) {
        best = lot;
      }
    }
    return best;
  }

  /// The first resource, over the queue in row order, that a site lacks
  /// beyond what the stores hold AND that some lot carries; kNoTableRow when
  /// none.
  ///
  /// "AND SOME LOT CARRIES" was added before the measurement: without it the
  /// first missing material of the queue — straw, clay — had no lot, and the
  /// chairman bought nothing while the granary behind it waited for glass
  /// (seed 1931 bought one lot in twenty years, 2026-09-14).
  std::uint32_t MissingMaterial(const core::WorldState& world) const {
    const std::uint32_t rising = rise_watch_ ? rise_watch_(world) : core::kNoRow;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      // And the step the chairman's yard waits to take (rise_watch.h).
      const bool waits_to_rise = row == rising;
      const bool queued = waits_to_rise ||
                          unit.construction.phase == core::ConstructionPhase::kMarked ||
                          unit.construction.phase == core::ConstructionPhase::kDelivering;
      const bool repair =
          !waits_to_rise && unit.level > 0 && unit.construction.target_level == unit.level;
      if (!queued || repair) {
        continue;
      }
      const std::uint8_t target = waits_to_rise ? static_cast<std::uint8_t>(unit.level + 1U)
                                                : unit.construction.target_level;
      for (const Cost& cost : costs_) {
        if (cost.type.value != unit.type.value || cost.level != target || cost.resource == log_ ||
            cost.resource == board_) {
          continue;
        }
        const core::Grams on_site =
            cost.resource < unit.stock.size() ? unit.stock[cost.resource] : 0;
        if (cost.grams > on_site + HeldInStores(world, cost.resource) &&
            CheapestLotCarrying(world, cost.resource) != core::kNoTableRow &&
            !OnTheRoad(world, cost.resource)) {
          return cost.resource;
        }
      }
    }
    return core::kNoTableRow;
  }

  bool NextOrder(const core::WorldState& world, core::OrderRow& order) const {
    const std::uint32_t missing = MissingMaterial(world);
    if (missing == core::kNoTableRow) {
      return false;
    }
    const std::uint32_t best = CheapestLotCarrying(world, missing);
    if (best == core::kNoTableRow || catalog_.lots[best].points > world.limit.points) {
      return false;
    }
    order.kind = core::OrderKind::kOrderLimitLot;
    order.lot = core::LimitLotId{static_cast<std::uint16_t>(best)};
    return true;
  }

  core::LimitCatalog catalog_;
  std::vector<Cost> costs_;

  RiseWatch rise_watch_;
  std::uint32_t log_ = core::kNoTableRow;
  std::uint32_t board_ = core::kNoTableRow;
  bool ready_ = false;
  std::uint32_t cooldown_ = 0;
  std::uint32_t bought_ = 0;
  std::uint16_t last_booked_year_ = 0;
  std::vector<Year> years_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_LIMIT_POLICY_H_
