/// @file
/// @brief The two verbs that were silent only because nobody said them:
/// kRepairUnit and kDemolishUnit.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THIS EXISTS. Of the twelve order kinds the runs said eight. Four were
/// silent, and boss's count of 2026-09-04 separated them by REASON: rotation
/// is a policy the run does not have by design, and a dismissal needs a
/// cause only a player has — but repair and demolition were silent for no
/// reason at all except that the fixture never uttered them. The mechanics
/// were there from the first year: the start yard holds twenty spare parts
/// and a log barn's repair asks for nine.
///
/// WHAT THAT MADE OF THE OLD MEASUREMENT. The run's line "590 units carry
/// wear, 49 at the ruin mark, and it costs the farm nothing" was taken in a
/// world where NOBODY EVER REPAIRS. It is a statement about a village
/// without repair, not about wear — and the six wear numbers boss set on
/// 2026-09-03 had never been played once.
///
/// THE THRESHOLD IS NOT A TASTE. Repair is ordered at wear 50, because 50
/// is the number the design already carries: the map's wear layer marks a
/// unit at fifty ("отметкой, а не криком" — layers design §8, quoted in
/// manual/73-wear-and-repair.md §4). Picking any other number here would be
/// inventing a design decision inside a measurement.
///
/// AND WHAT THIS RUN CANNOT ANSWER, SAID BEFORE IT ANSWERS ANYTHING ELSE.
/// "Is it worth repairing?" has two sides, and today the core has only one
/// of them. Wear costs the farm NOTHING yet — no output penalty, no warmth,
/// no comfort — and that is boss's own decision of 2026-09-03
/// (manual/73-wear-and-repair.md §4: "сегодня — только то, что видно").
/// So this measures what repair COSTS — man-days, spare parts, whether the
/// parts run out, how many units still reach the ruin mark with repair
/// alive — and cannot measure what it SAVES, because nothing is saved yet.
/// A check that cannot reach a class of question must say so out loud, or
/// its green is read wider than it can see.

#ifndef TESTS_RUN_COMMON_REPAIR_POLICY_H_
#define TESTS_RUN_COMMON_REPAIR_POLICY_H_

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief Orders a repair whenever a unit passes the design's wear mark,
/// demolishes one empty unit once, and reports the price of both.
class RepairPolicy {
 public:
  explicit RepairPolicy(const core::ITableSet& tables) {
    const core::ITable* types = tables.FindTable("unit_types");
    if (types != nullptr) {
      const std::uint32_t row = types->FindRowByKey("old_house");
      if (row != core::kNoTableRow) {
        old_house_ = core::UnitTypeId{static_cast<std::uint16_t>(row)};
      }
    }
    const core::ITable* resources = tables.FindTable("resources");
    if (resources != nullptr) {
      const std::uint32_t row = resources->FindRowByKey("spare_part");
      if (row != core::kNoTableRow) {
        spare_ = core::ResourceId{static_cast<std::uint16_t>(row)};
      }
    }
  }

  /// @brief One day of the chairman's attention, called after the day's
  /// steps like the other policies.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    ++day_;
    if (parts_at_start_ < 0) {
      parts_at_start_ = PartsInWorld(world);
    }
    CollectFinished(world);

    // ONE DEMOLITION, ONCE, AND LATE ENOUGH TO HAVE A SUBJECT. Early in the
    // campaign every standing unit is lived in or holds something, and the
    // rule refuses those (unit rules §14) — a refusal would be a silent
    // wasted day rather than a measurement.
    if (!demolition_ordered_ && day_ >= kDemolishOnDay) {
      core::OrderRow order;
      if (NextDemolition(world, order)) {
        demolition_subject_stock_ = StockOf(world, order.unit);
        simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
        demolition_ordered_ = true;
        return;  // one order a day, like every other policy here
      }
    }

