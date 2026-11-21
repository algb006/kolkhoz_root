/// @file
/// @brief #1922 (econ-boss-1922-dry-pantry-2026-11-20 [1]): DRY1922, a village-year y1-y6 -
///        the families whose pantry ran DRY, their dry days, and what carried them.
///
/// THE GROUND, printed with the line: «dry» is food at home under ONE DAY of the family's
/// need - food is every resource with kcal_per_gram > 0 (hay, wool, salt are not), the need is
/// members × food.csv adult_kg_grain_eq_per_year ÷ the year's days × the grain reference
/// kcal (children counted as adults: an over-count of the need, so «dry» is read generously).
/// On each dry day: whether the family's mean satiety was at or under the ration threshold
/// (the safety ration's ground), whether its pantry's food grew that day (any door: issue,
/// ration, garden, herd), and the food in every unit's stock (granary, church, cellar - and
/// the seed held for sowing, which the print does not take out: NOT all issuable).
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_DRY_PANTRY_TALLY_H_
#define TESTS_RUN_COMMON_DRY_PANTRY_TALLY_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class DryPantryTally {
 public:
  static constexpr std::uint32_t kYears = 16;  // y1-y16 since #1928's rerun (was 6)

  explicit DryPantryTally(const core::ITableSet& tables) {
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
  }

  /// @brief Once a day.
  void CountDay(const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    if (day == last_day_) {
      return;
    }
    last_day_ = day;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    if (day % core::kDaysPerYear == 0 && year >= 2 && year - 1U <= kYears) {
      CloseYear(year - 1U);
    }
    if (!settled_taken_) {  // the founders: the families standing on the first day read
      settled_taken_ = true;
      for (const core::FamilyId id : world.families.row_ids) {
        settled_.push_back(id.value);
      }
    }
    if (year > kYears) {
      return;
    }
    const std::size_t families = world.families.rows.size();
    std::vector<double> satiety(families, 0.0);
    std::vector<std::uint32_t> members(families, 0);
    for (const core::ResidentRow& resident : world.residents.rows) {
      const std::uint32_t row = core::FindRow(world.families, resident.family);
      if (row != core::kNoRow) {
        satiety[row] += static_cast<double>(resident.satiety);
        ++members[row];
      }
    }
    double store_kcal = -1.0;  // summed only on a day some family is dry
    const double need_a_member =
        adult_kg_a_year_ * 1000.0 / static_cast<double>(core::kDaysPerYear) * grain_kcal_;
    for (std::size_t row = 0; row < families; ++row) {
      if (members[row] == 0) {
        continue;
      }
      const std::uint32_t id = world.families.row_ids[row].value;
      const double held = Kcal(world.families.rows[row].pantry);
      double& before = held_before_[id];
      const bool grew = held > before + 0.5;
      before = held;
      if (held >= need_a_member * members[row]) {
        continue;
      }
      if (store_kcal < 0.0) {
        store_kcal = StoreKcal(world);
      }
      Family& family = year_families_[id];
      ++family.dry_days;
      family.members = members[row];
      family.settled = std::ranges::find(settled_, id) != settled_.end();
      family.below += satiety[row] / members[row] <= threshold_ ? 1U : 0U;
      family.fed += grew ? 1U : 0U;
      ++month_days_[(day % core::kDaysPerYear) / core::kDaysPerMonth];
      store_kg_on_dry_days_.push_back(store_kcal / grain_kcal_ / 1000.0);
    }
  }

  /// @brief The village's lines.
  void Print(std::ostream& out, std::uint64_t seed) const {
    std::istringstream lines(lines_.str());
    std::string line;
    while (std::getline(lines, line)) {
      out << "population_curve: DRY1922 seed " << seed << ' ' << line << '\n';
    }
  }

 private:
  struct Family {
    std::uint32_t dry_days = 0;
    std::uint32_t below = 0;
    std::uint32_t fed = 0;
    std::uint32_t members = 0;
    bool settled = false;
  };

  [[nodiscard]] double Kcal(const std::vector<core::Grams>& grams) const {
    double kcal = 0.0;
    for (std::size_t r = 0; r < grams.size() && r < kcal_per_gram_.size(); ++r) {
      kcal += static_cast<double>(grams[r]) * kcal_per_gram_[r];
    }
    return kcal;
  }

  [[nodiscard]] double StoreKcal(const core::WorldState& world) const {
    double kcal = 0.0;
    for (const core::UnitRow& unit : world.units.rows) {
      kcal += Kcal(unit.stock);
    }
    return kcal;
  }

  void CloseYear(std::uint32_t year) {
    std::vector<std::uint32_t> days;
    std::uint32_t below = 0;
    std::uint32_t fed = 0;
    std::uint32_t settled = 0;
    std::uint32_t small = 0;  // three members or fewer
    for (const auto& [id, family] : year_families_) {
      days.push_back(family.dry_days);
      below += family.below;
      fed += family.fed;
      settled += family.settled ? 1U : 0U;
      small += family.members <= 3 ? 1U : 0U;
    }
    std::ranges::sort(days);
    std::vector<double> kg = store_kg_on_dry_days_;
    std::ranges::sort(kg);
    const auto at = [](const auto& sorted, double share) {
      return sorted[static_cast<std::size_t>(share * static_cast<double>(sorted.size() - 1))];
    };
    lines_ << "year " << year << " ground dry=food_kcal_under_one_day_of_members_need families_dry "
           << days.size();
    if (!days.empty()) {
      std::uint32_t total = 0;
      for (const std::uint32_t one : days) {
        total += one;
      }
      lines_ << " dry_days " << total << " p50 " << at(days, 0.5) << " p90 " << at(days, 0.9)
             << " max " << days.back() << " of_them_at_or_under_ration_line " << below
             << " pantry_food_grew_on " << fed << " settled_families " << settled
             << " families_of_3_or_fewer " << small << " store_food_kg_grain_eq_on_dry_days p10 "
             << static_cast<long>(at(kg, 0.1)) << " p50 " << static_cast<long>(at(kg, 0.5))
             << " (seed held for sowing NOT taken out) by_month";
      for (const std::uint32_t month : month_days_) {
        lines_ << ' ' << month;
      }
    }
    lines_ << '\n';
    year_families_.clear();
    month_days_.fill(0);
    store_kg_on_dry_days_.clear();
  }

  std::vector<double> kcal_per_gram_;
  double adult_kg_a_year_ = 300.0;
  double grain_kcal_ = 3.3;
  double threshold_ = 40.0;
  std::vector<std::uint32_t> settled_;
  bool settled_taken_ = false;
  std::map<std::uint32_t, double> held_before_;
  std::map<std::uint32_t, Family> year_families_;
  std::array<std::uint32_t, core::kMonthsPerYear> month_days_{};
  std::vector<double> store_kg_on_dry_days_;
  core::SimDay last_day_ = static_cast<core::SimDay>(-1);
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_DRY_PANTRY_TALLY_H_
