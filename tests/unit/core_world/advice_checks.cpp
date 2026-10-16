// Every lamp's advice is a move the doors take (advice_checks.h).

#include "advice_checks.h"

#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/day_off.h"
#include "core_common/event_state.h"
#include "core_common/herd_state.h"
#include "core_common/land_state.h"
#include "core_common/limit_state.h"
#include "core_common/logistics_state.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {

int Expect(bool condition, const std::string& label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// The canon's world and the table set it reads, together.
struct Fixture {
  std::unique_ptr<core::ITableSet> tables;
  std::unique_ptr<core::ISimulation> simulation;
  core::WorldState genesis;

  std::uint32_t Row(std::string_view table, std::string_view key) const {
    const core::ITable* const found = tables->FindTable(table);
    return found == nullptr ? core::kNoTableRow : found->FindRowByKey(key);
  }
};

Fixture MakeFixture() {
  Fixture fixture;
  fixture.tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, nullptr);
  if (fixture.tables == nullptr) {
    return fixture;
  }
  core::StandardSimulationConfig config;
  config.tables = fixture.tables.get();
  config.world_seed = 1929;
  config.worker_count = 1;
  fixture.simulation = core::CreateStandardSimulation(config);
  if (fixture.simulation != nullptr) {
    fixture.genesis = fixture.simulation->CompletedState();
  }
  return fixture;
}

/// Moves `world` to hour 0 of `day_of_year`, `years_on` years after the year
/// it is in.
void SetDay(core::WorldState& world, std::uint32_t day_of_year, std::uint32_t years_on = 0) {
  const core::SimDay year_start = world.calendar.day - (world.calendar.day % core::kDaysPerYear) +
                                  (years_on * core::kDaysPerYear);
  world.calendar.tick = core::TickOfDayHour(year_start + day_of_year, 0);
  core::RefreshCalendarCaches(world.calendar);
}

/// Puts `grams` of `resource` into the standing unit that holds most of it
/// (a stack, a store); into the first standing unit when none holds any.
void Stock(core::WorldState& world, std::uint32_t resource, core::Grams grams) {
  std::uint32_t best = core::kNoRow;
  core::Grams most = -1;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const core::UnitRow& unit = world.units.rows[row];
    if (unit.level == 0) {
      continue;
    }
    const core::Grams held = resource < unit.stock.size() ? unit.stock[resource] : 0;
    if (held > most) {
      most = held;
      best = row;
    }
  }
  if (best == core::kNoRow) {
    return;
  }
  core::UnitRow& unit = world.units.rows[best];
  if (unit.stock.size() <= resource) {
    unit.stock.resize(resource + 1, 0);
  }
  unit.stock[resource] += grams;
}

/// What one advice did: whether its lamp named it, and the step's refusal.
struct AdviceRun {
  bool named = false;
  core::OrderRefusal refusal = core::OrderRefusal::kNone;
  std::uint32_t refused = 0;
};

/// Every advice some case below named AND whose order its door took, by
/// value - the census at the end reads it (#980).
std::array<bool, static_cast<std::size_t>(core::AlarmAdvice::kAlarmAdviceCount)> g_taken_advice{};

/// Lays `world` in, reads the lamps, finds `kind` with `advice`, stages the
/// order `order_of` builds from it and steps once: the refusals of that step.
AdviceRun RunAdvice(core::ISimulation& simulation,
                    const core::WorldState& world,
                    core::AlarmKind kind,
                    core::AlarmAdvice advice,
                    const std::function<core::OrderRow(const core::Alarm&)>& order_of) {
  AdviceRun run;
  simulation.ResetWorld(world);
  std::vector<core::Alarm> alarms;
  simulation.CollectAlarms(alarms);
  const core::Alarm* named = nullptr;
  for (const core::Alarm& alarm : alarms) {
    if (alarm.kind == kind && alarm.advice == advice) {
      named = &alarm;
      break;
    }
  }
  if (named == nullptr) {
    return run;
  }
  run.named = true;
  const core::OrderRow order = order_of(*named);
  simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
  simulation.AdvanceStep();
  for (const core::SimEvent& event : simulation.CompletedState().step_events) {
    if (event.kind == core::EventKind::kOrderRefused) {
      ++run.refused;
      run.refusal = static_cast<core::OrderRefusal>(event.amount);
    }
  }
  if (run.refused == 0 && order.kind != core::OrderKind::kNone) {
    g_taken_advice[static_cast<std::size_t>(advice)] = true;
  }
  return run;
}

/// An advice no order carries, and why (#980; econ's complexity pass of 16
/// October 2026, risk 2: «every advice carries the exact words of the button
/// it means, or names no move»). The lamp's string must then name no move.
struct NoDoor {
  core::AlarmAdvice advice;
  const char* why;
};

constexpr std::array<NoDoor, 2> kNoDoor = {{
    {core::AlarmAdvice::kBringFeed,
     "two moves, one word - «подвезти корма или уменьшить стадо»; no single order"},
    {core::AlarmAdvice::kInsulateStraw,
     "retired 0.37.264 (#979): named by no lamp, its door refuses a livestock unit"},
}};

/// An advice with a door that no case here has run through it yet: OWED, by
/// name and door, printed on every run until a case takes it.
struct Owed {
  core::AlarmAdvice advice;
  const char* door;
};

/// Empty since #991 took the last three (kLiftRush, kBuildStore,
/// kDeliverAbovePlan); a new advice with a door and no case goes here.
constexpr std::array<Owed, 0> kOwed = {};

