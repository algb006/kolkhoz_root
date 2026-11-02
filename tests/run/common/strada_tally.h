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
    if (const core::ITable* const food = tables.FindTable("food")) {
      const std::uint32_t row = food->FindRowByKey("plot_full_yield_hours");
      const std::uint32_t column = food->FindColumn("value");
      if (row != core::kNoTableRow && column != core::kNoTableColumn) {
        full_yield_hours_ = static_cast<float>(food->CellReal(row, column).value_or(4.0F));
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
    for (std::uint32_t row = 0; row < world.families.rows.size(); ++row) {
      const core::FamilyRow& family = world.families.rows[row];
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
        harvests_.push_back({.year = year,
                             .family = world.families.row_ids[row].value,
                             .ratio = last_ratio_[row],
                             .inworld_loss = static_cast<float>(inworld)});
        with_sum_[row] = 0.0;
        without_sum_[row] = 0.0;
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
    }
  }

  /// @brief The garden harvests as one line a village (K1, K2): «year:family
  ///        :ratio» of each harvest, ratio = the share of the full yield.
  void PrintHarvests(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: G1442 seed " << seed << " harvests";
    for (const Harvest& one : harvests_) {
      out << ' ' << one.year << ':' << one.family << ':' << one.ratio << ':' << one.inworld_loss;
    }
    out << '\n';
  }

  /// @brief The herds' care left undone at the day's end. Once a day.
  void CountDayEnd(const core::WorldState& world) {
    if (world.calendar.date.year < 1 || world.calendar.date.year > kYears) {
      return;
    }
    for (const core::HerdRow& herd : world.herds.rows) {
      care_undone_md_ += static_cast<double>(herd.care_days_remaining);
    }
  }

  /// @brief The closed year's book, y1-y3. Call at each year's turn.
  void CloseYear(const core::YearLedger& closed, std::uint32_t year) {
    if (year < 1 || year > kYears) {
      return;
    }
    by_yards_md_ += static_cast<double>(closed.herd_care_by_yards);
    double issued_kg = 0.0;
    for (const core::Grams grams : closed.issued) {
      issued_kg += static_cast<double>(grams) / 1000.0;
    }
    issued_kg_ += issued_kg;
    trudodni_ += static_cast<double>(closed.trudodni_accrued) / 100.0;
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
        << " care_undone_md_y1y3 " << care_undone_md_ << " yards_plot_share_taken mean "
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
  };

  std::vector<float> last_ratio_;
  std::vector<double> with_sum_, without_sum_;
  float full_yield_hours_ = 4.0F;
  std::vector<Harvest> harvests_;
  std::uint32_t winter_old_family_days_ = 0, winter_old_below70_days_ = 0;
  std::uint32_t field_wd_ = 0, hay_wd_ = 0, herd_wd_ = 0;
  std::uint32_t old_hungry_family_days_ = 0;
  std::uint32_t old_family_days_ = 0;
  double care_undone_md_ = 0.0;
  double by_yards_md_ = 0.0;
  double issued_kg_ = 0.0;
  double trudodni_ = 0.0;
  std::vector<double> yard_given_;
  std::vector<double> yard_gross_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_STRADA_TALLY_H_
