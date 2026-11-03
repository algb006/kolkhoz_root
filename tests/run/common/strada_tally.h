/// @file
/// @brief The strada's work and the yards' help, y1-y3 - #1442's pair print
/// (the acting chairman's «в страду до трети ухода за стадом берут дворы —
/// за трудодни»).
/// @threading SINGLE_THREADED
/// Test-side code, read from the thread that owns the simulation.
///
/// WHY IT EXISTS (econ's gates via boss, core-boss-1421-arms [9] §2 and
/// core-boss-yards-strada [3]):
/// - P: the strada's Jun-Aug kolkhoz field and hay worker-days, y1-y3, read
///   at the «noon» step (the state's hour 13): field = ploughing, harrowing,
///   sowing and reaping off a meadow; hay = reaping on a meadow. Worker-days
///   at noon, not man-days of norm.
/// - the herd care the yards took (the closed book's herd_care_by_yards);
/// - sides: grain per trudoden at the year's distribution (the closed book's
///   issued kg of every resource over its trudodni accrued, y1-y3); the old
///   households' hungry family-days y1-y2 as host's plan700 counts them
///   (hungry_family_days_old: a family formed at genesis or a game year
///   before, whose members' mean satiety is under kHungrySatiety; read here at
///   hour 22), with the old family-days beside as the base; herd care undone at
///   the day's end;
/// - print, not gate: the yards' plot hours by family, Jun-Aug y1-y3 - the
///   mean over yards of the share of their gross hours the herds took, and
///   the worst yard's share.