/// THE CENSUS (#980): every advice value is taken by its door in a case
/// above, flagged no-door with a reason, or owed by name - a new value in
/// none of the three is red, and so is one listed twice or a stale entry.
int CheckTheAdviceCensus() {
  int failures = 0;
  std::uint32_t taken = 0;
  for (std::size_t value = 1; value < g_taken_advice.size(); ++value) {
    const auto advice = static_cast<core::AlarmAdvice>(value);
    const bool no_door = std::ranges::any_of(
        kNoDoor, [advice](const NoDoor& entry) { return entry.advice == advice; });
    const bool owed =
        std::ranges::any_of(kOwed, [advice](const Owed& entry) { return entry.advice == advice; });
    const bool run = g_taken_advice[value];
    taken += run ? 1U : 0U;
    const int homes = (run ? 1 : 0) + (no_door ? 1 : 0) + (owed ? 1 : 0);
    failures += Expect(homes == 1,
                       "advice census: advice " + std::to_string(value) + " is in " +
                           std::to_string(homes) +
                           " of {taken by its door here, no door, owed} - want exactly 1");
  }
  std::cout << "advice census: " << taken << " taken by their doors, " << kNoDoor.size()
            << " with no door, " << kOwed.size() << " OWED of " << g_taken_advice.size() - 1U
            << " advice values\n";
  for (const NoDoor& entry : kNoDoor) {
    std::cout << "advice census: NO DOOR " << static_cast<int>(entry.advice) << " - " << entry.why
              << '\n';
  }
  for (const Owed& entry : kOwed) {
    std::cout << "advice census: OWED " << static_cast<int>(entry.advice) << " - " << entry.door
              << " - no case runs it through its door yet\n";
  }
  return failures;
}

int Report(const AdviceRun& run, const std::string& what) {
  int failures = Expect(run.named, what + ": the lamp names its advice on its own trigger");
  if (run.named) {
    failures += Expect(run.refused == 0,
                       what + ": its order is taken, not refused (refusal " +
                           std::to_string(static_cast<int>(run.refusal)) + ")");
  }
  return failures;
}

/// The kolkhoz cows of the genesis under a cattle yard of the first rung
/// placed by hand beside the village, and that yard's id.
core::UnitId CowsInAPen(const Fixture& fixture, core::WorldState& world) {
  const std::uint32_t yard_type = fixture.Row("unit_types", "cattle_yard");
  const std::uint32_t cow = fixture.Row("livestock", "cow");
  core::UnitRow pen;
  pen.type = core::UnitTypeId{static_cast<std::uint16_t>(yard_type)};
  pen.level = 1;
  pen.position = world.units.rows.empty() ? core::Vec2{} : world.units.rows.front().position;
  const core::UnitId id = core::AppendRow(world.units, pen);
  for (core::HerdRow& herd : world.herds.rows) {
    if (herd.household_owned == 0 && herd.kind.value == cow) {
      herd.unit = id;
      herd.household = core::FamilyId{};
      herd.billeted_count = 0;
    }
  }
  return id;
}

/// Opens the pen's clay warm rung, winter-stopped, under its labour.
void OpenTheClay(core::WorldState& world, core::UnitId pen) {
  core::UnitRow& unit = world.units.rows[core::FindRow(world.units, pen)];
  unit.construction.phase = core::ConstructionPhase::kBuilding;
  unit.construction.target_level = 2;
  unit.construction.winter_works = 0;
  unit.construction.labor_days_total = 30.0F;
  unit.construction.labor_days_remaining = 12.0F;
}

int CheckTheColdLamps(const Fixture& fixture) {
  int failures = 0;
  const std::uint32_t straw = fixture.Row("resources", "straw");
  const std::uint32_t log = fixture.Row("resources", "log");
  const std::uint32_t clay = fixture.Row("resources", "clay");
  const auto by_tonnes = [](float tonnes) {
    return static_cast<core::Grams>(tonnes * static_cast<float>(core::kGramsPerTonne));
  };
  const auto upgrade = [](core::UnitId pen) {
    core::OrderRow order;
    order.kind = core::OrderKind::kUpgradeUnit;
    order.unit = pen;
    return order;
  };
  // The advice `kind`'s lamp names on `world`, or nothing when it is dark.
  const auto advice_of = [&fixture](const core::WorldState& world, core::AlarmKind kind) {
    fixture.simulation->ResetWorld(world);
    std::vector<core::Alarm> alarms;
    fixture.simulation->CollectAlarms(alarms);
    std::optional<core::AlarmAdvice> advice;
    for (const core::Alarm& alarm : alarms) {
      advice = !advice && alarm.kind == kind ? std::optional(alarm.advice) : advice;
    }
    return advice;
  };
  // THE RED, DECEMBER: the cows freezing in a cold pen, the warm rung's
  // recipe in the village - «утеплить двор», built in winter too (registry
  // 316; #979). Until 0.37.264 the red named the straw coat.
  {
    core::WorldState world = fixture.genesis;
    SetDay(world, 45);
    const core::UnitId pen = CowsInAPen(fixture, world);
    for (core::HerdRow& herd : world.herds.rows) {
      herd.cold_nights = herd.unit.value == pen.value ? 1 : herd.cold_nights;
    }
    Stock(world, straw, by_tonnes(30.0F));
    Stock(world, log, by_tonnes(40.0F));
    Stock(world, clay, by_tonnes(30.0F));
    const AdviceRun run = RunAdvice(*fixture.simulation,
                                    world,
                                    core::AlarmKind::kHerdFreezing,
                                    core::AlarmAdvice::kWarmYard,
                                    [&](const core::Alarm&) { return upgrade(pen); });
    failures += Report(run, "herd_freezing, «утеплить двор» in December");
    // THE COAT IS GONE FROM THE DOOR TOO (#979): kInsulateUnit on the pen is
    // refused, the straw in the stores and nothing open on it.
    const AdviceRun coat = RunAdvice(*fixture.simulation,
                                     world,
                                     core::AlarmKind::kHerdFreezing,
                                     core::AlarmAdvice::kWarmYard,
                                     [pen](const core::Alarm&) {
                                       core::OrderRow order;
                                       order.kind = core::OrderKind::kInsulateUnit;
                                       order.unit = pen;
                                       return order;
                                     });
    failures +=
        Expect(coat.named && coat.refused == 1 && coat.refusal == core::OrderRefusal::kRuleForbids,
               "herd_freezing: the straw coat on a livestock unit is refused "
               "kRuleForbids - withdrawn by the human, 13 October 2026");
  }
  // OVER AN OPEN CLAY RUNG: the rung is under way, and the lamp names no
  // move (the coat it named here until 0.37.264 is withdrawn; STUB 795 B).
  {
    core::WorldState world = fixture.genesis;
    SetDay(world, 45);
    const core::UnitId pen = CowsInAPen(fixture, world);
    OpenTheClay(world, pen);
    for (core::HerdRow& herd : world.herds.rows) {
      herd.cold_nights = herd.unit.value == pen.value ? 1 : herd.cold_nights;
    }
    Stock(world, straw, by_tonnes(30.0F));
    failures += Expect(
        advice_of(world, core::AlarmKind::kHerdFreezing) == std::optional(core::AlarmAdvice::kNone),
        "herd_freezing over an open clay rung: lit, and no move named");
  }
  // THE YELLOW, SEPTEMBER: the cold season ahead, a pen whose next rung is
  // warm - «утеплить двор»; the recipe in the village.
  {
    core::WorldState world = fixture.genesis;
    SetDay(world, 33);
    const core::UnitId pen = CowsInAPen(fixture, world);
    Stock(world, straw, by_tonnes(30.0F));
    Stock(world, log, by_tonnes(40.0F));
    Stock(world, clay, by_tonnes(30.0F));
    const AdviceRun run = RunAdvice(*fixture.simulation,
                                    world,
                                    core::AlarmKind::kHerdColdAhead,
                                    core::AlarmAdvice::kWarmYard,
                                    [pen](const core::Alarm&) {
                                      core::OrderRow order;
                                      order.kind = core::OrderKind::kUpgradeUnit;
                                      order.unit = pen;
                                      return order;
                                    });
    failures += Report(run, "herd_cold_ahead, «утеплить двор»");
  }
  // AND WITH THE WARM RUNG OPEN ALREADY: not a second «утеплить двор», and
  // no coat either (#979): lit, no move.
  {
    core::WorldState world = fixture.genesis;
    SetDay(world, 40);
    const core::UnitId pen = CowsInAPen(fixture, world);
    OpenTheClay(world, pen);
    Stock(world, straw, by_tonnes(30.0F));
    failures += Expect(advice_of(world, core::AlarmKind::kHerdColdAhead) ==
                           std::optional(core::AlarmAdvice::kNone),
                       "herd_cold_ahead over an open warm rung: lit, and no move named");
  }
  return failures;
}

