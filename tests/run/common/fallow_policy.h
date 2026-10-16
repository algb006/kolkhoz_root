/// @file
/// @brief THE FALLOW PLOUGHED AS THE EATERS GROW (#725; econ,
///        fields-grow-with-eaters-2026-10-12 §2; boss,
///        core-boss-725-fallow-2026-10-16 [2]): the run's chairman opens the
///        derelict ground block by block when next year's food per eater
///        falls under the village's need.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN ORDERS THIS. The canon sowed the same 70 ha for fifteen years
/// while its people grew fivefold; the 90 ha of derelict ground were never
/// ploughed, because no policy ever told a field to grow. The design expects
/// the fields to grow («к третьей эпохе разворачивается на всю карту»), and by
/// the human's 2 October word the bots play well.
///
/// THE RULE, once a year on the year's first day (the winter, before the
/// spring's seed is set aside):
///   * next year's food kcal: for every crop the fields' coming slot sows, its
///     area x its mean yield over the last three closed years (the policy's
///     own book; a crop with none yet reads the table's yield) NET of the
///     closed year's losses (rot, the heap's, the snow's, over the harvest;
///     boss [3] (a): a gross forecast read plenty in a hungry village), less
///     the plan's due (the largest position named last year), less its seed
///     (sowing norm x area). A crop counts by what the residents EAT of it:
///     a resource out of the issue bundle (food.csv issue_kg_per_trudoden
///     blank: oat and pea, which the horses eat) counts nought, and stays in
///     the chain as feed (econ's condition, 16 October 2026);
///   * the need: the core's own village need (NeedUntilHarvest's
///     village_need_kcal_per_day, by age as the meal counts it) x 48 days x
///     (1 + last year's net growth);
///   * under it, the next block's chains for spring: rye_winter -> potato ->
///     oat, field k starting at slot k. At most one block a year, never taken
///     back. A block is three derelict fields; the first is the three nearest
///     a standing grain store (granary or church store), the second the rest.
///
/// The field whose chain starts at the winter rye cannot have it in hand in
/// the first spring (it is sown in autumn): its first year is a bare fallow,
/// the core's own rule for a lost winter slot, and the chain runs from the
/// next.

#ifndef TESTS_RUN_COMMON_FALLOW_POLICY_H_
#define TESTS_RUN_COMMON_FALLOW_POLICY_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief Opens a derelict block a year while food per eater runs short.
class FallowPolicy {
 public:
  explicit FallowPolicy(const core::ITableSet& tables) { ReadTables(tables); }