#ifndef TESTS_RUN_COMMON_STRADA_TALLY_H_
#define TESTS_RUN_COMMON_STRADA_TALLY_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <ostream>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/family_state.h"
#include "core_common/herd_state.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class StradaTally {
 public:
  static constexpr std::uint32_t kYears = 3;
  /// The old households' hunger line (core's STUB reading of econ's gate).
  static constexpr float kHungrySatiety = 40.0F;
  static constexpr std::uint32_t kEveningHour = 22;
  /// K3's winter line (econ, econ-boss-1442-garden-price [1]).
  static constexpr float kWinterSatiety = 70.0F;

  /// Reads the garden's full-yield hours (food.csv plot_full_yield_hours), the
  /// curve K2's in-world loss is read on.
  explicit StradaTally(const core::ITableSet& tables) {
    // The kolkhoz herds' milk y1-y3 (econ's S4 on #1446; the closed book's
    // herd_produce, as M1417 reads y1).
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      milk_ = resources->FindRowByKey("milk");
    }
    if (const core::ITable* const livestock = tables.FindTable("livestock")) {
      cow_ = livestock->FindRowByKey("cow");
      horse_ = livestock->FindRowByKey("horse");
    }
    for (auto& months : coverage_min_) {
      months.fill(1.0F);
    }
    const core::ITable* const food = tables.FindTable("food");
    if (food == nullptr) {
      return;
    }
    const std::uint32_t value = food->FindColumn("value");
    const std::uint32_t kcal = food->FindColumn("kcal_per_gram");
    const auto number = [&](const char* key, std::uint32_t column, float fallback) {
      const std::uint32_t row = food->FindRowByKey(key);
      return row == core::kNoTableRow || column == core::kNoTableColumn
                 ? fallback
                 : static_cast<float>(food->CellReal(row, column).value_or(fallback));
    };
    full_yield_hours_ = number("plot_full_yield_hours", value, 4.0F);
    // The net-food print (econ, econ-boss-1442 via boss [16]): a garden at full
    // yield in kcal, and every issued resource's kcal by its resource id.
    garden_full_kcal_ = static_cast<double>(number("potato_kg_per_yard_year", value, 0.0F)) *
                            1000.0 * static_cast<double>(number("potato", kcal, 0.0F)) +
                        static_cast<double>(number("vegetables_kg_per_yard_year", value, 0.0F)) *
                            1000.0 * static_cast<double>(number("vegetables", kcal, 0.0F));
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      kcal_per_gram_.assign(resources->RowCount(), 0.0F);
      for (std::uint32_t row = 0; row < food->RowCount(); ++row) {
        const std::uint32_t resource = resources->FindRowByKey(food->CellText(row, 0));
        if (resource < kcal_per_gram_.size() && kcal != core::kNoTableColumn) {
          kcal_per_gram_[resource] = static_cast<float>(food->CellReal(row, kcal).value_or(0.0F));
        }
      }
    }
    if (const core::ITable* const labor = tables.FindTable("labor")) {
      const std::uint32_t row = labor->FindRowByKey("herd_care");
      const std::uint32_t column = labor->FindColumn("trudodni_rate");
      if (row != core::kNoTableRow && column != core::kNoTableColumn) {
        care_rate_ = static_cast<float>(labor->CellReal(row, column).value_or(1.0F));
      }
    }
  }

  /// @brief The strada's field and hay worker-days, at the state's hour 13.
  void CountNoon(const core::WorldState& world) {
    if (!InStrada(world)) {
      return;
    }
    for (const core::ResidentRow& resident : world.residents.rows) {
      const core::WorkKind kind = resident.work.kind;
      if (kind == core::WorkKind::kHarvest) {
        ++(OnMeadow(world, resident.work.field) ? hay_wd_ : field_wd_);
      } else if (kind == core::WorkKind::kPlowing || kind == core::WorkKind::kHarrowing ||
                 kind == core::WorkKind::kSowing) {
        ++field_wd_;
      } else if (kind == core::WorkKind::kHerdCare) {
        ++herd_wd_;
      }
    }
  }

  /// @brief At the state's hour 22, after the plot (hour 22's metrics slot):
  ///        the yards' hours and the old households' hunger.
  void CountEvening(const core::WorldState& world) {
    const std::uint32_t year = world.calendar.date.year;
    if (year < 1 || year > kYears + 1) {  // y4 for K3's last winter only
      return;
    }
    if (yard_given_.size() < world.families.rows.size()) {
      yard_given_.resize(world.families.rows.size(), 0.0);
      yard_gross_.resize(world.families.rows.size(), 0.0);
    }
    std::vector<float> satiety_sum(world.families.rows.size(), 0.0F);
    std::vector<std::uint32_t> members(world.families.rows.size(), 0);
    for (const core::ResidentRow& resident : world.residents.rows) {
      const std::uint32_t family = core::FindRow(world.families, resident.family);
      if (family == core::kNoRow) {
        continue;
      }
      ++members[family];
      satiety_sum[family] += resident.satiety;
    }
    const bool strada = InStrada(world);
    // The day's share of every yard's gross plot hours (core's spread (b)):
    // gross read back from the net as labour reads it (kGrossShareCap 0.95).
    const float share = world.ledger.current.yard_herd_share_today;
    const float held = share < 0.95F ? share : 0.95F;
    const auto month = static_cast<std::uint32_t>(world.calendar.date.month);  // 0-based
    // K3's winter after each strada: Oct-Dec of y1-y3 and Jan-Apr of y2-y4.
    const bool winter = (month >= 9 && year <= kYears) || (month <= 3 && year >= 2);
    // THE YARDS' TRUDODNI TODAY, by family (the net-food print): the day's
    // man-days off the book's running sum, spread by today's gross hours as
    // labour spreads them by yesterday's - an ESTIMATE, the core keeps no
    // per-family figure. A book that turned since yesterday starts from nought.
    const double by_yards = static_cast<double>(world.ledger.current.herd_care_by_yards);
    const double day_md = by_yards >= prev_by_yards_ ? by_yards - prev_by_yards_ : by_yards;
    prev_by_yards_ = by_yards;
    if (yard_td_.size() < world.families.rows.size()) {
      yard_td_.resize(world.families.rows.size(), 0.0);
    }
    double gross_all = 0.0;
    const auto gross_of = [held](const core::FamilyRow& family) {
      return family.house.value != core::kInvalidEntityIdValue && family.in_barrack == 0 &&
                     family.household_hours > 0.0F
                 ? static_cast<double>(family.household_hours) / (1.0 - static_cast<double>(held))
                 : 0.0;
    };
    for (const core::FamilyRow& family : world.families.rows) {
      gross_all += gross_of(family);
    }
    for (std::uint32_t row = 0; row < world.families.rows.size(); ++row) {
      const core::FamilyRow& family = world.families.rows[row];
      if (day_md > 0.0 && gross_all > 0.0) {
        yard_td_[row] += day_md * static_cast<double>(care_rate_) * gross_of(family) / gross_all;
      }
      // OLD as host's plan700 FamilyIsOld (#1413's read; econ via boss,
      // core-boss-yards-strada [6]): formed at genesis or a game year before.
      const bool old = family.formed_day == core::kNotFormedInPlay ||
                       world.calendar.day >= family.formed_day + core::kDaysPerYear;
      const float mean_satiety =
          members[row] > 0 ? satiety_sum[row] / static_cast<float>(members[row]) : 100.0F;
      if (year <= 2 && old && members[row] > 0) {
        ++old_family_days_;
        if (mean_satiety < kHungrySatiety) {
          ++old_hungry_family_days_;
        }
      }
      if (winter && old && members[row] > 0) {
        ++winter_old_family_days_;
        if (mean_satiety < kWinterSatiety) {
          ++winter_old_below70_days_;
        }
      }
      if (strada && share > 0.0F && family.house.value != core::kInvalidEntityIdValue &&
          family.in_barrack == 0 && family.household_hours > 0.0F) {
        const double gross =
            static_cast<double>(family.household_hours) / (1.0 - static_cast<double>(held));
        yard_given_[row] += gross - static_cast<double>(family.household_hours);
        yard_gross_[row] += gross;
      }
      // K1: the garden's season ratio of the evening before it pays (the
      // harvest resets it to nought): its share of the full yield.
      if (row >= last_ratio_.size()) {
        last_ratio_.resize(world.families.rows.size(), -1.0F);
        with_sum_.resize(world.families.rows.size(), 0.0);
        without_sum_.resize(world.families.rows.size(), 0.0);
      }
      const float ratio = family.plot_ratio_days > 0
                              ? family.plot_ratio_sum / static_cast<float>(family.plot_ratio_days)
                              : -1.0F;
      if (ratio < 0.0F && last_ratio_[row] >= 0.0F && year <= kYears) {
        // K2 IN ONE WORLD: the season's attention with the yards' help over
        // the same season without it - the help's own price, free of the two
        // arms' divergence (a family of the twin arm is another family by y2).
        const double inworld =
            without_sum_[row] > 0.0 ? 1.0 - with_sum_[row] / without_sum_[row] : 0.0;
        // And MEASURED on the row (save 203): the evening before's sums and
        // the yard's own herd trudodni, beside the run's estimates above.
        const float measured_loss =
            last_without_m_[row] > 0.0F ? 1.0F - last_with_m_[row] / last_without_m_[row] : 0.0F;
        harvests_.push_back({.year = year,
                             .family = world.families.row_ids[row].value,
                             .ratio = last_ratio_[row],
                             .inworld_loss = static_cast<float>(inworld),
                             .yard_trudodni = static_cast<float>(yard_td_[row]),
                             .measured_loss = measured_loss,
                             .measured_trudodni = last_td_m_[row]});
        with_sum_[row] = 0.0;
        without_sum_[row] = 0.0;
        yard_td_[row] = 0.0;
      }
      if (ratio >= 0.0F && full_yield_hours_ > 0.0F) {
        const bool gave = share > 0.0F && family.house.value != core::kInvalidEntityIdValue &&
                          family.in_barrack == 0;
        const double net = static_cast<double>(family.household_hours);
        const double gross = gave ? net / (1.0 - static_cast<double>(held)) : net;
        const double full = static_cast<double>(full_yield_hours_);
        with_sum_[row] += net / full < 1.0 ? net / full : 1.0;
        without_sum_[row] += gross / full < 1.0 ? gross / full : 1.0;
      }
      last_ratio_[row] = ratio;
      if (row >= last_with_m_.size()) {
        last_with_m_.resize(world.families.rows.size(), 0.0F);
        last_without_m_.resize(world.families.rows.size(), 0.0F);
        last_td_m_.resize(world.families.rows.size(), 0.0F);
      }
      last_with_m_[row] = family.plot_ratio_sum;
      last_without_m_[row] = family.plot_ratio_without_sum;
      last_td_m_[row] = static_cast<float>(family.yard_herd_trudodni) / 100.0F;
    }
  }

  /// @brief The garden harvests as one line a village (K1, K2): «year:family
  ///        :ratio» of each harvest, ratio = the share of the full yield.
  void PrintHarvests(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: G1442 seed " << seed << " garden_full_kcal " << garden_full_kcal_
        << " kcal_per_trudoden y1/y2/y3 " << kcal_per_trudoden_[0] << '/' << kcal_per_trudoden_[1]
        << '/' << kcal_per_trudoden_[2]
        << " harvests (year:family:ratio:inworld_loss:yard_td:measured_loss:measured_td)";
    for (const Harvest& one : harvests_) {
      out << ' ' << one.year << ':' << one.family << ':' << one.ratio << ':' << one.inworld_loss
          << ':' << one.yard_trudodni << ':' << one.measured_loss << ':' << one.measured_trudodni;
    }
    out << '\n';
  }

  /// @brief The herds' care left undone at the day's end. Once a day.
  void CountDayEnd(const core::WorldState& world) {
    if (world.calendar.date.year < 1 || world.calendar.date.year > kYears) {
      return;
    }
    const auto month =
        static_cast<std::size_t>(((world.calendar.date.year - 1) * 12) +
                                 static_cast<std::uint32_t>(world.calendar.date.month));
    for (const core::HerdRow& herd : world.herds.rows) {
      care_undone_md_ += static_cast<double>(herd.care_days_remaining);
      // P0, THE CARE COVERAGE CENSUS (#1443; econ's gate): the kolkhoz cows'
      // and horses' month mean (HerdRow::care_coverage) at each day's end.
      if (herd.household_owned != 0 || herd.unit.value == core::kInvalidEntityIdValue) {
        continue;
      }
      const int which = herd.kind.value == cow_ ? 0 : (herd.kind.value == horse_ ? 1 : -1);
      if (which < 0 || month >= coverage_min_[0].size()) {
        continue;
      }
      auto& c = coverage_[static_cast<std::size_t>(which)];
      c[0] += 1.0;
      c[1] += static_cast<double>(herd.care_coverage);
      c[2] += herd.care_coverage < 0.9F ? 1.0 : 0.0;
      float& low = coverage_min_[static_cast<std::size_t>(which)][month];
      low = herd.care_coverage < low ? herd.care_coverage : low;
    }
  }

  /// @brief P0's line (#1443): the cows' and horses' coverage y1-y3 - herd-days,
  ///        mean, herd-days under 0.9, and each month's lowest.
  void PrintCoverage(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: C1443 seed " << seed;
    const char* names[2] = {"cows", "horses"};
    for (std::size_t which = 0; which < 2; ++which) {
      const auto& c = coverage_[which];
      out << ' ' << names[which] << " herd_days " << c[0] << " mean "
          << (c[0] > 0.0 ? c[1] / c[0] : 1.0) << " under09 " << c[2] << " month_min";
      for (const float low : coverage_min_[which]) {
        out << ' ' << low;
      }
    }
    out << '\n';
  }

  /// @brief The closed year's book, y1-y3. Call at each year's turn.
  void CloseYear(const core::YearLedger& closed, std::uint32_t year) {
    if (year < 1 || year > kYears) {
      return;
    }
    by_yards_md_ += static_cast<double>(closed.herd_care_by_yards);
    if (milk_ < closed.herd_produce.size()) {
      milk_kg_ += static_cast<double>(closed.herd_produce[milk_]) / 1000.0;
    }
    // #1446's defect 3: the plan's own «must stay nought» idle, y1-y3.
    unexplained_ +=
        closed.idle_person_days[static_cast<std::size_t>(core::IdleReason::kUnexplained)];
    double issued_kg = 0.0;
    for (const core::Grams grams : closed.issued) {
      issued_kg += static_cast<double>(grams) / 1000.0;
    }
    issued_kg_ += issued_kg;
    trudodni_ += static_cast<double>(closed.trudodni_accrued) / 100.0;
    // The year's distribution in kcal a trudoden, every issued resource.
    double kcal = 0.0;
    for (std::size_t resource = 0; resource < closed.issued.size(); ++resource) {
      const float per_gram = resource < kcal_per_gram_.size() ? kcal_per_gram_[resource] : 0.0F;
      kcal += static_cast<double>(closed.issued[resource]) * static_cast<double>(per_gram);
    }
    const double trudodni = static_cast<double>(closed.trudodni_accrued) / 100.0;
    kcal_per_trudoden_[year - 1] = trudodni > 0.0 ? kcal / trudodni : 0.0;
  }

  /// @brief The village's line.
  void Print(std::ostream& out, std::uint64_t seed) const {
    double share_sum = 0.0;
    double worst = 0.0;
    std::uint32_t yards = 0;
    std::vector<double> shares;
    for (std::size_t row = 0; row < yard_gross_.size(); ++row) {
      if (yard_gross_[row] <= 0.0) {
        continue;
      }
      const double share = yard_given_[row] / yard_gross_[row];
      share_sum += share;
      worst = share > worst ? share : worst;
      shares.push_back(share);
      ++yards;
    }
    std::sort(shares.begin(), shares.end());
    const double p90 =
        shares.empty() ? 0.0
                       : shares[(shares.size() * 9) / 10 < shares.size() ? (shares.size() * 9) / 10
                                                                         : shares.size() - 1];
    out << "population_curve: Y1442 seed " << seed << " strada_JunAug_y1y3 field_wd " << field_wd_
        << " hay_wd " << hay_wd_ << " herd_wd " << herd_wd_ << " herd_care_by_yards_md "
        << by_yards_md_ << " issued_kg_per_trudoden_y1y3 "
        << (trudodni_ > 0.0 ? issued_kg_ / trudodni_ : 0.0) << " old_hungry_family_days_y1y2 "
        << old_hungry_family_days_ << " of " << old_family_days_
        << " old family-days (mean satiety < " << kHungrySatiety << " at hour 22)"
        << " care_undone_md_y1y3 " << care_undone_md_ << " milk_kg_y1y3 " << milk_kg_
        << " idle_unexplained_pd_y1y3 " << unexplained_ << " yards_plot_share_taken mean "
        << (yards > 0 ? share_sum / yards : 0.0) << " worst " << worst << " yards " << yards
        << " p90 " << p90 << " winter_old_below70_family_days " << winter_old_below70_days_
        << " of " << winter_old_family_days_ << " (Oct-Apr after each strada, y1-y3)";
    if (by_yards_md_ <= 0.0) {
      out << " — THE YARDS TOOK NO CARE (expected for a chairman without the path)";
    }
    out << '\n';
  }

 private:
  static bool InStrada(const core::WorldState& world) {
    const auto month = static_cast<std::uint32_t>(world.calendar.date.month);  // 0-based
    const std::uint32_t year = world.calendar.date.year;
    return year >= 1 && year <= kYears && month >= 5 && month <= 7;  // Jun-Aug, econ's P
  }

  static bool OnMeadow(const core::WorldState& world, core::FieldId field) {
    const std::uint32_t row = core::FindRow(world.fields, field);
    return row != core::kNoRow &&
           (world.fields.rows[row].kind == core::LandKind::kMeadow ||
            world.fields.rows[row].kind == core::LandKind::kFloodplainMeadow);
  }

  struct Harvest {
    std::uint32_t year = 0;
    std::uint32_t family = 0;
    float ratio = 0.0F;
    float inworld_loss = 0.0F;
    float yard_trudodni = 0.0F;
    float measured_loss = 0.0F;      // the row's 1 - with / without (save 203)
    float measured_trudodni = 0.0F;  // the row's yard_herd_trudodni, trudodni
  };

  std::vector<float> last_ratio_;
  std::vector<double> with_sum_, without_sum_;
  float full_yield_hours_ = 4.0F;
  double garden_full_kcal_ = 0.0;
  std::vector<float> kcal_per_gram_;
  float care_rate_ = 1.0F;
  double prev_by_yards_ = 0.0;
  std::vector<double> yard_td_;
  std::vector<float> last_with_m_, last_without_m_, last_td_m_;
  std::array<double, kYears> kcal_per_trudoden_ = {};
  std::vector<Harvest> harvests_;
  std::uint32_t winter_old_family_days_ = 0, winter_old_below70_days_ = 0;
  std::uint32_t field_wd_ = 0, hay_wd_ = 0, herd_wd_ = 0;
  std::uint32_t old_hungry_family_days_ = 0;
  std::uint32_t old_family_days_ = 0;
  double care_undone_md_ = 0.0;
  double by_yards_md_ = 0.0;
  double issued_kg_ = 0.0;
  double trudodni_ = 0.0;
  std::uint32_t milk_ = core::kNoTableRow;
  double milk_kg_ = 0.0;
  std::uint64_t unexplained_ = 0;
  std::uint32_t cow_ = core::kNoTableRow;
  std::uint32_t horse_ = core::kNoTableRow;
  std::array<std::array<double, 3>, 2> coverage_ = {};
  std::array<std::array<float, 36>, 2> coverage_min_ = {{{}, {}}};
  std::vector<double> yard_given_;
  std::vector<double> yard_gross_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_STRADA_TALLY_H_