/// The first of `worlds` on which the lamp names `advice`, run as RunAdvice
/// does; not named on any of them is reported as such.
AdviceRun FirstNamed(
    core::ISimulation& simulation,
    const std::vector<core::WorldState>& worlds,
    core::AlarmKind kind,
    core::AlarmAdvice advice,
    const std::function<core::OrderRow(const core::Alarm&, const core::WorldState&)>& order_of) {
  // WHAT THE LAMP SAID INSTEAD, on every world tried: «not named» alone does
  // not tell a fixture that missed its trigger from a lamp that says another
  // word there.
  std::string said;
  for (const core::WorldState& world : worlds) {
    simulation.ResetWorld(world);
    std::vector<core::Alarm> alarms;
    simulation.CollectAlarms(alarms);
    std::uint32_t lit = 0;
    std::string words;
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind == kind && alarm.advice == advice) {
        return RunAdvice(simulation, world, kind, advice, [&](const core::Alarm& named) {
          return order_of(named, world);
        });
      }
      if (alarm.kind == kind) {
        ++lit;
        words += std::to_string(static_cast<int>(alarm.advice)) + " ";
      }
    }
    said += "[day " + std::to_string(world.calendar.day) + ": " + std::to_string(lit) +
            " lit, advice " + (words.empty() ? std::string("-") : words) + "] ";
  }
  std::cout << "advice: kind " << static_cast<int>(kind) << " never named "
            << static_cast<int>(advice) << " — said " << said << '\n';
  return AdviceRun{};
}

/// Every gram of hay off the village - the stores, the stacks and the fields'
/// heaps - and then `tonnes` of it into the stack that held most.
core::WorldState WithHay(const Fixture& fixture, core::WorldState world, float tonnes) {
  const std::uint32_t hay = fixture.Row("resources", "hay");
  std::uint32_t stack = core::kNoRow;
  core::Grams most = 0;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    core::UnitRow& unit = world.units.rows[row];
    if (hay < unit.stock.size()) {
      if (unit.stock[hay] > most) {
        most = unit.stock[hay];
        stack = row;
      }
      unit.stock[hay] = 0;
    }
  }
  for (core::FieldRow& field : world.fields.rows) {
    if (field.kind != core::LandKind::kArable) {
      field.reaped_grams = 0;
    }
  }
  if (stack != core::kNoRow) {
    world.units.rows[stack].stock[hay] =
        static_cast<core::Grams>(tonnes * static_cast<float>(core::kGramsPerTonne));
  }
  return world;
}

/// The kolkhoz herd of `kind` with the most adults, or kNoRow.
std::uint32_t KolkhozHerd(const core::WorldState& world, std::uint32_t kind) {
  std::uint32_t best = core::kNoRow;
  for (std::uint32_t row = 0; row < world.herds.rows.size(); ++row) {
    const core::HerdRow& herd = world.herds.rows[row];
    if (herd.household_owned == 0 && herd.kind.value == kind &&
        (best == core::kNoRow || herd.adult_count > world.herds.rows[best].adult_count)) {
      best = row;
    }
  }
  return best;
}

/// Adds `heads` adult cows to the kolkhoz's biggest cow herd: heads above the
/// floors, which «сдать головы» and a hay short in the mowing months need.
void MoreCows(const Fixture& fixture, core::WorldState& world, std::uint16_t heads) {
  const std::uint32_t herd = KolkhozHerd(world, fixture.Row("livestock", "cow"));
  if (herd != core::kNoRow) {
    world.herds.rows[herd].adult_count =
        static_cast<std::uint16_t>(world.herds.rows[herd].adult_count + heads);
  }
}

