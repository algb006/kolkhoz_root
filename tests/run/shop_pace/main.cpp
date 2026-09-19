// shop_pace — one sauerkraut master with everything he needs, day by day.
//
// WHY THIS RUN EXISTS. host measured the sauerkraut shop on 0.34.6 (thread
// host-econ-shops seq 9): one master pickled ~0.9 t of vegetables a day
// where the recipe (0.1667 man-days a batch of a tonne) promises six, stood
// idle on days with vegetables, grocery and barrels all in the stores, and
// the sauerkraut left the store as fast as it was made. No run of the core
// built a shop, so none of the three was visible here.
//
// What this run found on 0.34.7, seed 9 (boss seq 11):
// - THE PACE IS THE ROAD. The core credits a master hours × efficiency ÷
//   standard_day_hours, like every worker, and his hours are the daylight
//   less the road both ways at 2.4 game hours a kilometre. A yard at his
//   house: 1.03 man-days on a September day, 6.2 t of vegetables — the
//   recipe. The same yard 1.6 km away: 0.41.
// - THE STAND WAS THE ROOM. The shop's bound read the stores' room as they
//   stood, full of the cabbage it was to take; demand fell to zero beside
//   forty tonnes of it, and nothing said why.
// - THE ISSUE TOOK THE SAUERKRAUT beside the fresh vegetables, position by
//   position; four days emptied what a week had pickled.
//
// The run plays the chairman once, in September of the first year: a food
// yard with its store and its sauerkraut shop, standing and whole; a food
// master appointed; forty tonnes of vegetables, half a tonne of grocery and
// two hundred barrels in the store. `--far` puts the yard where the first
// unit of the world stands instead of at the master's house, and asserts
// nothing about the pace: it prints what the road costs.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "../common/run_harness.h"
#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/day_off.h"
#include "core_common/labor_state.h"
#include "core_common/ledger_state.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace {

constexpr std::uint64_t kSeed = 9;
constexpr std::uint32_t kSeasonOpens = 32;  // September of the first year
constexpr std::uint32_t kDaysWatched = 16;  // to the year's turn
constexpr core::Grams kTonne = 1'000'000;
constexpr core::Grams kBarrelGrams = 15'000;

/// A full working day of one master at his own door drains at least this —
/// the reference worker's day is one man-day, and a September day is long.
constexpr float kFullDayAtHome = 0.9F;

/// What the stores' sauerkraut may lose in a day while fresh vegetables are
/// there to issue instead: its own spoiling (600 days) and nothing more.
constexpr double kKrautDayLoss = 0.01;

template <typename Tag>
core::DefId<Tag> RowOf(const core::ITableSet& tables,
                       std::string_view table_name,
                       std::string_view key) {
  const core::ITable* const table = tables.FindTable(table_name);
  const std::uint32_t row = table == nullptr ? core::kNoTableRow : table->FindRowByKey(key);
  return row == core::kNoTableRow ? core::DefId<Tag>{}
                                  : core::DefId<Tag>{static_cast<std::uint16_t>(row)};
}

core::Grams HeldOf(const core::WorldState& world, core::ResourceId resource) {
  core::Grams held = 0;
  for (const core::UnitRow& unit : world.units.rows) {
    if (unit.level > 0 && resource.value < unit.stock.size()) {
      held += unit.stock[resource.value];
    }
  }
  return held;
}

void Put(core::UnitRow& unit, core::ResourceId resource, core::Grams grams) {
  if (resource.value >= unit.stock.size()) {
    unit.stock.resize(resource.value + 1, 0);
  }
  unit.stock[resource.value] += grams;
}

struct Keys {
  core::UnitTypeId yard;
  core::UnitTypeId store;
  core::UnitTypeId shop;
  core::ProfessionId master;
  core::ResourceId vegetables;
  core::ResourceId grocery;
  core::ResourceId barrel;
  core::ResourceId sauerkraut;
};

Keys ReadKeys(const core::ITableSet& tables) {
  return {.yard = RowOf<core::UnitTypeIdTag>(tables, "unit_types", "food_yard"),
          .store = RowOf<core::UnitTypeIdTag>(tables, "unit_types", "food_store"),
          .shop = RowOf<core::UnitTypeIdTag>(tables, "unit_types", "sauerkraut_shop"),
          .master = RowOf<core::ProfessionIdTag>(tables, "professions", "food_master"),
          .vegetables = RowOf<core::ResourceIdTag>(tables, "resources", "vegetables"),
          .grocery = RowOf<core::ResourceIdTag>(tables, "resources", "grocery"),
          .barrel = RowOf<core::ResourceIdTag>(tables, "resources", "barrel"),
          .sauerkraut = RowOf<core::ResourceIdTag>(tables, "resources", "sauerkraut")};
}