    core::OrderRow order;
    if (!NextRepair(world, order)) {
      return;
    }
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    ordered_.push_back(Watch{.unit = order.unit, .wear = WearOf(world, order.unit)});
    ++repairs_ordered_;
  }

  /// @brief The fixture difference, in words, before anything is measured.
  static void Declare() {
    std::cout << "thirty_years: FIXTURE — the run's chairman now REPAIRS at wear " << kRepairAtWear
              << " (the design's own wear-layer mark, layers §8) and DEMOLISHES one empty unit "
                 "once. Both verbs were silent only because no fixture said them (boss, "
                 "2026-09-04)\n";
  }

  /// @brief What repair cost over the run, and what it did not buy.
  /// @return failures, so the run can add them to its own count.
  int Report(const core::WorldState& world) const {
    int failures = 0;
    const std::int64_t parts_left = PartsInWorld(world);
    const std::int64_t eaten = parts_at_start_ - parts_left;
    std::cout << "repair: " << repairs_ordered_ << " ordered, " << repairs_finished_
              << " finished, " << (repairs_ordered_ - repairs_finished_)
              << " still on site or refused; " << repair_labor_days_ << " man-days and "
              << PiecesOf(eaten) << " spare parts of " << PiecesOf(parts_at_start_)
              << " the start held\n";
    std::cout << "repair: the run began with " << PiecesOf(parts_at_start_)
              << " spare parts and nothing in the tables makes another — " << PiecesOf(parts_left)
              << " left standing; " << free_of_parts_ << " of the " << repairs_finished_
              << " repairs cost NO parts at all, because a price under one whole piece "
                 "truncates to zero\n";
    if (repairs_finished_ > 0) {
      std::cout << "repair: the mean repair took "
                << repair_labor_days_ / static_cast<double>(repairs_finished_) << " man-days\n";
    }
    if (demolition_ordered_) {
      std::cout << "demolition: one ordered on unit " << demolition_subject_.value << ", "
                << (demolition_done_ ? "the unit is gone" : "the site is still being taken down")
                << "; " << static_cast<double>(demolition_subject_stock_) / 1.0e6
                << " t of stored goods moved out of it before the works began\n";
    }
    // THE VERB MUST HAVE BEEN SAID. That is this policy's whole first duty,
    // and it is checked rather than assumed: a policy that finds no subject
    // for thirty years reports a green run having said nothing at all.
    failures += Expect(repairs_ordered_ > 0, "the chairman said kRepairUnit at least once");
    failures += Expect(repairs_finished_ > 0, "and at least one repair went all the way through");
    failures += Expect(demolition_ordered_, "and he said kDemolishUnit once");
    return failures;
  }

  std::uint32_t Ordered() const { return repairs_ordered_; }

  std::uint32_t Finished() const { return repairs_finished_; }

  double LaborDays() const { return repair_labor_days_; }

 private:
  /// The design's own mark for a worn unit (layers design §8). Not a taste.
  static constexpr float kRepairAtWear = 50.0F;

  /// Late enough that a unit has emptied: an early demolition finds only
  /// lived-in houses and full granaries, and the rule refuses both.
  ///
  /// THE YEAR IS ARBITRARY, AND IT IS KEPT ARBITRARY ON PURPOSE. Moving this
  /// one order from year five to year six — same 320 repairs, same 481
  /// man-days, one order displaced by forty-eight days — moves the
  /// thirtieth year's population from 1606 to 1545. Sixty-one residents out
  /// of a single rescheduled order is the run's NOISE FLOOR, and it is why
  /// this line was not re-picked after the numbers were known: choosing the
  /// year that keeps a gate green is fitting the instrument to the answer.
  /// The first value written, before any result existed, is the one that
  /// ships.
  static constexpr std::uint32_t kDemolishOnDay = 5 * 48;

  /// construction.csv, repair_spare_parts_per_labor_day. Mirrored here for
  /// ONE purpose — to count how often the core's truncation to whole pieces
  /// makes a repair free — and the run says so rather than pretending to
  /// read it from the tables it does not parse.
  static constexpr float kPartsPerLaborDay = 0.30F;

  struct Watch {
    core::UnitId unit;

    /// The wear the repair was ordered at — the price is frozen on it.
    float wear = 0.0F;

    /// The man-days the site asked for, read off the site once it exists.
    /// Kept here because the site block is cleared the moment the repair
    /// completes, and that is exactly when the number is wanted.
    float labor_days = 0.0F;
  };

  static int Expect(bool condition, const char* label) {
    if (condition) {
      return 0;
    }
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }

  static double PiecesOf(std::int64_t grams) {
    return static_cast<double>(grams) / 5.0e3;  // resources.csv: a part is 5 kg
  }

  std::int64_t PartsInWorld(const core::WorldState& world) const {
    if (spare_.value == core::kInvalidDefIdValue) {
      return 0;
    }
    std::int64_t total = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      total += core::AmountOf(unit.stock, spare_);
    }
    return total;
  }

  static core::Grams StockOf(const core::WorldState& world, core::UnitId unit) {
    const std::uint32_t row = core::FindRow(world.units, unit);
    if (row == core::kNoRow) {
      return 0;
    }
    core::Grams total = 0;
    for (const core::Grams amount : world.units.rows[row].stock) {
      total += amount > 0 ? amount : 0;
    }
    return total;
  }

  static float WearOf(const core::WorldState& world, core::UnitId unit) {
    const std::uint32_t row = core::FindRow(world.units, unit);
    return row == core::kNoRow ? 0.0F : world.units.rows[row].wear;
  }

  /// A repair is FINISHED when the wear it was ordered at is gone and the
  /// site block is clear. Measured off the world, not off the order's
  /// acceptance: an order that was taken and never finished is exactly the
  /// case this run exists to find.
  void CollectFinished(const core::WorldState& world) {
    std::vector<Watch> still;
    still.reserve(ordered_.size());
    for (const Watch& watch : ordered_) {
      const std::uint32_t row = core::FindRow(world.units, watch.unit);
      if (row == core::kNoRow) {
        continue;  // the unit is gone; the repair is not coming
      }
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.construction.phase == core::ConstructionPhase::kNone && unit.wear < watch.wear) {
        ++repairs_finished_;
        repair_labor_days_ += static_cast<double>(watch.labor_days);
        // WHAT THE PARTS PRICE ACTUALLY ROUNDS TO. Parts are counted in
        // PIECES, so the core truncates the piece count to a whole number
        // (construction_system.cpp, RepairPartsGrams) — and a repair that
        // asks for less than one whole part therefore costs NOTHING. That
        // is not visible in a total; it is visible only as a count.
        const float pieces = watch.labor_days * kPartsPerLaborDay;
        free_of_parts_ += pieces < 1.0F ? 1U : 0U;
        continue;
      }
      Watch carried = watch;
      if (unit.construction.labor_days_total > 0.0F &&
          unit.construction.target_level == unit.level) {
        carried.labor_days = unit.construction.labor_days_total;
      }
      still.push_back(carried);
    }
    ordered_.swap(still);
    if (demolition_ordered_ && !demolition_done_ &&
        core::FindRow(world.units, demolition_subject_) == core::kNoRow) {
      demolition_done_ = true;
    }
  }

  bool Repairable(const core::WorldState& world, std::uint32_t row) const {
    const core::UnitRow& unit = world.units.rows[row];
    if (unit.level == 0 || unit.construction.phase != core::ConstructionPhase::kNone) {
      return false;
    }
    if (old_house_.value != core::kInvalidDefIdValue && unit.type.value == old_house_.value) {
      // "They are to be replaced, not improved" (housing design §10). Asking
      // anyway would spend a day of the fixture's one order on a refusal.
      return false;
    }
    return unit.wear >= kRepairAtWear;
  }

  /// The worst-worn repairable unit. Worst first because a repair is priced
  /// on the wear it is ordered at: putting it off makes it dearer, so a
  /// chairman with one order a day spends it where it has grown most.
  bool NextRepair(const core::WorldState& world, core::OrderRow& order) const {
    std::uint32_t best = core::kNoRow;
    float worst = 0.0F;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      if (!Repairable(world, row)) {
        continue;
      }
      if (world.units.rows[row].wear > worst) {
        worst = world.units.rows[row].wear;
        best = row;
      }
    }
    if (best == core::kNoRow) {
      return false;
    }
    order = core::OrderRow{};
    order.kind = core::OrderKind::kRepairUnit;
    order.unit = world.units.row_ids[best];
    return true;
  }

  /// An empty unit the rule will not refuse: nobody lives there, nothing is
  /// stored, and it is not one of the start's old houses, which fall on
  /// their own and are the demography's business, not the chairman's.
  bool NextDemolition(const core::WorldState& world, core::OrderRow& order) {
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.level == 0 || unit.construction.phase != core::ConstructionPhase::kNone) {
        continue;
      }
      if (unit.household.value != core::kInvalidEntityIdValue) {
        continue;
      }
      if (old_house_.value != core::kInvalidDefIdValue && unit.type.value == old_house_.value) {
        continue;
      }
      if (!(unit.wear > 0.0F)) {
        continue;  // a heap or a stack: nothing was built, nothing to take down
      }
      order = core::OrderRow{};
      order.kind = core::OrderKind::kDemolishUnit;
      order.unit = world.units.row_ids[row];
      demolition_subject_ = order.unit;
      return true;
    }
    return false;
  }

  core::UnitTypeId old_house_;

  core::ResourceId spare_;

  std::uint32_t day_ = 0;

  std::int64_t parts_at_start_ = -1;

  std::vector<Watch> ordered_;

  std::uint32_t repairs_ordered_ = 0;

  std::uint32_t repairs_finished_ = 0;

  double repair_labor_days_ = 0.0;

  std::uint32_t free_of_parts_ = 0;

  bool demolition_ordered_ = false;

  bool demolition_done_ = false;

  core::UnitId demolition_subject_;

  core::Grams demolition_subject_stock_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_REPAIR_POLICY_H_