/// A year closed behind the village with the district's milk position named
/// and the herd's yield in the book: the cows' floor is KNOWN (herd_floors.h -
/// until the plan's first announcement every cow is kept and «сдать головы»
/// has no head to name), at half the cows the year's book counted.
void AMilkFloor(const Fixture& fixture, core::WorldState& world) {
  const std::uint32_t cow = fixture.Row("livestock", "cow");
  const std::uint32_t milk = fixture.Row("resources", "milk");
  const std::uint32_t herd = KolkhozHerd(world, cow);
  if (herd == core::kNoRow) {
    return;
  }
  const auto at = [](core::ResourceAmounts& amounts, std::uint32_t index) -> core::Grams& {
    if (amounts.size() <= index) {
      amounts.resize(index + 1, 0);
    }
    return amounts[index];
  };
  const core::Grams due = 10 * core::kGramsPerTonne;
  at(world.plan.highest_due, milk) = due;
  at(world.ledger.closed.herd_produce, milk) = 2 * due;
  at(world.ledger.closed.adult_head_days, cow) =
      static_cast<core::Grams>(world.herds.rows[herd].adult_count) * core::kDaysPerYear;
}

/// A food yard and its granary standing beside the start's first unit: the
/// feed lot's store (district_limit kNowhereToStore otherwise).
void AFoodYard(const Fixture& fixture, core::WorldState& world, bool with_granary) {
  core::UnitRow yard;
  yard.type = core::UnitTypeId{static_cast<std::uint16_t>(fixture.Row("unit_types", "food_yard"))};
  yard.level = 1;
  yard.position = world.units.rows.empty() ? core::Vec2{} : world.units.rows.front().position;
  yard.position.x += 300.0F;
  const core::UnitId yard_id = core::AppendRow(world.units, yard);
  if (!with_granary) {
    return;
  }
  core::UnitRow granary;
  granary.type = core::UnitTypeId{static_cast<std::uint16_t>(fixture.Row("unit_types", "granary"))};
  granary.level = 1;
  granary.parent = yard_id;
  granary.position = yard.position;
  granary.position.y += 10.0F;
  core::AppendRow(world.units, granary);
}

