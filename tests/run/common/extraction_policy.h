/// @file
/// @brief The digging the run's chairman marks, because a run has no
/// chairman and a building site waits for clay, stone or sand.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN DIGS. Once no house came from nothing, the thirty-year run's
/// house sites stood for years waiting for clay: the start's 40 t went into
/// the first granaries and houses, and the core had no source of it (boss,
/// parcels 269-270). The design gives the chairman the plots and the verb
/// (construction design §3); a run has nobody to say it, so it plays the
/// player here, as it does for felling (felling_policy.h), and says so.

#ifndef TESTS_RUN_COMMON_EXTRACTION_POLICY_H_
#define TESTS_RUN_COMMON_EXTRACTION_POLICY_H_

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/extraction_catalog.h"
#include "core_common/extraction_state.h"
#include "core_common/order_state.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief Marks the nearest extraction site of a material when the building
/// sites wait for more of it than the village holds, dug and marked included.
class ExtractionPolicy {
 public:
  explicit ExtractionPolicy(const core::ITableSet& tables) {
    std::string error;
    ready_ = core::ParseExtractionCatalog(tables, catalog_, error) && !catalog_.sites.empty();
    ReadCosts(tables);
  }

  /// @brief The fixture difference, in words, for the run to print BEFORE it
  /// measures anything.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman MARKS the nearest "
                 "clay pit, stone quarry or sand pit when the building sites wait for more of it "
                 "than the village holds (construction design §3; boss, parcel 270)\n";
  }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    if (!ready_) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    for (std::size_t index = 0; index < core::kExtractedMaterialCount; ++index) {
      const core::ResourceId resource = catalog_.resources[index];
      if (resource.value == core::kInvalidDefIdValue) {
        continue;
      }
      const core::Grams short_of = Owed(world, resource) - InHand(world, resource);
      if (short_of <= 0) {
        continue;
      }
      core::OrderRow order;
      if (!NearestMark(world, resource, short_of, order)) {
        continue;
      }
      simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
      ++marked_[index];
      marked_grams_[index] += order.amount;
    }
  }

  /// @brief What the policy did, for the run to print at the end.
  void Report(const core::WorldState& world, std::string_view run_name) const {
    std::uint32_t exhausted = 0;
    for (const core::ExtractionSiteRow& site : world.extraction_sites.rows) {
      exhausted += site.exhausted;
    }
    std::cout << run_name << ": the run's chairman marked digging " << marked_[0] << " times for "
              << marked_grams_[0] / 1'000'000 << " t of clay, " << marked_[1] << " for "
              << marked_grams_[1] / 1'000'000 << " t of stone, " << marked_[2] << " for "
              << marked_grams_[2] / 1'000'000 << " t of sand; " << exhausted << " of "
              << world.extraction_sites.rows.size() << " sites exhausted\n";
  }

 private:
  /// Marked above the shortfall, so the crew is not sent back for the last
  /// barrow a day later: one house's clay at the design's two tonnes.
  static constexpr core::Grams kMarkAbove = 2'000'000;

  /// One cost row: what building a type up to a level takes of a resource.
  struct CostRow {
    std::uint16_t type = 0;
    std::uint8_t level = 0;
    std::uint16_t resource = 0;
    core::Grams grams = 0;
  };

  void ReadCosts(const core::ITableSet& tables) {
    const core::ITable* const costs = tables.FindTable("unit_level_cost");
    const core::ITable* const types = tables.FindTable("unit_types");
    const core::ITable* const resources = tables.FindTable("resources");
    if (costs == nullptr || types == nullptr || resources == nullptr) {
      return;
    }
    const std::uint32_t unit_col = costs->FindColumn("unit");
    const std::uint32_t level_col = costs->FindColumn("level");
    const std::uint32_t resource_col = costs->FindColumn("resource");
    const std::uint32_t amount_col = costs->FindColumn("amount");
    const std::uint32_t mass_col = resources->FindColumn("kg_per_unit");
    for (std::uint32_t row = 0; row < costs->RowCount(); ++row) {
      const std::uint32_t resource_row =
          resources->FindRowByKey(costs->CellText(row, resource_col));
      const std::uint32_t type_row = types->FindRowByKey(costs->CellText(row, unit_col));
      const std::optional<float> amount = costs->CellReal(row, amount_col);
      const std::optional<float> level = costs->CellReal(row, level_col);
      const std::optional<float> mass = resources->CellReal(resource_row, mass_col);
      if (resource_row == core::kNoTableRow || type_row == core::kNoTableRow ||
          !amount.has_value() || !level.has_value() || !mass.has_value()) {
        continue;
      }
      costs_.push_back(CostRow{
          .type = static_cast<std::uint16_t>(type_row),
          .level = static_cast<std::uint8_t>(*level),
          .resource = static_cast<std::uint16_t>(resource_row),
          .grams = static_cast<core::Grams>(
              std::llround(static_cast<double>(*amount) * static_cast<double>(*mass) * 1000.0))});
    }
  }

  /// What the sites waiting for their recipe want of a resource — marked and
  /// not yet started, since a start now needs the whole recipe in the village
  /// (construction design §6), or still delivering — less what lies on them.
  core::Grams Owed(const core::WorldState& world, core::ResourceId resource) const {
    core::Grams owed = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.construction.phase != core::ConstructionPhase::kDelivering &&
          unit.construction.phase != core::ConstructionPhase::kMarked) {
        continue;
      }
      const auto target = unit.construction.target_level;
      for (const CostRow& cost : costs_) {
        if (cost.type != unit.type.value || cost.level != target ||
            cost.resource != resource.value) {
          continue;
        }
        const core::Grams there =
            resource.value < unit.stock.size() ? unit.stock[resource.value] : 0;
        owed += cost.grams > there ? cost.grams - there : 0;
      }
    }
    return owed;
  }

  /// What the village has of a resource or will have: in the built stores,
  /// lying dug on a site, and marked for the crew.
  static core::Grams InHand(const core::WorldState& world, core::ResourceId resource) {
    core::Grams held = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.level > 0 && resource.value < unit.stock.size()) {
        held += unit.stock[resource.value];
      }
    }
    for (const core::ExtractionSiteRow& site : world.extraction_sites.rows) {
      if (site.resource.value == resource.value) {
        held += site.load_grams + site.marked_grams;
      }
    }
    return held;
  }

  /// The village's centre, as the building chairman reckons it.
  static core::Vec2 Centre(const core::WorldState& world) {
    core::Vec2 sum{.x = 0.0F, .y = 0.0F};
    for (const core::UnitRow& unit : world.units.rows) {
      sum.x += unit.position.x;
      sum.y += unit.position.y;
    }
    const auto count = static_cast<float>(world.units.rows.size());
    return count > 0.0F ? core::Vec2{.x = sum.x / count, .y = sum.y / count} : sum;
  }

  bool NearestMark(const core::WorldState& world,
                   core::ResourceId resource,
                   core::Grams short_of,
                   core::OrderRow& order) const {
    const core::Vec2 centre = Centre(world);
    std::uint32_t best = core::kNoRow;
    float best_distance = 0.0F;
    for (std::uint32_t row = 0; row < world.extraction_sites.rows.size(); ++row) {
      const core::ExtractionSiteRow& site = world.extraction_sites.rows[row];
      if (site.resource.value != resource.value || site.marked_grams > 0 || site.stock_grams <= 0) {
        continue;
      }
      const float dx = site.position.x - centre.x;
      const float dy = site.position.y - centre.y;
      const float distance = std::sqrt((dx * dx) + (dy * dy));
      if (best == core::kNoRow || distance < best_distance) {
        best = row;
        best_distance = distance;
      }
    }
    if (best == core::kNoRow) {
      return false;
    }
    const core::Grams stock = world.extraction_sites.rows[best].stock_grams;
    const core::Grams wanted = short_of + kMarkAbove;
    order.kind = core::OrderKind::kMarkExtraction;
    order.extraction_site = world.extraction_sites.row_ids[best];
    order.amount = wanted < stock ? wanted : stock;
    return true;
  }

  core::ExtractionCatalog catalog_;

  std::vector<CostRow> costs_;

  bool ready_ = false;

  std::array<std::uint32_t, core::kExtractedMaterialCount> marked_{};

  std::array<core::Grams, core::kExtractedMaterialCount> marked_grams_{};
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_EXTRACTION_POLICY_H_
