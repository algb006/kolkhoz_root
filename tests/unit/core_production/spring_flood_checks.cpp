// The checks of the spring flood (spring_flood_checks.h).

#include "spring_flood_checks.h"

#include <cstdint>
#include <iostream>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/flood_state.h"
#include "core_common/land_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "production_config.h"
#include "spring_flood.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr std::uint16_t kHay = 1;
constexpr core::Grams kTonne = 1'000'000;

/// The day of the year of a month's first day (0-based month).
constexpr core::SimDay MonthDay(std::uint32_t month) {
  return month * core::kDaysPerMonth;
}

void SetDay(core::WorldState& world, core::SimDay day) {
  world.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);
}

/// A world with one floodplain meadow (2 t of hay in its heap) and one dry meadow (3 t).
core::WorldState MakeMeadows() {
  core::WorldState world;
  core::FieldRow flood;
  flood.kind = core::LandKind::kFloodplainMeadow;
  flood.reaped_resource = core::ResourceId{kHay};
  flood.reaped_grams = 2 * kTonne;
  core::AppendRow(world.fields, flood);
  core::FieldRow dry;
  dry.kind = core::LandKind::kMeadow;
  dry.reaped_resource = core::ResourceId{kHay};
  dry.reaped_grams = 3 * kTonne;
  core::AppendRow(world.fields, dry);
  return world;
}

core::ProductionConfig MakeConfig() {
  core::ProductionConfig config;
  config.hay_resource = core::ResourceId{kHay};
  config.farming.flood_water_days = 8;
  return config;
}

/// Yesterday and today, `day` today, the cover `before` yesterday and `after` today.
bool FloodsOn(core::SimDay day,
              std::uint16_t before,
              std::uint16_t after,
              std::uint16_t last_flood_year = 0) {
  core::WorldState previous;
  core::WorldState current;
  SetDay(previous, day - 1U);
  SetDay(current, day);
  previous.weather.snow_cover_days = before;
  current.weather.snow_cover_days = after;
  current.flood.last_flood_year = last_flood_year;
  return core::IsFloodDay(previous, current);
}

}  // namespace

int CheckTheSpringFlood() {
  int failures = 0;
  const core::SimDay year2 = core::kDaysPerYear;
  // THE DAY: the cover clearing on or after 1 March, from the second spring; 1 March bare
  // floods; 1 May floods whatever the cover; once a year.
  failures +=
      Expect(!FloodsOn(MonthDay(2) + 1U, 5, 0), "flood: no flood in the first spring (the design)");
  failures += Expect(!FloodsOn(year2 + MonthDay(1) + 1U, 5, 0),
                     "flood: a February melt is not the flood day");
  failures += Expect(FloodsOn(year2 + MonthDay(2), 0, 0),
                     "flood: 1 March with the cover gone already floods");
  failures += Expect(!FloodsOn(year2 + MonthDay(2) + 1U, 4, 3),
                     "flood: the cover still lying in March holds the flood back");
  failures += Expect(FloodsOn(year2 + MonthDay(3) + 2U, 4, 0),
                     "flood: the day the cover clears in April is the flood day");
  failures += Expect(!FloodsOn(year2 + MonthDay(3) + 2U, 4, 0, 2),
                     "flood: a year already flooded floods no more");
  failures += Expect(FloodsOn(year2 + MonthDay(4), 9, 9),
                     "flood: 1 May floods whatever the cover (STUB fallback)");

  // WHAT IT TAKES: the floodplain meadow's heap, all of it, into lost_to_flood; the dry
  // meadow's stays; the flood's year and water are written.
  {
    const core::ProductionConfig config = MakeConfig();
    core::WorldState previous = MakeMeadows();
    core::WorldState current = MakeMeadows();
    SetDay(previous, year2 + MonthDay(2) - 1U);
    SetDay(current, year2 + MonthDay(2));
    core::RunSpringFlood(config, previous, current);
    const core::ResourceAmounts& lost = current.ledger.current.lost_to_flood;
    failures += Expect(current.fields.rows[0].reaped_grams == 0 &&
                           current.fields.rows[1].reaped_grams == 3 * kTonne,
                       "flood: the floodplain heap goes, the dry meadow's stays");
    failures += Expect(lost.size() > kHay && lost[kHay] == 2 * kTonne,
                       "flood: every gram it took is booked to lost_to_flood");
    failures += Expect(current.flood.last_flood_year == 2 &&
                           current.flood.water_until_day == year2 + MonthDay(2) + 8U,
                       "flood: the year and the water's end are written");
  }

  // THE PROJECTED DAY: next spring's 1 March from 1 December, this year's before it, today
  // from 1 March, none from May to November or in year 1's spring.
  {
    core::WorldState world;
    SetDay(world, MonthDay(11));
    failures += Expect(core::ProjectedFloodDay(world) == year2 + MonthDay(2),
                       "flood: from 1 December the next spring's 1 March");
    SetDay(world, year2 + 1U);
    failures += Expect(core::ProjectedFloodDay(world) == year2 + MonthDay(2),
                       "flood: in January this year's 1 March");
    SetDay(world, year2 + MonthDay(2) + 3U);
    failures += Expect(core::ProjectedFloodDay(world) == year2 + MonthDay(2) + 3U,
                       "flood: from 1 March «today» - early, never late");
    SetDay(world, year2 + MonthDay(6));
    failures +=
        Expect(core::ProjectedFloodDay(world) == core::kNoFloodDay, "flood: none in the summer");
    SetDay(world, 1U);
    failures += Expect(core::ProjectedFloodDay(world) == core::kNoFloodDay,
                       "flood: none in year 1's spring");
  }

  // THE LAMP: lit with the floodplain heap's grams and the days to the flood; dark with no
  // fodder on floodplain ground (econ's P0b) and dark in the summer.
  {
    const core::ProductionConfig config = MakeConfig();
    core::WorldState world = MakeMeadows();
    SetDay(world, MonthDay(11) + 1U);
    std::vector<core::Alarm> alarms;
    core::CollectFloodAlarms(config, world, alarms);
    failures +=
        Expect(alarms.size() == 1 && alarms[0].kind == core::AlarmKind::kFloodplainFodderAhead &&
                   alarms[0].amount == 2 * kTonne &&
                   alarms[0].days_ahead == year2 + MonthDay(2) - (MonthDay(11) + 1U),
               "flood lamp: lit in December with the floodplain tonnes and the days");
    world.fields.rows[0].reaped_grams = 0;
    alarms.clear();
    core::CollectFloodAlarms(config, world, alarms);
    failures += Expect(alarms.empty(), "flood lamp: dark with nothing on floodplain ground");
    world.fields.rows[0].reaped_grams = kTonne;
    SetDay(world, year2 + MonthDay(6));
    alarms.clear();
    core::CollectFloodAlarms(config, world, alarms);
    failures += Expect(alarms.empty(), "flood lamp: dark in the summer");
  }
  return failures;
}
