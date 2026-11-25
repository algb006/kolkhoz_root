// The checks of the chairman's authority (core_common/authority.h; #2164).

#include <cstdint>
#include <iostream>

#include "core_common/authority.h"
#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/land_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace core_test {

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

core::WorldState WorldAt(float authority) {
  core::WorldState world;
  world.chairman.authority = authority;
  return world;
}

}  // namespace

int TestAuthority() {
  int failures = 0;
  const core::AuthorityRules& rules = core::kAuthorityRules;
  // A DEED MOVES IT, and says so: −2 from 50, the event carrying the deed and −20 tenths.
  {
    core::WorldState world = WorldAt(50.0F);
    core::MoveAuthority(world, rules, core::AuthorityDeed::kDayOffWithoutReason);
    const bool said = world.step_events.size() == 1 &&
                      world.step_events[0].kind == core::EventKind::kAuthorityMoved &&
                      (world.step_events[0].amount & 0xFF) ==
                          static_cast<std::int64_t>(core::AuthorityDeed::kDayOffWithoutReason) &&
                      (world.step_events[0].amount >> 8) == -20;
    failures += Expect(world.chairman.authority == 48.0F && said,
                       "authority: a day off without reason takes 2 and is said with its deed");
  }
  // THE CATCH-UP: below 30 a plus counts ×1.5 (+4 → +6), a minus as it is (−2).
  {
    core::WorldState up = WorldAt(20.0F);
    core::MoveAuthority(up, rules, core::AuthorityDeed::kDefendedFoodPosition);
    core::WorldState down = WorldAt(20.0F);
    core::MoveAuthority(down, rules, core::AuthorityDeed::kDayOffWithoutReason);
    failures += Expect(up.chairman.authority == 26.0F && down.chairman.authority == 18.0F,
                       "authority: below 30 a plus counts x1.5, a minus as it is");
  }
  // THE STUBS AND THE SWITCH move nothing and say nothing.
  {
    core::WorldState stub = WorldAt(50.0F);
    core::MoveAuthority(stub, rules, core::AuthorityDeed::kHolidayWorked);
    core::WorldState off = WorldAt(50.0F);
    off.chairman.authority_live = 0;
    core::MoveAuthority(off, rules, core::AuthorityDeed::kAuditRemark);
    failures += Expect(stub.chairman.authority == 50.0F && stub.step_events.empty() &&
                           off.chairman.authority == 50.0F && off.step_events.empty(),
                       "authority: a stub deed and a world with authority off move nothing");
  }
  // THE BUDGET'S EDGES: 0 below 50, 1 to 74, 2 from 75.
  failures += Expect(core::UnderstandingBudget(rules, 49.9F) == 0 &&
                         core::UnderstandingBudget(rules, 50.0F) == 1 &&
                         core::UnderstandingBudget(rules, 74.9F) == 1 &&
                         core::UnderstandingBudget(rules, 75.0F) == 2,
                     "authority: the understanding budget is 0 / 1 / 2 at 50 and 75");
  // AN UNDERSTOOD DAY needs a reason: none with no field at harvest; with one, A 60 spends its
  // one day, the same day reads understood again, the next day of the season has none left,
  // and a new season gives it back.
  {
    core::WorldState world = WorldAt(60.0F);
    world.calendar.day = 13;  // a season's second day (season = 12 days from day 0 with +1)
    failures +=
        Expect(!core::SpendUnderstoodDay(world, rules), "authority: no reason, no understood day");
    core::FieldRow field;
    field.phase = core::FieldPhase::kHarvest;
    core::AppendRow(world.fields, field);
    const bool first = core::SpendUnderstoodDay(world, rules);
    const bool again = core::SpendUnderstoodDay(world, rules) && core::UnderstoodToday(world);
    world.calendar.day = 14;
    const bool next = core::SpendUnderstoodDay(world, rules);
    world.calendar.day = 13 + core::kDaysPerSeason;
    const bool new_season = core::SpendUnderstoodDay(world, rules);
    failures += Expect(first && again && !next && new_season,
                       "authority: one understood day a season at A 60, given back next season");
  }
  // THE UNFAIR MONTH (econ [6]): of ten families one ate 0.8 of its need (short), nine ate it
  // all - a tenth short, 200 kcal short. On the month's last day it fires with free grain over
  // the gap (200 / 3.3 g), not under it, not twice a season, and not in a month with a raise.
  {
    const auto village = [](float authority) {
      core::WorldState world = WorldAt(authority);
      for (int family = 0; family < 10; ++family) {
        core::FamilyRow row;
        row.month_need_kcal = 1000.0F;
        row.month_eaten_kcal = family == 0 ? 800.0F : 1000.0F;
        core::AppendRow(world.families, row);
      }
      world.calendar.day = core::kDaysPerMonth - 1U;  // the first month's last day
      return world;
    };
    const core::IssueShortfall shortfall = core::IssueShortfallOf(village(50.0F), rules);
    core::WorldState poor = village(50.0F);
    core::JudgeIssueMonth(poor, rules, 50);
    core::WorldState full = village(50.0F);
    core::JudgeIssueMonth(full, rules, 100);
    const float after_once = full.chairman.authority;
    full.calendar.day += core::kDaysPerMonth;  // the next month, the same season
    core::JudgeIssueMonth(full, rules, 100);
    core::WorldState raised = village(50.0F);
    core::SayIssueRaised(raised, rules);
    core::JudgeIssueMonth(raised, rules, 100);
    failures += Expect(shortfall.short_families == 1 && shortfall.shortfall_kcal == 200.0F &&
                           poor.chairman.authority == 50.0F && after_once == 48.0F &&
                           full.chairman.authority == 48.0F && raised.chairman.authority == 52.0F,
                       "authority: the unfair month needs a tenth short and the stock to cover "
                       "them, once a season, and a raise answers it (+2 instead)");
  }
  // THE TWO PLUS DOORS (econ [8]): a plan year met with no hard ask is +2, with one it is
  // nothing; the first of a social kind is +2, the second of it nothing.
  {
    core::WorldState fair = WorldAt(50.0F);
    core::SayPlanYearMet(fair, rules, true);
    core::WorldState driven = WorldAt(50.0F);
    core::CountHardAsk(driven);
    core::SayPlanYearMet(driven, rules, true);
    core::WorldState social = WorldAt(50.0F);
    core::SaySocialBuilt(social, rules, 1);
    core::SaySocialBuilt(social, rules, 1);
    failures +=
        Expect(fair.chairman.authority == 52.0F && driven.chairman.authority == 50.0F &&
                   driven.chairman.year_hard_asks == 0 && social.chairman.authority == 52.0F,
               "authority: a plan met without driving +2, the first of a kind +2 once");
  }
  // THE COMPLAINT'S SEASON MARK: A that fell over a season is remembered at the next.
  {
    core::WorldState world = WorldAt(28.0F);
    world.calendar.day = 13;
    const bool first = core::MarkAuthoritySeason(world);  // the first mark: nothing to compare
    world.chairman.authority = 25.0F;
    world.calendar.day = 13 + core::kDaysPerSeason;
    const bool turned = core::MarkAuthoritySeason(world);
    failures += Expect(!first && turned && world.chairman.authority_fell == 1,
                       "authority: a season that ended lower than it began marks a fall");
  }
  // THE THEFT FACTOR: exactly 1 at 50 (the world before 232), 0.25 at 100, 1.75 at 0.
  failures += Expect(core::TheftAuthorityFactor(rules, 50.0F) == 1.0F &&
                         core::TheftAuthorityFactor(rules, 100.0F) == 0.25F &&
                         core::TheftAuthorityFactor(rules, 0.0F) == 1.75F,
                     "authority: the theft factor is 1 at 50, clamped 0.25..1.75");
  failures += Expect(core::EffectiveAttitude(60.0F, 30.0F) == 40.0F &&
                         core::EffectiveAttitude(90.0F, 80.0F) == 100.0F,
                     "authority: the effective attitude is p + (A - 50), clamped");
  return failures;
}

}  // namespace core_test
