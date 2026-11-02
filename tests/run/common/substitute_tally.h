/// @file
/// @brief The stock's rest days, y1-y3: who did a post holder's herd care on a
/// rest day, how often each substitute was taken, and the holders' and the
/// others' rest and walk-offs - #1434's canon pair print.
/// @threading SINGLE_THREADED
/// Test-side code, read from the thread that owns the simulation.
///
/// WHY IT EXISTS (#1434; leisure.md «Скот в выходной — подменой»; econ's gates,
/// core-boss-1421-arms [5]): a rule that never fires is red, so the print
/// shows first that holders' rest days were substituted, by post, and how the
/// substitutes rotated (mean, P90, max substitutions a person a year, and the
/// largest share of one person's rest days). Then the gates: the holders'
/// June-August rest below 40 and their walk-offs, everyone else's, and the
/// herds' care left undone.
///
/// READS:
/// - at the «noon» step (hour 13) on a rest day: herd care by a post holder
///   (self-worked) or by someone without a post (a substitute), the post
///   attributed by the herd's unit type (horse yard / stable: groom; cattle
///   yard: milkmaid; any other);
/// - at hour 22: June-August adults' rest, holders apart;
/// - every step: walk-off events, holders apart;
/// - at each day's end: the herds' care left undone.

#ifndef TESTS_RUN_COMMON_SUBSTITUTE_TALLY_H_
#define TESTS_RUN_COMMON_SUBSTITUTE_TALLY_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/day_off.h"
#include "core_common/event_state.h"
#include "core_common/herd_state.h"
#include "core_common/labor_state.h"
#include "core_common/ledger_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class SubstituteTally {
 public:
  static constexpr std::uint32_t kYears = 3;

  enum Post : std::uint8_t { kGroom = 0, kMilkmaid = 1, kOtherPost = 2, kPosts = 3 };

  explicit SubstituteTally(const core::ITableSet& tables) {
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      cattle_ = Row(*types, "cattle_yard");
      for (const char* key : {"horse_yard", "stable"}) {
        const std::uint32_t row = types->FindRowByKey(key);
        if (row != core::kNoTableRow) {
          horses_.push_back(row);
        }
      }
    }
    if (const core::ITable* const life = tables.FindTable("life")) {
      const std::uint32_t column = life->FindColumn("value");
      const std::uint32_t row = life->FindRowByKey("life_speedup");
      if (row != core::kNoTableRow) {
        speedup_ = static_cast<float>(life->CellReal(row, column).value_or(1.0F));
      }
    }
  }

  /// @brief Rest-day herd care, at the «noon» step (the state's hour 13).
  void CountNoon(const core::WorldState& world) {
    const std::uint32_t year = world.calendar.date.year;  // one-based
    if (year > kYears || !core::IsDayOffIn(world, world.calendar.day)) {
      return;
    }
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const core::ResidentRow& resident = world.residents.rows[row];
      if (resident.work.kind != core::WorkKind::kHerdCare) {
        continue;
      }
      const Post post = PostOfHerd(world, resident.work.herd);
      if (resident.post.profession.value != core::kInvalidDefIdValue) {
        ++self_[post];
      } else {
        ++substituted_[post];
        ++per_person_[{year, world.residents.row_ids[row].value}];
      }
    }
    ++rest_days_[year - 1];
  }

  /// @brief Walk-offs, holders apart. Call after every step.
  void CountStep(const core::WorldState& world) {
    if (world.calendar.date.year > kYears) {
      return;
    }
    for (const core::SimEvent& event : world.step_events) {
      if (event.kind != core::EventKind::kWalkOff) {
        continue;
      }
      const std::uint32_t row = core::FindRow(world.residents, event.resident);
      if (row == core::kNoRow) {
        continue;
      }
      const bool holder =
          world.residents.rows[row].post.profession.value != core::kInvalidDefIdValue;
      ++(holder ? holder_walk_offs_ : other_walk_offs_);
    }
  }

  /// @brief June-August adults' rest at the state's hour 22.
  void CountEvening(const core::WorldState& world) {
    const auto month = static_cast<std::uint32_t>(world.calendar.date.month);
    if (world.calendar.date.year > kYears || month < 5 || month > 7) {
      return;
    }
    for (const core::ResidentRow& resident : world.residents.rows) {
      if (core::BiologicalAgeYears(speedup_, resident.birth_day, world.calendar.day) < 16.0F) {
        continue;
      }
      const bool holder = resident.post.profession.value != core::kInvalidDefIdValue;
      ++(holder ? holder_days_ : other_days_);
      if (resident.rest < 40.0F) {
        ++(holder ? holder_below40_ : other_below40_);
      }
    }
  }

  /// @brief The herds' care left undone at the day's end. Once a day.
  void CountDayEnd(const core::WorldState& world) {
    if (world.calendar.date.year > kYears) {
      return;
    }
    for (const core::HerdRow& herd : world.herds.rows) {
      care_undone_md_ += static_cast<double>(herd.care_days_remaining);
    }
  }

  /// @brief THE RULE'S OWN FIRINGS, from the closed book (YearLedger
  ///        herd_rest_days_substituted / _self_worked), y1-y3. Call at each
  ///        year's turn. The noon read above cannot tell the rule's substitute
  ///        from the day's list sending free hands to care left over, which it
  ///        did before #1434 too (a34old printed «substituted» on the old rule).
  void CloseYear(const core::YearLedger& closed, std::uint32_t year) {
    if (year < 1 || year > kYears) {
      return;
    }
    rule_substituted_ += closed.herd_rest_days_substituted;
    rule_self_worked_ += closed.herd_rest_days_self_worked;
  }

  /// @brief The village's line.
  void Print(std::ostream& out, std::uint64_t seed) const {
    std::vector<std::uint32_t> counts;
    double worst_share = 0.0;
    for (const auto& [key, count] : per_person_) {
      counts.push_back(count);
      const std::uint32_t days = rest_days_[key.first - 1];
      worst_share = std::max(worst_share, days > 0 ? static_cast<double>(count) / days : 0.0);
    }
    std::sort(counts.begin(), counts.end());
    double mean = 0.0;
    for (const std::uint32_t c : counts) {
      mean += c;
    }
    mean = counts.empty() ? 0.0 : mean / static_cast<double>(counts.size());
    const std::uint32_t p90 =
        counts.empty()
            ? 0
            : counts[(counts.size() * 9) / 10 == counts.size() ? counts.size() - 1
                                                               : (counts.size() * 9) / 10];
    const std::uint32_t max = counts.empty() ? 0 : counts.back();
    out << "population_curve: S1434 seed " << seed << " rule_substituted_y1y3 " << rule_substituted_
        << " rule_self_worked_y1y3 " << rule_self_worked_ << " rest_day_care_by_non_holders g/m/o "
        << substituted_[kGroom] << '/' << substituted_[kMilkmaid] << '/' << substituted_[kOtherPost]
        << " by_holders g/m/o " << self_[kGroom] << '/' << self_[kMilkmaid] << '/'
        << self_[kOtherPost] << " non_holder_carers_person_years " << counts.size()
        << " per_person_year mean " << mean << " p90 " << p90 << " max " << max
        << " worst_share_of_rest_days " << worst_share << " walk_offs holders " << holder_walk_offs_
        << " others " << other_walk_offs_ << " JunAug_pd holders " << holder_days_ << " below40 "
        << holder_below40_ << " others " << other_days_ << " below40 " << other_below40_
        << " care_undone_md_y1y3 " << care_undone_md_;
    if (rule_substituted_ == 0) {
      out << " — THE RULE SUBSTITUTED NO REST DAY (econ: red whatever else reads)";
    }
    out << '\n';
  }

 private:
  static std::uint32_t Row(const core::ITable& types, const char* key) {
    return types.FindRowByKey(key);
  }

  Post PostOfHerd(const core::WorldState& world, core::HerdId id) const {
    const std::uint32_t herd = core::FindRow(world.herds, id);
    if (herd == core::kNoRow) {
      return kOtherPost;
    }
    const std::uint32_t unit = core::FindRow(world.units, world.herds.rows[herd].unit);
    if (unit == core::kNoRow) {
      return kOtherPost;
    }
    const std::uint32_t type = world.units.rows[unit].type.value;
    if (type == cattle_) {
      return kMilkmaid;
    }
    return std::find(horses_.begin(), horses_.end(), type) != horses_.end() ? kGroom : kOtherPost;
  }

  std::uint32_t cattle_ = core::kNoTableRow;
  std::vector<std::uint32_t> horses_;
  float speedup_ = 1.0F;
  std::array<std::uint32_t, kPosts> substituted_ = {};
  std::array<std::uint32_t, kPosts> self_ = {};
  std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> per_person_;
  std::array<std::uint32_t, kYears> rest_days_ = {};
  std::uint32_t holder_walk_offs_ = 0, other_walk_offs_ = 0;
  std::uint32_t holder_days_ = 0, holder_below40_ = 0, other_days_ = 0, other_below40_ = 0;
  double care_undone_md_ = 0.0;
  std::uint64_t rule_substituted_ = 0, rule_self_worked_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SUBSTITUTE_TALLY_H_