int CheckTheHayLamp(const Fixture& fixture) {
  int failures = 0;
  const std::uint32_t cow = fixture.Row("livestock", "cow");
  const std::uint32_t horse = fixture.Row("livestock", "horse");
  // «СКОСИТЬ» in the mowing months, a meadow in its cut and no hay anywhere:
  // the avral on that meadow (kDeclareRush).
  {
    std::vector<core::WorldState> worlds;
    for (std::uint32_t day = 18; day <= 34; ++day) {
      core::WorldState world = WithHay(fixture, fixture.genesis, 0.0F);
      // YEAR 2: in year 1 the lamp is silent inside the mowing's window by
      // its own rule (herd_forecast.cpp, «nothing to forecast yet») - the
      // first run of this fixture stood there and was told nothing.
      SetDay(world, day, 1);
      // The meadows' coming hay is in the forecast (kNearestScythes): more
      // mouths than they feed, or nothing is short in the mowing months.
      MoreCows(fixture, world, 400);
      for (core::FieldRow& field : world.fields.rows) {
        if (field.kind != core::LandKind::kArable) {
          field.phase = core::FieldPhase::kHarvest;
          field.work_days_remaining = 5.0F;
        }
      }
      worlds.push_back(std::move(world));
    }
    const AdviceRun run =
        FirstNamed(*fixture.simulation,
                   worlds,
                   core::AlarmKind::kHerdHayShortAhead,
                   core::AlarmAdvice::kCutHay,
                   [](const core::Alarm&, const core::WorldState& world) {
                     core::OrderRow order;
                     order.kind = core::OrderKind::kDeclareRush;
                     for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
                       if (world.fields.rows[row].kind != core::LandKind::kArable) {
                         order.field = world.fields.row_ids[row];
                         break;
                       }
                     }
                     return order;
                   });
    failures += Report(run, "herd_hay_short_ahead, «скосить»");
  }
  // «СНЯТЬ АВРАЛ» (#991): the same cut, an avral standing on the meadow for
  // more days than a cut allows (farming hay_avral_days_a_cut) - the lamp's
  // first word is to lift it, and the door is kDeclareRush, amount 0, on the
  // meadow it stands on.
  {
    std::vector<core::WorldState> worlds;
    for (std::uint32_t day = 18; day <= 34; ++day) {
      core::WorldState world = WithHay(fixture, fixture.genesis, 0.0F);
      SetDay(world, day, 1);
      MoreCows(fixture, world, 400);
      for (core::FieldRow& field : world.fields.rows) {
        if (field.kind != core::LandKind::kArable) {
          field.phase = core::FieldPhase::kHarvest;
          field.work_days_remaining = 5.0F;
          field.rush_phase = core::FieldPhase::kHarvest;
          field.rush_step = 1;
        }
      }
      world.chairman.hay_cut_avral_days = 1000;
      worlds.push_back(std::move(world));
    }
    const AdviceRun run =
        FirstNamed(*fixture.simulation,
                   worlds,
                   core::AlarmKind::kHerdHayShortAhead,
                   core::AlarmAdvice::kLiftRush,
                   [](const core::Alarm&, const core::WorldState& world) {
                     core::OrderRow order;
                     order.kind = core::OrderKind::kDeclareRush;
                     order.amount = 0;
                     for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
                       if (world.fields.rows[row].kind != core::LandKind::kArable) {
                         order.field = world.fields.row_ids[row];
                         break;
                       }
                     }
                     return order;
                   });
    failures += Report(run, "herd_hay_short_ahead over a standing avral, «снять аврал»");
  }
  // «СДАТЬ ГОЛОВЫ», «КУПИТЬ КОРМ», «АМБАР ПОД КОРМ»: October, the hay from
  // nothing upward, the points of the year on hand; each on the first stock
  // the lamp names it at. The order hands the herd the advice names.
  // Three villages: as the genesis stands (no food yard); with a food yard
  // and no granary on it (the feed has no store); and with the yard, its
  // granary, a milk floor known and forty cows above it - the ladder's other
  // rungs.
  enum class Village : std::uint8_t { kBare, kYard, kFull };
  const auto autumn_of = [&fixture](Village village) {
    std::vector<core::WorldState> worlds;
    for (std::uint32_t tonnes = 0; tonnes <= 400; tonnes += 20) {
      core::WorldState world = WithHay(fixture, fixture.genesis, static_cast<float>(tonnes));
      SetDay(world, 37);
      world.limit.points = 2000;
      if (village != Village::kBare) {
        AFoodYard(fixture, world, village == Village::kFull);
      }
      if (village == Village::kFull) {
        AMilkFloor(fixture, world);
        MoreCows(fixture, world, 40);
      }
      worlds.push_back(std::move(world));
    }
    return worlds;
  };
  const std::vector<core::WorldState> autumn = autumn_of(Village::kFull);
  const std::vector<core::WorldState> autumn_yard = autumn_of(Village::kYard);
  const std::vector<core::WorldState> autumn_bare = autumn_of(Village::kBare);
  {
    const AdviceRun run =
        FirstNamed(*fixture.simulation,
                   autumn,
                   core::AlarmKind::kHerdHayShortAhead,
                   core::AlarmAdvice::kReduceHerd,
                   [cow, horse](const core::Alarm& alarm, const core::WorldState& world) {
                     core::OrderRow order;
                     order.kind = core::OrderKind::kHandStock;
                     const bool cows = alarm.hand_over_stock > 0;
                     const std::uint32_t herd = KolkhozHerd(world, cows ? cow : horse);
                     if (herd != core::kNoRow) {
                       order.herd = world.herds.row_ids[herd];
                     }
                     order.amount = cows ? alarm.hand_over_stock : alarm.hand_over_horses;
                     return order;
                   });
    failures += Report(run, "herd_hay_short_ahead, «сдать головы»");
  }
  {
    const core::ITable* const goods = fixture.tables->FindTable("limit_lot_goods");
    const core::ITable* const catalog = fixture.tables->FindTable("limit_catalog");
    const AdviceRun run = FirstNamed(
        *fixture.simulation,
        autumn,
        core::AlarmKind::kHerdHayShortAhead,
        core::AlarmAdvice::kBuyFeed,
        [&](const core::Alarm& alarm, const core::WorldState&) {
          core::OrderRow order;
          order.kind = core::OrderKind::kOrderLimitLot;
          const core::ITable* const resources = fixture.tables->FindTable("resources");
          const std::uint32_t lot_col =
              goods == nullptr ? core::kNoTableColumn : goods->FindColumn("lot");
          const std::uint32_t res_col =
              goods == nullptr ? core::kNoTableColumn : goods->FindColumn("resource");
          for (std::uint32_t row = 0; goods != nullptr && row < goods->RowCount(); ++row) {
            const std::uint32_t resource = resources->FindRowByKey(goods->CellText(row, res_col));
            if (resource == alarm.advice_resource.value && catalog != nullptr) {
              order.lot = core::LimitLotId{
                  static_cast<std::uint16_t>(catalog->FindRowByKey(goods->CellText(row, lot_col)))};
              break;
            }
          }
          return order;
        });
    failures += Report(run, "herd_hay_short_ahead, «купить корм»");
  }
  {
    const std::uint32_t granary = fixture.Row("unit_types", "granary");
    const std::uint32_t food_yard = fixture.Row("unit_types", "food_yard");
    // THE ALARM CARRIES THE TYPE TO BUILD (Alarm::advice_unit_type, 0.37.239):
    // the granary, or the food yard it is a module of while none stands. The
    // yard goes on the former elder's point (suggestions.csv), the granary on
    // the standing yard's plot.
    core::Vec2 yard_point;
    if (const core::ITable* const suggestions = fixture.tables->FindTable("suggestions")) {
      const std::uint32_t row = suggestions->FindRowByKey("suggest_food_store");
      yard_point.x = suggestions->CellReal(row, suggestions->FindColumn("x_m")).value_or(0.0F);
      yard_point.y = suggestions->CellReal(row, suggestions->FindColumn("y_m")).value_or(0.0F);
    }
    core::UnitTypeId named_type;
    const auto build_named = [food_yard, yard_point, &named_type](const core::Alarm& alarm,
                                                                  const core::WorldState& world) {
      named_type = alarm.advice_unit_type;
      core::OrderRow order;
      order.kind = core::OrderKind::kBuildUnit;
      order.unit_type = alarm.advice_unit_type;
      order.position = yard_point;
      for (const core::UnitRow& unit : world.units.rows) {
        if (alarm.advice_unit_type.value != food_yard && unit.type.value == food_yard &&
            unit.level > 0) {
          order.position = unit.position;
          order.position.x += 12.0F;
          break;
        }
      }
      return order;
    };
    const AdviceRun run = FirstNamed(*fixture.simulation,
                                     autumn_yard,
                                     core::AlarmKind::kHerdHayShortAhead,
                                     core::AlarmAdvice::kGranaryForFeed,
                                     build_named);
    failures += Report(run, "herd_hay_short_ahead with a food yard, «амбар под корм»");
    failures += Expect(named_type.value == granary,
                       "«амбар под корм» with a food yard standing names the granary");
    // WITH NO FOOD YARD STANDING - THE START: until 0.37.239 the lamp named
    // the granary all the same and its door answered kNoParent (found by this
    // guard, 13 Oct 2026); the alarm names the yard first.
    named_type = core::UnitTypeId{};
    const AdviceRun bare = FirstNamed(*fixture.simulation,
                                      autumn_bare,
                                      core::AlarmKind::kHerdHayShortAhead,
                                      core::AlarmAdvice::kGranaryForFeed,
                                      build_named);
    failures += Report(bare, "herd_hay_short_ahead with no food yard, «амбар под корм»");
    failures += Expect(named_type.value == food_yard,
                       "«амбар под корм» with no food yard standing names the food yard");
  }
  return failures;
}