  /// @brief The run says so before it measures.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman ploughs the "
                 "derelict ground three fields (45 ha) at a time, one block a year at most, when "
                 "next year's food per eater falls under the village's need (#725; econ "
                 "fields-grow-with-eaters); chain rye_winter -> potato -> oat; oat and pea "
                 "count nought as food (the meal does not eat them)\n";
  }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    if (derelict_.empty() && !derelict_read_) {
      ReadDerelict(world);
    }
    WatchTheDue(world);
    const std::uint32_t day_of_year = world.calendar.day % core::kDaysPerYear;
    if (day_of_year == kFirstOfDecember) {
      CountPotatoOnTheBlocks(world);
    }
    if (world.calendar.day == 0 || day_of_year != 0 || world.calendar.day == last_turn_day_) {
      return;
    }
    last_turn_day_ = world.calendar.day;
    BookLastYear(world);
    PrintLossShares(world);
    const double forecast = ForecastFoodKcal(world);
    const double need = NeedKcal(simulation, world);
    const std::uint32_t year = world.ledger.closed.year + 1U;
    std::cout << "fallow_policy: year " << year << " forecast "
              << std::llround(forecast / kKcalPerGcal) << " Gcal of food against a need of "
              << std::llround(need / kKcalPerGcal) << " Gcal";
    if (!(need > 0.0) || forecast >= need || opened_blocks_ >= kBlocks) {
      std::cout << (opened_blocks_ >= kBlocks ? " - every block open\n" : " - enough\n");
      return;
    }
    OpenNextBlock(simulation, world);
    std::cout << " - SHORT, block " << opened_blocks_ << " opened for this spring\n";
    opened_in_year_.push_back(year);
  }

  /// @brief The pair's prints: the blocks by year, the block's potato seed,
  ///        its potato on the field on 1 December, its fertility now.
  void Report(const core::WorldState& world) const {
    std::cout << "fallow_policy: " << opened_blocks_ << " of " << kBlocks
              << " blocks opened, in years:";
    for (const std::uint32_t year : opened_in_year_) {
      std::cout << ' ' << year;
    }
    std::cout << (opened_in_year_.empty() ? " none" : "") << '\n';
    for (const auto& [year, tonnes] : block_potato_seed_t_) {
      std::cout << "fallow_policy: year " << year << " block potato seed taken " << tonnes
                << " t\n";
    }
    for (const auto& [year, tonnes] : block_potato_on_field_t_) {
      std::cout << "fallow_policy: year " << year << " block potato on the field on 1 December "
                << tonnes << " t\n";
    }
    for (const core::FieldId field : opened_fields_) {
      const std::uint32_t row = core::FindRow(world.fields, field);
      if (row != core::kNoRow) {
        std::cout << "fallow_policy: block field " << field.value << " fertility "
                  << world.fields.rows[row].fertility << " at year " << world.ledger.closed.year
                  << "'s end\n";
      }
    }
  }

  /// @brief The fields opened so far, for a run's own prints.
  const std::vector<core::FieldId>& OpenedFields() const { return opened_fields_; }

 private:
  static constexpr std::uint32_t kFieldsPerBlock = 3;
  static constexpr std::uint32_t kBlocks = 2;
  static constexpr std::uint32_t kHistoryYears = 3;
  static constexpr std::uint32_t kFirstOfDecember =
      static_cast<std::uint32_t>(core::Month::kDecember) * core::kDaysPerMonth;
  static constexpr double kKcalPerGcal = 1e6;

  struct CropRow {
    std::uint32_t resource = core::kNoTableRow;
    double table_yield_kg_per_ha = 0.0;
    double seed_kg_per_ha = 0.0;
  };

  void ReadTables(const core::ITableSet& tables) {
    const core::ITable* const crops = tables.FindTable("crops");
    const core::ITable* const resources = tables.FindTable("resources");
    const core::ITable* const food = tables.FindTable("food");
    const core::ITable* const types = tables.FindTable("unit_types");
    if (crops == nullptr || resources == nullptr || food == nullptr || types == nullptr) {
      return;
    }
    const std::uint32_t resource_col = crops->FindColumn("resource");
    const std::uint32_t yield_col = crops->FindColumn("yield_kg_per_ha");
    const std::uint32_t seed_col = crops->FindColumn("sowing_norm_kg_per_ha");
    crops_.resize(crops->RowCount());
    for (std::uint32_t row = 0; row < crops->RowCount(); ++row) {
      CropRow& crop = crops_[row];
      crop.resource = resources->FindRowByKey(crops->CellText(row, resource_col));
      crop.table_yield_kg_per_ha =
          static_cast<double>(crops->CellReal(row, yield_col).value_or(0.0F));
      crop.seed_kg_per_ha = static_cast<double>(crops->CellReal(row, seed_col).value_or(0.0F));
    }
    // EATEN: a resource in the issue bundle, at its kilocalories. Oat and pea
    // carry kilocalories and no issue norm: the families never get them.
    const std::uint32_t kcal_col = food->FindColumn("kcal_per_gram");
    const std::uint32_t issue_col = food->FindColumn("issue_kg_per_trudoden");
    eaten_kcal_per_gram_.assign(resources->RowCount(), 0.0F);
    for (std::uint32_t row = 0; row < food->RowCount(); ++row) {
      const std::uint32_t resource = resources->FindRowByKey(food->CellText(row, 0));
      const float issue = food->CellReal(row, issue_col).value_or(0.0F);
      if (resource < eaten_kcal_per_gram_.size() && issue > 0.0F) {
        eaten_kcal_per_gram_[resource] = food->CellReal(row, kcal_col).value_or(0.0F);
      }
    }
    for (const std::string_view key : {"granary", "church_store"}) {
      const std::uint32_t row = types->FindRowByKey(key);
      if (row != core::kNoTableRow) {
        grain_stores_.push_back(row);
      }
    }
    const std::array<std::string_view, 3> chain_keys = {"rye_winter", "potato", "oat"};
    for (std::size_t slot = 0; slot < chain_keys.size(); ++slot) {
      const std::uint32_t row = crops->FindRowByKey(chain_keys[slot]);
      chain_[slot] =
          row == core::kNoTableRow ? core::CropId{} : core::CropId{static_cast<std::uint16_t>(row)};
    }
    potato_resource_ = resources->FindRowByKey("potato");
    history_.assign(resources->RowCount(), {});
    for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
      resource_keys_.emplace_back(resources->CellText(row, 0));
    }
  }

  /// The derelict ground of the genesis: overgrown, and never told to grow.
  void ReadDerelict(const core::WorldState& world) {
    derelict_read_ = true;
    for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
      const core::FieldRow& field = world.fields.rows[row];
      const bool no_chain = field.rotation_year0.value == core::kInvalidDefIdValue &&
                            field.rotation_year1.value == core::kInvalidDefIdValue &&
                            field.rotation_year2.value == core::kInvalidDefIdValue;
      if (field.kind == core::LandKind::kArable && field.overgrown != 0 && no_chain) {
        derelict_.push_back(world.fields.row_ids[row]);
      }
    }
  }

  /// The largest position of each resource named over the year that closes.
  void WatchTheDue(const core::WorldState& world) {
    if (world.calendar.day % core::kDaysPerYear == 0) {
      due_last_year_ = due_this_year_;
      due_this_year_.assign(due_this_year_.size(), 0);
    }
    if (due_this_year_.size() < world.plan.due.size()) {
      due_this_year_.resize(world.plan.due.size(), 0);
    }
    for (std::size_t index = 0; index < world.plan.due.size(); ++index) {
      due_this_year_[index] = std::max(due_this_year_[index], world.plan.due[index]);
    }
  }

  /// The closed year's yield of each resource, kg/ha: its harvest over the
  /// area that reaped it (the fields' last crop).
  void BookLastYear(const core::WorldState& world) {
    std::vector<double> hectares(history_.size(), 0.0);
    for (const core::FieldRow& field : world.fields.rows) {
      if (field.last_crop.value < crops_.size()) {
        const std::uint32_t resource = crops_[field.last_crop.value].resource;
        if (resource < hectares.size()) {
          hectares[resource] += static_cast<double>(field.area_ga);
        }
      }
    }
    const core::YearLedger& closed = world.ledger.closed;
    const core::ResourceAmounts& harvest = closed.harvest;
    loss_share_.assign(history_.size(), 0.0);
    for (std::size_t resource = 0; resource < history_.size(); ++resource) {
      const core::Grams grams = resource < harvest.size() ? harvest[resource] : 0;
      // WHAT DID NOT REACH THE MOUTH (#725, boss [3] (a)): the closed year's
      // rot in the stores, the heap's loss and the snow's, over its harvest.
      const auto lost = [resource](const core::ResourceAmounts& amounts) {
        return resource < amounts.size() ? static_cast<double>(amounts[resource]) : 0.0;
      };
      if (grams > 0) {
        loss_share_[resource] = std::clamp(
            (lost(closed.spoiled) + lost(closed.lost_no_room) + lost(closed.lost_to_snow)) /
                static_cast<double>(grams),
            0.0,
            1.0);
      }
      if (hectares[resource] > 0.0 && grams > 0) {
        std::vector<double>& years = history_[resource];
        years.push_back(static_cast<double>(grams) / kGramsPerKilogram / hectares[resource]);
        if (years.size() > kHistoryYears) {
          years.erase(years.begin());
        }
      }
    }
    // The block's potato seed of the closed year, as the pair prints it.
    if (potato_resource_ < history_.size()) {
      double seed_kg = 0.0;
      for (const core::FieldId id : opened_fields_) {
        const std::uint32_t row = core::FindRow(world.fields, id);
        if (row == core::kNoRow) {
          continue;
        }
        const core::FieldRow& field = world.fields.rows[row];
        if (field.last_crop.value < crops_.size() &&
            crops_[field.last_crop.value].resource == potato_resource_) {
          seed_kg +=
              crops_[field.last_crop.value].seed_kg_per_ha * static_cast<double>(field.area_ga);
        }
      }
      if (!opened_fields_.empty()) {
        block_potato_seed_t_.emplace_back(world.ledger.closed.year, seed_kg / 1000.0);
      }
    }
  }

  /// The loss share the forecast takes off, by eaten resource (boss [4]): a
  /// block opened for losses the year-1 clamp now cuts reads as such here.
  void PrintLossShares(const core::WorldState& world) const {
    std::cout << "fallow_policy: year " << world.ledger.closed.year << " loss share";
    for (std::size_t resource = 0; resource < loss_share_.size(); ++resource) {
      if (eaten_kcal_per_gram_[resource] > 0.0F && !history_[resource].empty()) {
        std::cout << ' ' << resource_keys_[resource] << '='
                  << std::lround(loss_share_[resource] * 100.0) << '%';
      }
    }
    std::cout << '\n';
  }

  double YieldKgPerHa(core::CropId crop) const {
    const CropRow& row = crops_[crop.value];
    if (row.resource < history_.size() && !history_[row.resource].empty()) {
      double sum = 0.0;
      for (const double kg : history_[row.resource]) {
        sum += kg;
      }
      return sum / static_cast<double>(history_[row.resource].size());
    }
    return row.table_yield_kg_per_ha;
  }

  /// Next year's eaten kilocalories off the fields' coming slot.
  double ForecastFoodKcal(const core::WorldState& world) const {
    std::vector<double> grams(eaten_kcal_per_gram_.size(), 0.0);
    for (const core::FieldRow& field : world.fields.rows) {
      const core::CropId crop = field.rotation_year0;
      if (field.kind != core::LandKind::kArable || crop.value >= crops_.size()) {
        continue;
      }
      const CropRow& row = crops_[crop.value];
      if (row.resource >= grams.size()) {
        continue;
      }
      const double kept = row.resource < loss_share_.size() ? 1.0 - loss_share_[row.resource] : 1.0;
      grams[row.resource] += ((YieldKgPerHa(crop) * kept) - row.seed_kg_per_ha) *
                             static_cast<double>(field.area_ga) * kGramsPerKilogram;
    }
    double kcal = 0.0;
    for (std::size_t resource = 0; resource < grams.size(); ++resource) {
      const double due =
          resource < due_last_year_.size() ? static_cast<double>(due_last_year_[resource]) : 0.0;
      const double left = std::max(0.0, grams[resource] - due);
      kcal += left * static_cast<double>(eaten_kcal_per_gram_[resource]);
    }
    return kcal;
  }

  /// The village's need for a year, by the core's own count, with last
  /// year's net growth on top.
  double NeedKcal(core::ISimulation& simulation, const core::WorldState& world) const {
    double per_day = 0.0;
    for (std::size_t resource = 0; resource < eaten_kcal_per_gram_.size() && per_day <= 0.0;
         ++resource) {
      if (eaten_kcal_per_gram_[resource] <= 0.0F) {
        continue;
      }
      const std::optional<core::HarvestNeed> need =
          simulation.NeedUntilHarvest(core::ResourceId{static_cast<std::uint16_t>(resource)});
      per_day = need.has_value() ? static_cast<double>(need->village_need_kcal_per_day) : 0.0;
    }
    const auto residents = static_cast<double>(world.residents.rows.size());
    const core::YearLedger& closed = world.ledger.closed;
    const double net =
        static_cast<double>(closed.births) + closed.arrivals - closed.deaths - closed.departures;
    const double growth = residents > 0.0 ? std::max(0.0, net / residents) : 0.0;
    return per_day * static_cast<double>(core::kDaysPerYear) * (1.0 + growth);
  }

  /// The next block: the derelict fields nearest a standing grain store.
  void OpenNextBlock(core::ISimulation& simulation, const core::WorldState& world) {
    std::vector<std::pair<double, core::FieldId>> by_distance;
    for (const core::FieldId id : derelict_) {
      if (std::ranges::find(opened_fields_, id) != opened_fields_.end()) {
        continue;
      }
      const std::uint32_t row = core::FindRow(world.fields, id);
      if (row == core::kNoRow) {
        continue;
      }
      double nearest = std::numeric_limits<double>::max();
      for (const core::UnitRow& unit : world.units.rows) {
        if (unit.level > 0 &&
            std::ranges::find(grain_stores_, unit.type.value) != grain_stores_.end()) {
          const auto dx = static_cast<double>(unit.position.x - world.fields.rows[row].center.x);
          const auto dy = static_cast<double>(unit.position.y - world.fields.rows[row].center.y);
          nearest = std::min(nearest, std::sqrt((dx * dx) + (dy * dy)));
        }
      }
      by_distance.emplace_back(nearest, id);
    }
    // Nearest first; equal distances keep the genesis order.
    std::ranges::stable_sort(by_distance, {}, &std::pair<double, core::FieldId>::first);
    std::vector<core::OrderRow> orders;
    for (std::size_t k = 0; k < by_distance.size() && k < kFieldsPerBlock; ++k) {
      core::OrderRow order;
      order.kind = core::OrderKind::kSetRotation;
      order.field = by_distance[k].second;
      order.rotation_year0 = chain_[k % 3];
      order.rotation_year1 = chain_[(k + 1) % 3];
      order.rotation_year2 = chain_[(k + 2) % 3];
      orders.push_back(order);
      opened_fields_.push_back(by_distance[k].second);
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
      ++opened_blocks_;
    }
  }

  /// The block's potato lying on its fields on 1 December, apart from the
  /// old 70 ha.
  void CountPotatoOnTheBlocks(const core::WorldState& world) {
    if (opened_fields_.empty() || potato_resource_ == core::kNoTableRow) {
      return;
    }
    // THE BLOCK APART FROM THE OLD GROUND (boss, 16 October 2026): what lies
    // on each, over the hectares that grew potato this year, so a block that
    // loses more reads as storage and hands short, not land. The stores' rot
    // pools every field's potato and cannot be split by field: not printed.
    core::Grams block_grams = 0;
    core::Grams old_grams = 0;
    double block_ha = 0.0;
    double old_ha = 0.0;
    for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
      const core::FieldRow& field = world.fields.rows[row];
      if (field.kind != core::LandKind::kArable) {
        continue;
      }
      const bool block =
          std::ranges::find(opened_fields_, world.fields.row_ids[row]) != opened_fields_.end();
      const bool potato_lies = field.reaped_resource.value == potato_resource_;
      const bool potato_grew = field.last_crop.value < crops_.size() &&
                               crops_[field.last_crop.value].resource == potato_resource_;
      (block ? block_grams : old_grams) += potato_lies ? field.reaped_grams : 0;
      (block ? block_ha : old_ha) += potato_grew ? static_cast<double>(field.area_ga) : 0.0;
    }
    const std::uint32_t year =
        world.ledger.current.year == 0 ? world.ledger.closed.year + 1U : world.ledger.current.year;
    block_potato_on_field_t_.emplace_back(year, static_cast<double>(block_grams) / 1e6);
    std::cout << "fallow_policy: year " << year << " potato on the field on 1 December: block "
              << static_cast<double>(block_grams) / 1e6 << " t over " << block_ha
              << " ha of potato, old ground " << static_cast<double>(old_grams) / 1e6 << " t over "
              << old_ha << " ha\n";
  }

  static constexpr double kGramsPerKilogram = 1000.0;

  std::vector<CropRow> crops_;
  std::vector<float> eaten_kcal_per_gram_;
  std::vector<std::uint32_t> grain_stores_;
  std::array<core::CropId, 3> chain_{};
  std::uint32_t potato_resource_ = core::kNoTableRow;
  std::vector<std::vector<double>> history_;
  std::vector<std::string> resource_keys_;
  /// The closed year's loss over its harvest, by resource, 0..1.
  std::vector<double> loss_share_;
  std::vector<core::Grams> due_this_year_;
  std::vector<core::Grams> due_last_year_;

  std::vector<core::FieldId> derelict_;
  bool derelict_read_ = false;
  std::vector<core::FieldId> opened_fields_;
  std::uint32_t opened_blocks_ = 0;
  std::vector<std::uint32_t> opened_in_year_;
  core::SimDay last_turn_day_ = 0;

  std::vector<std::pair<std::uint32_t, double>> block_potato_seed_t_;
  std::vector<std::pair<std::uint32_t, double>> block_potato_on_field_t_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FALLOW_POLICY_H_