/// The first man of 25 to 40 without a post; kNoRow if none.
std::uint32_t ChooseMaster(const core::WorldState& world) {
  const auto today = static_cast<std::int32_t>(world.calendar.day);
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const core::ResidentRow& person = world.residents.rows[row];
    const std::int32_t years = (today - person.birth_day) * 4 / 48;
    if (person.sex == core::Sex::kMale && years >= 25 && years <= 40 &&
        person.post.profession.value == core::kInvalidDefIdValue) {
      return row;
    }
  }
  return core::kNoRow;
}

core::Vec2 HouseOf(const core::WorldState& world, std::uint32_t resident_row) {
  const core::FamilyRow& family =
      world.families.rows[core::FindRow(world.families, world.residents.rows[resident_row].family)];
  return world.units.rows[core::FindRow(world.units, family.house)].position;
}

/// Puts up the yard, its store and its shop, appoints the master; returns
/// the shop's id.
core::UnitId BuildShop(core::WorldState& world,
                       const Keys& keys,
                       std::uint32_t master_row,
                       bool far) {
  core::UnitRow yard;
  yard.type = keys.yard;
  yard.position = far ? world.units.rows.front().position : HouseOf(world, master_row);
  const core::UnitId yard_id = core::AppendRow(world.units, yard);
  core::UnitRow store;
  store.type = keys.store;
  store.position = yard.position;
  store.parent = yard_id;
  Put(store, keys.vegetables, 40 * kTonne);
  Put(store, keys.grocery, kTonne / 2);
  Put(store, keys.barrel, 200 * kBarrelGrams);
  core::AppendRow(world.units, store);
  core::UnitRow shop;
  shop.type = keys.shop;
  shop.position = yard.position;
  shop.parent = yard_id;
  const core::UnitId shop_id = core::AppendRow(world.units, shop);
  world.residents.rows[master_row].post.profession = keys.master;
  world.residents.rows[master_row].post.unit = yard_id;
  const core::Vec2 home = HouseOf(world, master_row);
  const float dx = home.x - yard.position.x;
  const float dy = home.y - yard.position.y;
  std::cout << "shop_pace: FIXTURE DIFFERS FROM THE START CANON — on day " << kSeasonOpens
            << " a food yard, its store and a sauerkraut shop stand whole "
            << std::sqrt((dx * dx) + (dy * dy)) << " m from the house of resident row "
            << master_row
            << ", appointed food master; 40 t vegetables, 500 kg grocery, 200 barrels in the "
               "store\n";
  return shop_id;
}

