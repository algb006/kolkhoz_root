/// @file
/// @brief AUTH2164 (#2164's pair prints, econ authority-epoch1-2161 §5.6): per village-year, the
///        chairman's authority at the year's end, the loud deeds by kind (count / points), the
///        complaints upward, the understood days against the cancelled days off, and the food
///        stolen (kg, every food resource of the closed book). Gate it serves: no avatar's
///        village ends Epoch I with A < 15 in more than 10 % of villages.
/// @threading SINGLE_THREADED
/// Test-side code, driven after every step and once a day from the thread that owns the
/// simulation.

#ifndef TESTS_RUN_COMMON_AUTHORITY_TALLY_H_
#define TESTS_RUN_COMMON_AUTHORITY_TALLY_H_

#include <array>
#include <cstdint>
#include <map>
#include <ostream>
#include <vector>

#include "core_common/authority.h"
#include "core_common/calendar.h"
#include "core_common/day_off.h"
#include "core_common/event_state.h"
#include "core_common/land_state.h"
#include "core_common/logistics_state.h"
#include "core_common/world_state.h"

namespace run {

class AuthorityTally {
 public:
  /// @brief After every step: the deeds and complaints the step said.
  void CountStep(const core::WorldState& world) {
    Year& row = by_year_[(world.calendar.day / core::kDaysPerYear) + 1U];
    for (const core::SimEvent& event : world.step_events) {
      if (event.kind == core::EventKind::kAuthorityMoved) {
        const auto deed = static_cast<std::size_t>(event.amount & 0xFF);
        if (deed < row.deeds.size()) {
          ++row.deeds[deed];
          row.points[deed] += static_cast<double>(event.amount >> 8) / 10.0;
        }
        // econ [6]'s question: what stood on a day off judged «without reason» - a sowing
        // window (a field ploughed, harrowed or sown) or an urgent load (level 0).
        if (deed == static_cast<std::size_t>(core::AuthorityDeed::kDayOffWithoutReason)) {
          ++row.cancelled_days;
          bool sowing = false;
          for (const core::FieldRow& field : world.fields.rows) {
            sowing = sowing || field.phase == core::FieldPhase::kPlowing ||
                     field.phase == core::FieldPhase::kHarrowing ||
                     field.phase == core::FieldPhase::kSowing;
          }
          bool urgent = false;
          for (const core::LogisticsTaskRow& task : world.logistics_tasks.rows) {
            urgent = urgent || task.level == core::LogisticsLevel::kUrgent;
          }
          row.no_reason_sowing += sowing ? 1U : 0U;
          row.no_reason_urgent += urgent ? 1U : 0U;
          row.no_reason_months.push_back(static_cast<std::uint32_t>(world.calendar.date.month));
        }
      } else if (event.kind == core::EventKind::kComplaintToDistrict) {
        ++row.complaints;
      }
    }
    // An understood day is spent where understood_day takes a new value.
    if (world.chairman.understood_day != last_understood_day_) {
      last_understood_day_ = world.chairman.understood_day;
      if (last_understood_day_ != 0) {
        ++row.understood_days;
        ++row.cancelled_days;
      }
    }
  }

  /// @brief Once a day, after the day's step.
  void CountDay(const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    Year& row = by_year_[year];
    row.authority_end = world.chairman.authority;
    // The judged cancelled days are counted at their judgement (CountStep): the first read
    // compared cancelled_day_off with yesterday and counted day 1's 0 as a cancelled day 0 -
    // 64 of 64 in year 1 of au2on, all spurious.
    if (day % core::kDaysPerYear == 0 && day > 0) {
      Year& closed = by_year_[year - 1U];
      closed.authority_end = world.chairman.authority;
      double stolen = 0.0;
      for (const core::Grams grams : world.ledger.closed.stolen) {
        stolen += static_cast<double>(grams) / 1000.0;
      }
      closed.stolen_kg = stolen;
      closed.closed = true;
    }
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    static constexpr std::array<const char*, core::kAuthorityDeedCountValue> kNames = {
        "holiday",
        "holiday_half",
        "day_off_no_reason",
        "defended_food",
        "over_plan_at_line",
        "raised_issue",
        "unfair_month",
        "audit_remark"};
    for (const auto& [year, row] : by_year_) {
      if (!row.closed) {
        continue;
      }
      out << "population_curve: AUTH2164 seed " << seed << " year " << year
          << " ground=deeds_by_event;stolen=closed_book_all_resources_kg;understood=of_"
             "cancelled_days_off"
          << " authority_end " << row.authority_end << " complaints " << row.complaints
          << " cancelled_days " << row.cancelled_days << " understood " << row.understood_days
          << " stolen_kg " << row.stolen_kg << " no_reason(sowing/urgent/months) "
          << row.no_reason_sowing << '/' << row.no_reason_urgent << '/';
      for (const std::uint32_t month : row.no_reason_months) {
        out << month << ';';
      }
      out << " deeds(count/points)";
      for (std::size_t deed = 0; deed < kNames.size(); ++deed) {
        out << ' ' << kNames[deed] << ' ' << row.deeds[deed] << '/' << row.points[deed];
      }
      out << '\n';
    }
  }

 private:
  struct Year {
    float authority_end = 0.0F;
    std::uint32_t complaints = 0;
    std::uint32_t cancelled_days = 0;
    std::uint32_t understood_days = 0;
    std::uint32_t no_reason_sowing = 0;
    std::uint32_t no_reason_urgent = 0;
    std::vector<std::uint32_t> no_reason_months;
    double stolen_kg = 0.0;
    std::array<std::uint32_t, core::kAuthorityDeedCountValue> deeds{};
    std::array<double, core::kAuthorityDeedCountValue> points{};
    bool closed = false;
  };

  std::map<std::uint32_t, Year> by_year_;
  std::uint32_t last_understood_day_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_AUTHORITY_TALLY_H_
