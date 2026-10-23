// The checks of the oat lamp's thin year by the field plan (team_oat_plan_checks.h).
//
// THE KNOWN ANSWERS, by hand: the oat is worth 1.0 feed unit a kilogram, so a
// request of 25 units is 25 kg; 7.5 ha of oat at 850 kg/ha expect 6 375 kg;
// a closed request of 31 t at a margin of 0.8 is a 24.8 t threshold - thin
// with nothing in store (6.375 t < 24.8 t), not thin with 20 t of oat bought
// in (26.375 t >= 24.8 t).
#include "team_oat_plan_checks.h"

#include <cstdint>
#include <iostream>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "production_alarms.h"
#include "production_config.h"
#include "team_oat_plan.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: oat plan: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr core::Grams kKilo = core::kGramsPerKilogram;

/// Oat (resource 0, crop 0) at 850 kg/ha sown in months 4-5; the horse (kind
/// 0) eats it as its first work-only feed at 1.0 unit a kilogram.
core::ProductionConfig OatConfig() {
  core::ProductionConfig config;
  core::CropDef oat;
  oat.resource = core::ResourceId{0};
  oat.yield_kg_per_ha = 850.0F;
  oat.sow_from_month = 4;
  oat.sow_to_month = 5;
  config.crops = {oat};
  config.feed_values = {1.0F};
  config.horse_kind = core::LivestockKindId{0};
  config.livestock.resize(1);
  core::FeedLinkDef link;
  link.kind = core::LivestockKindId{0};
  link.resource = core::ResourceId{0};
  link.work_only = 1;
  config.feed_links = {link};
  return config;
}

/// The world on `day`, one field of 7.5 ha whose chain grows oat this year,
/// standing in `phase` with `work` days left, and one kolkhoz horse.
core::WorldState OatWorld(core::SimDay day, core::FieldPhase phase, float work) {
  core::WorldState world;
  world.calendar.tick = core::TickOfDayHour(day, 8);
  core::RefreshCalendarCaches(world.calendar);
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.area_ga = 7.5F;
  field.rotation_assigned = 1;
  field.rotation_year0 = core::CropId{0};
  field.crop = core::CropId{0};
  field.phase = phase;
  field.work_days_remaining = work;
  core::AppendRow(world.fields, field);
  core::HerdRow horse;
  horse.kind = core::LivestockKindId{0};
  horse.adult_count = 1;
  core::AppendRow(world.herds, horse);
  return world;
}

int CheckTheRequestsUnits(const core::ProductionConfig& config) {
  int failures = 0;
  // FEED UNITS, NOT GRAMS (econ's note on #1040): 25 units at 1.0 a kilogram
  // are 25 kg; at 2.0 a kilogram they would be 12.5 kg.
  failures += Expect(core::TeamWorkFeedGrams(config, 25.0F) == 25 * kKilo,
                     "25 feed units of oat at 1.0 a kilogram are 25 kg");
  core::ProductionConfig rich = config;
  rich.feed_values = {2.0F};
  failures += Expect(core::TeamWorkFeedGrams(rich, 25.0F) == 12'500,
                     "the same 25 units of a feed worth 2.0 a kilogram are 12.5 kg - the "
                     "request is divided by the feed's value");
  failures += Expect(core::TeamWorkFeedGrams(config, 0.0F) == 0 &&
                         core::TeamWorkFeedGrams(core::ProductionConfig{}, 25.0F) == 0,
                     "no room or no horse in the tables: no grams");
  return failures;
}