/// @param billeted false - the team stands in its yard (a unit); true - it
///        stands where the genesis put it, in the families' yards (0.37.245:
///        until then the lamp skipped a kolkhoz herd billeted in a family's
///        yard - `household` is where it stands, not whose it is - and the
///        first winter's billeted stock starved with no red lamp at all).
int CheckTheStarvingHerdAt(const Fixture& fixture, bool billeted) {
  // «РАСПЕЧАТАТЬ ФОНД»: a kolkhoz herd unfed today, its feed lying in a fund.
  // THE ADVICE NAMES THE FEED AND ITS GRAMS, NOT THE FUND, and kUnsealFund
  // asks for one - the seed fund is tried, then the plan's reserve: the gap
  // between the lamp and its door is printed beside the verdict.
  int failures = 0;
  core::WorldState world = fixture.genesis;
  SetDay(world, 2);
  // The team unfed today - in its yard (a kolkhoz herd at a unit) or billeted
  // as the genesis has it - and the spring's seed oats in the stores: its
  // feed in the fund.
  core::UnitRow yard;
  yard.type = core::UnitTypeId{static_cast<std::uint16_t>(fixture.Row("unit_types", "horse_yard"))};
  yard.level = 1;
  yard.position = world.units.rows.empty() ? core::Vec2{} : world.units.rows.front().position;
  const core::UnitId yard_id = core::AppendRow(world.units, yard);
  const std::uint32_t horse = fixture.Row("livestock", "horse");
  std::uint32_t in_family_yards = 0;
  for (core::HerdRow& herd : world.herds.rows) {
    if (herd.household_owned == 0 && herd.kind.value == horse) {
      if (!billeted) {
        herd.unit = yard_id;
        herd.household = core::FamilyId{};
      }
      in_family_yards += herd.household.value != core::kInvalidEntityIdValue ? 1U : 0U;
      herd.unfed_days = 1.0F;
    }
  }
  if (billeted) {
    failures +=
        Expect(in_family_yards > 0,
               "herd_starving, billeted: the genesis keeps the team in the families' yards");
  }
  Stock(world, fixture.Row("resources", "oat"), 20 * core::kGramsPerTonne);
  const auto unseal = [](core::FundKind fund) {
    return [fund](const core::Alarm& alarm, const core::WorldState&) {
      core::OrderRow order;
      order.kind = core::OrderKind::kUnsealFund;
      order.fund = fund;
      order.resource = alarm.advice_resource;
      order.amount = alarm.advice_amount;
      return order;
    };
  };
  const AdviceRun seed = FirstNamed(*fixture.simulation,
                                    {world},
                                    core::AlarmKind::kHerdStarving,
                                    core::AlarmAdvice::kUnsealFund,
                                    unseal(core::FundKind::kSeed));
  const std::string where =
      billeted ? "herd_starving, billeted in the families' yards, " : "herd_starving, ";
  if (!seed.named || seed.refused == 0) {
    return failures + Report(seed, where + "«распечатать фонд» (the seed fund)");
  }
  const AdviceRun plan = FirstNamed(*fixture.simulation,
                                    {world},
                                    core::AlarmKind::kHerdStarving,
                                    core::AlarmAdvice::kUnsealFund,
                                    unseal(core::FundKind::kPlanReserve));
  std::cout << "advice: «распечатать фонд» names no fund; the seed fund refused "
            << static_cast<int>(seed.refusal) << ", the plan's reserve answered "
            << (plan.refused == 0 ? 0 : static_cast<int>(plan.refusal)) << '\n';
  failures += Report(plan, where + "«распечатать фонд» (the plan's reserve)");
  return failures;
}

int CheckTheStarvingHerd(const Fixture& fixture) {
  return CheckTheStarvingHerdAt(fixture, false) + CheckTheStarvingHerdAt(fixture, true);
}

int CheckTheDayOff(const Fixture& fixture) {
  // «ОБЪЯВИТЬ ДЕНЬ РАБОЧИМ»: a level-0 load waiting on a plain day off.
  core::WorldState world = fixture.genesis;
  for (std::uint32_t day = 1; day < core::kDaysPerYear; ++day) {
    SetDay(world, day);
    if (core::TodayMayBeDeclaredWorking(world)) {
      break;
    }
  }
  core::LogisticsTaskRow task;
  task.load_kind = core::LogisticsLoadKind::kSiteSupply;
  task.unit = world.units.row_ids.empty() ? core::UnitId{} : world.units.row_ids.front();
  task.level = core::LogisticsLevel::kUrgent;
  task.base_level = core::LogisticsLevel::kUrgent;
  task.unserved_light_hours = 20;
  core::AppendRow(world.logistics_tasks, task);
  const AdviceRun run = RunAdvice(*fixture.simulation,
                                  world,
                                  core::AlarmKind::kLogisticsLate,
                                  core::AlarmAdvice::kDeclareDayWorking,
                                  [](const core::Alarm&) {
                                    core::OrderRow order;
                                    order.kind = core::OrderKind::kCancelDayOff;
                                    order.enable = 1;
                                    return order;
                                  });
  return Report(run, "logistics_late on a day off, «объявить день рабочим»");
}

