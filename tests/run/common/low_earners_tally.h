/// @file
/// @brief #1953's low-earners print (econ low-earners-at-the-line-2026-11-22 §1; boss): LOW1953,
///        a village-year y9-y16 - for every worker-day of a family that hit the ration line that
///        year, and of one that never did: placed or not, and if not, why.
///
/// THE GROUND, printed with the line:
/// - a worker is the advance's: from food.csv adult_from_bio_years to elderly_from_bio_years;
/// - a family is «at the line» in a year if its members' mean satiety was at or under
///   ration_satiety_threshold on any day of it (FAM1953's line);
/// - placed: a work today (WorkAssignment::kind) or a post held;
/// - not placed, in this order: away (away_until_day past today), sick (health under 50, the
///   start drinking's «больной» line - the core has no sickness flag), else the accountant's own
///   idle reason (ResidentRow::idle_reason, by its enum name). PREGNANCY IS NOT MODELLED in the
///   core: no class for it, printed as such;
/// - the trudodni a placed worker-day earned: the family's account rise that day over its
///   placed workers that day (redemptions not counted).
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_LOW_EARNERS_TALLY_H_
#define TESTS_RUN_COMMON_LOW_EARNERS_TALLY_H_

#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

#include "core_common/calendar.h"
#include "core_common/labor_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class LowEarnersTally {
 public:
  static constexpr std::uint32_t kFirstYear = 9;
  static constexpr std::uint32_t kLastYear = 16;
  static constexpr float kSickBelow = 50.0F;
  /// Classes: [0] placed, [1] away, [2] sick, then 3 + IdleReason (its count = not placed and no
  /// reason written).
  static constexpr std::size_t kClasses =
      3 + static_cast<std::size_t>(core::IdleReason::kIdleReasonCount) + 1;

  explicit LowEarnersTally(const core::ITableSet& tables) {
    if (const core::ITable* const food = tables.FindTable("food")) {
      const std::uint32_t value = food->FindColumn("value");
      for (std::uint32_t row = 0; row < food->RowCount() && value != core::kNoTableColumn; ++row) {
        const std::string_view key = food->CellText(row, 0);
        const std::string_view text = food->CellText(row, value);
        if (text.empty()) {
          continue;
        }
        const double number = std::stod(std::string(text));
        if (key == "adult_from_bio_years") {
          adult_from_ = number;
        } else if (key == "elderly_from_bio_years") {
          elderly_from_ = number;
        } else if (key == "ration_satiety_threshold") {
          threshold_ = number;
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
  void CountDay(const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    if (day == last_day_) {
      return;
    }
    last_day_ = day;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    if (day % core::kDaysPerYear == 0 && year >= kFirstYear + 1U && year - 1U <= kLastYear) {
      CloseYear(year - 1U);
    }
    if (year < kFirstYear || year > kLastYear) {
      return;
    }
    std::map<std::uint32_t, std::pair<double, std::uint32_t>> satiety;  // family: sum, members
    std::map<std::uint32_t, std::uint32_t> placed_today;
    for (const core::ResidentRow& person : world.residents.rows) {
      auto& [sum, members] = satiety[person.family.value];
      sum += static_cast<double>(person.satiety);
      ++members;
      const double bio = static_cast<double>(static_cast<std::int64_t>(day) -
                                             static_cast<std::int64_t>(person.birth_day)) /
                         static_cast<double>(core::kDaysPerYear) * life_speedup_;
      if (bio < adult_from_ || bio >= elderly_from_) {
        continue;
      }
      FamilyYear& family = families_[person.family.value];
      std::size_t cls = 0;
      const bool placed = person.work.kind != core::WorkKind::kNone ||
                          person.post.profession.value != core::kInvalidDefIdValue;
      if (placed) {
        ++placed_today[person.family.value];
      } else if (person.away_until_day > day) {
        cls = 1;
      } else if (person.health < kSickBelow) {
        cls = 2;
      } else {
        cls = 3 + static_cast<std::size_t>(person.idle_reason);
      }
      ++family.days[cls < kClasses ? cls : kClasses - 1];
    }
    for (std::size_t row = 0; row < world.families.rows.size(); ++row) {
      const std::uint32_t id = world.families.row_ids[row].value;
      FamilyYear& family = families_[id];
      const auto found = satiety.find(id);
      if (found != satiety.end() && found->second.second > 0 &&
          found->second.first / found->second.second <= threshold_) {
        family.at_line = true;
      }
      const core::TrudodniHundredths account = world.families.rows[row].trudodni_account;
      core::TrudodniHundredths& before = accounts_[id];
      if (account > before && placed_today[id] > 0 && day % core::kDaysPerYear != 0) {
        family.placed_trudodni += static_cast<double>(account - before) / 100.0;
      }
      before = account;
    }
  }

  /// @brief The village's lines.
  void Print(std::ostream& out, std::uint64_t seed) const {
    std::istringstream lines(lines_.str());
    std::string line;
    while (std::getline(lines, line)) {
      out << "population_curve: LOW1953 seed " << seed << ' ' << line << '\n';
    }
  }

 private:
  struct FamilyYear {
    bool at_line = false;
    std::array<std::uint32_t, kClasses> days{};
    double placed_trudodni = 0.0;
  };

  // By IdleReason's own order (labor_state.h); the static_assert binds the list to the enum.
  static constexpr std::array<std::string_view, kClasses> kNames = {"placed",
                                                                    "away",
                                                                    "sick",
                                                                    "idle_no_open_work",
                                                                    "idle_day_off",
                                                                    "idle_work_covered",
                                                                    "idle_no_horse",
                                                                    "idle_crew_cap",
                                                                    "idle_road",
                                                                    "idle_horse_lock",
                                                                    "idle_no_day_left",
                                                                    "idle_resting",
                                                                    "idle_unexplained",
                                                                    "idle_rain",
                                                                    "idle_funeral",
                                                                    "idle_none_written"};
  static_assert(static_cast<std::size_t>(core::IdleReason::kIdleReasonCount) == 12,
                "a new IdleReason: name it in LowEarnersTally::kNames");

  static std::string_view ClassName(std::size_t cls) { return kNames[cls]; }

  void CloseYear(std::uint32_t year) {
    std::array<std::array<std::uint64_t, kClasses>, 2> sums{};
    std::array<double, 2> trudodni{};
    std::array<std::uint32_t, 2> families{};
    for (const auto& [id, family] : families_) {
      const std::size_t group = family.at_line ? 0 : 1;
      ++families[group];
      for (std::size_t cls = 0; cls < kClasses; ++cls) {
        sums[group][cls] += family.days[cls];
      }
      trudodni[group] += family.placed_trudodni;
    }
    lines_ << "year " << year
           << " ground=worker_age_band; at_line=satiety_at_or_under_threshold_any_day; "
              "sick=health_under_50; pregnancy=NOT_MODELLED";
    for (std::size_t group = 0; group < 2; ++group) {
      lines_ << " | " << (group == 0 ? "at_line" : "never") << " families " << families[group];
      for (std::size_t cls = 0; cls < kClasses; ++cls) {
        if (sums[group][cls] > 0) {
          lines_ << ' ' << ClassName(cls) << ' ' << sums[group][cls];
        }
      }
      lines_ << " trudodni_per_placed_day "
             << (sums[group][0] > 0 ? trudodni[group] / static_cast<double>(sums[group][0]) : 0.0);
    }
    lines_ << '\n';
    families_.clear();
  }

  double adult_from_ = 16.0;
  double elderly_from_ = 60.0;
  double threshold_ = 40.0;
  double life_speedup_ = 4.0;
  std::map<std::uint32_t, FamilyYear> families_;
  std::map<std::uint32_t, core::TrudodniHundredths> accounts_;
  core::SimDay last_day_ = static_cast<core::SimDay>(-1);
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_LOW_EARNERS_TALLY_H_