int CheckTheYearsReading(const core::ProductionConfig& config) {
  int failures = 0;
  core::WorldState thin = OatWorld(core::kDaysPerYear, core::FieldPhase::kIdle, 0.0F);
  thin.ledger.current.team_work_oat_requested = 31'000 * kKilo;
  thin.traction_watch.by_plan_stop_given = 1;  // last year's stop, to clear
  core::ReadTheTeamsOatPlan(config, thin);
  const core::TractionWatch& read = thin.traction_watch;
  failures += Expect(read.plan_oat_ha == 7.5F && read.plan_oat_expected == 6'375 * kKilo &&
                         read.plan_team_need == 31'000 * kKilo,
                     "the year's line: 7.5 ha, 6 375 kg expected, 31 t needed");
  failures += Expect(read.oat_short_by_plan == 1 && read.by_plan_stop_given == 0,
                     "6.375 t against 31 t x 0.8: thin by the plan, and the year's stop is new");
  // THE FIELDS' OWN YIELD (boss, the #1040 thread [5]): soil at 80 against
  // the neutral 50 gives 850 x 1.6 = 1 360 kg/ha, 10 200 kg on 7.5 ha - not
  // the table's 6 375.
  core::WorldState rich = OatWorld(core::kDaysPerYear, core::FieldPhase::kIdle, 0.0F);
  rich.fields.rows[0].fertility = 80.0F;
  rich.ledger.current.team_work_oat_requested = 31'000 * kKilo;
  core::ReadTheTeamsOatPlan(config, rich);
  failures += Expect(rich.traction_watch.plan_oat_expected == 10'200 * kKilo,
                     "rich soil: the line's expected oat is the field's own yield, 10 200 kg");
  // BOUGHT OATS COUNT (econ §1; core's worry in the #1040 thread [1]): 20 t in
  // store make the year 26.375 t, over the 24.8 t threshold.
  core::WorldState bought = OatWorld(core::kDaysPerYear, core::FieldPhase::kIdle, 0.0F);
  bought.ledger.current.team_work_oat_requested = 31'000 * kKilo;
  core::UnitRow store;
  store.level = 1;
  store.stock.assign(1, 20'000 * kKilo);
  core::AppendRow(bought.units, store);
  core::ReadTheTeamsOatPlan(config, bought);
  failures += Expect(bought.traction_watch.oat_short_by_plan == 0,
                     "20 t of oat in store cover the year: not thin, the old lamp stands");
  // THE PLOUGH'S OATS ARE THE TEAM'S OWN (0.37.286's first pair arm read every
  // year thin): on the ploughless first of January the herds' door holds the
  // plough's oats back - 1 000 plough-days closed x 10 units a day x the full
  // share 0.5 over 1.0 a kilogram = 5 t - and the year's reading must count
  // them. Through HerdFeedAllowance the 20 t read 15 t, 21.375 t < 24.8 t:
  // thin. Above the seed and the plan only: 26.375 t, not thin.
  core::ProductionConfig ploughing = config;
  ploughing.livestock[0].feed_units_per_game_day = 10.0F;
  core::WorldState held = bought;
  held.traction_watch = core::TractionWatch{};
  held.ledger.closed.work_days_by_kind[static_cast<std::size_t>(core::WorkKind::kPlowing)] =
      1000.0F;
  core::ReadTheTeamsOatPlan(ploughing, held);
  failures += Expect(held.traction_watch.oat_short_by_plan == 0,
                     "the plough's 5 t held on the first of January still count as the team's "
                     "oat: not thin");
  return failures;
}

int CheckTheFieldWorkDay(const core::ProductionConfig& config) {
  int failures = 0;
  // Day 17 is May (month 4) and a Thursday - not day 16, the first of May, a
  // holiday; day 20 is June and a Sunday.
  failures += Expect(!core::IsFieldWorkDay(config, OatWorld(16, core::FieldPhase::kSowing, 2.0F)),
                     "the first of May is a holiday, not a field-work day");
  failures += Expect(core::IsFieldWorkDay(config, OatWorld(17, core::FieldPhase::kSowing, 2.0F)),
                     "a weekday in the oat's window with sowing left is a field-work day");
  failures += Expect(!core::IsFieldWorkDay(config, OatWorld(20, core::FieldPhase::kSowing, 2.0F)),
                     "a Sunday is not");
  failures += Expect(!core::IsFieldWorkDay(config, OatWorld(17, core::FieldPhase::kSowing, 0.0F)),
                     "a field with no work left is not");
  failures += Expect(!core::IsFieldWorkDay(config, OatWorld(17, core::FieldPhase::kGrowing, 2.0F)),
                     "a growing field is not");
  // Day 30 is August (month 7), outside the oat's window; an autumn furrow
  // is its own season.
  failures += Expect(!core::IsFieldWorkDay(config, OatWorld(30, core::FieldPhase::kPlowing, 2.0F)),
                     "a ploughing outside the crop's window is not");
  core::WorldState furrow = OatWorld(30, core::FieldPhase::kPlowing, 2.0F);
  furrow.fields.rows[0].autumn_furrowing = 1;
  failures += Expect(core::IsFieldWorkDay(config, furrow), "an autumn furrow is, in its season");
  return failures;
}

/// The team's lamp on a world whose watch says what the herd day decided.
std::vector<core::Alarm> TeamLamps(const core::ProductionConfig& config,
                                   const core::WorldState& world) {
  std::vector<core::Alarm> alarms;
  core::CollectHerdAlarms(config, world, alarms);
  std::vector<core::Alarm> lamps;
  for (const core::Alarm& alarm : alarms) {
    if (alarm.kind == core::AlarmKind::kTeamOnHay) {
      lamps.push_back(alarm);
    }
  }
  return lamps;
}

int CheckTheLampByPlan(const core::ProductionConfig& config) {
  int failures = 0;
  core::WorldState world = OatWorld(17, core::FieldPhase::kSowing, 2.0F);
  world.traction_watch.short_ration_days = 5;
  world.traction_watch.work_grain_short = 10 * kKilo;
  failures += Expect(TeamLamps(config, world).size() == 1 &&
                         TeamLamps(config, world)[0].advice == core::AlarmAdvice::kNone,
                     "not thin: the old lamp, by the streak, with no by-plan advice");
  world.traction_watch.oat_short_by_plan = 1;
  failures += Expect(TeamLamps(config, world).empty(),
                     "thin, the streak standing, but today not lit by the herd day: no lamp");
  world.traction_watch.by_plan_lit_today = 1;
  world.traction_watch.by_plan_stop_today = 1;
  const std::vector<core::Alarm> stop = TeamLamps(config, world);
  failures += Expect(stop.size() == 1 && stop[0].lamp == 1 &&
                         stop[0].advice == core::AlarmAdvice::kSowMoreOat &&
                         stop[0].logistics_task.value == core::kInvalidEntityIdValue,
                     "the year's first lit day: a stop, «посейте больше овса», and no carting "
                     "task to lower - no move");
  world.traction_watch.by_plan_stop_today = 0;
  const std::vector<core::Alarm> line = TeamLamps(config, world);
  failures +=
      Expect(line.size() == 1 && line[0].lamp == 0, "a later lit day: the yellow line, not a stop");
  return failures;
}

}  // namespace

int CheckTheTeamsOatPlan() {
  const core::ProductionConfig config = OatConfig();
  int failures = 0;
  failures += CheckTheRequestsUnits(config);
  failures += CheckTheYearsReading(config);
  failures += CheckTheFieldWorkDay(config);
  failures += CheckTheLampByPlan(config);
  return failures;
}