/// «ОБЪЯВИТЬ ДЕНЬ РАБОЧИМ» BY THE HAY'S LAMP (lever 1; 0.37.250): a dry day off
/// inside the cut with its pace short - and THE DAY LIVED THROUGH TO ITS
/// CLOSE, the engine's own order of things. The unit test of the count calls
/// it directly and could not see WHEN the engine calls it: the first pair
/// counted at the day's last tick, after the day's close had let «today's
/// cancelled day» go - the worked day off added nothing to the pace and
/// marked no month.
int CheckTheHayDayOff(const Fixture& fixture) {
  int failures = 0;
  const std::uint32_t hay = fixture.Row("resources", "hay");
  core::WorldState world = WithHay(fixture, fixture.genesis, 20.0F);
  // The cut's months, past its opening day: June's and July's days.
  constexpr std::uint32_t kFirst = 21;
  constexpr std::uint32_t kLast = 27;
  std::uint32_t found = 0;
  for (std::uint32_t day = kFirst; day <= kLast && found == 0; ++day) {
    SetDay(world, day);
    if (core::TodayMayBeDeclaredWorking(world)) {
      found = day;
    }
  }
  if (Expect(found != 0, "hay day off: the cut's months hold a day off the door would take") != 0) {
    return 1;
  }
  for (core::FieldRow& field : world.fields.rows) {
    if (field.kind != core::LandKind::kArable) {
      field.phase = core::FieldPhase::kHarvest;
      field.harvest_work_days = 10.0F;
      field.work_days_remaining = 9.0F;
      field.harvest_laid_share = 0.1F;
    }
  }
  world.chairman.hay_cut_dry_work_days = 2;
  world.chairman.hay_day_worked_month = 0;
  if (world.ledger.current.harvest.size() <= hay) {
    world.ledger.current.harvest.resize(static_cast<std::size_t>(hay) + 1U, 0);
  }
  world.ledger.current.harvest[hay] = 2 * core::kGramsPerTonne;
  world.weather.precipitation = core::Precipitation::kNone;
  const core::SimDay today = world.calendar.day;
  const AdviceRun run = RunAdvice(*fixture.simulation,
                                  world,
                                  core::AlarmKind::kHerdHayShortAhead,
                                  core::AlarmAdvice::kDeclareDayWorking,
                                  [](const core::Alarm&) {
                                    core::OrderRow order;
                                    order.kind = core::OrderKind::kCancelDayOff;
                                    order.enable = 1;
                                    return order;
                                  });
  failures +=
      Report(run, "herd_hay_short_ahead on a dry day off in the cut, «объявить день рабочим»");
  if (!run.named) {
    return failures;
  }
  // To the day's noon - the count's hour - and on through its close.
  bool rained_at_noon = false;
  for (std::uint32_t step = 1; step < core::kTicksPerDay; ++step) {
    fixture.simulation->AdvanceStep();
    const core::WorldState& now = fixture.simulation->CompletedState();
    if (now.calendar.day == today && core::HourFromTick(now.calendar.tick) == 12) {
      rained_at_noon = now.weather.precipitation == core::Precipitation::kRain;
    }
  }
  const core::WorldState& after = fixture.simulation->CompletedState();
  failures += Expect(after.chairman.hay_day_worked_month == 1U + (today / core::kDaysPerMonth),
                     "hay day off: lived through to its close, the worked day off has marked its "
                     "month - the lamp does not name a second one in it");
  failures += Expect(after.chairman.hay_cut_dry_work_days == (rained_at_noon ? 2U : 3U),
                     "hay day off: and it is a dry working day of the cut's pace (or, rained out "
                     "at noon, none)");
  return failures;
}

/// «ПОСТАВИТЬ ПОДВОЗ ВАЖНЕЕ ПОЛЕВЫХ РАБОТ» (#734; kSiteSupplyWaits): a house's
/// site whose materials await at a store and to which nothing came for the
/// floor's days. The door is kSetLogisticsLevel on the lamp's own task with
/// the switch and the task's own level - and once taken, the lamp stays lit
/// and names no move (the door would refuse a repeat).
int CheckTheSupplyFloor(const Fixture& fixture) {
  core::WorldState world = fixture.genesis;
  SetDay(world, 5);
  core::UnitRow site;
  site.type = core::UnitTypeId{static_cast<std::uint16_t>(fixture.Row("unit_types", "house"))};
  site.position = world.units.rows.empty() ? core::Vec2{} : world.units.rows.front().position;
  site.construction.phase = core::ConstructionPhase::kDelivering;
  site.construction.target_level = 1;
  const std::uint32_t log = fixture.Row("resources", "log");
  core::SupplyLeg leg;
  leg.from = site.position;
  leg.grams.assign(static_cast<std::size_t>(log) + 1U, 0);
  leg.grams[log] = 3'000'000;
  site.construction.awaiting = leg.grams;
  site.construction.legs = {leg};
  site.construction.supply_from = leg.from;
  site.construction.supply_days_remaining = 1.0F;
  site.construction.supply_days_written = 1.0F;
  site.construction.supply_since_day = 1;
  site.construction.supply_idle_days = 6;
  const core::UnitId id = core::AppendRow(world.units, site);
  core::LogisticsTaskRow task;
  task.load_kind = core::LogisticsLoadKind::kSiteSupply;
  task.unit = id;
  task.level = core::LogisticsLevel::kTerm;
  task.base_level = core::LogisticsLevel::kTerm;
  const core::LogisticsTaskId task_id = core::AppendRow(world.logistics_tasks, task);
  const auto over_fields = [](const core::Alarm& alarm) {
    core::OrderRow order;
    order.kind = core::OrderKind::kSetLogisticsLevel;
    order.logistics_task = alarm.logistics_task;
    order.logistics_level = core::LogisticsLevel::kTerm;
    order.enable = 1;
    return order;
  };
  int failures = Report(RunAdvice(*fixture.simulation,
                                  world,
                                  core::AlarmKind::kSiteSupplyWaits,
                                  core::AlarmAdvice::kSupplyOverFields,
                                  over_fields),
                        "site_supply_waits, «поставить подвоз важнее полевых работ»");
  const core::WorldState& after = fixture.simulation->CompletedState();
  const std::uint32_t row = core::FindRow(after.logistics_tasks, task_id);
  failures += Expect(row != core::kNoRow && after.logistics_tasks.rows[row].over_fields,
                     "site_supply_waits: the order set the task's switch");
  // THE PAIR: with the switch set the lamp names no move, and the same order
  // again is refused - the lamp and the door agree both ways.
  world.logistics_tasks.rows.back().over_fields = true;
  const AdviceRun repeat = RunAdvice(*fixture.simulation,
                                     world,
                                     core::AlarmKind::kSiteSupplyWaits,
                                     core::AlarmAdvice::kNone,
                                     over_fields);
  failures += Expect(
      repeat.named && repeat.refused == 1 && repeat.refusal == core::OrderRefusal::kRuleForbids,
      "site_supply_waits: with the switch set the lamp stays lit, names no move, "
      "and the door refuses the repeat");
  return failures;
}

