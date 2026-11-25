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
  // (A meadow in its cut is in kHarvest too: «сено не успевает», econ [7], is this reason.)
  bool sowing = false;
  for (const FieldRow& field : world.fields.rows) {
    if (field.phase == FieldPhase::kHarvest || field.reaped_grams > 0) {
      return UnderstoodReason::kWeather;
    }
    sowing = sowing || field.phase == FieldPhase::kPlowing ||
             field.phase == FieldPhase::kHarrowing || field.phase == FieldPhase::kSowing;
  }
  // THE SOWING WINDOW CLOSING (econ [7]): a field ploughed, harrowed or sown today.
  if (sowing) {
    return UnderstoodReason::kSowingWindow;
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

IssueShortfall IssueShortfallOf(const WorldState& world, const AuthorityRules& rules) {
  IssueShortfall result;
  for (const FamilyRow& family : world.families.rows) {
    if (!(family.month_need_kcal > 0.0F)) {
      continue;  // nobody in it eats from the pantry: no need, no shortfall
    }
    ++result.families;
    if (family.month_eaten_kcal < rules.short_eaten_share * family.month_need_kcal) {
      ++result.short_families;
      result.shortfall_kcal += family.month_need_kcal - family.month_eaten_kcal;
    }
  }
  result.share = result.families > 0 ? static_cast<float>(result.short_families) /
                                           static_cast<float>(result.families)
                                     : 0.0F;
  return result;
}

bool FamiliesShort(const IssueShortfall& shortfall, const AuthorityRules& rules) {
  return shortfall.short_families > 0 && shortfall.share >= rules.short_share;
}

void SayIssueRaised(WorldState& world, const AuthorityRules& rules) {
  ChairmanState& chairman = world.chairman;
  if (chairman.authority_live == 0) {
    return;
  }
  // The month is the chairman's who raised it, hungry or not: it is not judged unfair.
  chairman.issue_raised_month = (world.calendar.day / kDaysPerMonth) + 1U;
  // The +2 only when the village is short (raising it with nobody short earns nothing, econ's
  // pump check) and once a season (econ [6]).
  const std::uint32_t season = AuthoritySeasonOf(world.calendar.day);
  if (chairman.raise_season == season || !FamiliesShort(IssueShortfallOf(world, rules), rules)) {
    return;
  }
  chairman.raise_season = season;
  MoveAuthority(world, rules, AuthorityDeed::kRaisedIssueAtLine);
}

void JudgeIssueMonth(WorldState& world, const AuthorityRules& rules, Grams free_grain_eq_grams) {
  const std::uint32_t day = world.calendar.day;
  if ((day + 1U) % kDaysPerMonth != 0) {
    return;  // the month's last day only
  }
  ChairmanState& chairman = world.chairman;
  const std::uint32_t month = (day / kDaysPerMonth) + 1U;
  const std::uint32_t season = AuthoritySeasonOf(day);
  const IssueShortfall shortfall = IssueShortfallOf(world, rules);
  // (The year's pressure is the plan's, econ [10]: core_production MarkPlanPressure - not this
  // month's shortfall, which marked 74 % of the canon's years.)
  if (chairman.issue_raised_month == month || chairman.unfair_season == season) {
    return;
  }
  // «МЕСЯЦ ПОД ЧЕРТОЙ ПРИ ПОЛНОМ СКЛАДЕ» (econ [6]): a tenth of the families short of 0.9 of
  // their need over the month, AND the free stock over the funds could have closed their gap.
  const auto gap_grams = static_cast<double>(shortfall.shortfall_kcal) /
                         static_cast<double>(rules.grain_kcal_per_gram);
  if (!FamiliesShort(shortfall, rules) || static_cast<double>(free_grain_eq_grams) < gap_grams) {
    return;
  }
  chairman.unfair_season = season;
  MoveAuthority(world, rules, AuthorityDeed::kUnfairIssueMonth);
}

void CountHardAsk(WorldState& world) {
  if (world.chairman.year_hard_asks < 0xFFFFU) {
    ++world.chairman.year_hard_asks;
  }
}

void SayPlanYearMet(WorldState& world, const AuthorityRules& rules, bool met) {
  // THE PLAN MET WITHOUT DRIVING THE PEOPLE (econ [8]): no rush and no cancelled day off - IN A
  // YEAR UNDER PRESSURE (econ [9]; au6: +2 in 74 % of canon years drove A to 80). The third of
  // econ's pressures, a position's harvest below its expected yield, is a STUB: the core keeps
  // no expected yield a position can be compared with.
  if (met && world.chairman.year_hard_asks == 0 && world.chairman.year_under_pressure != 0) {
    MoveAuthority(world, rules, AuthorityDeed::kPlanMetFairly);
  }
  world.chairman.year_hard_asks = 0;
  world.chairman.year_under_pressure = 0;
}

void SaySocialBuilt(WorldState& world, const AuthorityRules& rules, std::uint32_t kind) {
  constexpr std::uint32_t kKinds = 8;
  if (kind >= kKinds) {
    return;
  }
  const auto bit = static_cast<std::uint8_t>(1U << kind);
  if ((world.chairman.social_kinds_built & bit) != 0 || world.chairman.authority_live == 0) {
    return;  // the first of a kind only
  }
  world.chairman.social_kinds_built =
      static_cast<std::uint8_t>(world.chairman.social_kinds_built | bit);
  MoveAuthority(world, rules, AuthorityDeed::kFirstSocialBuilding);
}

bool MarkAuthoritySeason(WorldState& world) {
  ChairmanState& chairman = world.chairman;
  const std::uint32_t season = AuthoritySeasonOf(world.calendar.day);
  if (chairman.authority_season == season) {
    return false;
  }
  const bool first = chairman.authority_season == 0;
  chairman.authority_fell =
      !first && chairman.authority < chairman.authority_at_season_start ? 1U : 0U;
  chairman.authority_at_season_start = chairman.authority;
  chairman.authority_season = season;
  return !first;
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
