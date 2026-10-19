/// @file
/// @brief The hands the morning left idle for want of a horse, and the
/// morning's list, a day over the last week (#993 C-1; econ's audit
/// old-household-hunger-residual-2026-10-17 §3e-§3f): what the lamp
/// «лошадей не хватает» reads besides the harness share.
/// @threading PARALLEL_READONLY for the read; written by core_labor's
/// morning placement (labor_system.cpp), single-threaded.
///
/// WHY. kTooFewHorses counted horse jobs done without a horse, and the
/// placement never makes those: on 789 of 814 kNoHorse mornings the pool of
/// adult horses was empty at the placement, the hands waited, and the lamp
/// was dark (host's placement-tick print, §3e). The numbers are the book's
/// own - YearLedger::idle_person_days[kNoHorse] and candidate_person_days of
/// the morning - so the lamp reads what host's base print measured.

#ifndef CORE_COMMON_HORSE_IDLE_WEEK_H_
#define CORE_COMMON_HORSE_IDLE_WEEK_H_

#include <array>
#include <cstdint>

namespace core {

/// The week's days: seven calendar days, days off included.
inline constexpr std::uint32_t kHorseIdleWeekDays = 7;

/// @brief A slot a day by `calendar.day % kHorseIdleWeekDays`, written every
/// morning (noughts included), so the sums are the last seven days'. SAVED
/// (save 173).
struct HorseIdleWeek {
  /// The person-days the morning booked idle for want of a horse.
  std::array<std::uint16_t, kHorseIdleWeekDays> no_horse = {};

  /// The morning's list: the person-days the accountant could place.
  std::array<std::uint16_t, kHorseIdleWeekDays> candidates = {};
};

}  // namespace core

#endif  // CORE_COMMON_HORSE_IDLE_WEEK_H_
