/// @file
/// @brief The run's chairman answers a short village by raising the issue (#2164, econ [6];
///        boss: «the canon bot must play well», the 2 Oct rule).
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
///
/// THE RULE: on any day when at least a tenth of the families ate less than 0.9 of their need
/// over the month so far (core::IssueShortfallOf, the authority gate's first half) AND the free
/// stock over the funds covers their shortfall in grain equivalent (its second half), he raises
/// every food position that food.csv gives an issue norm by a quarter of its norm in force
/// (the table's for a position under the default rule) - once a season, as the +2 is counted.
/// A world with authority off is left as before 232: the rule does not act there.
/// THE INSTRUMENT: IR2164 a closed year - raises ordered and positions raised.

#ifndef TESTS_RUN_COMMON_ISSUE_RAISE_POLICY_H_
#define TESTS_RUN_COMMON_ISSUE_RAISE_POLICY_H_

#include <cstdint>
#include <map>
#include <ostream>
#include <span>
#include <vector>

#include "core_common/authority.h"
#include "core_common/calendar.h"
#include "core_common/issue_norm_view.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class IssueRaisePolicy {
 public:
  explicit IssueRaisePolicy(const core::ITableSet& tables) {
    const core::ITable* const food = tables.FindTable("food");
    const core::ITable* const resources = tables.FindTable("resources");
    if (food == nullptr || resources == nullptr) {
      return;
    }
    const std::uint32_t norm_col = food->FindColumn("issue_kg_per_trudoden");
    const std::uint32_t key_col = food->FindColumn("key");
    if (norm_col == core::kNoTableColumn || key_col == core::kNoTableColumn) {
      return;
    }
    for (std::uint32_t row = 0; row < food->RowCount(); ++row) {
      const float kg = food->CellReal(row, norm_col).value_or(0.0F);
      const std::uint32_t resource = resources->FindRowByKey(food->CellText(row, key_col));
      if (kg > 0.0F && resource != core::kNoTableRow) {
        table_grams_[resource] = static_cast<core::Grams>(kg * kGramsPerKg);
      }
    }
  }

  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t year = (world.calendar.day / core::kDaysPerYear) + 1U;
    const std::uint32_t season = core::AuthoritySeasonOf(world.calendar.day);
    // econ [7]'s worst-village print: the free stock at each year's turn, in both arms.
    if (world.calendar.day % core::kDaysPerYear == 0) {
      const core::FreeStockView turn = simulation.FreeStockToday();
      by_year_[year].free_days_at_turn = turn.measured ? turn.days : -1.0F;
      // A RAISE IS A SEASON'S ACT, NOT A NEW RULE: at the turn the raised positions go back
      // under the default rule (a frozen explicit norm would outlast the harvest it was set on).
      if (!raised_.empty()) {
        std::vector<core::OrderRow> resets;
        for (const std::uint32_t resource : raised_) {
          core::OrderRow reset;
          reset.kind = core::OrderKind::kResetIssueNorm;
          reset.resource = core::ResourceId{static_cast<std::uint16_t>(resource)};
          resets.push_back(reset);
        }
        simulation.StageOrders(std::span<const core::OrderRow>(resets), {});
        raised_.clear();
      }
    }
    if (world.chairman.authority_live == 0 || season == last_season_ || table_grams_.empty()) {
      return;
    }
    // ONCE A MONTH, on its last day as the core judges it (boss: a cost growing with the world -
    // a scan of every family - is not paid daily). The first cut scanned every day.
    // On the month's THIRD day, the day before the core judges it (the month's last day, at its
    // first tick): au7's bot raised on the last day itself, AFTER the judgement, and the +2 and
    // the −2 fired in the same 403 seasons, cancelling.
    if ((world.calendar.day + 2U) % core::kDaysPerMonth != 0) {
      return;
    }
    const core::IssueShortfall shortfall = core::IssueShortfallOf(world, core::kAuthorityRules);
    if (!core::FamiliesShort(shortfall, core::kAuthorityRules)) {
      return;
    }
    const core::FreeStockView free = simulation.FreeStockToday();
    const double gap_grams = static_cast<double>(shortfall.shortfall_kcal) /
                             static_cast<double>(core::kAuthorityRules.grain_kcal_per_gram);
    if (!free.measured || static_cast<double>(free.free_grain_eq_grams) < gap_grams) {
      return;
    }
    // THE NORM IN FORCE, as the core resolves it (ISimulation::IssueNorms): under the default
    // rule a harvest's position is a SHARE OF ITS REMAINDER, far above the table's floor. The
    // first cut raised the table's floor by a quarter - an explicit norm below the share in
    // force, i.e. a CUT that froze the issue for good (au6's worst village: issued 123 t
    // against the base's 252 t in year 9, hunger family-days 148 against 10, the free stock
    // at 0 every turn).
    std::map<std::uint32_t, core::Grams> in_force_of;
    for (const core::IssueNormLine& line : simulation.IssueNorms()) {
      in_force_of[line.resource.value] = line.grams_per_trudoden;
    }
    std::vector<core::OrderRow> orders;
    for (const auto& [resource, table] : table_grams_) {
      const auto found = in_force_of.find(resource);
      const core::Grams in_force = found != in_force_of.end() ? found->second : table;
      if (in_force <= 0) {
        continue;
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kSetIssueNorm;
      order.resource = core::ResourceId{static_cast<std::uint16_t>(resource)};
      order.amount = in_force + (in_force / kRaiseQuarter);
      orders.push_back(order);
      raised_.push_back(resource);
    }
    simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
    last_season_ = season;
    Year& row = by_year_[year];
    ++row.raises;
    row.positions += static_cast<std::uint32_t>(orders.size());
    row.raise_days.push_back((world.calendar.day % core::kDaysPerYear) + 1U);
    row.free_days_at_raise = free.days;
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const auto& [year, row] : by_year_) {
      out << "population_curve: IR2164 seed " << seed << " year " << year
          << " ground=issue_raised_when_a_tenth_short_and_the_free_stock_covers_it raises "
          << row.raises << " positions " << row.positions << " raise_days ";
      for (const std::uint32_t day : row.raise_days) {
        out << day << ';';
      }
      out << " free_days_at_raise " << row.free_days_at_raise << " free_days_at_turn "
          << row.free_days_at_turn << '\n';
    }
  }

 private:
  static constexpr float kGramsPerKg = 1000.0F;
  static constexpr core::Grams kRaiseQuarter = 4;

  struct Year {
    std::uint32_t raises = 0;
    std::vector<std::uint32_t> raise_days;
    float free_days_at_raise = -1.0F;
    float free_days_at_turn = -1.0F;
    std::uint32_t positions = 0;
  };

  std::map<std::uint32_t, core::Grams> table_grams_;
  std::uint32_t last_season_ = 0;
  std::vector<std::uint32_t> raised_;  ///< positions raised this year, reset at the turn
  std::map<std::uint32_t, Year> by_year_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_ISSUE_RAISE_POLICY_H_
