// The chairman's authority in Epoch I (#2164; authority.h).

#include "core_common/authority.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/family_state.h"
#include "core_common/fund_ladder.h"
#include "core_common/land_state.h"
#include "core_common/world_state.h"

namespace core {

namespace {

constexpr float kAuthorityMax = 100.0F;
constexpr float kAuthorityMid = 50.0F;
constexpr float kTenths = 10.0F;
constexpr int kDeedBits = 8;

bool IsStubDeed(AuthorityDeed deed) {
  return deed == AuthorityDeed::kHolidayWorked || deed == AuthorityDeed::kHolidayHalfWorked;
}

}  // namespace

std::uint32_t AuthoritySeasonOf(std::uint32_t day) {
  return (((day / kDaysPerMonth) + 1U) / kMonthsPerSeason) + 1U;
}

void MoveAuthority(WorldState& world, const AuthorityRules& rules, AuthorityDeed deed) {
  const auto index = static_cast<std::size_t>(deed);
  if (world.chairman.authority_live == 0 || IsStubDeed(deed) || index >= rules.deed_delta.size()) {
    return;
  }
  ChairmanState& chairman = world.chairman;
  float delta = rules.deed_delta[index];
  // A WEAK START GROWS FASTER (boss, #2175): below the line a plus counts ×1.5, a minus as it is.
  if (delta > 0.0F && chairman.authority < rules.catch_up_below) {
    delta *= rules.catch_up_factor;
  }
  const float before = chairman.authority;
  chairman.authority = std::clamp(before + delta, 0.0F, kAuthorityMax);
  const auto moved_tenths =
      static_cast<std::int64_t>(std::lround((chairman.authority - before) * kTenths));
  EmitEvent(world, EventKind::kAuthorityMoved, EventSeverity::kNotable).amount =
      static_cast<std::int64_t>(index) | (moved_tenths * (std::int64_t{1} << kDeedBits));
}

std::uint8_t UnderstandingBudget(const AuthorityRules& rules, float authority) {
  if (authority >= rules.understanding_two_from) {
    return 2;
  }
  return authority >= rules.understanding_one_from ? 1 : 0;
}

std::uint8_t UnderstoodDaysLeft(const WorldState& world, const AuthorityRules& rules) {
  const ChairmanState& chairman = world.chairman;
  if (chairman.authority_live == 0) {
    return 0;
  }
  const std::uint8_t budget = UnderstandingBudget(rules, chairman.authority);
  // A NEW SEASON forgets the spent days: the understanding does not carry over, either way.
  const bool this_season = chairman.understood_season == AuthoritySeasonOf(world.calendar.day);
  const std::uint8_t used = this_season ? chairman.understood_used : 0;
  return budget > used ? static_cast<std::uint8_t>(budget - used) : 0;
}

UnderstoodReason UnderstoodReasonToday(const WorldState& world, const AuthorityRules& rules) {
  // THE WEATHER PRESSES: a field in its harvest, or its reaped crop still lying on it. Read off
  // the fields, not the lamps: the lamps are the session's, derived between steps, and no
  // subsystem sees them inside one.
  for (const FieldRow& field : world.fields.rows) {
    if (field.phase == FieldPhase::kHarvest || field.reaped_grams > 0) {
      return UnderstoodReason::kWeather;
    }
  }
  // THE PLAN'S DEADLINE: the turn within `deadline_days`, a position still short.
  if (world.plan.announced != 0 && DaysToPlanTurn(world) <= rules.deadline_days) {
    for (std::size_t at = 0; at < world.plan.due.size(); ++at) {
      const Grams sent = at < world.plan.delivered.size() ? world.plan.delivered[at] : 0;
      if (world.plan.due[at] > sent) {
        return UnderstoodReason::kPlanDeadline;
      }
    }
  }
  // AN EMERGENCY (fire, flood, herd loss): STUB - Epoch I's build has no fire, and the flood
  // and the herd's loss leave no «today» a decision step can read yet. Named in authority.h.
  return UnderstoodReason::kNone;
}

bool SpendUnderstoodDay(WorldState& world, const AuthorityRules& rules) {
  if (UnderstoodToday(world)) {
    return true;  // already spent on this day: one day, one spend
  }
  if (UnderstoodDaysLeft(world, rules) == 0 ||
      UnderstoodReasonToday(world, rules) == UnderstoodReason::kNone) {
    return false;
  }
  ChairmanState& chairman = world.chairman;
  const std::uint32_t season = AuthoritySeasonOf(world.calendar.day);
  if (chairman.understood_season != season) {
    chairman.understood_season = season;
    chairman.understood_used = 0;
  }
  ++chairman.understood_used;
  chairman.understood_day = world.calendar.day + 1U;
  return true;
}

bool FamiliesAtRationLine(const WorldState& world) {
  for (const FamilyRow& family : world.families.rows) {
    if (family.hunger_alarm_lit != 0) {
      return true;
    }
  }
  return false;
}

void SayIssueRaised(WorldState& world, const AuthorityRules& rules) {
  if (world.chairman.authority_live == 0 || !FamiliesAtRationLine(world)) {
    return;  // raising it with nobody hungry earns nothing (econ's pump check)
  }
  const std::uint32_t month = (world.calendar.day / kDaysPerMonth) + 1U;
  if (world.chairman.issue_raised_month == month) {
    return;  // once a month
  }
  world.chairman.issue_raised_month = month;
  MoveAuthority(world, rules, AuthorityDeed::kRaisedIssueAtLine);
}

void JudgeIssueMonth(WorldState& world, const AuthorityRules& rules, bool store_holds_food) {
  const std::uint32_t day = world.calendar.day;
  if ((day + 1U) % kDaysPerMonth != 0) {
    return;  // the month's last day only
  }
  const std::uint32_t month = (day / kDaysPerMonth) + 1U;
  if (store_holds_food && world.chairman.issue_raised_month != month &&
      FamiliesAtRationLine(world)) {
    MoveAuthority(world, rules, AuthorityDeed::kUnfairIssueMonth);
  }
}

bool UnderstoodToday(const WorldState& world) {
  return world.chairman.understood_day == world.calendar.day + 1U;
}

float TheftAuthorityFactor(const AuthorityRules& rules, float authority) {
  return std::clamp(1.0F - ((authority - kAuthorityMid) * rules.theft_slope),
                    rules.theft_factor_low,
                    rules.theft_factor_high);
}

float EffectiveAttitude(float personal, float authority) {
  return std::clamp(personal + (authority - kAuthorityMid), 0.0F, kAuthorityMax);
}

}  // namespace core
