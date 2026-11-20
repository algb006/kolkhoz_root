/// @file
/// @brief #589's print (boss [2] §5): CLERK589 a village-year y1-y6 - whether the post was
///        held, the clerk-days and vacancy-days, the appointments, and the clerk's family's
///        mean satiety against the village's (his post_grade pay as a number, not an
///        assumption). Read through ISimulation::ClerkPostToday - the core's own door.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_CLERK_TALLY_H_
#define TESTS_RUN_COMMON_CLERK_TALLY_H_

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/clerk_post_view.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"

namespace run {

class ClerkTally {
 public:
  static constexpr std::uint32_t kYears = 6;

  /// @brief Once a day.
  void CountDay(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const core::SimDay day = world.calendar.day;
    if (day == last_day_) {
      return;
    }
    last_day_ = day;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    if (day % core::kDaysPerYear == 0 && year >= 2 && year - 1U <= kYears) {
      CloseYear(year - 1U);
    }
    if (year > kYears) {
      return;
    }
    const core::ClerkPostView post = simulation.ClerkPostToday();
    if (!post.measured) {
      ++unmeasured_days_;
      return;
    }
    if (post.vacancy != core::ClerkVacancy::kInPost) {
      ++vacant_days_;
      return;
    }
    ++clerk_days_;
    if (post.clerk.value != last_clerk_) {
      ++clerks_seen_;
      last_clerk_ = post.clerk.value;
    }
    const std::uint32_t clerk_row = core::FindRow(world.residents, post.clerk);
    if (clerk_row == core::kNoRow) {
      return;
    }
    const core::FamilyId family = world.residents.rows[clerk_row].family;
    double own = 0.0;
    double all = 0.0;
    std::uint32_t own_count = 0;
    for (const core::ResidentRow& resident : world.residents.rows) {
      all += static_cast<double>(resident.satiety);
      if (resident.family.value == family.value) {
        own += static_cast<double>(resident.satiety);
        ++own_count;
      }
    }
    if (own_count > 0 && !world.residents.rows.empty()) {
      clerk_family_satiety_ += own / own_count;
      village_satiety_ += all / static_cast<double>(world.residents.rows.size());
      ++satiety_days_;
    }
  }

  /// @brief The village's lines.
  void Print(std::ostream& out, std::uint64_t seed) const {
    std::istringstream lines(lines_.str());
    std::string line;
    while (std::getline(lines, line)) {
      out << "population_curve: CLERK589 seed " << seed << ' ' << line << '\n';
    }
  }

 private:
  void CloseYear(std::uint32_t year) {
    lines_ << "year " << year << " clerk_days " << clerk_days_ << " vacant_days " << vacant_days_
           << " unmeasured_days " << unmeasured_days_ << " clerks_seen " << clerks_seen_;
    if (satiety_days_ > 0) {
      lines_ << " clerk_family_satiety " << clerk_family_satiety_ / satiety_days_
             << " village_satiety " << village_satiety_ / satiety_days_;
    } else {
      lines_ << " clerk_family_satiety - village_satiety -";
    }
    lines_ << '\n';
    clerk_days_ = vacant_days_ = unmeasured_days_ = clerks_seen_ = satiety_days_ = 0;
    clerk_family_satiety_ = village_satiety_ = 0.0;
  }

  core::SimDay last_day_ = static_cast<core::SimDay>(-1);
  std::uint32_t last_clerk_ = 0;
  std::uint32_t clerk_days_ = 0;
  std::uint32_t vacant_days_ = 0;
  std::uint32_t unmeasured_days_ = 0;
  std::uint32_t clerks_seen_ = 0;
  std::uint32_t satiety_days_ = 0;
  double clerk_family_satiety_ = 0.0;
  double village_satiety_ = 0.0;
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_CLERK_TALLY_H_
