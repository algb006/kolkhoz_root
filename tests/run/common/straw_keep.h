/// @file
/// @brief The first pen's straw kept ahead: while the pen stands at rung 1 with
/// cows on the farm and its warm not begun, no other site starts a step whose
/// straw would leave the store below the warm's own straw.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// #546 (boss, 11 October 2026; econ, v2-relanded-reds-2026-10-11 §11, 3e92326):
/// ff21a17 kept the warm's logs and clay ahead (the felling and the digging
/// watch FixturePolicy::PenWaitingForWarm) and named two of the recipe's three
/// materials. #423's pair printed the third, straw, as the only one short on
/// days 23-36 (1931: 6 t needed, 1 t held), and five villages of nine fell back
/// on the straw coat. Straw is harvested, not felled or dug, so it cannot be
/// brought ahead; it is KEPT: every other straw step — house thatch, the horse
/// yard, a granary, an upgrade — starts only when the store keeps the warm's
/// straw after it. Not a rank: nobody's turn moves, a start waits for days.
/// The pen's own warm passes (it is what the straw is kept for).

#ifndef TESTS_RUN_COMMON_STRAW_KEEP_H_
#define TESTS_RUN_COMMON_STRAW_KEEP_H_

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core_common/ids.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class StrawKeep {
 public:
  /// @brief The pen whose warm the straw is kept for, or kNoRow
  ///        (FixturePolicy::PenWaitingForWarm).
  using PenWatch = std::function<std::uint32_t(const core::WorldState&)>;

  explicit StrawKeep(const core::ITableSet& tables) {
    const core::ITable* const types = tables.FindTable("unit_types");
    const core::ITable* const resources = tables.FindTable("resources");
    const core::ITable* const costs = tables.FindTable("unit_level_cost");
    if (types == nullptr || resources == nullptr || costs == nullptr) {
      return;
    }
    const std::uint32_t straw_row = resources->FindRowByKey("straw");
    const std::uint32_t cattle_row = types->FindRowByKey("cattle_yard");
    if (straw_row == core::kNoTableRow || cattle_row == core::kNoTableRow) {
      return;
    }
    straw_ = core::ResourceId{static_cast<std::uint16_t>(straw_row)};
    cattle_ = core::UnitTypeId{static_cast<std::uint16_t>(cattle_row)};
    const std::optional<float> mass =
        resources->CellReal(straw_row, resources->FindColumn("kg_per_unit"));
    const std::uint32_t unit_col = costs->FindColumn("unit");
    const std::uint32_t level_col = costs->FindColumn("level");
    const std::uint32_t resource_col = costs->FindColumn("resource");
    const std::uint32_t amount_col = costs->FindColumn("amount");
    for (std::uint32_t row = 0; row < costs->RowCount() && mass.has_value(); ++row) {
      if (costs->CellText(row, resource_col) != "straw") {
        continue;
      }
      const std::uint32_t type_row = types->FindRowByKey(costs->CellText(row, unit_col));
      const std::optional<float> amount = costs->CellReal(row, amount_col);
      const std::optional<float> level = costs->CellReal(row, level_col);
      if (type_row == core::kNoTableRow || !amount.has_value() || !level.has_value()) {
        continue;
      }
      costs_.push_back(Cost{
          .type = static_cast<std::uint16_t>(type_row),
          .level = static_cast<std::uint8_t>(*level),
          .grams = static_cast<core::Grams>(
              std::llround(static_cast<double>(*amount) * static_cast<double>(*mass) * 1000.0))});
    }
    keep_ = CostGrams(cattle_, kWarmLevel);
  }

  void SetPenWatch(PenWatch watch) { pen_watch_ = std::move(watch); }

  /// @brief True when a site of `type` may start `level` today as far as the
  /// pen's straw goes: no pen waits for its warm, or the step is the pen's
  /// warm itself, or it takes no straw, or the store keeps the warm's straw
  /// after it. A refusal is counted (Report).
  bool SparesStrawFor(const core::WorldState& world, core::UnitTypeId type, std::uint8_t level) {
    if (keep_ <= 0 || !pen_watch_ || (type.value == cattle_.value && level == kWarmLevel)) {
      return true;
    }
    const core::Grams need = CostGrams(type, level);
    if (need <= 0 || pen_watch_(world) == core::kNoRow) {
      return true;
    }
    if (Held(world) - need >= keep_) {
      return true;
    }
    if (world.calendar.day != last_refused_day_) {
      last_refused_day_ = world.calendar.day;
      ++refused_days_;
    }
    return false;
  }

  /// @brief Says the rule before the run measures.
  static void Declare(const std::string& run_name) {
    std::printf(
        "%s: THE FIRST PEN's STRAW IS KEPT (#546) — while the first cattle yard stands at "
        "rung 1 with cows and its warm not begun, no other step whose straw would leave the "
        "store below the warm's straw starts\n",
        run_name.c_str());
  }

  /// @brief Days on which some start waited for the pen's straw.
  void Report(const std::string& run_name) const {
    std::printf("%s: straw kept for the first pen: %u days a start waited (keep %.1f t)\n",
                run_name.c_str(),
                refused_days_,
                static_cast<double>(keep_) / static_cast<double>(core::kGramsPerTonne));
  }

 private:
  /// The cattle yard's warm barn (rung 2; FixturePolicy::kWarmBarnLevel).
  static constexpr std::uint8_t kWarmLevel = 2;

  struct Cost {
    std::uint16_t type = 0;
    std::uint8_t level = 0;
    core::Grams grams = 0;
  };

  core::Grams CostGrams(core::UnitTypeId type, std::uint8_t level) const {
    for (const Cost& cost : costs_) {
      if (cost.type == type.value && cost.level == level) {
        return cost.grams;
      }
    }
    return 0;
  }

  core::Grams Held(const core::WorldState& world) const {
    core::Grams held = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.level > 0) {
        held += core::UnreservedOf(unit, straw_);
      }
    }
    return held;
  }

  core::ResourceId straw_;
  core::UnitTypeId cattle_;
  core::Grams keep_ = 0;
  std::vector<Cost> costs_;
  PenWatch pen_watch_;
  std::uint32_t refused_days_ = 0;
  core::SimDay last_refused_day_ = 0xFFFFFFFFU;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_STRAW_KEEP_H_