/// THE STORE LAMP'S TWO MOVES (#991; row 936): kHarvestWillNotFit on a rye
/// field of year 2 that the stores will not hold. A shortfall of at least the
/// smallest store's third names «build the store» - the door kBuildUnit of
/// the alarm's advice_unit_type; a smaller one, with rye a position of the
/// announced plan and rye in the stores, names «сдать сверх плана» - the door
/// kDeliverPlan of advice_resource, advice_amount.
int CheckTheStoreLamp(const Fixture& fixture) {
  int failures = 0;
  const std::uint32_t rye_crop = fixture.Row("crops", "rye_winter");
  const std::uint32_t rye = fixture.Row("resources", "rye");
  const std::uint32_t food_yard = fixture.Row("unit_types", "food_yard");
  core::Vec2 yard_point;
  if (const core::ITable* const suggestions = fixture.tables->FindTable("suggestions")) {
    const std::uint32_t row = suggestions->FindRowByKey("suggest_food_store");
    yard_point.x = suggestions->CellReal(row, suggestions->FindColumn("x_m")).value_or(0.0F);
    yard_point.y = suggestions->CellReal(row, suggestions->FindColumn("y_m")).value_or(0.0F);
  }
  // July of year 2, a rye field of `hectares` standing in the ground; with
  // `planned`, the district's rye position named and 2 t of rye in store.
  const auto july_with_rye = [&](float hectares, bool planned) {
    core::WorldState world = fixture.genesis;
    SetDay(world, 25, 1);
    core::FieldRow field;
    field.kind = core::LandKind::kArable;
    field.area_ga = hectares;
    field.fertility = 60.0F;
    field.phase = core::FieldPhase::kGrowing;
    field.crop = core::CropId{static_cast<std::uint16_t>(rye_crop)};
    field.center = world.units.rows.empty() ? core::Vec2{} : world.units.rows.front().position;
    core::AppendRow(world.fields, field);
    if (planned) {
      world.plan.announced = 1;
      world.plan.due.resize(std::max<std::size_t>(world.plan.due.size(), rye + 1U), 0);
      world.plan.due[rye] = 5 * core::kGramsPerTonne;
      Stock(world, rye, 2 * core::kGramsPerTonne);
    }
    return world;
  };
  {
    std::vector<core::WorldState> worlds;
    for (const float hectares : {400.0F, 800.0F, 1600.0F, 3200.0F}) {
      worlds.push_back(july_with_rye(hectares, false));
    }
    const AdviceRun run = FirstNamed(*fixture.simulation,
                                     worlds,
                                     core::AlarmKind::kHarvestWillNotFit,
                                     core::AlarmAdvice::kBuildStore,
                                     [&](const core::Alarm& alarm, const core::WorldState& world) {
                                       core::OrderRow order;
                                       order.kind = core::OrderKind::kBuildUnit;
                                       order.unit_type = alarm.advice_unit_type;
                                       order.position = yard_point;
                                       for (const core::UnitRow& unit : world.units.rows) {
                                         if (alarm.advice_unit_type.value != food_yard &&
                                             unit.type.value == food_yard && unit.level > 0) {
                                           order.position = unit.position;
                                           order.position.x += 12.0F;
                                           break;
                                         }
                                       }
                                       return order;
                                     });
    failures += Report(run, "harvest_will_not_fit by a store's third or more, «построить склад»");
  }
  {
    std::vector<core::WorldState> worlds;
    for (const float hectares : {5.0F, 10.0F, 20.0F, 40.0F, 80.0F, 160.0F}) {
      worlds.push_back(july_with_rye(hectares, true));
    }
    core::Alarm named;
    const AdviceRun run = FirstNamed(*fixture.simulation,
                                     worlds,
                                     core::AlarmKind::kHarvestWillNotFit,
                                     core::AlarmAdvice::kDeliverAbovePlan,
                                     [&named](const core::Alarm& alarm, const core::WorldState&) {
                                       named = alarm;
                                       core::OrderRow order;
                                       order.kind = core::OrderKind::kDeliverPlan;
                                       order.resource = alarm.advice_resource;
                                       order.amount = alarm.advice_amount;
                                       return order;
                                     });
    failures += Report(run, "harvest_will_not_fit under a store's third, «сдать сверх плана»");
    // THE DOOR TAKES A SHAPELESS ORDER TOO: kDeliverPlan with no resource
    // ships the whole debt and refuses nothing (district_plan.h,
    // DeliverPlanNow), so «taken» cannot tell an advice that names its
    // resource from one that names none - the fault test of #991 (e2) found
    // it. The advice must name the lamp's resource and an amount.
    failures +=
        Expect(!run.named || (named.advice_resource.value == rye && named.advice_amount > 0 &&
                              named.advice_amount <= named.amount_more),
               "«сдать сверх плана» names the lamp's resource and an amount no larger "
               "than the shortfall");
  }
  return failures;
}

}  // namespace

int CheckEveryAdviceIsTaken() {
  const Fixture fixture = MakeFixture();
  if (fixture.simulation == nullptr) {
    return Expect(false, "advice: the shipped tables assemble a world");
  }
  int failures = 0;
  failures += CheckTheColdLamps(fixture);
  failures += CheckTheHayLamp(fixture);
  failures += CheckTheStarvingHerd(fixture);
  failures += CheckTheDayOff(fixture);
  failures += CheckTheHayDayOff(fixture);
  failures += CheckTheSupplyFloor(fixture);
  failures += CheckTheStoreLamp(fixture);
  // LAST: it reads what every case above took.
  failures += CheckTheAdviceCensus();
  return failures;
}