/// The day's alarm that says why the shop stands, in words; empty if none.
std::string StandReason(core::ISimulation& simulation, core::UnitId shop) {
  std::vector<core::Alarm> alarms;
  simulation.CollectAlarms(alarms);
  for (const core::Alarm& alarm : alarms) {
    if (alarm.kind != core::AlarmKind::kProcessingStopped || alarm.unit.value != shop.value) {
      continue;
    }
    switch (alarm.stop_reason) {
      case core::ProcessingStopReason::kShortOf:
        return "short_of r" + std::to_string(alarm.resource.value);
      case core::ProcessingStopReason::kNoRoom:
        return "no_room r" + std::to_string(alarm.resource.value);
      case core::ProcessingStopReason::kTooFar:
        return "too_far " + std::to_string(alarm.amount) + " h";
      case core::ProcessingStopReason::kNone:
      case core::ProcessingStopReason::kProcessingStopReasonCount:
        return "UNNAMED";
    }
  }
  return "";
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<std::string_view> args(argv + 1, argv + argc);
  const bool far = std::ranges::find(args, "--far") != args.end();
  run::Simulation world = run::Start(kSeed);
  if (!world) {
    return 1;
  }
  const Keys keys = ReadKeys(*world.tables);
  run::AdvanceDays(*world, kSeasonOpens);
  core::WorldState fixture = world.State();
  const std::uint32_t master_row = ChooseMaster(fixture);
  if (master_row == core::kNoRow) {
    std::cout << "FAIL: no man of 25-40 to appoint\n";
    return 1;
  }
  const core::UnitId shop_id = BuildShop(fixture, keys, master_row, far);
  world->ResetWorld(fixture);

  int failures = 0;
  float best_day = 0.0F;
  std::uint32_t silent_stands = 0;
  std::uint32_t kraut_issued_days = 0;
  std::uint32_t too_far_days = 0;
  std::uint32_t walked_too_far = 0;
  bool previous_too_far = false;
  std::cout << "day      master by the hour (U shop, . none) | demand | worked man-days | veg t | "
               "kraut t | daylight h | stands for\n";
  for (std::uint32_t day = 0; day < kDaysWatched; ++day) {
    const core::Grams kraut_before = HeldOf(world.State(), keys.sauerkraut);
    const float asked_this_morning = world.State()
                                         .units.rows[core::FindRow(world.State().units, shop_id)]
                                         .production_days_written;
    std::string hours;
    float worked = 0.0F;
    for (std::uint32_t tick = 0; tick < core::kTicksPerDay; ++tick) {
      world->AdvanceStep();
      const core::WorkAssignment& work = world.State().residents.rows[master_row].work;
      worked = std::max(worked, work.worked_norm_days_today);
      hours += work.kind == core::WorkKind::kUnitWork ? 'U' : '.';
    }
    const core::WorldState& after = world.State();
    const core::UnitRow& shop = after.units.rows[core::FindRow(after.units, shop_id)];
    const core::Grams vegetables = HeldOf(after, keys.vegetables);
    const core::Grams kraut = HeldOf(after, keys.sauerkraut);
    const auto reserve = static_cast<core::Grams>(
        0.25 * static_cast<double>(core::AmountOf(after.ledger.current.harvest, keys.vegetables)));
    const std::string reason = StandReason(*world, shop_id);
    const core::SimDay watched = after.calendar.day - 1;
    const bool day_off = core::IsDayOffIn(after, watched);
    std::cout << std::setw(3) << watched << (day_off ? " off " : "     ") << hours << " | "
              << shop.production_days_written << " | " << worked << " | "
              << static_cast<double>(vegetables) / 1e6 << " | " << static_cast<double>(kraut) / 1e6
              << " | " << after.weather.daylight_hours << " | " << (reason.empty() ? "-" : reason)
              << '\n';
    best_day = std::max(best_day, worked);
    too_far_days += reason.starts_with("too_far") ? 1U : 0U;
    // The alarm is tomorrow's word: labor must agree with it tomorrow.
    walked_too_far += previous_too_far && hours.find('U') != std::string::npos ? 1U : 0U;
    previous_too_far = reason.starts_with("too_far");
    // Two silences, each must carry the shop's reason: a shop with a tonne
    // above the reserve that asks nothing for tomorrow, and a working day on
    // which the master worked nothing of what the shop asked this morning.
    // The alarm is read at the turn of the day, so a day is judged only when
    // tomorrow is in the season too (world_params sauerkraut_from/to_month,
    // September to December): the season's last day is not.
    const bool in_season = after.calendar.date.month >= core::Month::kSeptember &&
                           after.calendar.date.month <= core::Month::kDecember;
    const bool asks_nothing =
        shop.production_days_written <= 0.0F && vegetables - reserve >= kTonne;
    const bool stayed_home = asked_this_morning > 0.0F && !day_off && !(worked > 0.0F);
    if (in_season && (asks_nothing || stayed_home) && reason.empty()) {
      ++silent_stands;
    }
    if (vegetables >= kTonne &&
        static_cast<double>(kraut) <
            static_cast<double>(kraut_before) * (1.0 - kKrautDayLoss) - 1.0) {
      ++kraut_issued_days;
    }
  }
  std::cout << "shop_pace: best day " << best_day << " man-days; silent stands " << silent_stands
            << "; days the sauerkraut left beside fresh vegetables " << kraut_issued_days << " of "
            << kDaysWatched << "; days too far " << too_far_days
            << ", of them the master set out anyway " << walked_too_far << '\n';
  if (far) {
    // 1.6 km is 3.9 game hours a way: in December's seven-hour days the
    // road leaves less than min_usable_hours, and he stays home (boss seq 13).
    failures += run::Expect(too_far_days > 0 && walked_too_far == 0,
                            "shop_pace --far: when the road eats the short day the master stays "
                            "home and the shop says it is too far");
  } else {
    failures += run::Expect(best_day >= kFullDayAtHome,
                            "shop_pace: a master at his own door works a whole man-day — six "
                            "tonnes of vegetables, the recipe");
  }
  failures += run::Expect(silent_stands == 0,
                          "shop_pace: a shop with vegetables above the reserve that asks for "
                          "nothing says why (processing_stopped)");
  failures += run::Expect(kraut_issued_days == 0,
                          "shop_pace: the sauerkraut stays in the store while fresh vegetables are "
                          "there to issue");
  return failures == 0 ? 0 : 1;
}
