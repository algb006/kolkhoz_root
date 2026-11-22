/// @file
/// @brief #1953's cause print (econ-boss-1953-cause-print-2026-11-22 [1], econ's two answers):
///        CAUSE1953, a village-year y9-y16 - why (A)'s advance did not lift the families at the
///        ration line. Rides the same (A) arm and its base.
///
/// THE GROUND, printed with the lines; «dry» and «at the line» are FAM1953's.
/// (1) An advance is a day advance_given rose: kg given (grain equivalent) over the family's
///     day need = the days it covers.
/// (2) After it: the pantry the next day over a day's need, and the family's mean satiety on
///     days +1, +2, +3 less the advance day's.
/// (3) A repayment is a day advance_debt fell on a day that is not the year's first (the
///     write-off day): kg withheld. Dry days in the 10 after a repayment against the 10 before
///     the advance that opened the debt (the last advance before it).
/// (4) Per at-line family-day: eaters (members), workers (the advance's age band), eaters per
///     worker, and THE FLOW = the family's trudodni_account rise (redemptions not counted)
///     averaged over its PRECEDING 10 working days (a working day: the village's accounts rose)
///     x the issue per trudoden in force that day (IssueNorms, grain-eq) / the family's day
///     need. «short» when fewer than 10 working days stand behind it. Per worker as a column.
/// (5) The same flow for families never at the line that year, over all their days.
/// At the year line: the closed year's REALISED issue per trudoden (book issued in grain-eq /
/// trudodni_accrued) beside the in-force mean, their gap, and both groups' flow medians
/// recomputed on the realised one (econ: over 20 % apart, H2 reads the realised one).
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_FAMILY_CAUSE_TALLY_H_
#define TESTS_RUN_COMMON_FAMILY_CAUSE_TALLY_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/issue_norm_view.h"
#include "core_common/ledger_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class FamilyCauseTally {
 public:
  static constexpr std::uint32_t kFirstYear = 9;
  static constexpr std::uint32_t kLastYear = 16;
  static constexpr std::size_t kWindow = 10;

  explicit FamilyCauseTally(const core::ITableSet& tables) {
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
        const double number = std::stod(std::string(text));
        if (key == "adult_kg_grain_eq_per_year") {
          adult_kg_a_year_ = number;
        } else if (key == "grain_reference_kcal_per_gram") {
          grain_kcal_ = number;
        } else if (key == "ration_satiety_threshold") {
          threshold_ = number;
        } else if (key == "adult_from_bio_years") {
          adult_from_ = number;
        } else if (key == "elderly_from_bio_years") {
          elderly_from_ = number;
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
    const bool year_first_day = day % core::kDaysPerYear == 0;
    if (year_first_day && year >= kFirstYear + 1U && year - 1U <= kLastYear) {
      CloseYear(year - 1U, world.ledger.closed);
    }
    const bool in_window = year >= kFirstYear && year <= kLastYear;
    // The members, the workers and the mean satiety by family row.
    const std::size_t families = world.families.rows.size();
    std::vector<double> satiety(families, 0.0);
    std::vector<std::uint32_t> members(families, 0);
    std::vector<std::uint32_t> workers(families, 0);
    for (const core::ResidentRow& resident : world.residents.rows) {
      const std::uint32_t row = core::FindRow(world.families, resident.family);
      if (row == core::kNoRow) {
        continue;
      }
      satiety[row] += static_cast<double>(resident.satiety);
      ++members[row];
      const double bio = static_cast<double>(static_cast<std::int64_t>(day) -
                                             static_cast<std::int64_t>(resident.birth_day)) /
                         static_cast<double>(core::kDaysPerYear) * life_speedup_;
      workers[row] += bio >= adult_from_ && bio < elderly_from_ ? 1U : 0U;
    }
    // The issue per trudoden in force today, grams of grain equivalent.
    double norm_g = 0.0;
    if (in_window) {
      for (const core::IssueNormLine& line : simulation.IssueNorms()) {
        const double kcal =
            line.resource.value < kcal_per_gram_.size() ? kcal_per_gram_[line.resource.value] : 0.0;
        norm_g += static_cast<double>(line.grams_per_trudoden) * kcal / grain_kcal_;
      }
      year_.norm_g_sum += norm_g;
      ++year_.norm_days;
    }
    // A working day: any family's account rose today.
    bool working_day = false;
    for (std::size_t row = 0; row < families; ++row) {
      const std::uint32_t id = world.families.row_ids[row].value;
      const Memory& memory = memory_[id];
      if (world.families.rows[row].trudodni_account > memory.account) {
        working_day = true;
        break;
      }
    }
    const double need_a_member_g =
        adult_kg_a_year_ * 1000.0 / static_cast<double>(core::kDaysPerYear);
    for (std::size_t row = 0; row < families; ++row) {
      const core::FamilyRow& family = world.families.rows[row];
      const std::uint32_t id = world.families.row_ids[row].value;
      Memory& memory = memory_[id];
      const double rise =
          family.trudodni_account > memory.account && !year_first_day
              ? static_cast<double>(family.trudodni_account - memory.account) / 100.0
              : 0.0;
      if (working_day) {
        memory.rises.push_back(rise);
        if (memory.rises.size() > kWindow) {
          memory.rises.pop_front();
        }
      }
      const double need_g = need_a_member_g * static_cast<double>(members[row]);
      const double held_g = GrainEq(family.pantry);
      const double mean_satiety = members[row] > 0 ? satiety[row] / members[row] : 0.0;
      const bool dry = members[row] > 0 && held_g < need_g;
      const bool at_line = members[row] > 0 && mean_satiety <= threshold_;
      if (in_window && members[row] > 0) {
        memory.dry_by_day[day] = dry;
        // (2) the open advances' follow-ups.
        for (Advance& open : memory.open) {
          const core::SimDay after = day - open.day;
          if (after == 1) {
            year_.next_pantry_days.push_back(need_g > 0.0 ? held_g / need_g : 0.0);
          }
          if (after >= 1 && after <= 3) {
            year_.satiety_moves[after - 1].push_back(mean_satiety - open.satiety);
          }
        }
        std::erase_if(memory.open, [day](const Advance& a) { return day - a.day >= 3; });
        // (1) an advance today.
        if (family.advance_given > memory.advance_given && need_g > 0.0) {
          const double given = static_cast<double>(family.advance_given - memory.advance_given);
          year_.advance_days_covered.push_back(given / need_g);
          memory.open.push_back(Advance{.day = day, .satiety = mean_satiety});
          memory.last_advance_day = day;
          memory.has_advance = true;
        }
        // (3) a repayment today (the debt fell, not on the year's first day: the write-off's).
        if (family.advance_debt < memory.advance_debt && !year_first_day) {
          year_.repaid_kg.push_back(static_cast<double>(memory.advance_debt - family.advance_debt) /
                                    1000.0);
          if (memory.has_advance) {
            pending_repayments_.push_back(
                Repayment{.family = id, .day = day, .advance_day = memory.last_advance_day});
          }
        }
        // (4) and (5) the flow.
        const double mean_rise = memory.rises.empty() ? 0.0 : [&memory] {
          double sum = 0.0;
          for (const double one : memory.rises) {
            sum += one;
          }
          return sum / static_cast<double>(memory.rises.size());
        }();
        const bool short_window = memory.rises.size() < kWindow;
        const double flow = need_g > 0.0 ? mean_rise * norm_g / need_g : 0.0;
        FamilyYear& family_year = year_.families[id];
        family_year.flow_rows.push_back(FlowRow{.mean_rise = mean_rise, .need_g = need_g});
        family_year.flows.push_back(flow);
        family_year.at_line = family_year.at_line || at_line;
        if (at_line) {
          ++year_.at_line_rows;
          year_.short_rows += short_window ? 1U : 0U;
          year_.eaters.push_back(static_cast<double>(members[row]));
          year_.workers.push_back(static_cast<double>(workers[row]));
          if (workers[row] > 0) {
            year_.eaters_per_worker.push_back(static_cast<double>(members[row]) / workers[row]);
            year_.rise_per_worker.push_back(mean_rise / workers[row]);
          }
          year_.at_line_flows.push_back(flow);
        }
      }
      memory.account = family.trudodni_account;
      memory.advance_given = family.advance_given;
      memory.advance_debt = family.advance_debt;
    }
    ResolveRepayments(day);
  }

  /// @brief The village's lines.
  void Print(std::ostream& out, std::uint64_t seed) const {
    std::istringstream lines(lines_.str());
    std::string line;
    while (std::getline(lines, line)) {
      out << "population_curve: CAUSE1953 seed " << seed << ' ' << line << '\n';
    }
  }

 private:
  struct Advance {
    core::SimDay day = 0;
    double satiety = 0.0;
  };

  struct Repayment {
    std::uint32_t family = 0;
    core::SimDay day = 0;
    core::SimDay advance_day = 0;
  };

  struct Memory {
    core::TrudodniHundredths account = 0;
    core::Grams advance_given = 0;
    core::Grams advance_debt = 0;
    std::deque<double> rises;
    std::vector<Advance> open;
    std::map<core::SimDay, bool> dry_by_day;
    core::SimDay last_advance_day = 0;
    bool has_advance = false;
  };

  struct FlowRow {
    double mean_rise = 0.0;
    double need_g = 0.0;
  };

  struct FamilyYear {
    bool at_line = false;
    std::vector<double> flows;
    std::vector<FlowRow> flow_rows;
  };

  struct Year {
    std::vector<double> advance_days_covered;
    std::vector<double> next_pantry_days;
    std::array<std::vector<double>, 3> satiety_moves;
    std::vector<double> repaid_kg;
    std::vector<double> dry_after_repayment;
    std::vector<double> dry_before_advance;
    std::uint32_t at_line_rows = 0;
    std::uint32_t short_rows = 0;
    std::vector<double> eaters;
    std::vector<double> workers;
    std::vector<double> eaters_per_worker;
    std::vector<double> rise_per_worker;
    std::vector<double> at_line_flows;
    std::map<std::uint32_t, FamilyYear> families;
    double norm_g_sum = 0.0;
    std::uint32_t norm_days = 0;
  };

  [[nodiscard]] double GrainEq(const std::vector<core::Grams>& grams) const {
    double kcal = 0.0;
    for (std::size_t r = 0; r < grams.size() && r < kcal_per_gram_.size(); ++r) {
      kcal += static_cast<double>(grams[r]) * kcal_per_gram_[r];
    }
    return kcal / grain_kcal_;
  }

  /// A repayment's 10 days after are counted once they have passed.
  void ResolveRepayments(core::SimDay today) {
    std::erase_if(pending_repayments_, [&](const Repayment& paid) {
      if (today < paid.day + kWindow) {
        return false;
      }
      const Memory& memory = memory_[paid.family];
      const auto dry_in = [&memory](core::SimDay from, core::SimDay to) {
        double count = 0.0;
        for (core::SimDay day = from; day < to; ++day) {
          const auto found = memory.dry_by_day.find(day);
          count += found != memory.dry_by_day.end() && found->second ? 1.0 : 0.0;
        }
        return count;
      };
      year_.dry_after_repayment.push_back(dry_in(paid.day + 1, paid.day + 1 + kWindow));
      const core::SimDay before = paid.advance_day >= kWindow ? paid.advance_day - kWindow : 0;
      year_.dry_before_advance.push_back(dry_in(before, paid.advance_day));
      return true;
    });
  }

  static double At(std::vector<double> values, double share) {
    if (values.empty()) {
      return 0.0;
    }
    std::ranges::sort(values);
    return values[static_cast<std::size_t>(share * static_cast<double>(values.size() - 1))];
  }

  static double Mean(const std::vector<double>& values) {
    if (values.empty()) {
      return 0.0;
    }
    double sum = 0.0;
    for (const double one : values) {
      sum += one;
    }
    return sum / static_cast<double>(values.size());
  }

  [[nodiscard]] double IssuedGrainEq(const core::YearLedger& book) const {
    return GrainEq(std::vector<core::Grams>(book.issued.begin(), book.issued.end()));
  }

  void CloseYear(std::uint32_t year, const core::YearLedger& closed) {
    const double accrued = static_cast<double>(closed.trudodni_accrued) / 100.0;
    const double realised_g = accrued > 0.0 ? IssuedGrainEq(closed) / accrued : 0.0;
    const double norm_mean_g = year_.norm_days > 0 ? year_.norm_g_sum / year_.norm_days : 0.0;
    std::vector<double> at_line_family_flow;
    std::vector<double> never_family_flow;
    std::vector<double> at_line_family_realised;
    std::vector<double> never_family_realised;
    for (const auto& [id, family] : year_.families) {
      const double flow = Mean(family.flows);
      double realised_sum = 0.0;
      for (const FlowRow& row : family.flow_rows) {
        realised_sum += row.need_g > 0.0 ? row.mean_rise * realised_g / row.need_g : 0.0;
      }
      const double realised = family.flow_rows.empty()
                                  ? 0.0
                                  : realised_sum / static_cast<double>(family.flow_rows.size());
      (family.at_line ? at_line_family_flow : never_family_flow).push_back(flow);
      (family.at_line ? at_line_family_realised : never_family_realised).push_back(realised);
    }
    const double gap = norm_mean_g > 0.0 ? (realised_g - norm_mean_g) / norm_mean_g : 0.0;
    lines_ << "year " << year << " ground=FAM1953_dry_and_line; flow=family_account_rise_mean_"
           << "preceding_10_working_days x issue_per_trudoden / day_need"
           << " | (1) advances " << year_.advance_days_covered.size() << " days_covered p10 "
           << At(year_.advance_days_covered, 0.1) << " p50 " << At(year_.advance_days_covered, 0.5)
           << " p90 " << At(year_.advance_days_covered, 0.9) << " | (2) next_day_pantry_days p50 "
           << At(year_.next_pantry_days, 0.5) << " satiety_move_p50 +1 "
           << At(year_.satiety_moves[0], 0.5) << " +2 " << At(year_.satiety_moves[1], 0.5) << " +3 "
           << At(year_.satiety_moves[2], 0.5) << " | (3) repayments " << year_.repaid_kg.size()
           << " kg_p50 " << At(year_.repaid_kg, 0.5) << " dry_days_10_after_repayment_mean "
           << Mean(year_.dry_after_repayment) << " dry_days_10_before_advance_mean "
           << Mean(year_.dry_before_advance) << " (pairs " << year_.dry_after_repayment.size()
           << ")"
           << " | (4) at_line_rows " << year_.at_line_rows << " short_window " << year_.short_rows
           << " eaters_p50 " << At(year_.eaters, 0.5) << " workers_p50 " << At(year_.workers, 0.5)
           << " eaters_per_worker_p50 " << At(year_.eaters_per_worker, 0.5)
           << " rise_per_worker_p50 " << At(year_.rise_per_worker, 0.5) << " flow_in_force p10 "
           << At(year_.at_line_flows, 0.1) << " p50 " << At(year_.at_line_flows, 0.5) << " p90 "
           << At(year_.at_line_flows, 0.9) << " | (5) family_flow_in_force_p50 at_line "
           << At(at_line_family_flow, 0.5) << " (" << at_line_family_flow.size()
           << ") never_at_line " << At(never_family_flow, 0.5) << " (" << never_family_flow.size()
           << ")"
           << " | year: issue_per_trudoden_g in_force_mean " << norm_mean_g << " realised "
           << realised_g << " gap " << gap << (gap > 0.2 || gap < -0.2 ? " OVER_20PCT" : "")
           << " family_flow_realised_p50 at_line " << At(at_line_family_realised, 0.5)
           << " never_at_line " << At(never_family_realised, 0.5) << '\n';
    year_ = Year{};
    for (auto& [id, memory] : memory_) {
      memory.dry_by_day.clear();
    }
  }

  std::vector<double> kcal_per_gram_;
  double adult_kg_a_year_ = 300.0;
  double grain_kcal_ = 3.3;
  double threshold_ = 40.0;
  double adult_from_ = 16.0;
  double elderly_from_ = 60.0;
  double life_speedup_ = 4.0;
  std::map<std::uint32_t, Memory> memory_;
  std::vector<Repayment> pending_repayments_;
  Year year_;
  core::SimDay last_day_ = static_cast<core::SimDay>(-1);
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FAMILY_CAUSE_TALLY_H_
