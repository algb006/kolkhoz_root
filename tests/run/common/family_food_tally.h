/// @file
/// @brief #1953 (econ-boss-1928-cap-read-2026-11-21 [4]): FAM1953, a village-year y9-y16 - on
///        every DRY family-day, what the family got and why the issue did not carry it.
///
/// THE GROUND, printed with the line. «Dry» is DRY1922's: food at home under ONE DAY of the
/// members' need (children counted as adults). On each dry family-day, read off the state
/// against the day before:
/// - the issue reached it: trudodni_redeemed rose (the distribution covers account - redeemed);
/// - the advance reached it: advance_given rose;
/// - the pantry's food rose with neither: the ration, a barter or the family's own (not split);
/// - own food: own_kcal_mean (the 7-day mean of the plot, yard, nets, forest) over the need;
/// - earned and not yet issued: (trudodni_account - trudodni_redeemed) a member;
/// - able members: 16 biological years and older (life.csv life_speedup);
/// - the ration's door open: chairman ration_auto, or the family's ration_granted.
/// The village on those days, from the core's OWN door (ISimulation::IssueNorms, the code the
/// distribution runs): the stores' food, against what of it is FREE over the funds (free_grams;
/// a substitute's stock counted in its category too, so the free sum is an UPPER bound) and the
/// norm in kcal a trudoden. The issue is NOT hauled: family_exchange.cpp's TakeFromUnits puts it
/// in the pantry the same step - no cart, no hand-days to print.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_FAMILY_FOOD_TALLY_H_
#define TESTS_RUN_COMMON_FAMILY_FOOD_TALLY_H_

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/issue_norm_view.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class FamilyFoodTally {
 public:
  static constexpr std::uint32_t kFirstYear = 9;
  static constexpr std::uint32_t kLastYear = 16;

  explicit FamilyFoodTally(const core::ITableSet& tables) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      const std::uint32_t kcal = resources->FindColumn("kcal_per_gram");
      for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
        const std::string_view text =
            kcal == core::kNoTableColumn ? std::string_view{} : resources->CellText(row, kcal);
        kcal_per_gram_.push_back(text.empty() ? 0.0 : std::stod(std::string(text)));
      }
    }
    if (const core::ITable* const food = tables.FindTable("food")) {
      const std::uint32_t value = food->FindColumn("value");
      for (std::uint32_t row = 0; row < food->RowCount(); ++row) {
        const std::string_view key = food->CellText(row, 0);
        const std::string_view text =
            value == core::kNoTableColumn ? std::string_view{} : food->CellText(row, value);
        if (text.empty()) {
          continue;
        }
        if (key == "adult_kg_grain_eq_per_year") {
          adult_kg_a_year_ = std::stod(std::string(text));
        } else if (key == "grain_reference_kcal_per_gram") {
          grain_kcal_ = std::stod(std::string(text));
        } else if (key == "ration_satiety_threshold") {
          threshold_ = std::stod(std::string(text));
        }
      }
    }
    if (const core::ITable* const life = tables.FindTable("life")) {
      const std::uint32_t value = life->FindColumn("value");
      for (std::uint32_t row = 0; row < life->RowCount(); ++row) {
        if (life->CellText(row, 0) == "life_speedup" && value != core::kNoTableColumn) {
          life_speedup_ = std::stod(std::string(life->CellText(row, value)));
        }
      }
    }
  }

  /// @brief Once a day.
  void CountDay(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const core::SimDay day = world.calendar.day;
    if (day == last_day_) {
      return;
    }
    last_day_ = day;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    if (day % core::kDaysPerYear == 0 && year >= kFirstYear + 1U && year - 1U <= kLastYear) {
      CloseYear(year - 1U);
    }
    const std::size_t families = world.families.rows.size();
    std::vector<double> satiety(families, 0.0);
    std::vector<std::uint32_t> members(families, 0);
    std::vector<std::uint32_t> able(families, 0);
    for (const core::ResidentRow& resident : world.residents.rows) {
      const std::uint32_t row = core::FindRow(world.families, resident.family);
      if (row == core::kNoRow) {
        continue;
      }
      satiety[row] += static_cast<double>(resident.satiety);
      ++members[row];
      const double bio_years = static_cast<double>(static_cast<std::int64_t>(day) -
                                                   static_cast<std::int64_t>(resident.birth_day)) /
                               static_cast<double>(core::kDaysPerYear) * life_speedup_;
      able[row] += bio_years >= 16.0 ? 1U : 0U;
    }
    const bool in_window = year >= kFirstYear && year <= kLastYear;
    const double need_a_member =
        adult_kg_a_year_ * 1000.0 / static_cast<double>(core::kDaysPerYear) * grain_kcal_;
    bool village_read = false;
    for (std::size_t row = 0; row < families; ++row) {
      const core::FamilyRow& family = world.families.rows[row];
      const std::uint32_t id = world.families.row_ids[row].value;
      const double held = Kcal(family.pantry);
      Before& before = before_[id];
      const bool issued = family.trudodni_redeemed > before.redeemed;
      const bool advanced = family.advance_given > before.advance_given;
      const bool grew = held > before.held + 0.5;
      before = Before{.held = held,
                      .redeemed = family.trudodni_redeemed,
                      .advance_given = family.advance_given};
      if (!in_window || members[row] == 0 || held >= need_a_member * members[row]) {
        continue;
      }
      if (!village_read) {
        village_read = true;
        ReadVillage(simulation, world);
      }
      const double need = need_a_member * members[row];
      ++year_.dry_family_days;
      year_.at_line += satiety[row] / members[row] <= threshold_ ? 1U : 0U;
      year_.issued += issued ? 1U : 0U;
      year_.advanced += advanced ? 1U : 0U;
      year_.grew_other += grew && !issued && !advanced ? 1U : 0U;
      year_.ration_open += world.chairman.ration_auto != 0 || family.ration_granted != 0 ? 1U : 0U;
      year_.no_able += able[row] == 0 ? 1U : 0U;
      const double unissued =
          static_cast<double>(family.trudodni_account - family.trudodni_redeemed) / 100.0;
      year_.unissued_a_member.push_back(unissued / members[row]);
      year_.zero_unissued += unissued <= 0.0 ? 1U : 0U;
      year_.own_share.push_back(static_cast<double>(family.own_kcal_mean) / need);
      // What the norm in force would hand this family for its unissued trudodni, in days of
      // its need: the issue's ceiling for it that day.
      year_.earned_days.push_back(unissued * village_.kcal_a_trudoden / need);
      year_.store_t.push_back(village_.store_kcal / grain_kcal_ / 1.0e6);
      year_.free_t.push_back(village_.free_kcal / grain_kcal_ / 1.0e6);
    }
  }

  /// @brief The village's lines.
  void Print(std::ostream& out, std::uint64_t seed) const {
    std::istringstream lines(lines_.str());
    std::string line;
    while (std::getline(lines, line)) {
      out << "population_curve: FAM1953 seed " << seed << ' ' << line << '\n';
    }
  }

 private:
  struct Before {
    double held = 0.0;
    core::TrudodniHundredths redeemed = 0;
    core::Grams advance_given = 0;
  };

  struct Year {
    std::uint32_t dry_family_days = 0;
    std::uint32_t at_line = 0;
    std::uint32_t issued = 0;
    std::uint32_t advanced = 0;
    std::uint32_t grew_other = 0;
    std::uint32_t ration_open = 0;
    std::uint32_t no_able = 0;
    std::uint32_t zero_unissued = 0;
    std::vector<double> unissued_a_member;
    std::vector<double> own_share;
    std::vector<double> earned_days;
    std::vector<double> store_t;
    std::vector<double> free_t;
  };

  struct Village {
    double store_kcal = 0.0;
    double free_kcal = 0.0;
    double kcal_a_trudoden = 0.0;
  };

  [[nodiscard]] double KcalOf(std::uint32_t resource) const {
    return resource < kcal_per_gram_.size() ? kcal_per_gram_[resource] : 0.0;
  }

  [[nodiscard]] double Kcal(const std::vector<core::Grams>& grams) const {
    double kcal = 0.0;
    for (std::size_t r = 0; r < grams.size(); ++r) {
      kcal += static_cast<double>(grams[r]) * KcalOf(static_cast<std::uint32_t>(r));
    }
    return kcal;
  }

  void ReadVillage(const core::ISimulation& simulation, const core::WorldState& world) {
    village_ = Village{};
    for (const core::UnitRow& unit : world.units.rows) {
      village_.store_kcal += Kcal(unit.stock);
    }
    for (const core::IssueNormLine& line : simulation.IssueNorms()) {
      const double kcal = KcalOf(line.resource.value);
      village_.free_kcal += static_cast<double>(line.free_grams) * kcal;
      village_.kcal_a_trudoden += static_cast<double>(line.grams_per_trudoden) * kcal;
    }
  }

  static double At(std::vector<double> values, double share) {
    if (values.empty()) {
      return 0.0;
    }
    std::ranges::sort(values);
    return values[static_cast<std::size_t>(share * static_cast<double>(values.size() - 1))];
  }

  void CloseYear(std::uint32_t year) {
    lines_ << "year " << year
           << " ground dry=food_kcal_under_one_day_of_members_need dry_family_days "
           << year_.dry_family_days;
    if (year_.dry_family_days > 0) {
      lines_ << " at_or_under_ration_line " << year_.at_line << " | got: issue " << year_.issued
             << " advance " << year_.advanced << " other_growth(ration/barter/own) "
             << year_.grew_other << " | ration_door_open " << year_.ration_open
             << " | family: no_able_member " << year_.no_able << " zero_unissued_trudodni "
             << year_.zero_unissued << " unissued_trudodni_a_member p10 "
             << At(year_.unissued_a_member, 0.1) << " p50 " << At(year_.unissued_a_member, 0.5)
             << " own_food_share_of_need p50 " << At(year_.own_share, 0.5)
             << " issue_ceiling_days_of_need(unissued x norm) p10 " << At(year_.earned_days, 0.1)
             << " p50 " << At(year_.earned_days, 0.5)
             << " | village on those days: store_food_t p10 " << At(year_.store_t, 0.1) << " p50 "
             << At(year_.store_t, 0.5) << " free_over_funds_t(upper bound) p10 "
             << At(year_.free_t, 0.1) << " p50 " << At(year_.free_t, 0.5)
             << " | the issue is not hauled (TakeFromUnits)";
    }
    lines_ << '\n';
    year_ = Year{};
  }

  std::vector<double> kcal_per_gram_;
  double adult_kg_a_year_ = 300.0;
  double grain_kcal_ = 3.3;
  double threshold_ = 40.0;
  double life_speedup_ = 4.0;
  std::map<std::uint32_t, Before> before_;
  Year year_;
  Village village_;
  core::SimDay last_day_ = static_cast<core::SimDay>(-1);
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FAMILY_FOOD_TALLY_H_
