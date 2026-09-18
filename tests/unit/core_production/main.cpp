// Unit test of core_production. Two parts:
//   * the contract's shape and the table-less world, which must idle rather
//     than refuse to exist;
//   * the herd day of stage 6 (task O3) — the fodder units and the feeding
//     order, billeting instead of slaughter, the produce and its two leaks,
//     the cohort flows of maturation and birth, and the autumn pigs.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "../../common/fake_tables.h"
#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_production/production_system.h"
#include "core_tables/tables.h"
#include "district_limit.h"
#include "district_plan.h"
#include "district_visit.h"
#include "extraction_digging.h"
#include "field_haul.h"
#include "field_work.h"
#include "herd_system.h"
#include "night_pasture.h"
#include "production_alarms.h"
#include "production_config.h"
#include "stock_lights.h"
#include "stock_ops.h"
#include "timber_felling.h"
#include "unit_production.h"

static_assert(std::is_abstract_v<core::IProductionSystem>, "IProductionSystem is a contract");
static_assert(std::has_virtual_destructor_v<core::IProductionSystem>,
              "implementations are destroyed through the interface");

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

// --- stage 6, task O3: the herd day ---------------------------------------

/// Capacity lives on the LEVEL LADDER and nowhere else (production_config.h:
/// the type's own figure was a copy of level 1 that the export wrote, so the
/// fallback reading it could never differ from the step it fell back from).
/// A test that wants a store therefore fills steps, and it fills the first
/// four of them: the ladder must not have a blank step among named ones —
/// the load refuses that — and no test here stands above level two.
constexpr std::size_t kTestLadderSteps = 4;

void SetStorageKg(core::UnitTypeDef& type, float kilograms) {
  type.level_storage_capacity_kg.assign(kTestLadderSteps, kilograms);
}

void SetLivestockHead(core::UnitTypeDef& type, float heads) {
  type.level_livestock_capacity_head.assign(kTestLadderSteps, heads);
}

/// Two kinds and three resources, all in round numbers so that the
/// expectations below are exact.
///   kind 0  "cow": one fodder unit a game day, sexed, breeds, gives milk
///   kind 1  "pig": the autumn slaughter
///   resource 0 hay (feed value 1), 1 milk, 2 meat
core::ProductionConfig MakeHerdConfig() {
  core::ProductionConfig config;
  config.livestock.resize(2);
  core::LivestockDef& cow = config.livestock[0];
  cow.feed_units_per_game_day = 1.0F;
  cow.sexed = 1;
  cow.males_share = 0.5F;
  cow.newborn_game_months = 1.0F;     // 4 game days on the newborn rung
  cow.adult_from_game_months = 2.0F;  // and 4 more as a juvenile
  cow.life_game_years_min = 100.0F;   // old age is out of the way unless asked
  cow.life_game_years_max = 100.0F;
  cow.births_per_game_year = 1.0F;
  cow.litter_heads = 1.0F;
  cow.milk_l_per_year = 480.0F;  // 10 litres a game day per female
  cow.meat_kg_per_head = 100.0F;
  core::LivestockDef& pig = config.livestock[1];
  pig.feed_units_per_game_day = 0.0F;
  pig.sexed = 1;
  pig.males_share = 0.1F;
  pig.adult_from_game_months = 1.0F;
  pig.life_game_years_min = 100.0F;
  pig.life_game_years_max = 100.0F;
  pig.meat_kg_per_head = 50.0F;
  config.unit_types.resize(1);
  SetStorageKg(config.unit_types[0], 1.0e6F);
  SetLivestockHead(config.unit_types[0], 10.0F);
  config.feed_values = {1.0F, 0.0F, 0.0F};
  config.feed_links = {core::FeedLinkDef{
      .kind = core::LivestockKindId{0}, .resource = core::ResourceId{0}, .reserve = 0}};
  config.milk_resource = core::ResourceId{1};
  config.meat_resource = core::ResourceId{2};
  config.pig_kind = core::LivestockKindId{1};
  config.farming.pasture_from_month = 11;  // out of the way of the tests
  config.farming.pasture_to_month = 11;
  config.farming.birth_from_month = 0;
  config.farming.birth_to_month = 11;  // breed all year, to keep the sums easy
  config.farming.pig_slaughter_month = 9;
  config.farming.sow_keep_share = 0.5F;
  return config;
}

/// A world with one store holding `hay_kg`, and one barn.
///
/// THE RNG IS SEEDED, and it has to be: a default RngState is all zeros, and
/// a zero-state generator returns zero for ever — so every fractional flow
/// (a death, a birth, a head maturing) would fire on every single draw. The
/// checks below that exercise age deaths measured one a day out of a herd
/// that should have lost one a year.
core::WorldState MakeHerdWorld(float hay_kg) {
  core::WorldState world;
  world.rng = core::SeedRngState(20260831, 0);
  core::RefreshCalendarCaches(world.calendar);
  core::UnitRow store;
  store.type = core::UnitTypeId{0};
  store.stock.assign(3, 0);
  store.stock[0] = static_cast<core::Grams>(hay_kg) * core::kGramsPerKilogram;
  AppendRow(world.units, store);
  return world;
}

core::HerdId AddHerd(core::WorldState& world,
                     std::uint16_t kind,
                     std::uint16_t adults,
                     std::uint16_t males,
                     bool at_unit) {
  core::HerdRow herd;
  herd.kind = core::LivestockKindId{kind};
  herd.adult_count = adults;
  herd.adult_male_count = males;
  if (at_unit) {
    herd.unit = world.units.row_ids[0];
  }
  return AppendRow(world.herds, herd);
}

core::Grams StoreOf(const core::WorldState& world, std::uint32_t resource) {
  const core::ResourceAmounts& stock = world.units.rows[0].stock;
  return stock.size() > resource ? stock[resource] : 0;
}

int CheckFeeding() {
  int failures = 0;
  const core::ProductionConfig config = MakeHerdConfig();
  constexpr core::Grams kKilo = core::kGramsPerKilogram;

  // Four adults eat one fodder unit each; hay is worth a unit a kilogram.
  {
    core::WorldState world = MakeHerdWorld(100.0F);
    AddHerd(world, 0, 4, 2, true);
    core::RunHerdDay(config, world);
    failures += Expect(StoreOf(world, 0) == 96 * kKilo,
                       "an adult eats its own norm out of the store, by feed value");
    failures += Expect(world.herds.rows[0].unfed_days == 0.0F, "a fed herd is not hungry");
  }

  // A juvenile eats half the adult norm, a newborn at the dam nothing.
  {
    core::WorldState world = MakeHerdWorld(100.0F);
    const core::HerdId id = AddHerd(world, 0, 2, 1, true);
    core::HerdRow& herd = world.herds.rows[FindRow(world.herds, id)];
    herd.juvenile_count = 2;
    herd.newborn_count = 4;
    core::RunHerdDay(config, world);
    failures += Expect(StoreOf(world, 0) == 97 * kKilo,
                       "two adults and two juveniles eat three units, the newborns none");
  }

  // An empty store leaves the herd hungry, and hunger halves the milk.
  {
    core::WorldState world = MakeHerdWorld(0.0F);
    AddHerd(world, 0, 4, 2, true);
    core::RunHerdDay(config, world);
    failures += Expect(world.herds.rows[0].unfed_days == 1.0F, "an empty store makes a hungry day");
    failures += Expect(StoreOf(world, 1) == 10 * kKilo,
                       "and a hungry day halves the milk: two cows at 10 l, halved");
  }
  return failures;
}

/// THE KOLKHOZ HERD'S PRODUCE GOES THROUGH THE DOOR (boss, parcel 408). Until
/// 2026-09-15 it went into the first numbered store whatever it was: milk
/// into a store that is no home of milk. Now it goes only where it is at
/// home, and what has no home is booked lost the same day.
int CheckTheHerdsMilkGoesToItsHome() {
  int failures = 0;
  constexpr core::Grams kKilo = core::kGramsPerKilogram;
  core::ProductionConfig config = MakeHerdConfig();
  config.resource_stores_read = 1;
  config.unit_types[0].home_of = {core::ResourceId{0}};  // the store keeps hay alone
  {
    core::WorldState world = MakeHerdWorld(100.0F);
    world.units.rows[0].level = 1;
    AddHerd(world, 0, 4, 2, true);
    core::RunHerdDay(config, world);
    const core::ResourceAmounts& lost = world.ledger.current.lost_no_room;
    failures += Expect(StoreOf(world, 1) == 0 && lost.size() > 1 && lost[1] == 20 * kKilo,
                       "milk with no home among the stores goes in nowhere and is booked lost");
  }
  config.unit_types[0].home_of = {core::ResourceId{0}, core::ResourceId{1}};
  {
    core::WorldState world = MakeHerdWorld(100.0F);
    world.units.rows[0].level = 1;
    AddHerd(world, 0, 4, 2, true);
    core::RunHerdDay(config, world);
    const core::ResourceAmounts& lost = world.ledger.current.lost_no_room;
    failures += Expect(StoreOf(world, 1) == 20 * kKilo && (lost.size() < 2 || lost[1] == 0),
                       "and milk at home goes in whole, with nothing lost");
  }
  return failures;
}

/// THE HERDS STAY BELOW THE PLAN RESERVE (resources design §6; boss,
/// 2026-09-13): grain this year's reaping has set aside for the district is
/// not fodder, and a herd that finds only that grain goes hungry.
int CheckTheHerdDoesNotEatThePlan() {
  int failures = 0;
  const core::ProductionConfig config = MakeHerdConfig();
  constexpr core::Grams kKilo = core::kGramsPerKilogram;
  {
    core::WorldState world = MakeHerdWorld(100.0F);
    world.plan.due.assign(1, 98 * kKilo);
    world.ledger.current.harvest.assign(1, 98 * kKilo);
    AddHerd(world, 0, 4, 2, true);
    core::RunHerdDay(config, world);
    failures += Expect(StoreOf(world, 0) == 98 * kKilo,
                       "a herd eats only the two kilograms above the plan reserve");
    failures += Expect(world.herds.rows[0].unfed_days == 1.0F,
                       "and goes hungry rather than into the district's grain");
  }
  {
    core::WorldState world = MakeHerdWorld(100.0F);
    world.plan.due.assign(1, 98 * kKilo);
    world.ledger.current.harvest.assign(1, 0);  // nothing reaped: nothing set aside yet
    AddHerd(world, 0, 4, 2, true);
    core::RunHerdDay(config, world);
    failures += Expect(StoreOf(world, 0) == 96 * kKilo,
                       "before the reaping the reserve holds nothing and the herd eats in full");
  }
  return failures;
}

/// The two checks the design asked for by name once the cap existed: a horse
/// takes half its ration in oats and the rest in hay, and a cow with no hay
/// cannot be fed at all however much grain is standing in the barn.
int CheckFeedCaps() {
  int failures = 0;
  constexpr core::Grams kKilo = core::kGramsPerKilogram;
  core::ProductionConfig config = MakeHerdConfig();
  // Resource 0 keeps its feed value of 1; add hay as resource 1 with the
  // same value so the arithmetic stays exact and the shares are readable.
  config.feed_values = {1.0F, 1.0F, 0.0F};
  config.milk_resource = core::ResourceId{};  // no produce in the way
  config.feed_links = {core::FeedLinkDef{.kind = core::LivestockKindId{0},
                                         .resource = core::ResourceId{0},
                                         .reserve = 0,
                                         .max_share = 0.5F},
                       core::FeedLinkDef{.kind = core::LivestockKindId{0},
                                         .resource = core::ResourceId{1},
                                         .reserve = 0,
                                         .max_share = 1.0F}};

  // Four heads need four units; the oats may cover only two of them.
  {
    core::WorldState world = MakeHerdWorld(100.0F);
    world.units.rows[0].stock[1] = 100 * kKilo;
    AddHerd(world, 0, 4, 2, true);
    core::RunHerdDay(config, world);
    failures += Expect(StoreOf(world, 0) == 98 * kKilo,
                       "the capped feed covers only its half of the ration");
    failures += Expect(StoreOf(world, 1) == 98 * kKilo, "and the uncapped feed covers the rest");
    failures += Expect(world.herds.rows[0].unfed_days == 0.0F, "so the herd is fed");
  }

  // A full granary and an empty hayloft: the herd goes hungry anyway. That
  // is a winter without hay, and it is meant to hurt.
  {
    core::WorldState world = MakeHerdWorld(1000.0F);
    AddHerd(world, 0, 4, 2, true);
    core::RunHerdDay(config, world);
    failures +=
        Expect(StoreOf(world, 0) == 998 * kKilo, "the capped feed still gives only its half");
    failures += Expect(world.herds.rows[0].unfed_days == 1.0F,
                       "and no amount of it makes up for the missing half");
  }
  return failures;
}

int CheckBilletingAndProduce() {
  int failures = 0;
  const core::ProductionConfig config = MakeHerdConfig();
  constexpr core::Grams kKilo = core::kGramsPerKilogram;

  // Ten heads fit the barn; the milk is the full two litres a female a day.
  {
    core::WorldState world = MakeHerdWorld(1000.0F);
    AddHerd(world, 0, 10, 5, true);
    core::RunHerdDay(config, world);
    failures += Expect(world.herds.rows[0].billeted_count == 0, "a herd that fits is not billeted");
    failures += Expect(StoreOf(world, 1) == 50 * kKilo, "five females give ten litres each");
  }

  // Twenty heads in a barn for ten: half of them stand at the yards — and
  // NOT ONE of them is slaughtered, which is the whole point of the rule.
  {
    core::WorldState world = MakeHerdWorld(1000.0F);
    AddHerd(world, 0, 20, 10, true);
    core::RunHerdDay(config, world);
    const core::HerdRow& herd = world.herds.rows[0];
    failures += Expect(herd.billeted_count == 10, "a head with no room is billeted, not culled");
    failures += Expect(herd.adult_count == 20, "and the herd keeps every head it had");
    failures += Expect(StoreOf(world, 2) == 0, "billeting produces no meat at all");
    // Ten females at 10 l, with half the herd billeted at 0.6 of yield:
    // 100 x (1 - 0.5 x 0.4) = 80.
    failures += Expect(StoreOf(world, 1) == 80 * kKilo, "billeting is paid for in leakage");
    failures += Expect(herd.birth_progress == 0.0F, "and a full roof stops the offspring");
  }

  // A kolkhoz herd with no roof at all — the sixteen start horses — is
  // billeted whole and still eats from the farm's store.
  {
    core::WorldState world = MakeHerdWorld(100.0F);
    AddHerd(world, 0, 2, 1, false);
    core::RunHerdDay(config, world);
    failures += Expect(world.herds.rows[0].billeted_count == 2,
                       "a kolkhoz herd with no unit stands wholly at the yards");
    failures += Expect(StoreOf(world, 0) == 98 * kKilo, "and the farm still feeds it");
  }
  return failures;
}

int CheckCohortFlows() {
  int failures = 0;
  const core::ProductionConfig config = MakeHerdConfig();

  // Maturation: four newborns on a four-day rung move one a day.
  {
    core::WorldState world = MakeHerdWorld(1000.0F);
    const core::HerdId id = AddHerd(world, 0, 2, 1, true);
    world.herds.rows[FindRow(world.herds, id)].newborn_count = 4;
    world.herds.rows[FindRow(world.herds, id)].birth_progress = 0.0F;
    core::RunHerdDay(config, world);
    const core::HerdRow& herd = world.herds.rows[0];
    failures += Expect(herd.newborn_count == 3 && herd.juvenile_count == 1,
                       "the newborn rung moves one head a day, not zero and not four");
  }

  // Birth: one female, one litter a year, a band of the whole year — the
  // fractional stream carries until a whole head is due. Two years, because
  // a rate of exactly 1/48 summed 48 times lands a hair under one and the
  // calf arrives on the first day of the next year; that is float addition,
  // not a modelling choice, and it costs the yearly total nothing.
  {
    core::WorldState world = MakeHerdWorld(1000.0F);
    AddHerd(world, 0, 2, 1, true);
    std::uint32_t calves = 0;
    std::uint16_t seen_newborns = 0;
    for (std::uint32_t day = 0; day <= 2U * core::kDaysPerYear; ++day) {
      world.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay;
      core::RefreshCalendarCaches(world.calendar);
      core::RunHerdDay(config, world);
      const std::uint16_t newborns = world.herds.rows[0].newborn_count;
      if (newborns > seen_newborns) {
        calves += newborns - seen_newborns;
      }
      seen_newborns = newborns;
    }
    failures += Expect(calves >= 2, "one female at one litter a year yields a calf a year");
  }

  // Old age: a herd past the top of its band dies out rather than standing
  // there forever.
  {
    core::ProductionConfig old_age = MakeHerdConfig();
    old_age.livestock[0].life_game_years_min = 1.0F;
    old_age.livestock[0].life_game_years_max = 2.0F;
    core::WorldState world = MakeHerdWorld(100000.0F);
    world.rng = core::SeedRngState(7, 0);
    const core::HerdId id = AddHerd(world, 0, 40, 20, true);
    world.herds.rows[FindRow(world.herds, id)].adult_age_game_years_total = 40.0F * 3.0F;
    for (std::uint32_t day = 0; day < core::kDaysPerYear; ++day) {
      world.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay;
      core::RefreshCalendarCaches(world.calendar);
      core::RunHerdDay(old_age, world);
    }
    failures += Expect(world.herds.rows[0].adult_count < 40,
                       "a herd above the top of its lifespan band loses heads to age");
  }
  return failures;
}

int CheckAutumnPigs() {
  int failures = 0;
  const core::ProductionConfig config = MakeHerdConfig();
  core::WorldState world = MakeHerdWorld(1000.0F);
  const core::HerdId id = AddHerd(world, 1, 8, 1, true);
  world.herds.rows[FindRow(world.herds, id)].juvenile_count = 6;
  // 1 October, the slaughter day.
  world.calendar.tick = static_cast<core::Tick>(9U * core::kDaysPerMonth) * core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);
  core::RunHerdDay(config, world);
  const core::HerdRow& herd = world.herds.rows[0];
  failures += Expect(herd.juvenile_count == 0, "the autumn takes the whole fattening stock");
  failures += Expect(herd.adult_count == 5, "and leaves the sows and the boar");
  failures += Expect(StoreOf(world, 2) > 0, "the slaughter pays out in meat");
  return failures;
}

// --- stage 7, task O2b: what the reconciliation's fixes changed -----------

/// The yard's own birds and pig eat nothing from any store (question Q1),
/// and the kolkhoz herd of the same kind still eats by the norm.
int CheckSelfFedYard() {
  int failures = 0;
  core::ProductionConfig config = MakeHerdConfig();
  config.livestock[0].household_self_fed = 1;
  config.milk_resource = core::ResourceId{};  // no produce in the way

  {
    core::WorldState world = MakeHerdWorld(100.0F);
    const core::HerdId id = AddHerd(world, 0, 4, 2, false);
    core::HerdRow& herd = world.herds.rows[FindRow(world.herds, id)];
    herd.household_owned = 1;
    core::RunHerdDay(config, world);
    failures += Expect(StoreOf(world, 0) == 100 * core::kGramsPerKilogram,
                       "a self-fed yard herd takes nothing off the store");
    failures += Expect(world.herds.rows[0].unfed_days == 0.0F,
                       "and it is fed, not starving: range and scraps are its ration");
  }
  {
    core::WorldState world = MakeHerdWorld(100.0F);
    AddHerd(world, 0, 4, 2, true);
    core::RunHerdDay(config, world);
    failures += Expect(StoreOf(world, 0) == 96 * core::kGramsPerKilogram,
                       "the same kind in a kolkhoz unit eats by the norm — a farm is not a yard");
  }
  return failures;
}

/// A work-only feed is the WAGE of a working day (question Q2): none of it
/// on a day the team stands, all of its share on a day the team is out.
int CheckWorkOnlyFeed() {
  int failures = 0;
  constexpr core::Grams kKilo = core::kGramsPerKilogram;
  core::ProductionConfig config = MakeHerdConfig();
  config.feed_values = {1.0F, 1.0F, 0.0F};
  config.milk_resource = core::ResourceId{};
  config.horse_kind = core::LivestockKindId{0};
  // Oats (resource 1) are the wage and may cover half the need; hay
  // (resource 0) carries the rest and is never work-only.
  config.feed_links = {
      core::FeedLinkDef{.kind = core::LivestockKindId{0},
                        .resource = core::ResourceId{1},
                        .max_share = 0.5F,
                        .work_only = 1},
      core::FeedLinkDef{
          .kind = core::LivestockKindId{0}, .resource = core::ResourceId{0}, .max_share = 1.0F}};

  const auto run_with_workers = [&](std::uint32_t workers) {
    core::WorldState world = MakeHerdWorld(100.0F);
    world.units.rows[0].stock[1] = 100 * kKilo;
    AddHerd(world, 0, 4, 2, true);
    for (std::uint32_t worker = 0; worker < workers; ++worker) {
      core::ResidentRow hand;
      hand.work.kind = core::WorkKind::kPlowing;
      hand.work.worked_norm_days_today = 1.0F;
      AppendRow(world.residents, hand);
    }
    core::RunHerdDay(config, world);
    return world;
  };

  {
    const core::WorldState idle = run_with_workers(0);
    failures +=
        Expect(idle.units.rows[0].stock[1] == 100 * kKilo, "a standing team gets no oats at all");
    failures += Expect(idle.units.rows[0].stock[0] == 96 * kKilo,
                       "hay carries the whole ration on a day of rest");
    failures += Expect(idle.herds.rows[0].unfed_days == 0.0F, "and the team is fed all the same");
  }
  {
    const core::WorldState working = run_with_workers(4);
    failures += Expect(working.units.rows[0].stock[1] == 98 * kKilo,
                       "a working team takes its half in oats");
    failures += Expect(working.units.rows[0].stock[0] == 98 * kKilo, "and the other half in hay");
  }
  {
    // Two hands out of four heads: the wage ration is spread, because a work
    // order names a field and a worker, never an animal.
    const core::WorldState half = run_with_workers(2);
    failures +=
        Expect(half.units.rows[0].stock[1] == 99 * kKilo, "half the team out means half the oats");
  }
  return failures;
}

/// The manger of the settlement is reachable from any barn: hay is delivered
/// to the ONE stock yard, and a herd standing elsewhere must still reach it.
int CheckMangerReach() {
  int failures = 0;
  core::ProductionConfig config = MakeHerdConfig();
  config.milk_resource = core::ResourceId{};
  config.unit_types.resize(2);
  SetStorageKg(config.unit_types[1], 0.0F);  // a barn, not a store
  SetLivestockHead(config.unit_types[1], 10.0F);

  core::WorldState world;
  core::RefreshCalendarCaches(world.calendar);
  // Row 0 is the stock yard where the cut lands; row 1 is another barn with
  // an empty manger, and neither is a "storing" unit.
  core::UnitRow stock_yard;
  stock_yard.type = core::UnitTypeId{1};
  stock_yard.stock.assign(3, 0);
  stock_yard.stock[0] = 100 * core::kGramsPerKilogram;
  AppendRow(world.units, stock_yard);
  core::UnitRow other_barn;
  other_barn.type = core::UnitTypeId{1};
  other_barn.stock.assign(3, 0);
  const core::UnitId barn = AppendRow(world.units, other_barn);

  core::HerdRow herd;
  herd.kind = core::LivestockKindId{0};
  herd.adult_count = 4;
  herd.adult_male_count = 2;
  herd.unit = barn;
  AppendRow(world.herds, herd);
  core::RunHerdDay(config, world);
  failures += Expect(world.units.rows[0].stock[0] == 96 * core::kGramsPerKilogram,
                     "a herd at an empty barn eats from the settlement's manger");
  failures += Expect(world.herds.rows[0].unfed_days == 0.0F,
                     "and does not starve two hundred metres from the hay");
  return failures;
}

/// No foals without a roof: the stable is the SECOND step of the yard, and
/// until it stands the team only ages (livestock design §5).
int CheckStableGate() {
  int failures = 0;
  core::ProductionConfig config = MakeHerdConfig();
  config.horse_kind = core::LivestockKindId{0};
  config.stable_type = core::UnitTypeId{0};

  const auto foals_at_level = [&](std::uint8_t level) {
    core::WorldState world = MakeHerdWorld(1000.0F);
    world.units.rows[0].level = level;
    AddHerd(world, 0, 6, 2, true);
    for (std::uint32_t day = 0; day < 20; ++day) {
      world.calendar.tick += core::kTicksPerDay;
      core::RefreshCalendarCaches(world.calendar);
      core::RunHerdDay(config, world);
    }
    const core::HerdRow& herd = world.herds.rows[0];
    // THE WHOLE HERD AND NOT THE YOUNG RUNGS, since 2026-09-16. This counted
    // `newborn + juvenile`, which is not "a foal was born" but "a foal is
    // still young TODAY" — and this fixture's kind grows up in eight days
    // inside a twenty-day run. The two readings parted the moment the herd
    // bred faster: the carried sire count left four females where the old
    // daily re-derive left three, the foal arrived earlier, and by day twenty
    // it was an ADULT. The check then read zero young and called it no
    // offspring, with the birth sitting in plain sight in adult_count.
    //
    // A measure that fails when the thing it measures happens SOONER is
    // measuring the calendar, not the gate.
    return static_cast<std::uint32_t>(herd.newborn_count + herd.juvenile_count + herd.adult_count);
  };
  // Six adults go in; the herd is six heads still if nothing was born.
  failures += Expect(foals_at_level(1) == 6, "a summer yard brings no foals");
  failures += Expect(foals_at_level(2) > 6, "a stable does");
  return failures;
}

/// THE NIGHT PASTURE'S THREE CONDITIONS, and the discount behind them.
///
/// The team's summer feed discount had been applied to every pasture month
/// unconditionally since day zero — no yard, no chairman's order, no children
/// — and for the horses that discount IS the night pasture and nothing else
/// (livestock design, «Ночное»). Measured over fifteen years before the gate
/// went in: the free gain was worth 6.443 t of oats and 115.869 t of hay.
/// HANDING A HEAD BACK TO THE DISTRICT (district_limit.h, OrderHandStock).
/// The verb that makes bought stock reversible: until it existed the village
/// could buy a head on the limit and had no way at all to be rid of one,
/// which is the dead end the design's «никаких безвыходных ситуаций» forbids.
/// ELECTRIFICATION (district_limit.h, RunEraEvents): the first era event the
/// core raises, and the root four others hang on.
int CheckElectrification() {
  int failures = 0;
  core::ProductionConfig config = MakeHerdConfig();
  config.limit.electrification_points_min = 900;
  config.farm_office_type = core::UnitTypeId{0};

  /// A world with the three blockers set as asked.
  const auto make = [&](std::int32_t granted, std::uint32_t day, bool office) {
    core::WorldState world;
    world.limit.points_granted_total = granted;
    world.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay;
    core::RefreshCalendarCaches(world.calendar);
    if (office) {
      core::UnitRow built;
      built.type = core::UnitTypeId{0};
      built.level = 1;
      AppendRow(world.units, built);
    }
    return world;
  };
  const auto fired = [](const core::WorldState& world) {
    for (const core::SimEvent& event : world.step_events) {
      if (event.kind == core::EventKind::kElectrificationUnlocked) {
        return true;
      }
    }
    return false;
  };
  constexpr std::uint32_t kAYear = core::kDaysPerYear;

  // -- each blocker alone holds it back, and each is asked by name ----------
  {
    core::WorldState world = make(899, kAYear, true);
    core::RunEraEvents(config, world);
    failures += Expect(!fired(world) && world.era_events.electrification_unlocked == 0,
                       "electrification: a point short of the threshold and it does not come");
  }
  {
    core::WorldState world = make(5000, kAYear - 1, true);
    core::RunEraEvents(config, world);
    failures += Expect(!fired(world) && world.era_events.electrification_unlocked == 0,
                       "electrification: a day short of the year and it does not come, however "
                       "rich the farm");
  }
  {
    core::WorldState world = make(5000, kAYear, false);
    core::RunEraEvents(config, world);
    failures += Expect(!fired(world) && world.era_events.electrification_unlocked == 0,
                       "electrification: with no office standing there is no address to write to");
  }
  // A SITE IS NOT AN OFFICE. Level nought is a marked plot, not a building,
  // and the district writes to buildings.
  {
    core::WorldState world = make(5000, kAYear, true);
    world.units.rows[0].level = 0;
    core::RunEraEvents(config, world);
    failures += Expect(!fired(world), "electrification: a marked site is not a standing office");
  }

  // -- all three, and it comes once ----------------------------------------
  {
    core::WorldState world = make(900, kAYear, true);
    core::RunEraEvents(config, world);
    failures += Expect(fired(world) && world.era_events.electrification_unlocked == 1,
                       "electrification: the threshold exactly, a year lived and an office — it "
                       "comes");
    world.step_events.clear();
    core::RunEraEvents(config, world);
    failures += Expect(!fired(world), "and it is said ONCE: the next day raises nothing");
  }
  return failures;
}

int CheckHandingStockBack() {
  int failures = 0;
  core::ProductionConfig config = MakeHerdConfig();
  config.livestock[0].life_game_years_min = 8.0F;  // an adult is "old" past eight
  config.livestock[0].life_game_years_max = 10.0F;
  core::LimitCatalog& limit = config.limit;
  limit.lots.resize(2);
  // Row 0: the district sells one head of kind 0 for 100 points. Row 1: kind
  // 1 only as a batch, which is what the shipped piglet and chick lots are.
  limit.lots[0].points = 100;
  limit.lots[0].kind = core::LimitLotKind::kLivestock;
  limit.lots[0].livestock = core::LivestockKindId{0};
  limit.lots[0].head_count = 1;
  limit.lots[1].points = 40;
  limit.lots[1].kind = core::LimitLotKind::kLivestock;
  limit.lots[1].livestock = core::LivestockKindId{1};
  limit.lots[1].head_count = 8;
  limit.handover_share_newborn = 0.15F;
  limit.handover_share_young = 0.35F;
  limit.handover_share_adult = 0.55F;
  limit.handover_share_old = 0.25F;

  const auto hand = [&config](core::WorldState& world, core::HerdId herd, std::int64_t heads) {
    core::OrderRow order;
    order.kind = core::OrderKind::kHandStock;
    order.herd = herd;
    order.amount = heads;
    return core::OrderHandStock(config, world, order);
  };

  // -- the three refusals, each by its own name ------------------------------
  {
    core::WorldState world = MakeHerdWorld(1000.0F);
    const core::HerdId absent{4242};
    failures += Expect(hand(world, absent, 1) == core::OrderRefusal::kNoSuchSubject,
                       "hand stock: a herd that is not there is no subject");

    core::HerdRow yard;
    yard.kind = core::LivestockKindId{0};
    yard.adult_count = 4;
    yard.household_owned = 1;
    const core::HerdId theirs = AppendRow(world.herds, yard);
    failures += Expect(hand(world, theirs, 1) == core::OrderRefusal::kNotEligible,
                       "hand stock: a family's own animal is not the chairman's to sell");

    const core::HerdId batch = AddHerd(world, 1, 6, 2, true);
    failures += Expect(hand(world, batch, 1) == core::OrderRefusal::kNotEligible,
                       "hand stock: a kind the district takes only by the batch has no per-head "
                       "price");
  }

  // -- a herd is not left with adults and no sire ---------------------------
  //
  // THE CASE THAT MATTERS IS THE ONE THE FIRST CUT MISSED, and it is first
  // here for that reason. The sires go in PROPORTION when adults leave, so a
  // partial take can round the only sire away while animals are still
  // standing: three adults with one sire, asked for two, kept one adult and
  // no sire and raised no refusal. The first guard tested how many heads were
  // ASKED FOR; the rounding happens on what is LEFT.
  {
    core::WorldState world = MakeHerdWorld(1000.0F);
    const core::HerdId herd = AddHerd(world, 0, 3, 1, true);
    failures += Expect(hand(world, herd, 2) == core::OrderRefusal::kLastSire,
                       "hand stock: a partial take that would round the last sire away is "
                       "refused");
    failures += Expect(world.herds.rows[0].adult_count == 3 &&
                           world.herds.rows[0].adult_male_count == 1 && world.limit.points == 0,
                       "and a refusal costs the village nothing");
  }
  // A take that leaves a sire standing goes through: the refusal is narrow
  // enough to be an exit and not a second cage.
  {
    core::WorldState world = MakeHerdWorld(1000.0F);
    const core::HerdId herd = AddHerd(world, 0, 3, 2, true);
    failures += Expect(hand(world, herd, 2) == core::OrderRefusal::kNone,
                       "hand stock: a take that leaves a sire behind is allowed");
    failures +=
        Expect(world.herds.rows[0].adult_count == 1 && world.herds.rows[0].adult_male_count == 1,
               "and the herd it leaves can still breed");
  }
  // EMPTYING THE HERD OF ADULTS IS ALLOWED, and that is a decision and not a
  // trap: a farm with no herd of a kind has made a visible choice and the
  // district sells that kind by the head. The invisible one — mares and no
  // stallion — is what the refusal above is for.
  {
    core::WorldState world = MakeHerdWorld(1000.0F);
    const core::HerdId herd = AddHerd(world, 0, 3, 1, true);
    failures += Expect(
        hand(world, herd, 3) == core::OrderRefusal::kNone && world.herds.rows[0].adult_count == 0,
        "hand stock: handing over every adult is a decision, not a dead end");
  }
  // A NEGATIVE AMOUNT IS REFUSED, AND FED STRAIGHT TO THE RULE. The boundary
  // rejects one, but the boundary is not the only door: a row replayed out of
  // a save never passes it, and the save codec reads this field unchecked.
  // Before the guard, -1 clamped to 65535 and emptied the herd with the
  // points paid.
  {
    core::WorldState world = MakeHerdWorld(1000.0F);
    const core::HerdId herd = AddHerd(world, 0, 4, 2, true);
    world.herds.rows[0].newborn_count = 3;
    failures += Expect(hand(world, herd, -1) == core::OrderRefusal::kNoSuchSubject,
                       "hand stock: a negative head count is refused, not wrapped");
    failures += Expect(world.herds.rows[0].adult_count == 4 &&
                           world.herds.rows[0].newborn_count == 3 && world.limit.points == 0,
                       "and the herd it names is untouched and nothing is paid");
  }

  // -- the oldest go first, and the price follows the band -------------------
  {
    core::WorldState world = MakeHerdWorld(1000.0F);
    const core::HerdId herd = AddHerd(world, 0, 2, 0, true);
    world.herds.rows[0].juvenile_count = 3;
    world.herds.rows[0].newborn_count = 4;
    // Young adults: a mean age under life_game_years_min, so the adult band.
    world.herds.rows[0].adult_age_game_years_total = 2.0F * 3.0F;
    failures +=
        Expect(hand(world, herd, 1) == core::OrderRefusal::kNone, "hand stock: one head goes");
    failures +=
        Expect(world.herds.rows[0].adult_count == 1 && world.herds.rows[0].juvenile_count == 3 &&
                   world.herds.rows[0].newborn_count == 4,
               "and it is an ADULT: the oldest cohort empties first");
    failures += Expect(world.limit.points == 55,
                       "and it fetched the adult share of the buying price, 55 of 100");

    // Four more: the last adult, then all three juveniles. 55 + 3*35 = 160.
    failures += Expect(hand(world, herd, 4) == core::OrderRefusal::kNone,
                       "hand stock: more heads than one cohort holds walk down the ladder");
    failures +=
        Expect(world.herds.rows[0].adult_count == 0 && world.herds.rows[0].juvenile_count == 0 &&
                   world.herds.rows[0].newborn_count == 4,
               "and they come off adults first, then juveniles, and the newborns stay");
    failures += Expect(world.limit.points == 55 + 55 + (3 * 35),
                       "and each cohort was paid at its own band");

    // Asking for more than the herd holds takes what there is and no more.
    //
    // THE LAST BAND IS CHECKED BY ITS DELTA AND NOT BY THE RUNNING TOTAL,
    // and a damage run is why. Written against the total, this assertion was
    // GREEN under an implementation that took the newborns FIRST: once the
    // whole herd has gone, the sum over every head is the same whatever
    // order they went in, so the total cannot see an order at all. It was an
    // assertion true under both implementations, which is decoration and not
    // a check (predicted five reddened, got four; the fourth was this one).
    const std::int32_t before_last = world.limit.points;
    failures += Expect(hand(world, herd, 99) == core::OrderRefusal::kNone &&
                           world.herds.rows[0].newborn_count == 0,
                       "hand stock: asking for more than the herd holds empties it");
    failures += Expect(world.limit.points - before_last == 4 * 15,
                       "and what was left — four newborns — fetched the newborn band and no "
                       "other");
  }

  // -- an old herd is paid the old band, which is BELOW the adult one --------
  {
    core::WorldState world = MakeHerdWorld(1000.0F);
    const core::HerdId herd = AddHerd(world, 0, 2, 0, true);
    world.herds.rows[0].adult_age_game_years_total = 9.0F * 2.0F;  // past eight
    failures +=
        Expect(hand(world, herd, 1) == core::OrderRefusal::kNone && world.limit.points == 25,
               "hand stock: a herd past its lifespan floor fetches the old band, 25");
  }

  // -- THE RULE THE WHOLE SCALE EXISTS FOR ----------------------------------
  // Every band pays LESS than the district charges. Without it the order is
  // not an exit from a dead end but a mint: buy at 100, hand back at 100 or
  // more, repeat. The parse refuses a share at or above one, and this is the
  // same claim asked of the numbers rather than of the table.
  failures += Expect(limit.handover_share_newborn < 1.0F && limit.handover_share_young < 1.0F &&
                         limit.handover_share_adult < 1.0F && limit.handover_share_old < 1.0F,
                     "hand stock: every band pays less than the district charges");
  return failures;
}

int CheckNightPasture() {
  int failures = 0;
  core::ProductionConfig config = MakeHerdConfig();
  config.horse_kind = core::LivestockKindId{0};
  config.farming.pasture_from_month = 4;       // May
  config.farming.pasture_to_month = 8;         // September
  config.farming.school_year_start_month = 8;  // September
  config.farming.school_year_end_month = 5;    // June: school runs to the end of May
  config.livestock[0].pasture_coverage_summer = 0.5F;

  /// A world in `month` with a team, a teenager, and the yard as asked.
  const auto make = [&](std::uint8_t month, bool gathered, bool teenager) {
    core::WorldState world = MakeHerdWorld(1000.0F);
    world.calendar.tick = static_cast<core::Tick>(month) * core::kDaysPerMonth * core::kTicksPerDay;
    core::RefreshCalendarCaches(world.calendar);
    world.chairman.horses_stabled = gathered ? 1U : 0U;
    AddHerd(world, 0, 6, 1, true);
    core::FieldRow meadow;
    meadow.kind = core::LandKind::kFloodplainMeadow;
    meadow.center = core::Vec2{.x = 100.0F, .y = 200.0F};
    AppendRow(world.fields, meadow);
    if (teenager) {
      core::ResidentRow child;
      // Thirteen biological years at the fixture's speedup: inside the senior
      // school band and below the working age.
      const auto lived = static_cast<std::int32_t>(13.0F / config.farming.life_speedup *
                                                   static_cast<float>(core::kDaysPerYear));
      child.birth_day = static_cast<std::int32_t>(world.calendar.day) - lived;
      AppendRow(world.residents, child);
    }
    return world;
  };
  const auto order = [&](core::WorldState& world) {
    return core::OrderNightPasture(config, world);
  };

  core::WorldState winter = make(0, true, true);  // January
  failures += Expect(order(winter) == core::OrderRefusal::kRuleForbids,
                     "night pasture: school is in, so there is nobody to keep the team");
  core::WorldState scattered = make(6, false, true);  // July, horses still at the yards
  failures +=
      Expect(order(scattered) == core::OrderRefusal::kRuleForbids,
             "night pasture: «пока лошади стоят по личным дворам, уводить некого и некому»");
  core::WorldState childless = make(6, true, false);
  failures += Expect(order(childless) == core::OrderRefusal::kRuleForbids,
                     "night pasture: no children of the age, and it is they who keep it");

  core::WorldState ready = make(6, true, true);  // July, gathered, a teenager
  failures +=
      Expect(order(ready) == core::OrderRefusal::kNone && ready.chairman.night_pasture_ordered == 1,
             "night pasture: in the holidays, with the team gathered and children to keep "
             "it, the order stands");
  failures += Expect(ready.chairman.night_pasture_place.x == 100.0F &&
                         ready.chairman.night_pasture_place.y == 200.0F,
                     "and the camp is on a floodplain meadow, chosen by the core");
  failures += Expect(core::TeamOutTonight(config, ready), "and the team is out tonight");

  // THE DISCOUNT FOLLOWS THE ORDER AND NOT THE SEASON. June is a pasture
  // month either way; what decides is whether the team is actually out.
  core::WorldState idle = make(6, true, true);  // no order given
  failures += Expect(!core::TeamOutTonight(config, idle),
                     "without the order the team stays in, though the month is the same");
  const core::HerdRow& herd = ready.herds.rows[0];
  const float out = core::FeedNeedUnits(config, config.livestock[0], herd, 6, true);
  const float in = core::FeedNeedUnits(config, config.livestock[0], herd, 6, false);
  failures += Expect(out < in && out > 0.0F,
                     "a night at grass halves the day's fodder, and a night in the yard does not");

  // AND THE FIRST NIGHT IS SAID ONCE.
  core::RunNightPasture(config, ready);
  std::uint32_t said = 0;
  for (const core::SimEvent& event : ready.step_events) {
    said += event.kind == core::EventKind::kNightPastureBegan ? 1U : 0U;
  }
  ready.step_events.clear();
  core::RunNightPasture(config, ready);
  for (const core::SimEvent& event : ready.step_events) {
    said += event.kind == core::EventKind::kNightPastureBegan ? 1U : 0U;
  }
  failures += Expect(said == 1, "the first night is news once, and every night after it is not");
  return failures;
}

/// The age hazard is read over the ages the herd HOLDS, not at its mean.
int CheckAgeSpread() {
  int failures = 0;
  core::ProductionConfig config = MakeHerdConfig();
  config.milk_resource = core::ResourceId{};
  config.livestock[0].life_game_years_min = 3.0F;
  config.livestock[0].life_game_years_max = 4.0F;
  config.livestock[0].births_per_game_year = 0.0F;  // ageing alone, no calves

  const auto survivors_after = [&](float mean_age, std::uint32_t years) {
    core::WorldState world = MakeHerdWorld(100000.0F);
    const core::HerdId id = AddHerd(world, 0, 40, 2, true);
    core::HerdRow& herd = world.herds.rows[FindRow(world.herds, id)];
    herd.adult_age_game_years_total = 40.0F * mean_age;
    for (std::uint32_t day = 0; day < years * core::kDaysPerYear; ++day) {
      world.calendar.tick += core::kTicksPerDay;
      core::RefreshCalendarCaches(world.calendar);
      core::RunHerdDay(config, world);
    }
    return world.herds.rows.empty() ? 0U
                                    : static_cast<std::uint32_t>(world.herds.rows[0].adult_count);
  };

  // A herd whose MEAN is below the band still loses heads, because part of
  // it is past its years. At the mean alone this was exactly zero.
  failures += Expect(survivors_after(1.5F, 1) < 40 && survivors_after(1.5F, 1) > 30,
                     "a young herd buries a few of its eldest, not none and not many");
  // And a herd inside the band does not lose half of itself in a year.
  failures +=
      Expect(survivors_after(3.5F, 1) > 20, "an old herd thins, it does not collapse in one year");
  // The spiral: with the dead removed at the mean, the mean never fell and
  // any herd that could not breed emptied itself. It must not.
  failures +=
      Expect(survivors_after(3.5F, 4) > 0, "and four years on there is still a herd to speak of");
  return failures;
}

/// The drought branch, which was DEAD in every run before the diurnal swing:
/// heat is read on the afternoon (mean + the season's amplitude), and the
/// summer mean tops out at 24 against a threshold of 25. Built over a real
/// table set, because the field cycle lives behind the factory.
/// A BROKEN plan_positions MUST NOT KILL THE TABLES, and it must say what it
/// dropped. campaign.csv carries the district's positions as `crop=share`
/// pairs, and every way a hand can spoil that line — a share that is not a
/// number, a share outside 0..100, a crop the tables do not carry, the same
/// crop twice — is a warning and a dropped entry, never a refusal: a village
/// must still load when the district's line is mistyped.
///
/// UNTESTED UNTIL 2026-09-16, and coverage found it: production_config.cpp
/// was the largest block of never-executed lines in the core (114), almost
/// all of it this parser's complaints (boss, standstill parcels 11, 14). The
/// log is the witness, because the complaint IS the behaviour — a silent drop
/// would look the same from outside and mean something else.
int CheckABrokenPlanPositionsLineIsNamedNotFatal() {
  int failures = 0;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_production_positions";
  std::filesystem::create_directories(root);
  std::ofstream(root / "resources.csv") << "key,feed_value\nrye,1.15\npotato,0.8\n";
  std::ofstream(root / "crops.csv")
      << "key,resource,is_winter,is_perennial,sow_from_month,sow_to_month,sow_min_temp_c,"
         "growth_min_temp_c,harvest_from_month,harvest_to_month,harvest_min_temp_c,"
         "yield_kg_per_ha,sowing_norm_kg_per_ha,fertility_delta,drought_sensitivity,"
         "wet_sensitivity,sow_days_per_ha,harvest_days_per_ha,straw_ratio\n"
         "rye,rye,0,0,4,5,5,5,8,8,2,850,180,-1,1,1,3,8,0\n"
         "potato,potato,0,0,4,5,5,5,8,8,2,900,200,-1,1,1,3,8,0\n";
  std::ofstream(root / "farming.csv")
      << "key,value\nfertility_neutral,50\nmanure_norm_kg_per_ha,20000\n"
         "manure_fertility_bonus,10\nfallow_recovery,6\nrepeat_penalty_per_year,3\n"
         "drought_temp_c,25\nstress_per_day,0.02\nstress_cap,0.3\nweather_state_days,5\n";
  std::ofstream(root / "weather.csv")
      << "key,temp_mean_c,temp_spread_c,temp_amplitude_c,precipitation_chance_percent\n"
         "winter,-10,2,3,35\nspring,5,7,5,35\nsummer,19,5,6,25\nautumn,6,7,5,45\n";
  // Four spoiled entries and one good one: not a number, out of the band,
  // a crop the tables do not carry, and rye said twice.
  std::ofstream(root / "campaign.csv")
      << "key,value\nplan_positions,rye=6.7 potato=lots rye=200 barley=5 rye=1.5\n";

  const std::filesystem::path log = root / "parse.log";
  const bool logging = core::InitLogFile(log.string());
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto system = tables == nullptr
                          ? nullptr
                          : core::CreateProductionSystem(*tables, core::StubTables::kAllowed);
  core::ShutdownLogFile();
  failures += Expect(system != nullptr,
                     "a mistyped plan_positions line still builds the production tables");
  if (!logging) {
    return failures + Expect(false, "the parse log opened");
  }
  std::ifstream reading(log);
  const std::string said((std::istreambuf_iterator<char>(reading)),
                         std::istreambuf_iterator<char>());
  failures += Expect(said.find("'lots' is not a number") != std::string::npos,
                     "and says which share is not a number");
  failures += Expect(said.find("outside 0..100") != std::string::npos,
                     "and which share is outside the band a share lives in");
  failures += Expect(said.find("no crop 'barley'") != std::string::npos,
                     "and which crop the tables do not carry");
  failures += Expect(said.find("named twice") != std::string::npos,
                     "and that the second share of a crop is ignored rather than added");
  return failures;
}

int CheckDroughtReadsTheAfternoon() {
  int failures = 0;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_production_drought";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "resources.csv") << "key,feed_value\nrye,1.15\n";
  std::ofstream(root / "crops.csv")
      << "key,resource,is_winter,is_perennial,sow_from_month,sow_to_month,sow_min_temp_c,"
         "growth_min_temp_c,harvest_from_month,harvest_to_month,harvest_min_temp_c,"
         "yield_kg_per_ha,sowing_norm_kg_per_ha,fertility_delta,drought_sensitivity,"
         "wet_sensitivity,sow_days_per_ha,harvest_days_per_ha,straw_ratio\n"
         // Sensitive to BOTH, so that the two sides can be damaged apart.
         "rye,rye,0,0,4,5,5,5,8,8,2,850,180,-1,1,1,3,8,0\n";
  std::ofstream(root / "farming.csv")
      << "key,value\nfertility_neutral,50\nmanure_norm_kg_per_ha,20000\n"
         "manure_fertility_bonus,10\nfallow_recovery,6\nrepeat_penalty_per_year,3\n"
         "drought_temp_c,25\nstress_per_day,0.02\nstress_cap,0.3\n"
         "weather_state_days,5\n";  // the unit fixture pins its own
  std::ofstream(root / "weather.csv")
      << "key,temp_mean_c,temp_spread_c,temp_amplitude_c,precipitation_chance_percent\n"
         "winter,-10,2,3,35\nspring,5,7,5,35\nsummer,19,5,6,25\nautumn,6,7,5,45\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto system = tables == nullptr
                          ? nullptr
                          : core::CreateProductionSystem(*tables, core::StubTables::kAllowed);
  if (Expect(system != nullptr, "the drought table set builds a production system") != 0) {
    std::cout << error << '\n';
    return 1;
  }

  // A run of `days` identical days on one field, returned whole: the two
  // accumulators, the two run counters and the judgement have to be read
  // SEPARATELY, which is the entire point of the split.
  const auto after_days = [&](float mean_celsius, core::Precipitation precipitation, int days) {
    core::WorldState world;
    world.calendar.tick = 30 * core::kTicksPerDay;  // late July: summer
    core::RefreshCalendarCaches(world.calendar);
    world.weather.air_temperature_celsius = mean_celsius;
    // The DAY's half-swing, which the time phase writes and this test has to
    // state for itself: production no longer keeps a copy of the season
    // amplitudes. Six is the summer figure of the fixture's weather table.
    world.weather.temperature_swing_celsius = 6.0F;
    world.weather.precipitation = precipitation;
    core::FieldRow field;
    field.area_ga = 10.0F;
    field.phase = core::FieldPhase::kGrowing;
    field.crop = core::CropId{0};
    core::AppendRow(world.fields, field);
    for (int day = 0; day < days; ++day) {
      const core::WorldState previous = world;
      system->ProductionPhase().RunItemRange(previous, world, 0, 1);
      world.calendar.tick += core::kTicksPerDay;
      core::RefreshCalendarCaches(world.calendar);
    }
    return world.fields.rows[0];
  };
  const auto drought_after_a_day = [&](float mean_celsius) {
    return after_days(mean_celsius, core::Precipitation::kNone, 1).drought_stress;
  };
  failures += Expect(drought_after_a_day(18.0F) == 0.0F,
                     "a summer day with a mean of 18 reads 24 in the afternoon: no drought");
  failures += Expect(drought_after_a_day(19.0F) > 0.0F,
                     "a mean of 19 reads 25 in the afternoon, and the field starts to burn");
  failures += Expect(drought_after_a_day(24.0F) == drought_after_a_day(19.0F),
                     "hotter is not more stress a day: the rate is the crop's, the gate is heat");

  // THE TWO SIDES MUST FALL APART, AND ONE DAMAGE MAY NOT DROP BOTH. While
  // they were one accumulator neither could be checked: "the field is
  // drying" and "the field is drowning" were the same reading.
  const core::FieldRow burnt = after_days(24.0F, core::Precipitation::kNone, 5);
  const core::FieldRow drowned = after_days(10.0F, core::Precipitation::kRain, 5);
  failures += Expect(burnt.drought_stress > 0.0F && burnt.wet_stress == 0.0F,
                     "five days of heat move the drought accumulator and only it");
  failures += Expect(drowned.wet_stress > 0.0F && drowned.drought_stress == 0.0F,
                     "five days of rain move the wet accumulator and only it");
  failures += Expect(burnt.weather_state == core::FieldWeatherState::kDrying,
                     "and the field says which way it suffers: drying");
  failures += Expect(drowned.weather_state == core::FieldWeatherState::kSoaking,
                     "and the other one says soaking");

  // TWO THRESHOLDS, AND THEY MOVE APART. They hold the same number today, so
  // nothing above can tell whether the code reads one row or two — which is
  // precisely the state the split was made to leave behind. A second table
  // set names them separately, and each half must then follow its own row.
  //
  // THE ROWS ARE READ, NOT MERELY DECLARED: this fixture is also the check
  // that a `drought_spell_days` added to farming.csv is not dropped in
  // silence by a parser that knows every other name in the file.
  {
    const std::filesystem::path two = root / "two_spells";
    std::filesystem::create_directories(two);
    for (const char* name : {"resources.csv", "crops.csv", "weather.csv"}) {
      std::filesystem::copy_file(
          root / name, two / name, std::filesystem::copy_options::overwrite_existing);
    }
    std::ofstream(two / "farming.csv")
        << "key,value\nfertility_neutral,50\nmanure_norm_kg_per_ha,20000\n"
           "manure_fertility_bonus,10\nfallow_recovery,6\nrepeat_penalty_per_year,3\n"
           "drought_temp_c,25\nstress_per_day,0.02\nstress_cap,0.3\n"
           "weather_state_days,5\ndrought_spell_days,5\nwet_spell_days,3\n";
    const auto split_tables = core::LoadTableSet(two.string(), nullptr);
    const auto split = split_tables == nullptr ? nullptr
                                               : core::CreateProductionSystem(
                                                     *split_tables, core::StubTables::kAllowed);
    failures += Expect(split != nullptr, "a farming table naming both spell lengths builds");
    if (split != nullptr) {
      const auto with = [&](float mean, core::Precipitation sky, int days) {
        core::WorldState world;
        world.calendar.tick = 30 * core::kTicksPerDay;
        core::RefreshCalendarCaches(world.calendar);
        world.weather.air_temperature_celsius = mean;
        world.weather.temperature_swing_celsius = 6.0F;
        world.weather.precipitation = sky;
        core::FieldRow field;
        field.area_ga = 10.0F;
        field.phase = core::FieldPhase::kGrowing;
        field.crop = core::CropId{0};
        core::AppendRow(world.fields, field);
        for (int day = 0; day < days; ++day) {
          const core::WorldState previous = world;
          split->ProductionPhase().RunItemRange(previous, world, 0, 1);
          world.calendar.tick += core::kTicksPerDay;
          core::RefreshCalendarCaches(world.calendar);
        }
        return world.fields.rows[0].weather_state;
      };
      failures +=
          Expect(with(24.0F, core::Precipitation::kNone, 4) == core::FieldWeatherState::kNone,
                 "four hot days are under a drought spell of five");
      failures +=
          Expect(with(24.0F, core::Precipitation::kNone, 5) == core::FieldWeatherState::kDrying,
                 "and five reach it");
      failures +=
          Expect(with(10.0F, core::Precipitation::kRain, 3) == core::FieldWeatherState::kSoaking,
                 "while three rain days already reach a wet spell of three — the two lengths come "
                 "from two rows and not from one");
    }
  }

  // A SPELL LENGTH OUT OF THE BAND IS REFUSED, and the check names the row it
  // is about. The two spell rows never pass through the required loop, so the
  // shared band did not reach them: for a day the floor was their only bound,
  // while production_system.cpp cast both to std::uint32_t. A float past
  // UINT32_MAX converts as undefined exactly as a negative one does.
  //
  // THE CONTROL IS THE POINT. "The build was refused" passes when it was
  // refused for any reason at all — a typo in the fixture would do it. So the
  // same table is built twice and differs in ONE cell: 1e9 is refused, 5 is
  // taken. Only then does the refusal say something about drought_spell_days.
  {
    const std::filesystem::path band = root / "spell_band";
    std::filesystem::create_directories(band);
    for (const char* name : {"resources.csv", "crops.csv", "weather.csv"}) {
      std::filesystem::copy_file(
          root / name, band / name, std::filesystem::copy_options::overwrite_existing);
    }
    const auto build_with_spell = [&](const char* spell) {
      std::ofstream(band / "farming.csv")
          << "key,value\nfertility_neutral,50\nmanure_norm_kg_per_ha,20000\n"
             "manure_fertility_bonus,10\nfallow_recovery,6\nrepeat_penalty_per_year,3\n"
             "drought_temp_c,25\nstress_per_day,0.02\nstress_cap,0.3\n"
             "weather_state_days,5\ndrought_spell_days,"
          << spell << "\nwet_spell_days,3\n";
      const auto set = core::LoadTableSet(band.string(), nullptr);
      return set == nullptr ? nullptr
                            : core::CreateProductionSystem(*set, core::StubTables::kAllowed);
    };
    failures += Expect(build_with_spell("1e9") == nullptr,
                       "a drought spell of a billion days is refused, not cast to unsigned");
    failures += Expect(build_with_spell("-1") == nullptr, "and a negative one is refused too");
    failures += Expect(build_with_spell("5") != nullptr,
                       "while the same table with a sane spell builds — the refusal above is "
                       "about that row and not about the fixture");
  }

  // The threshold is a RUN, not a tally: four days is under it, and a mild
  // day in the middle of a spell breaks it rather than pausing it.
  failures += Expect(after_days(24.0F, core::Precipitation::kNone, 4).weather_state ==
                         core::FieldWeatherState::kNone,
                     "four days of heat are a spell of weather, not a state of the field");
  failures += Expect(after_days(10.0F, core::Precipitation::kNone, 5).weather_state ==
                         core::FieldWeatherState::kNone,
                     "and a mild spell announces nothing at all");
  std::filesystem::remove_all(root);
  return failures;
}

/// The store door and the field's buffer (task A3): a harvest bigger than
/// the settlement can hold must be REFUSED at the ceiling, kept on the field
/// and reported — never forced in, never lost in silence. Built over a real
/// table set, because both the capacity and the payout live behind the
/// factory.
int CheckStoreCeilingAndAlarms() {
  int failures = 0;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_production_ceiling";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "resources.csv") << "key,feed_value\nrye,1.15\n";
  std::ofstream(root / "crops.csv")
      << "key,resource,is_winter,is_perennial,sow_from_month,sow_to_month,sow_min_temp_c,"
         "growth_min_temp_c,harvest_from_month,harvest_to_month,harvest_min_temp_c,"
         "yield_kg_per_ha,sowing_norm_kg_per_ha,fertility_delta,drought_sensitivity,"
         "wet_sensitivity,sow_days_per_ha,harvest_days_per_ha,straw_ratio\n"
         "rye,rye,0,0,4,5,5,5,8,8,2,1000,0,-1,0,0,3,8,0\n";
  std::ofstream(root / "farming.csv")
      << "key,value\nfertility_neutral,50\nmanure_norm_kg_per_ha,20000\n"
         "manure_fertility_bonus,10\nfallow_recovery,6\nrepeat_penalty_per_year,3\n"
         "drought_temp_c,25\nstress_per_day,0.02\nstress_cap,0.3\n"
         "weather_state_days,5\n";
  // A barn of one tonne at level 1, five at level 2. The type row still
  // carries nine, and nine is what the ceiling would be if anything still
  // read it — it does not, and that is half of what this test proves.
  std::ofstream(root / "unit_types.csv") << "key,storage_capacity_t,capacity_by_plot\nbarn,9,0\n";
  std::ofstream(root / "unit_levels.csv") << "unit,level,storage_capacity_t\nbarn,1,1\nbarn,2,5\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto system = tables == nullptr
                          ? nullptr
                          : core::CreateProductionSystem(*tables, core::StubTables::kAllowed);
  if (Expect(system != nullptr, "the ceiling table set builds a production system") != 0) {
    std::cout << error << '\n';
    return 1;
  }

  // Ten hectares at a tonne a hectare, into a barn that holds one tonne.
  core::WorldState world;
  world.calendar.tick = 31 * core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);
  core::UnitRow barn;
  barn.type = core::UnitTypeId{0};
  barn.level = 1;
  const core::UnitId barn_id = core::AppendRow(world.units, barn);
  core::FieldRow field;
  field.area_ga = 10.0F;
  field.fertility = 50.0F;
  field.phase = core::FieldPhase::kHarvest;
  field.crop = core::CropId{0};
  field.work_days_remaining = 0.0F;
  const core::FieldId field_id = core::AppendRow(world.fields, field);

  const core::WorldState before = world;
  system->RunProductionDecisions(before, world);

  // THE WHOLE TEN TONNES STAY ON THE FIELD. Until task A4 the store took
  // its tonne in the same tick the crop was cut, for nothing, and only the
  // nine that would not fit waited. Reaping and carrying are two jobs now:
  // what is cut is cut, and what is carried is carried by somebody.
  const core::Grams held = world.units.rows[0].stock.size() > 0 ? world.units.rows[0].stock[0] : 0;
  failures += Expect(held == 0, "cutting a field puts nothing in a store: that is a second job");
  failures += Expect(world.fields.rows[0].reaped_grams == 10'000 * core::kGramsPerKilogram,
                     "the whole yield lies where it was cut, waiting for hands");
  failures += Expect(world.fields.rows[0].haul_days_remaining > 0.0F,
                     "and the field asks for those hands, in its own seam");
  failures += Expect(world.fields.rows[0].reaped_resource.value == 0,
                     "the waiting load names what it is, so a cart knows");
  failures += Expect(world.fields.rows[0].reaped_day == world.calendar.day,
                     "and the field remembers the day it gave its crop, for the seed fund");
  failures += Expect(
      (world.ledger.current.harvest.empty() ? core::Grams{0} : world.ledger.current.harvest[0]) ==
          10'000 * core::kGramsPerKilogram,
      "the book still records the whole yield: the field gave it");
  failures += Expect(
      (world.ledger.current.lost_no_room.empty() ? core::Grams{0}
                                                 : world.ledger.current.lost_no_room[0]) == 0,
      "and nothing is written off — what waits is not lost");

  // Both alarms stand: the store is full, and the field is holding produce.
  std::vector<core::Alarm> alarms;
  system->CollectAlarms(world, alarms);
  bool store_full = false;
  bool waiting = false;
  for (const core::Alarm& alarm : alarms) {
    store_full = store_full ||
                 (alarm.kind == core::AlarmKind::kStoreFull && alarm.unit.value == barn_id.value);
    waiting = waiting || (alarm.kind == core::AlarmKind::kHarvestWaitingOnField &&
                          alarm.field.value == field_id.value &&
                          alarm.amount == 10'000 * core::kGramsPerKilogram);
  }
  // The store is EMPTY now, not full: nothing was carried into it, so the
  // ceiling alarm has nothing to complain about and the field alarm carries
  // the whole story. That is the right division — a full store and an
  // uncarried harvest are different troubles with different cures.
  failures += Expect(!store_full, "an empty store does not cry that it is full");
  failures += Expect(waiting, "and the field says how much is lying on it");

  // AND THE POSITIVE TWIN OF THAT NEGATION, which was missing until
  // 2026-09-16: the line above says an empty store is silent, and nothing
  // said that a FULL one speaks. Measured by coverage that day — the whole
  // body of CollectStoreAlarms was never executed by any test or run of the
  // suite, so the alarm the player sees when a granary fills had no check at
  // all (boss, standstill parcel 11). The barn holds one tonne at level 1.
  {
    core::WorldState full = world;
    const std::uint32_t row = core::FindRow(full.units, barn_id);
    full.units.rows[row].stock.assign(1, 1000 * core::kGramsPerKilogram);
    std::vector<core::Alarm> cries;
    system->CollectAlarms(full, cries);
    bool full_alarm = false;
    core::Grams said_capacity = 0;
    for (const core::Alarm& alarm : cries) {
      if (alarm.kind == core::AlarmKind::kStoreFull && alarm.unit.value == barn_id.value) {
        full_alarm = true;
        said_capacity = alarm.amount;
      }
    }
    failures += Expect(full_alarm && said_capacity == 1000 * core::kGramsPerKilogram,
                       "and a store filled to its ceiling says so, with the ceiling in the alarm");
  }

  // ROOM ALONE NO LONGER EMPTIES THE FIELD. Until task A4 a daily retry
  // moved whatever fitted, for nothing, the moment it fitted — and this
  // check measured that stub. The load waits for HANDS now: the seam is the
  // demand for carrying it, and until somebody drains the seam the barn
  // stays as empty as the day it was built, however much room it has.
  world.units.rows[0].level = 2;  // the barn was upgraded: five tonnes now
  {
    const core::WorldState yesterday = world;
    world.calendar.tick += core::kTicksPerDay;
    core::RefreshCalendarCaches(world.calendar);
    system->RunProductionDecisions(yesterday, world);
  }
  failures += Expect(world.units.rows[0].stock.empty() || world.units.rows[0].stock[0] == 0,
                     "room that appeared moves nothing by itself: a load needs carrying");
  failures += Expect(world.fields.rows[0].haul_days_remaining > 0.0F,
                     "what it does instead is ask for carriers, by the day");

  // And when the carriers have done their day — which is what draining the
  // seam MEANS, and what core_labor does with real people — the grain
  // arrives, up to the ceiling, and the rest keeps waiting.
  // The carriers do their day, and THE DAY IS SETTLED AT ITS LAST TICK —
  // which is the hour this has to be driven at. Settling at dawn instead
  // would read a seam nobody had worked yet, and the whole model of "the
  // load leaves when somebody carries it" would quietly become "the load
  // leaves".
  world.fields.rows[0].haul_days_remaining = 0.0F;
  {
    const core::WorldState yesterday = world;
    world.calendar.tick += core::kTicksPerDay - 1U;
    core::RefreshCalendarCaches(world.calendar);
    system->RunProductionDecisions(yesterday, world);
  }
  failures += Expect(!world.units.rows[0].stock.empty() &&
                         world.units.rows[0].stock[0] == 5'000 * core::kGramsPerKilogram,
                     "a day of hauling fills the room that appeared, up to the new ceiling");
  failures += Expect(world.fields.rows[0].reaped_grams == 5'000 * core::kGramsPerKilogram,
                     "and what still does not fit keeps waiting");

  // THE SETTLED SNOW TAKES WHAT STILL LIES OUT (farming design §6; boss,
  // parcel 408). A cover on its first day is a dusting the melt rule may yet
  // take away, so the load waits on; a cover on its second day is settled,
  // and the load is written off that day rather than lying into next year.
  world.weather.snow_cover_days = 1;
  {
    const core::WorldState yesterday = world;
    world.calendar.tick += 1U;
    core::RefreshCalendarCaches(world.calendar);
    system->RunProductionDecisions(yesterday, world);
  }
  failures += Expect(world.fields.rows[0].reaped_grams == 5'000 * core::kGramsPerKilogram,
                     "a first day's snow cover takes nothing off the field");
  world.weather.snow_cover_days = 2;
  {
    const core::WorldState yesterday = world;
    world.calendar.tick += core::kTicksPerDay;
    core::RefreshCalendarCaches(world.calendar);
    system->RunProductionDecisions(yesterday, world);
  }
  failures += Expect(
      world.fields.rows[0].reaped_grams == 0 &&
          !(world.fields.rows[0].haul_days_remaining > 0.0F) &&
          !world.ledger.current.lost_no_room.empty() &&
          world.ledger.current.lost_no_room[0] == 5'000 * core::kGramsPerKilogram,
      "settled snow writes the waiting load off as lost, and the field asks for no carriers");

  // TWO THRESHOLDS FOR TWO THINGS (boss, parcel 430; fields and crops §6):
  // the crop still standing on its root dies on the first snowfall of the
  // reaping season, with no cover lying yet — frozen tops cannot be dug nor
  // flattened grain cut — while the reaped load above waited for the cover.
  world.fields.rows[0].phase = core::FieldPhase::kHarvest;
  world.fields.rows[0].crop = core::CropId{0};
  world.fields.rows[0].work_days_remaining = 1.0F;
  world.weather.precipitation = core::Precipitation::kSnow;
  world.weather.snow_cover_days = 0;
  {
    const core::WorldState yesterday = world;
    world.calendar.tick += core::kTicksPerDay;
    core::RefreshCalendarCaches(world.calendar);
    system->RunProductionDecisions(yesterday, world);
  }
  failures += Expect(world.fields.rows[0].crop.value == core::kInvalidDefIdValue &&
                         world.fields.rows[0].phase == core::FieldPhase::kIdle,
                     "and the first snowfall of the reaping season takes the crop still on its "
                     "root, with no cover lying yet");
  std::filesystem::remove_all(root);
  return failures;
}

/// THE FEED LIGHT COUNTS THE WINTERING, and the corner is midsummer.
///
/// In the pasture months the grass covers most of the ration and the daily
/// draw on the stores falls to nearly nothing. A light that counted TODAY
/// would stand green all summer and turn yellow in November — when the hay
/// can no longer be cut. It is meant to be most useful in haymaking, and
/// that is only true if it looks past the grass.
int CheckFeedLightCountsTheWinter() {
  int failures = 0;
  constexpr core::Grams kKilo = core::kGramsPerKilogram;
  core::ProductionConfig config = MakeHerdConfig();
  config.feed_values = {1.0F, 1.0F, 0.0F};
  config.feed_links = {core::FeedLinkDef{.kind = core::LivestockKindId{0},
                                         .resource = core::ResourceId{0},
                                         .reserve = 0,
                                         .max_share = 1.0F}};
  config.farming.pasture_from_month = 4;
  config.farming.pasture_to_month = 8;
  // Half the ration comes off the grass in summer. If the forecast used the
  // summer need it would report twice the days it should.
  config.livestock[0].pasture_coverage_summer = 0.5F;

  core::WorldState world = MakeHerdWorld(0.0F);
  world.units.rows[0].stock[0] = 40 * kKilo;  // 40 feed units
  AddHerd(world, 0, 4, 0, true);              // four adults: 4 units a winter day

  world.calendar.tick = 6 * core::kDaysPerMonth * core::kTicksPerDay;  // July
  core::RefreshCalendarCaches(world.calendar);
  const core::StockForecast summer = core::FeedLight(config, world);
  failures += Expect(static_cast<std::uint8_t>(world.calendar.date.month) == 6,
                     "the test stands in the pasture season");
  failures += Expect(summer.days_of_stock == 10,
                     "forty units against the WINTER ration of four a day: ten days, "
                     "not the twenty the grass would suggest");

  world.calendar.tick = 11 * core::kDaysPerMonth * core::kTicksPerDay;  // December
  core::RefreshCalendarCaches(world.calendar);
  const core::StockForecast winter = core::FeedLight(config, world);
  failures += Expect(winter.days_of_stock == summer.days_of_stock,
                     "and the same answer in December: the light does not change its mind "
                     "with the season, only the date it is measured against does");
  return failures;
}

/// THE CEILING, which is where dividing would lie.
///
/// Every feed has a cap on the share of the day's need it may cover — a
/// ruminant does not live on grain however much of it there is. Total units
/// over daily need OVERSTATES a lopsided store, and an overstating light is
/// the green one that lies.
int CheckFeedLightRespectsTheCeiling() {
  int failures = 0;
  constexpr core::Grams kKilo = core::kGramsPerKilogram;
  core::ProductionConfig config = MakeHerdConfig();
  config.feed_values = {1.0F, 1.0F, 0.0F};
  config.farming.pasture_from_month = 4;
  config.farming.pasture_to_month = 8;
  config.livestock[0].pasture_coverage_summer = 0.0F;
  // One feed, capped at half the ration. The store holds twenty units and
  // the herd needs two a day: dividing says ten days, the cap says five,
  // and after five days the herd is starving beside ten unusable units.
  config.feed_links = {core::FeedLinkDef{.kind = core::LivestockKindId{0},
                                         .resource = core::ResourceId{0},
                                         .reserve = 0,
                                         .max_share = 0.5F}};
  core::WorldState world = MakeHerdWorld(0.0F);
  world.units.rows[0].stock[0] = 20 * kKilo;
  AddHerd(world, 0, 2, 0, true);
  world.calendar.tick = 11 * core::kDaysPerMonth * core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);

  const core::StockForecast light = core::FeedLight(config, world);
  failures += Expect(light.days_of_stock == 0,
                     "a ration that can never be covered in full runs out on day one, "
                     "and dividing the units would have promised ten days");
  failures += Expect(light.light == core::StockLight::kRed,
                     "and the herd that cannot be fed today is red, not amber");
  return failures;
}

/// "NEVER" IS AN ANSWER, and it is not "no data". A village with no kolkhoz
/// herd has nothing eating the fodder; the question was asked and answered.
int CheckFeedLightNeverRunsOut() {
  int failures = 0;
  core::ProductionConfig config = MakeHerdConfig();
  config.feed_values = {1.0F, 1.0F, 0.0F};
  core::WorldState world = MakeHerdWorld(0.0F);
  const core::StockForecast light = core::FeedLight(config, world);
  failures +=
      Expect(light.days_of_stock == core::kStockNeverRunsOut, "nothing eats, so nothing runs out");
  failures += Expect(light.light == core::StockLight::kGreen,
                     "which is green, and pointedly not the dark of an unanswered light");
  return failures;
}

/// THE SEED LIGHT MEASURES COVERAGE, because seed has no daily spending.
///
/// It goes into the ground all at once, so "days of seed" is not a hard
/// number — it is one that does not exist: infinite until the sowing, zero
/// on the day of it (boss, 2026-09-04). The light answers what the stock
/// actually has to say: what share of the campaign can be sown.
int CheckSeedLightMeasuresCoverage() {
  int failures = 0;
  constexpr core::Grams kKilo = core::kGramsPerKilogram;
  core::ProductionConfig config = MakeHerdConfig();
  config.crops.resize(1);
  core::CropDef& rye = config.crops[0];
  rye.resource = core::ResourceId{0};
  rye.sowing_norm_kg_per_ha = 10.0F;
  rye.sow_from_month = 4;
  rye.sow_to_month = 5;
  config.farming.seed_light_margin_share = 0.1F;

  core::WorldState world = MakeHerdWorld(0.0F);
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.area_ga = 10.0F;  // a hundred kilograms of seed
  field.rotation_year0 = core::CropId{0};
  field.phase = core::FieldPhase::kIdle;
  core::AppendRow(world.fields, field);
  world.calendar.tick = 0;  // January: the sowing is four months off
  core::RefreshCalendarCaches(world.calendar);

  world.units.rows[0].stock[0] = 60 * kKilo;
  const core::StockForecast thin = core::SeedLight(config, world);
  failures += Expect(thin.measure == core::StockMeasure::kCoverage,
                     "the seed light says outright which number it answers with");
  failures += Expect(thin.coverage > 0.59F && thin.coverage < 0.61F,
                     "sixty kilograms against a hundred is six tenths of the campaign");
  failures += Expect(thin.light == core::StockLight::kRed,
                     "and short of a whole covering it burns at once, not when the date nears: "
                     "seed is mended slowly, so waiting for the sowing would warn too late");
  failures += Expect(thin.days_to_date == 4 * static_cast<std::int32_t>(core::kDaysPerMonth),
                     "the date beside it is the CALENDAR, not a number derived from the stock");

  world.units.rows[0].stock[0] = 105 * kKilo;
  failures += Expect(core::SeedLight(config, world).light == core::StockLight::kYellow,
                     "covered by a twentieth is covered only just");
  world.units.rows[0].stock[0] = 200 * kKilo;
  failures += Expect(core::SeedLight(config, world).light == core::StockLight::kGreen,
                     "and twice the norm is room to spare");
  return failures;
}

/// THE CAMPAIGN IS THE CALENDAR, NOT THE ROTATION INDEX, and an occupied
/// field is not in it.
///
/// The light used to read rotation_year0 — THIS year's crop, shifted only at
/// the year's turn — so from the sowing to the new year a spring field was
/// charged its seed a second time and the light stood amber for a third of
/// the year with nothing behind it. Asking about the nearest campaign fixes
/// it by asking the right question: once the winter crop is in the ground,
/// winter seed is not what the farm is short of.
int CheckSeedLightAsksAboutTheNearestCampaign() {
  int failures = 0;
  core::ProductionConfig config = MakeHerdConfig();
  config.crops.resize(2);
  config.crops[0].resource = core::ResourceId{0};  // winter rye, sown in month 8
  config.crops[0].sowing_norm_kg_per_ha = 10.0F;
  config.crops[0].sow_from_month = 8;
  config.crops[0].sow_to_month = 8;
  config.crops[1].resource = core::ResourceId{1};  // spring oats, sown in month 3
  config.crops[1].sowing_norm_kg_per_ha = 10.0F;
  config.crops[1].sow_from_month = 3;
  config.crops[1].sow_to_month = 3;

  // FOUR FIELDS, AND EACH IS EXCLUDED BY AT MOST ONE RULE. A field that two
  // rules both throw out measures neither of them: drop either rule and the
  // answer does not move. The first draft of this test had exactly that —
  // one winter field, standing AND of the other campaign — and both
  // mutations passed in silence.
  core::WorldState world = MakeHerdWorld(0.0F);
  const auto add_field = [&world](std::uint16_t crop, core::FieldPhase phase) {
    core::FieldRow field;
    field.kind = core::LandKind::kArable;
    field.area_ga = 1.0F;
    field.rotation_year0 = core::CropId{crop};
    field.phase = phase;
    core::AppendRow(world.fields, field);
  };
  add_field(1, core::FieldPhase::kIdle);     // spring, free: the campaign's own
  add_field(0, core::FieldPhase::kIdle);     // winter, free: only the CAMPAIGN excludes it
  add_field(1, core::FieldPhase::kGrowing);  // spring, standing: only OCCUPIED excludes it

  // October: the winter crop is in the ground, the nearest campaign is the
  // spring one. There is no winter seed left in store at all — and that is
  // not a shortage, because nobody is going to sow winter rye again this
  // year.
  world.calendar.tick = 9 * core::kDaysPerMonth * core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);
  world.units.rows[0].stock[0] = 0;
  world.units.rows[0].stock[1] = 100 * core::kGramsPerKilogram;
  const core::StockForecast autumn = core::SeedLight(config, world);
  failures += Expect(autumn.light == core::StockLight::kGreen,
                     "an empty winter-seed bin in October is not a shortage: the winter crop "
                     "is already in the ground");
  // ONE free spring hectare wants ten kilograms, and a hundred stand there.
  // Count the winter field too and the tightest crop becomes rye at zero;
  // count the standing spring field and the cover halves to five.
  failures += Expect(autumn.coverage > 9.9F && autumn.coverage < 10.1F,
                     "exactly one hectare is waiting to be sown, and it is covered ten times "
                     "over — the standing field and the other campaign are both out");
  return failures;
}

/// TWO KINDS, AND EACH EATS ITS OWN. The first version of this forecast
/// walked every feed link without looking at whose it was, so the pigs'
/// barley covered the cows' hay and every ceiling — a share of ONE herd's
/// day — became a share of the whole settlement, which is to say nothing at
/// all. The tests written with it could not see that: each had one kind and
/// one feed. A forecast is only as honest as the smallest farm it was tried
/// on, and one kind is not a farm.
int CheckFeedLightKeepsTheKindsApart() {
  int failures = 0;
  constexpr core::Grams kKilo = core::kGramsPerKilogram;
  core::ProductionConfig config = MakeHerdConfig();
  config.feed_values = {1.0F, 1.0F, 0.0F};
  config.farming.pasture_from_month = 4;
  config.farming.pasture_to_month = 8;
  config.livestock[0].pasture_coverage_summer = 0.0F;
  config.livestock[1].feed_units_per_game_day = 1.0F;
  config.livestock[1].pasture_coverage_summer = 0.0F;
  // Resource 0 is the cows' feed, resource 1 the pigs'. Neither kind is ever
  // offered the other's.
  config.feed_links = {core::FeedLinkDef{.kind = core::LivestockKindId{0},
                                         .resource = core::ResourceId{0},
                                         .reserve = 0,
                                         .max_share = 1.0F},
                       core::FeedLinkDef{.kind = core::LivestockKindId{1},
                                         .resource = core::ResourceId{1},
                                         .reserve = 0,
                                         .max_share = 1.0F}};

  core::WorldState world = MakeHerdWorld(0.0F);
  world.units.rows[0].stock[0] = 4 * kKilo;    // the cows have four days
  world.units.rows[0].stock[1] = 100 * kKilo;  // the pigs have a hundred
  AddHerd(world, 0, 1, 0, true);               // one cow: one unit a day
  AddHerd(world, 1, 1, 0, true);               // one pig: one unit a day
  world.calendar.tick = 11 * core::kDaysPerMonth * core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);

  const core::StockForecast light = core::FeedLight(config, world);
  failures += Expect(light.days_of_stock == 4,
                     "the farm runs out when the FIRST herd does: the pigs' hundred days "
                     "do not feed the cows for one");
  return failures;
}

/// TWO CROPS, ONE RESOURCE — and two crops that are not one.
///
/// Winter wheat and spring wheat are two crops and a single resource, so
/// summing the stock per crop counted the same grain twice. And summing the
/// crops TOGETHER let a mountain of one cancel the absence of another: you
/// cannot sow oats with rye, and a village with no potato seed is short
/// however much rye it has.
int CheckSeedLightDoesNotNetCropsOff() {
  int failures = 0;
  constexpr core::Grams kKilo = core::kGramsPerKilogram;
  core::ProductionConfig config = MakeHerdConfig();
  config.crops.resize(2);
  config.crops[0].resource = core::ResourceId{0};  // rye
  config.crops[0].sowing_norm_kg_per_ha = 10.0F;
  config.crops[0].sow_from_month = 4;
  config.crops[0].sow_to_month = 5;
  config.crops[1].resource = core::ResourceId{1};  // potato, a different heap
  config.crops[1].sowing_norm_kg_per_ha = 10.0F;
  config.crops[1].sow_from_month = 4;
  config.crops[1].sow_to_month = 5;
  config.farming.seed_light_margin_share = 0.0F;

  core::WorldState world = MakeHerdWorld(0.0F);
  core::FieldRow rye_field;
  rye_field.kind = core::LandKind::kArable;
  rye_field.area_ga = 1.0F;
  rye_field.rotation_year0 = core::CropId{0};
  core::AppendRow(world.fields, rye_field);
  core::FieldRow potato_field = rye_field;
  potato_field.rotation_year0 = core::CropId{1};
  core::AppendRow(world.fields, potato_field);

  // Rye to spare, no potato seed at all. Netted together the village looks
  // comfortable; crop by crop it cannot sow half its land.
  world.units.rows[0].stock[0] = 1000 * kKilo;
  world.units.rows[0].stock[1] = 0;
  const core::StockForecast light = core::SeedLight(config, world);
  failures += Expect(light.light == core::StockLight::kRed,
                     "no potato seed is short, however much rye stands beside it");
  failures += Expect(light.coverage == 0.0F, "and the coverage is the tightest crop's: none");

  // Both covered, but one only just: the tightest crop decides the days.
  world.units.rows[0].stock[1] = 40 * kKilo;  // 10 needed, 30 spare
  world.units.rows[0].stock[0] = 1000 * kKilo;
  const core::StockForecast tight = core::SeedLight(config, world);
  failures += Expect(tight.coverage > 3.9F && tight.coverage < 4.1F,
                     "the crop with the thinnest cover is the one that counts, not the fat one "
                     "beside it");
  return failures;
}

/// NOBODY HAULED, AND THE GRAIN MOVED ANYWAY.
///
/// The settlement compares TODAY'S demand with YESTERDAY'S leftover and
/// treats the difference as work done. That is right only while the demand
/// can shrink: the demand is capped by the room the stores can still take,
/// and the room GROWS every day, because the village eats. On a day when
/// nobody was sent to the field at all, today's demand comes out larger than
/// yesterday's leftover, and the difference is booked as a load carried.
///
/// Found by the seventh reconciliation pass, and it explains its strangest
/// number: the thirtieth year harvests 1172 tonnes of field produce and
/// spends 0.00 man-days carrying it.
int CheckHaulingIsNotFree() {
  int failures = 0;
  constexpr core::Grams kKilo = core::kGramsPerKilogram;
  core::ProductionConfig config = MakeHerdConfig();
  config.cart_load_kg = 750.0F;
  config.carry_kg_adult = 20.0F;
  config.walk_speed_kmh = 5.0F;
  config.harness_speed_kmh = 12.0F;
  config.standard_day_hours = 10.0F;
  SetStorageKg(config.unit_types[0], 10000.0F);

  core::WorldState world = MakeHerdWorld(0.0F);
  world.units.rows[0].position = core::Vec2{.x = 0.0F, .y = 0.0F};
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.area_ga = 10.0F;
  field.center = core::Vec2{.x = 2000.0F, .y = 0.0F};
  field.reaped_grams = 8000 * kKilo;  // eight tonnes lying on the ground
  field.reaped_resource = core::ResourceId{0};
  core::AppendRow(world.fields, field);

  // Day one sizes the demand. Nobody is assigned, so the seam is untouched.
  core::SettleHauling(config, world);
  const core::Grams after_first = world.fields.rows[0].reaped_grams;
  failures += Expect(after_first == 8000 * kKilo,
                     "the first evening carries nothing: the demand was only just written");
  failures += Expect(world.fields.rows[0].haul_days_remaining > 0.0F, "and it asks for carriers");

  // Overnight the stores empty a little — the village ate. NOBODY is sent to
  // the field: the seam is left exactly as the settlement wrote it.
  world.units.rows[0].stock[0] = 0;
  core::SettleHauling(config, world);
  failures += Expect(world.fields.rows[0].reaped_grams == after_first,
                     "and the second evening carries nothing either, because nobody went: "
                     "room that grew overnight is not a day's work by somebody");
  return failures;
}

/// The warning must come a season before the loss, and it did not.
///
/// host measured the window between kHarvestWillNotFit and
/// kHarvestWaitingOnField on 0.17.24: 0 days on two seeds of three, 4 on
/// the third, against a granary that takes 15 days to raise. There is no
/// arithmetic in which that is a warning.
///
/// The cause is in the predicate, not the balance. Every field was compared
/// against the WHOLE free room, so fields sharing one store each "fitted"
/// on their own and nobody was told that together they did not. The warning
/// became true only once the room had shrunk below a single field — and the
/// room shrinks because the harvest has begun, which is why the two alarms
/// arrived on the same tick.
///
/// So the control quantity here is not a number of days but an ORDER: with
/// three fields sharing a room too small for their sum, the warning must
/// stand while every one of them is still growing — before any of them is
/// cut, and therefore before there is anything to be lost.
int CheckTheHarvestWarningComesBeforeTheHarvest() {
  int failures = 0;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_production_will_not_fit";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "resources.csv") << "key,feed_value\nrye,1.15\n";
  std::ofstream(root / "crops.csv")
      << "key,resource,is_winter,is_perennial,sow_from_month,sow_to_month,sow_min_temp_c,"
         "growth_min_temp_c,harvest_from_month,harvest_to_month,harvest_min_temp_c,"
         "yield_kg_per_ha,sowing_norm_kg_per_ha,fertility_delta,drought_sensitivity,"
         "wet_sensitivity,sow_days_per_ha,harvest_days_per_ha,straw_ratio\n"
         "rye,rye,0,0,4,5,5,5,8,8,2,1000,0,-1,0,0,3,8,0\n";
  std::ofstream(root / "farming.csv")
      << "key,value\nfertility_neutral,50\nmanure_norm_kg_per_ha,20000\n"
         "manure_fertility_bonus,10\nfallow_recovery,6\nrepeat_penalty_per_year,3\n"
         "drought_temp_c,25\nstress_per_day,0.02\nstress_cap,0.3\n"
         "weather_state_days,5\n";
  std::ofstream(root / "unit_types.csv") << "key,capacity_by_plot\nbarn,0\n";
  // A barn of sixty tonnes, and three fields of fifty growing into it.
  std::ofstream(root / "unit_levels.csv") << "unit,level,storage_capacity_t\nbarn,1,60\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto system = tables == nullptr
                          ? nullptr
                          : core::CreateProductionSystem(*tables, core::StubTables::kAllowed);
  if (Expect(system != nullptr, "the shared-room table set builds a production system") != 0) {
    std::cout << error << '\n';
    return 1;
  }

  core::WorldState world;
  world.calendar.tick = 31 * core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);
  core::UnitRow barn;
  barn.type = core::UnitTypeId{0};
  barn.level = 1;
  core::AppendRow(world.units, barn);
  std::vector<core::FieldId> field_ids;
  for (int index = 0; index < 3; ++index) {
    core::FieldRow field;
    field.kind = core::LandKind::kArable;
    field.area_ga = 50.0F;  // fifty hectares at a tonne a hectare
    field.fertility = 50.0F;
    field.phase = core::FieldPhase::kGrowing;
    field.crop = core::CropId{0};
    field_ids.push_back(core::AppendRow(world.fields, field));
  }

  const auto warned = [&system, &world](const core::FieldId field) {
    std::vector<core::Alarm> alarms;
    system->CollectAlarms(world, alarms);
    core::Grams total = 0;
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind == core::AlarmKind::kHarvestWillNotFit &&
          (field.value == core::kInvalidEntityIdValue || alarm.field.value == field.value)) {
        total += alarm.amount;
      }
    }
    return total;
  };

  // Every field is still GROWING: nothing has been cut, so nothing can have
  // been lost, and this is the moment the warning exists for.
  failures += Expect(warned(core::FieldId{}) == 90'000 * core::kGramsPerKilogram,
                     "three fields of fifty into sixty: the warning names the ninety over");
  failures +=
      Expect(warned(field_ids[0]) == 0, "the first field fits in the room and is not warned about");
  failures += Expect(warned(field_ids[1]) == 40'000 * core::kGramsPerKilogram,
                     "the second overruns by what is left of the room after the first");
  failures += Expect(warned(field_ids[2]) == 50'000 * core::kGramsPerKilogram,
                     "and the third by the whole of itself: the room is spent");

  // One field alone into the same barn fits, and silence is the right answer
  // — otherwise the check above would only be proving that it always warns.
  core::WorldState one_field = world;
  one_field.fields.rows.resize(1);
  one_field.fields.row_ids.resize(1);
  {
    std::vector<core::Alarm> alarms;
    system->CollectAlarms(one_field, alarms);
    bool any = false;
    for (const core::Alarm& alarm : alarms) {
      any = any || alarm.kind == core::AlarmKind::kHarvestWillNotFit;
    }
    failures += Expect(!any, "fifty tonnes into sixty is not a warning");
  }

  // AND IT DOES NOT SHRINK AS THE FIELD IS CUT. This is the one-day hole
  // host measured on 0.17.58 (seed 53, oat f7, 10.5 ha): the warning stood
  // at 22.63 t through d29, went dark for d30 alone, and 11.46 t landed on
  // d31. Silence for exactly the day it mattered.
  //
  // The claim used to count only the part still STANDING, on the stated
  // ground that the cut part was already a heap. It is not: Harvest() runs
  // ONCE, when the phase finishes, so while a field is being reaped nothing
  // has been placed, reaped_grams is zero and no straw has left. The cut
  // part was in neither place, and the gap grew as the reaping went on —
  // widest on its last day.
  //
  // The guard walks the labour down instead of asserting one point, because
  // the defect was a SLOPE: a single sample at half-cut would have passed on
  // the old code too, at half the amount. Nothing is delivered between these
  // samples, so the right answer is the same number every time.
  {
    core::WorldState reaping = world;
    const core::Grams standing = warned(field_ids[2]);
    bool every_sample_matches = true;
    core::Grams smallest = standing;
    for (const float left : {1.0F, 0.75F, 0.5F, 0.25F, 0.01F}) {
      reaping.fields.rows[2].phase = core::FieldPhase::kHarvest;
      // The phase norm is harvest_days_per_ha (8) times the fifty hectares.
      reaping.fields.rows[2].work_days_remaining = left * 8.0F * 50.0F;
      std::vector<core::Alarm> alarms;
      system->CollectAlarms(reaping, alarms);
      core::Grams over = 0;
      for (const core::Alarm& alarm : alarms) {
        if (alarm.kind == core::AlarmKind::kHarvestWillNotFit &&
            alarm.field.value == field_ids[2].value) {
          over += alarm.amount;
        }
      }
      every_sample_matches = every_sample_matches && over == standing;
      smallest = over < smallest ? over : smallest;
    }
    failures += Expect(standing > 0,
                       "the standing field is warned about at all — without that the samples "
                       "below would agree at zero");
    failures += Expect(every_sample_matches,
                       "and a field being reaped claims exactly what it claimed standing, at "
                       "every stage of the cut: nothing has been delivered yet");
    failures += Expect(smallest > 0,
                       "in particular it never falls to nothing on the last day of reaping — the "
                       "day the whole load lands tomorrow");
  }

  // AND IT KEEPS BURNING WHILE THE LOAD IS STILL HOMELESS. An earlier
  // version of this check asserted the opposite — that a field already
  // holding its own load says nothing more, because a forecast of what has
  // happened is not a forecast. That reasoning was sound and the rule was
  // wrong, and host measured why: the alarm went out a median of FOUR DAYS
  // before the grain hit the ground, so the last thing the player saw
  // before losing a crop was the warning going away. A signal that switches
  // off just before the trouble does not read as silence — it reads as "it
  // turned out fine" (boss and host, 2026-09-05). The alarm goes out when
  // the harvest is STORED or LOST, not when the field changes phase.
  world.fields.rows[2].reaped_grams = 10'000 * core::kGramsPerKilogram;
  world.fields.rows[2].reaped_resource = core::ResourceId{0};
  failures +=
      Expect(warned(field_ids[2]) > 0, "a field holding its load homeless goes on saying so");
  failures += Expect(warned(field_ids[1]) == 40'000 * core::kGramsPerKilogram,
                     "and its neighbours are warned exactly as before: the room is spent the same");

  std::filesystem::remove_all(root);
  return failures;
}

/// A field being reaped spends the room, even though nobody is warned about
/// it.
///
/// host, seed 1930: on day 20 a timothy field entered the HARVEST phase; the
/// warnings that day named two other fields, and timothy was never named on
/// any day. Two days later it gave up 27.3 tonnes — into the room that had
/// just been promised to those two. The alarm's loop skipped any field that
/// was not growing, so a field being reaped was neither warned about NOR
/// subtracted, and the forecast was wrong about every field at once.
///
/// The two are separate questions. Keeping quiet about a field whose harvest
/// has begun is a choice and a defensible one — there is no season left to
/// answer in. Leaving its load out of the arithmetic is not a choice: those
/// tonnes take room whoever is told about them.
int CheckAReapedFieldStillSpendsTheRoom() {
  int failures = 0;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_production_reaping_claim";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "resources.csv") << "key,feed_value\nrye,1.15\n";
  // Eight harvest days a hectare, so a ten-hectare field's harvest phase is
  // eighty real days of work — the measure of "how much is still standing".
  std::ofstream(root / "crops.csv")
      << "key,resource,is_winter,is_perennial,sow_from_month,sow_to_month,sow_min_temp_c,"
         "growth_min_temp_c,harvest_from_month,harvest_to_month,harvest_min_temp_c,"
         "yield_kg_per_ha,sowing_norm_kg_per_ha,fertility_delta,drought_sensitivity,"
         "wet_sensitivity,sow_days_per_ha,harvest_days_per_ha,straw_ratio\n"
         "rye,rye,0,0,4,5,5,5,8,8,2,1000,0,-1,0,0,3,8,0\n";
  std::ofstream(root / "farming.csv")
      << "key,value\nfertility_neutral,50\nmanure_norm_kg_per_ha,20000\n"
         "manure_fertility_bonus,10\nfallow_recovery,6\nrepeat_penalty_per_year,3\n"
         "drought_temp_c,25\nstress_per_day,0.02\nstress_cap,0.3\n"
         "weather_state_days,5\n";
  std::ofstream(root / "unit_types.csv") << "key,capacity_by_plot\nbarn,0\n";
  std::ofstream(root / "unit_levels.csv") << "unit,level,storage_capacity_t\nbarn,1,60\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto system = tables == nullptr
                          ? nullptr
                          : core::CreateProductionSystem(*tables, core::StubTables::kAllowed);
  if (Expect(system != nullptr, "the reaping-claim table set builds a production system") != 0) {
    std::cout << error << '\n';
    return 1;
  }

  core::WorldState world;
  core::RefreshCalendarCaches(world.calendar);
  core::UnitRow barn;
  barn.type = core::UnitTypeId{0};
  barn.level = 1;
  core::AppendRow(world.units, barn);
  // Row 0 is being reaped and is half uncut: fifty tonnes expected, so
  // twenty-five are still standing. Row 1 is growing fifty more.
  core::FieldRow reaping;
  reaping.kind = core::LandKind::kArable;
  reaping.area_ga = 50.0F;
  reaping.fertility = 50.0F;
  reaping.phase = core::FieldPhase::kHarvest;
  reaping.crop = core::CropId{0};
  reaping.work_days_remaining = 0.5F * (8.0F / core::kRealDaysPerGameDay) * 50.0F;
  const core::FieldId cut = core::AppendRow(world.fields, reaping);
  core::FieldRow growing;
  growing.kind = core::LandKind::kArable;
  growing.area_ga = 50.0F;
  growing.fertility = 50.0F;
  growing.phase = core::FieldPhase::kGrowing;
  growing.crop = core::CropId{0};
  const core::FieldId ahead = core::AppendRow(world.fields, growing);

  const auto warned = [&system, &world](const core::FieldId field) {
    std::vector<core::Alarm> alarms;
    system->CollectAlarms(world, alarms);
    core::Grams total = 0;
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind == core::AlarmKind::kHarvestWillNotFit && alarm.field.value == field.value) {
        total += alarm.amount;
      }
    }
    return total;
  };

  // THE HALF-CUT HEAP IS UNREACHABLE BY DESIGN, NOT BY OMISSION (boss,
  // 2026-09-06). A field does not accumulate this year's crop while it is
  // being reaped, and it never will: the intermediate state changes no
  // decision the player makes — the carting happens on its own, nobody
  // assigns it — and the one thing it was wanted for, the room warning, is
  // answered by the whole field claiming its whole yield. A model that
  // yields no decision is not modelled (design limits §1). The design line
  // that made it look intended — "everything still on this field, standing,
  // in swaths, in stooks, in heaps at the edge" — is a PICTURE: the layer
  // draws swaths and stooks from the share of labour done, which it already
  // has, without the core counting them.
  // Sixty tonnes of room and a field being reaped that is still going to
  // deliver all fifty of itself: ten left, and the growing fifty overruns by
  // forty. Under the OLDEST rule the reaped field claimed nothing at all;
  // under the one this replaces it claimed only the half still on the stalk,
  // which is a smaller lie with the same shape — nothing has left the field
  // until its phase ends, so half of it was in neither place.
  failures += Expect(warned(ahead) == 40'000 * core::kGramsPerKilogram,
                     "a field being reaped is taken out of the room whole, not by the part still "
                     "standing");
  // The field being reaped is silent because its twenty-five tonnes FIT in
  // the sixty, not because it is being reaped: it spends the room first and
  // finds enough. A field being reaped that does NOT fit says so — that is
  // what CheckTheWarningBurnsUntilTheHarvestIsResolved is for.
  failures +=
      Expect(warned(cut) == 0, "the field being reaped fits into the room and says nothing");

  // (The half-cut heap is unreachable BY DESIGN — see the note above; a
  // model that yields no decision is not modelled.)
  // A load already CUT and lying on the field claims its own weight ON TOP,
  // and here that is LAST year's heap, which is the only heap a field can
  // hold while this year's crop is still being reaped. It is not in a store,
  // so it has not touched today's free room, and it goes in the moment there
  // is anywhere to put it. Twenty lying plus the fifty still to come spends
  // the sixty and leaves nothing, so the growing fifty overruns by all of
  // itself.
  world.fields.rows[0].reaped_grams = 20'000 * core::kGramsPerKilogram;
  world.fields.rows[0].reaped_resource = core::ResourceId{0};
  failures += Expect(warned(ahead) == 50'000 * core::kGramsPerKilogram,
                     "a load already cut and lying on a field claims room as well, on top of the "
                     "crop still coming off it");

  // (The half-cut heap is unreachable BY DESIGN — see the note above; a
  // model that yields no decision is not modelled.)
  // And when it is all cut and carried away, the field claims nothing and
  // the growing fifty fits into the sixty again — or the check above would
  // only be proving that something always overruns.
  //
  // THE PHASE IS PART OF THAT STATE. It used to be left at kHarvest with the
  // labour at zero, which said "carried away" only because the claim was
  // computed from the labour left. A field being reaped claims its whole
  // yield now, whatever the labour says, and "carried away" is kIdle with an
  // empty heap — which is also the only one of the two the running core ever
  // holds: AdvanceFinishedPhases resolves a reaped field in the same step
  // its work reaches zero.
  world.fields.rows[0].phase = core::FieldPhase::kIdle;
  world.fields.rows[0].reaped_grams = 0;
  world.fields.rows[0].work_days_remaining = 0.0F;
  failures += Expect(warned(ahead) == 0,
                     "a field with nothing left to give claims nothing, and the room is free");

  std::filesystem::remove_all(root);
  return failures;
}

/// The straw arrives with the grain, and it was never counted.
///
/// Harvest() puts the grain through the store door and then, in the same
/// tick, `yield x straw_ratio` behind it — 1.5 for rye, 1.1 for oat, 0.8 for
/// buckwheat. RoomClaimOf credited the grain alone, so a rye field was
/// measured at two-fifths of what it actually delivers.
///
/// host traced one on seed 1930: an oat field's warning stood from day 20 to
/// day 26 at 11.6 t, WENT OUT on day 27 because the grain by then fitted,
/// and on day 30 the load landed with 26.6 tonnes of straw beside it. Nine
/// of the fifteen remaining losses were of this shape.
///
/// The straw is also the sharper half, and this test says so: grain that
/// does not fit WAITS on the field, straw that does not fit is written off
/// the same tick. Only the standing crop brings any — what is already cut
/// has had its straw placed or lost already.
int CheckTheStrawClaimsRoomToo() {
  int failures = 0;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_production_straw";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "resources.csv") << "key,feed_value\nrye,1.15\nstraw,0.2\n";
  // straw_ratio 1.5, the rye figure: fifty tonnes of grain bring seventy-five
  // of straw, and the field asks the stores for a hundred and twenty-five.
  std::ofstream(root / "crops.csv")
      << "key,resource,is_winter,is_perennial,sow_from_month,sow_to_month,sow_min_temp_c,"
         "growth_min_temp_c,harvest_from_month,harvest_to_month,harvest_min_temp_c,"
         "yield_kg_per_ha,sowing_norm_kg_per_ha,fertility_delta,drought_sensitivity,"
         "wet_sensitivity,sow_days_per_ha,harvest_days_per_ha,straw_ratio\n"
         "rye,rye,0,0,4,5,5,5,8,8,2,1000,0,-1,0,0,3,8,1.5\n";
  std::ofstream(root / "farming.csv")
      << "key,value\nfertility_neutral,50\nmanure_norm_kg_per_ha,20000\n"
         "manure_fertility_bonus,10\nfallow_recovery,6\nrepeat_penalty_per_year,3\n"
         "drought_temp_c,25\nstress_per_day,0.02\nstress_cap,0.3\n"
         "weather_state_days,5\n";
  std::ofstream(root / "unit_types.csv") << "key,capacity_by_plot\nbarn,0\n";
  std::ofstream(root / "unit_levels.csv") << "unit,level,storage_capacity_t\nbarn,1,60\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto system = tables == nullptr
                          ? nullptr
                          : core::CreateProductionSystem(*tables, core::StubTables::kAllowed);
  if (Expect(system != nullptr, "the straw table set builds a production system") != 0) {
    std::cout << error << '\n';
    return 1;
  }

  core::WorldState world;
  core::RefreshCalendarCaches(world.calendar);
  core::UnitRow barn;
  barn.type = core::UnitTypeId{0};
  barn.level = 1;
  core::AppendRow(world.units, barn);
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.area_ga = 50.0F;  // fifty tonnes of grain at a tonne a hectare
  field.fertility = 50.0F;
  field.phase = core::FieldPhase::kGrowing;
  field.crop = core::CropId{0};
  core::AppendRow(world.fields, field);

  const auto warned = [&system, &world]() {
    std::vector<core::Alarm> alarms;
    system->CollectAlarms(world, alarms);
    core::Grams total = 0;
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind == core::AlarmKind::kHarvestWillNotFit) {
        total += alarm.amount;
      }
    }
    return total;
  };

  // WITHOUT THE STRAW THIS FIELD IS SILENT: fifty tonnes fit into sixty and
  // nobody is told anything. With it the field asks for a hundred and
  // twenty-five and overruns by sixty-five — which is the whole claim of
  // this check, and the reason a 50 t field into a 60 t barn was chosen.
  failures += Expect(warned() == 65'000 * core::kGramsPerKilogram,
                     "a rye field claims its straw as well: fifty of grain is a hundred and "
                     "twenty-five with it");

  // (The half-cut heap is unreachable BY DESIGN — see the note above; a
  // model that yields no decision is not modelled.)
  // HALF REAPED, AND THE CLAIM DOES NOT MOVE. This block used to say the
  // opposite — that the cut half had already had its straw placed, so only
  // the standing half still claimed any. THAT STATE DOES NOT EXIST in this
  // core: Harvest() runs once, when the phase finishes, and until then not a
  // gram of grain or straw has left the field. A heap standing during kHarvest
  // is LAST year's, not this year's half.
  //
  // Which is why the number below is the same as the one above: nothing has
  // been delivered, so nothing has stopped claiming room. The defect this
  // replaces was measured before it was explained — host, 0.17.58, one day
  // of silence on the last day of reaping.
  world.fields.rows[0].phase = core::FieldPhase::kHarvest;
  world.fields.rows[0].work_days_remaining = 0.5F * (8.0F / core::kRealDaysPerGameDay) * 50.0F;
  failures += Expect(warned() == 65'000 * core::kGramsPerKilogram,
                     "half reaped, the field still claims grain and straw alike: neither has "
                     "left it yet");

  std::filesystem::remove_all(root);
  return failures;
}

/// The alarm burns until the harvest is RESOLVED — stored or lost — and not
/// until the field changes phase.
///
/// host measured the old rule from outside, on 0.17.30: between the warning
/// going dark and the load hitting the ground there was a median of FOUR
/// days of silence, spread three to eight, every single time. The field
/// enters its harvest a few days before the grain lands, the alarm stopped,
/// and the last thing the player saw before losing a crop was the warning
/// going away.
///
/// A SIGNAL THAT SWITCHES OFF JUST BEFORE THE TROUBLE DOES NOT READ AS
/// SILENCE. IT READS AS "IT TURNED OUT FINE." Acting on the last state you
/// were shown is the whole of what a live signal is; this one told the
/// player he was safe four days before he was robbed.
///
/// The estimate still comes from a growing field alone. On the other two
/// states the quantity is known BETTER, not worse — the part still standing
/// and the weight of the heap — which is why there is something to burn on.
int CheckTheWarningBurnsUntilTheHarvestIsResolved() {
  int failures = 0;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_production_burns_on";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "resources.csv") << "key,feed_value\nrye,1.15\n";
  std::ofstream(root / "crops.csv")
      << "key,resource,is_winter,is_perennial,sow_from_month,sow_to_month,sow_min_temp_c,"
         "growth_min_temp_c,harvest_from_month,harvest_to_month,harvest_min_temp_c,"
         "yield_kg_per_ha,sowing_norm_kg_per_ha,fertility_delta,drought_sensitivity,"
         "wet_sensitivity,sow_days_per_ha,harvest_days_per_ha,straw_ratio\n"
         "rye,rye,0,0,4,5,5,5,8,8,2,1000,0,-1,0,0,3,8,0\n";
  std::ofstream(root / "farming.csv")
      << "key,value\nfertility_neutral,50\nmanure_norm_kg_per_ha,20000\n"
         "manure_fertility_bonus,10\nfallow_recovery,6\nrepeat_penalty_per_year,3\n"
         "drought_temp_c,25\nstress_per_day,0.02\nstress_cap,0.3\n"
         "weather_state_days,5\n";
  std::ofstream(root / "unit_types.csv") << "key,capacity_by_plot\nbarn,0\n";
  // Ten tonnes of room against a fifty-tonne field: it will not fit in any
  // of the three states, so the alarm has to speak in all three.
  std::ofstream(root / "unit_levels.csv") << "unit,level,storage_capacity_t\nbarn,1,10\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto system = tables == nullptr
                          ? nullptr
                          : core::CreateProductionSystem(*tables, core::StubTables::kAllowed);
  if (Expect(system != nullptr, "the burning table set builds a production system") != 0) {
    std::cout << error << '\n';
    return 1;
  }

  core::WorldState world;
  core::RefreshCalendarCaches(world.calendar);
  core::UnitRow barn;
  barn.type = core::UnitTypeId{0};
  barn.level = 1;
  core::AppendRow(world.units, barn);
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.area_ga = 50.0F;
  field.fertility = 50.0F;
  field.phase = core::FieldPhase::kGrowing;
  field.crop = core::CropId{0};
  core::AppendRow(world.fields, field);

  const auto warned = [&system, &world]() {
    std::vector<core::Alarm> alarms;
    system->CollectAlarms(world, alarms);
    core::Grams total = 0;
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind == core::AlarmKind::kHarvestWillNotFit) {
        total += alarm.amount;
      }
    }
    return total;
  };

  // Growing: forty of the fifty will not fit.
  failures += Expect(warned() == 40'000 * core::kGramsPerKilogram,
                     "growing and too big for the room: the warning stands");

  // (The half-cut heap is unreachable BY DESIGN — see the note above; a
  // model that yields no decision is not modelled.)
  // Being reaped: the whole fifty is still coming, because nothing leaves a
  // field until its harvest phase finishes. Forty of them still have nowhere
  // to go, exactly as while it stood. THE OLD RULE WENT SILENT HERE, and it
  // went silent for a second reason nobody had named: it counted only what
  // was still on the stalk, so the closer the reaping came to done, the less
  // the field appeared to need.
  world.fields.rows[0].phase = core::FieldPhase::kHarvest;
  world.fields.rows[0].work_days_remaining = 0.5F * (8.0F / core::kRealDaysPerGameDay) * 50.0F;
  failures += Expect(warned() == 40'000 * core::kGramsPerKilogram,
                     "being reaped and still too big: the warning does not go out");

  // All cut, nothing standing, the whole fifty in a heap on the ground.
  world.fields.rows[0].phase = core::FieldPhase::kIdle;
  world.fields.rows[0].work_days_remaining = 0.0F;
  world.fields.rows[0].reaped_grams = 50'000 * core::kGramsPerKilogram;
  failures += Expect(warned() == 40'000 * core::kGramsPerKilogram,
                     "lying in a heap with nowhere to put it: the warning still stands");

  // Carried away: the trouble is over and the alarm goes out. Without this
  // the three checks above would only be proving that it never goes out.
  world.fields.rows[0].reaped_grams = 0;
  failures += Expect(warned() == 0, "carried into a store, the warning goes out");

  std::filesystem::remove_all(root);
  return failures;
}

/// The room is spent in the order the fields will be REAPED, not in row
/// order.
///
/// Row order does the arithmetic just as well — the sum over the warned
/// fields is the same whoever is named — so a check built on one crop
/// cannot tell the two apart. It takes crops reaped in different months,
/// laid out in rows that disagree with those months: then the field the
/// alarm points at is different under the two rules, and the difference is
/// the whole claim. The alarm names ONE field and the player walks to it;
/// an arbitrary field shown as a definite one is a lie (boss, 2026-09-05).
/// kSowingWillNotFit (farming design, «Поздний сев»): the team's days are
/// spent in the order the fields must be sown, a field is named for the
/// square metres it cannot get, no horses means nothing harnessed gets done,
/// and a winter crop and a field already at the (hand) sowing are not asked.
/// THE SNOW TAKES A STANDING FIELD, AND THE BOOK SAYS HOW MUCH (farming
/// design §6; host, econ-host-lever-pass3 seq 35): the standing crop at the
/// harvest's own estimate goes to lost_to_snow, the heap lying there to
/// lost_no_room under ITS resource, the hectares to area_lost_ha, and
/// kFieldLost carries the resource and the grams — it carried nought.
int CheckTheSnowBooksWhatItTakes() {
  int failures = 0;
  constexpr core::Grams kTonne = core::kGramsPerTonne;
  core::ProductionConfig config;
  core::CropDef potato;
  potato.resource = core::ResourceId{1};
  potato.yield_kg_per_ha = 1000.0F;
  config.crops = {potato};
  core::WorldState world;
  core::FieldRow standing;
  standing.kind = core::LandKind::kArable;
  standing.area_ga = 10.0F;
  standing.fertility = 50.0F;  // neutral: the yield is the table's
  standing.phase = core::FieldPhase::kHarvest;
  standing.crop = core::CropId{0};
  standing.reaped_grams = 2 * kTonne;  // last year's heap, of another crop
  standing.reaped_resource = core::ResourceId{0};
  const core::FieldId id = core::AppendRow(world.fields, standing);

  core::FieldRow& field = world.fields.rows[core::FindRow(world.fields, id)];
  core::LoseFieldToSnow(config, world, field, config.crops[0]);

  const core::YearLedger& book = world.ledger.current;
  failures += Expect(book.lost_to_snow.size() > 1 && book.lost_to_snow[1] == 10 * kTonne,
                     "snow: the standing crop is booked at what the harvest would have given");
  failures += Expect(book.lost_no_room.size() > 0 && book.lost_no_room[0] == 2 * kTonne &&
                         book.area_lost_ha == 10.0F,
                     "snow: the heap under its own resource, and the hectares");
  std::int64_t said = -1;
  core::ResourceId said_of;
  for (const core::SimEvent& event : world.step_events) {
    if (event.kind == core::EventKind::kFieldLost && event.field.value == id.value) {
      said = event.amount;
      said_of = event.resource;
    }
  }
  failures += Expect(said == 10 * kTonne && said_of.value == 1,
                     "snow: kFieldLost says what and how much, not nought");
  failures += Expect(field.crop.value == core::kInvalidDefIdValue &&
                         field.phase == core::FieldPhase::kIdle && field.reaped_grams == 0,
                     "snow: the field is left idle, its crop and its heap gone");
  return failures;
}

/// kHarvestWillNotBeGathered (boss seq 78 and 91): the reaping's days are
/// spent in the order the annuals ripen, at the season's best reaping day —
/// or every hand of working age before the season has reaped — and a field
/// that cannot be reaped by the snow is named with the grams the snow takes.
int CheckTheHarvestWillNotBeGathered() {
  int failures = 0;
  core::ProductionConfig config;
  config.growing_season_last_day = 40;
  config.farming.life_speedup = 1.0F;
  config.farming.adult_age_years = 16.0F;
  core::CropDef potato;
  potato.resource = core::ResourceId{1};
  potato.yield_kg_per_ha = 1000.0F;
  potato.sow_to_month = 4;        // sown by day 19
  potato.harvest_from_month = 8;  // reaped from day 32: ripens in 13 days
  potato.harvest_to_month = 8;
  potato.harvest_days_per_ha = 5.0F;  // 50 norm-days on 10 ha
  core::CropDef oat = potato;
  oat.resource = core::ResourceId{0};
  oat.harvest_from_month = 7;      // reaped from day 28: ripens in 9 days
  oat.harvest_days_per_ha = 2.0F;  // 20 norm-days on 10 ha
  config.crops = {potato, oat};
  core::WorldState world;
  world.calendar.tick = 30 * static_cast<core::Tick>(core::kTicksPerDay);
  core::RefreshCalendarCaches(world.calendar);
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.area_ga = 10.0F;
  field.fertility = 50.0F;
  field.phase = core::FieldPhase::kGrowing;
  field.crop = core::CropId{0};
  field.sown_day = 19;  // ripe on day 32
  const core::FieldId id = core::AppendRow(world.fields, field);
  // The oat ripens FIRST and stands SECOND in row order: the pair is what
  // tells the ripening order from the row order.
  core::FieldRow oats = field;
  oats.crop = core::CropId{1};  // ripe on day 28, open today
  core::AppendRow(world.fields, oats);
  const auto warned = [&config, &world, id]() {
    std::vector<core::Alarm> alarms;
    core::CollectGatherAlarms(config, world, alarms);
    std::int64_t grams = 0;
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind == core::AlarmKind::kHarvestWillNotBeGathered &&
          alarm.field.value == id.value) {
        grams += alarm.amount;
      }
    }
    return grams;
  };
  // Day 30, snow after 40. At the season's 5 norm-days a day the oat, open
  // today, takes four (30..33); the potato, ripe on 32, starts on 34 with
  // seven days left and needs ten: three tenths of 10 t go to the snow.
  // Reaped in row order it would start on 32 and lose one tenth.
  world.ledger.current.reaping_best_day = 5.0F;
  failures += Expect(warned() == 3'000'000,
                     "gather: the potato ripening after the oat finds the days it took, and it is "
                     "said before the potato is ripe");
  world.ledger.current.reaping_best_day = 10.0F;
  failures += Expect(warned() == 0, "gather: at twice the pace both are reaped in time");
  // No reaping yet this season: every hand of working age, one norm-day each.
  world.ledger.current.reaping_best_day = 0.0F;
  failures += Expect(warned() == 10'000'000,
                     "gather: before the season's first reaping, no hands means the whole crop");
  // Three adults and a child in a tent (a home without a house): three hands.
  // The oat takes 6.67 days, the potato starts at 36.67 with 4.33 left of the
  // 16.67 it needs — 74 % of 10 t. Counting the child would make it 52 %.
  core::FamilyRow family;
  family.in_tent = 1;
  const core::FamilyId household = core::AppendRow(world.families, family);
  for (const std::int32_t age_years : {30, 25, 40, 5}) {
    core::ResidentRow person;
    person.family = household;
    person.birth_day = 30 - (age_years * static_cast<std::int32_t>(core::kDaysPerYear));
    core::AppendRow(world.residents, person);
  }
  const std::int64_t by_hands = warned();
  failures += Expect(by_hands > 7'300'000 && by_hands < 7'500'000,
                     "gather: before the season reaps, the hands of working age are the pace");
  return failures;
}

int CheckTheSowingWillNotFit() {
  int failures = 0;
  core::ProductionConfig config;
  config.growing_season_last_day = 40;
  config.farming.harrow_days_per_ha = 0.5F;
  config.horse_kind = core::LivestockKindId{0};
  const auto crop = [&config](std::uint16_t resource, std::uint8_t reap_from, bool winter) {
    core::CropDef def;
    def.resource = core::ResourceId{resource};
    def.is_winter = winter;
    def.sow_to_month = 4;  // last sowing day of the window: 19
    def.harvest_from_month = reap_from;
    config.crops.push_back(def);
  };
  crop(0, 7, false);  // oat: ripens in 28 − 19 = 9 days, so sown by day 31
  crop(1, 8, false);  // potato: 13 days, so sown by day 27
  crop(2, 7, true);   // rye: a winter crop, the snow does not gate it
  core::WorldState world;
  world.calendar.tick = 20 * static_cast<core::Tick>(core::kTicksPerDay);
  core::RefreshCalendarCaches(world.calendar);
  core::HerdRow team;
  team.kind = core::LivestockKindId{0};
  team.adult_count = 2;
  const core::HerdId team_id = core::AppendRow(world.herds, team);
  const auto field = [&world](std::uint16_t crop_id, core::FieldPhase phase, float ha, float owed) {
    core::FieldRow row;
    row.kind = core::LandKind::kArable;
    row.area_ga = ha;
    row.phase = phase;
    row.crop = core::CropId{crop_id};
    row.work_days_remaining = owed;
    return core::AppendRow(world.fields, row);
  };
  // Day 20. The oat has 12 days, the potato 8. Row order is NOT sowing order:
  // the potato, due first, is the third row.
  const core::FieldId oat_plough = field(0, core::FieldPhase::kPlowing, 10.0F, 14.0F);  // 19 d
  const core::FieldId oat_harrow = field(0, core::FieldPhase::kHarrowing, 8.0F, 9.0F);  // 9 d
  const core::FieldId potato = field(1, core::FieldPhase::kPlowing, 4.0F, 6.0F);        // 8 d
  const core::FieldId rye = field(2, core::FieldPhase::kPlowing, 1.0F, 100.0F);
  const core::FieldId at_sowing = field(0, core::FieldPhase::kSowing, 5.0F, 50.0F);

  const auto short_of = [&config, &world](core::FieldId id) {
    std::vector<core::Alarm> alarms;
    core::CollectSowingAlarms(config, world, alarms);
    std::int64_t square_metres = 0;
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind == core::AlarmKind::kSowingWillNotFit && alarm.field.value == id.value) {
        square_metres += alarm.amount;
      }
    }
    return square_metres;
  };
  // Two horses. The potato takes 4 days of its 8 and fits; the ploughed oat
  // then needs 9.5 of the 8 left, short 1.5/9.5 of 10 ha; the harrowed oat
  // finds nothing left and is short whole.
  failures += Expect(short_of(potato) == 0,
                     "sowing: the field due first takes the team's days first, and fits");
  failures += Expect(short_of(oat_plough) == 15'789,
                     "sowing: the next field is short by what the days left cannot cover");
  failures += Expect(short_of(oat_harrow) == 80'000,
                     "sowing: the last field finds the days gone — the whole of it short");
  failures += Expect(short_of(rye) == 0 && short_of(at_sowing) == 0,
                     "sowing: a winter crop and a field at the hand sowing are not asked");
  world.herds.rows[core::FindRow(world.herds, team_id)].adult_count = 10;
  failures +=
      Expect(short_of(potato) == 0 && short_of(oat_plough) == 0 && short_of(oat_harrow) == 0,
             "sowing: with ten horses it all fits, and nothing is said");
  world.herds.rows[core::FindRow(world.herds, team_id)].adult_count = 0;
  failures += Expect(short_of(potato) == 40'000 && short_of(oat_plough) == 100'000 &&
                         short_of(oat_harrow) == 80'000,
                     "sowing: with no horses nothing harnessed is done — every field short whole");
  return failures;
}

int CheckTheRoomIsSpentInHarvestOrder() {
  int failures = 0;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_production_harvest_order";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "resources.csv") << "key,feed_value\nrye,1.15\noat,1\npotato,0.3\n";
  // Three crops reaped in three different months: oat in the sixth, rye in
  // the eighth, potato in the ninth.
  std::ofstream(root / "crops.csv")
      << "key,resource,is_winter,is_perennial,sow_from_month,sow_to_month,sow_min_temp_c,"
         "growth_min_temp_c,harvest_from_month,harvest_to_month,harvest_min_temp_c,"
         "yield_kg_per_ha,sowing_norm_kg_per_ha,fertility_delta,drought_sensitivity,"
         "wet_sensitivity,sow_days_per_ha,harvest_days_per_ha,straw_ratio\n"
         "rye,rye,0,0,4,5,5,5,8,8,2,1000,0,-1,0,0,3,8,0\n"
         "oat,oat,0,0,4,5,5,5,6,6,2,1000,0,-1,0,0,3,8,0\n"
         "potato,potato,0,0,4,5,5,5,9,9,2,1000,0,-1,0,0,3,8,0\n";
  std::ofstream(root / "farming.csv")
      << "key,value\nfertility_neutral,50\nmanure_norm_kg_per_ha,20000\n"
         "manure_fertility_bonus,10\nfallow_recovery,6\nrepeat_penalty_per_year,3\n"
         "drought_temp_c,25\nstress_per_day,0.02\nstress_cap,0.3\n"
         "weather_state_days,5\n";
  std::ofstream(root / "unit_types.csv") << "key,capacity_by_plot\nbarn,0\n";
  std::ofstream(root / "unit_levels.csv") << "unit,level,storage_capacity_t\nbarn,1,60\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto system = tables == nullptr
                          ? nullptr
                          : core::CreateProductionSystem(*tables, core::StubTables::kAllowed);
  if (Expect(system != nullptr, "the harvest-order table set builds a production system") != 0) {
    std::cout << error << '\n';
    return 1;
  }

  core::WorldState world;  // January: every harvest month is still ahead
  core::RefreshCalendarCaches(world.calendar);
  core::UnitRow barn;
  barn.type = core::UnitTypeId{0};
  barn.level = 1;
  core::AppendRow(world.units, barn);
  // Rows deliberately out of harvest order: potato (reaped last) is row 0,
  // oat (reaped first) is row 1, rye is row 2.
  std::vector<core::FieldId> field_ids;
  for (const std::uint16_t crop : {std::uint16_t{2}, std::uint16_t{1}, std::uint16_t{0}}) {
    core::FieldRow field;
    field.kind = core::LandKind::kArable;
    field.area_ga = 50.0F;
    field.fertility = 50.0F;
    field.phase = core::FieldPhase::kGrowing;
    field.crop = core::CropId{crop};
    field_ids.push_back(core::AppendRow(world.fields, field));
  }

  std::vector<core::Alarm> alarms;
  system->CollectAlarms(world, alarms);
  const auto warned = [&alarms](const core::FieldId field) {
    core::Grams total = 0;
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind == core::AlarmKind::kHarvestWillNotFit && alarm.field.value == field.value) {
        total += alarm.amount;
      }
    }
    return total;
  };

  // The oat comes in first and fits; the rye finds ten tonnes of room left;
  // the potato, reaped last, finds none. Under row order the silent field
  // would be the potato and the empty-handed one the rye — the exact swap
  // this check exists to see.
  failures += Expect(warned(field_ids[1]) == 0,
                     "the oat is reaped first and fits: it is not the field to walk to");
  failures += Expect(warned(field_ids[2]) == 40'000 * core::kGramsPerKilogram,
                     "the rye comes next and overruns by what the oat left");
  failures += Expect(warned(field_ids[0]) == 50'000 * core::kGramsPerKilogram,
                     "and the potato, last in, finds the room gone — the whole of it over");

  std::filesystem::remove_all(root);
  return failures;
}

/// A capacity that no level row answers for must STOP the load, not read as
/// zero.
///
/// This is the check that had to be built before the fallback could go. The
/// export wrote `LEFT JOIN unit_level ON level = 1`, so unit_types.csv
/// carried a copy of level 1 and the fallback reading it was an arm that
/// could not differ from its control — live-looking and never plugged in.
/// Taking it out turns "no level row" from a wrong-but-plausible number into
/// a zero, and a store that holds nothing looks exactly like a store nobody
/// filled: host nearly concluded that capacity means nothing from precisely
/// that shape. So the load refuses, and this test is the proof the refusal
/// fires — three shapes, one of which must still pass, or the test would
/// only be proving that nothing loads.
int CheckCapacityWithoutALadderIsRefused() {
  int failures = 0;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_production_ladder";
  const auto loads = [&root](const char* types, const char* levels) {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / "unit_types.csv") << types;
    if (levels != nullptr) {
      std::ofstream(root / "unit_levels.csv") << levels;
    }
    std::string error;
    const auto tables = core::LoadTableSet(root.string(), &error);
    return tables != nullptr &&
           core::CreateProductionSystem(*tables, core::StubTables::kAllowed) != nullptr;
  };
  failures += Expect(!loads("key,storage_capacity_t\nbarn,9\n", nullptr),
                     "a capacity with no ladder at all refuses the load");
  failures += Expect(!loads("key,storage_capacity_t\nbarn,9\n",
                            "unit,level,storage_capacity_t\nbarn,1,\nbarn,2,\n"),
                     "a capacity whose ladder names none refuses the load");
  failures += Expect(!loads("key,storage_capacity_t\nbarn,9\n",
                            "unit,level,storage_capacity_t\nbarn,1,1\nbarn,2,\n"),
                     "a blank step among named ones refuses the load");
  failures += Expect(!loads("key,livestock_capacity_head\ncattle_yard,24\n",
                            "unit,level,storage_capacity_t\ncattle_yard,1,\n"),
                     "a head count with no ladder behind it refuses the load too");
  failures += Expect(loads("key,storage_capacity_t\nbarn,9\n",
                           "unit,level,storage_capacity_t\nbarn,1,1\nbarn,2,5\n"),
                     "a ladder that answers for every step still loads");
  // And the column may simply be GONE from unit_types.csv: it is on its way
  // out of the export, and the loader must not be what breaks when it goes.
  failures += Expect(loads("key\nbarn\n", "unit,level,storage_capacity_t\nbarn,1,1\nbarn,2,5\n"),
                     "the type column is not required at all");
  std::filesystem::remove_all(root);
  return failures;
}

}  // namespace

/// The turn of the start canon (task A7; manual/74-posts.md §5): the yard is
/// built, a groom is appointed — and the team comes in off the private
/// yards, all of it, once and for good.
int CheckHorsesComeInWhenAGroomIsAppointed() {
  int failures = 0;
  core::ProductionConfig config = MakeHerdConfig();
  config.horse_kind = core::LivestockKindId{0};  // "cow" plays the horse here
  config.groom_post = core::ProfessionId{3};     // any post id; the key is data
  SetLivestockHead(config.unit_types[0], 100.0F);

  // A world of three private yards, each hosting part of the kolkhoz team,
  // and a built kolkhoz yard nobody has been appointed to yet.
  const auto build = [&config](bool with_groom) {
    core::WorldState world;
    core::UnitRow yard;
    yard.level = 2;
    const core::UnitId yard_id = core::AppendRow(world.units, yard);
    core::FamilyRow household;
    const core::FamilyId family = core::AppendRow(world.families, household);
    for (std::uint32_t index = 0; index < 3; ++index) {
      core::HerdRow team;
      team.kind = config.horse_kind;
      team.household = family;
      team.adult_count = 5;
      team.adult_age_game_years_total = 15.0F;
      core::AppendRow(world.herds, team);
    }
    core::ResidentRow groom;
    groom.family = family;
    if (with_groom) {
      groom.post.profession = config.groom_post;
      groom.post.unit = yard_id;
    }
    core::AppendRow(world.residents, groom);
    world.rng.state = 12345;
    return std::pair<core::WorldState, core::UnitId>{world, yard_id};
  };

  // -- no groom: nothing happens, and that is the design's own answer -------
  auto [waiting, waiting_yard] = build(false);
  core::RunHerdDay(config, waiting);
  failures += Expect(waiting.herds.rows.size() == 3 && waiting.chairman.horses_stabled == 0,
                     "a yard without a groom leaves the team where it stands");

  // -- appointed: one herd, at the yard, and the milestone is set -----------
  auto [stabled, yard_id] = build(true);
  core::RunHerdDay(config, stabled);
  failures += Expect(stabled.herds.rows.size() == 1, "the team comes in as one herd, not three");
  failures += Expect(stabled.chairman.horses_stabled == 1, "and the campaign's milestone is set");
  bool announced = false;
  std::int64_t heads = 0;
  for (const core::SimEvent& event : stabled.step_events) {
    if (event.kind == core::EventKind::kHorsesStabled) {
      announced = true;
      heads = event.amount;
    }
  }
  failures += Expect(announced && heads == 15, "the day the village hears about: fifteen head");
  if (!stabled.herds.rows.empty()) {
    const core::HerdRow& team = stabled.herds.rows[0];
    failures += Expect(
        team.unit.value == yard_id.value && team.household.value == core::kInvalidEntityIdValue,
        "standing at the yard now, and at nobody's household");
    failures += Expect(team.adult_count == 15, "with every head of the three that came in");
    // The sire count is a herd invariant and must be RE-DERIVED, not summed:
    // three lone herds were each their own stallion.
    failures += Expect(team.adult_male_count < 15,
                       "and one team of mares and sires, not fifteen stallions");
  }

  // -- and only once ------------------------------------------------------
  const std::size_t events_before = stabled.step_events.size();
  core::RunHerdDay(config, stabled);
  bool announced_twice = false;
  for (std::size_t index = events_before; index < stabled.step_events.size(); ++index) {
    announced_twice =
        announced_twice || stabled.step_events[index].kind == core::EventKind::kHorsesStabled;
  }
  failures += Expect(!announced_twice, "the horses are gathered once in a campaign");
  return failures;
}

/// Pause and resume (task A8): the two verbs of the order book that stop a
/// unit and start it again.
///
/// Driven through the SUBSYSTEM, not through a helper, because the claim
/// includes WHERE the book is read: before the early return that a world
/// with no crops takes. A pause is about a unit, and "the tables were thin"
/// is not a reason the chairman should ever be shown.
/// kHerdWithoutStable: the team has a roof to breed under, or it does not.
///
/// FOUR STATES AND ONE OF THEM IS THE WHOLE POINT. A yard at step one with a
/// groom in it is the position the player reaches by doing exactly what the
/// office told him — and kYardWithoutGroom goes out there, correctly, while
/// the team goes on dying. This kind must still be burning at that moment or
/// the silence lies (alarm_state.h; boss, 2026-09-05).
int CheckTheTeamWithoutARoofSaysSo() {
  int failures = 0;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_production_stable";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "resources.csv") << "key,feed_value\nhay,1\n";
  std::ofstream(root / "livestock.csv")
      << "key,sexed,kolkhoz_only,household_only,feed_units_per_real_day,care_days_per_real_year,"
         "pasture_coverage_summer,newborn_game_months,adult_from_game_months,life_game_years_min,"
         "life_game_years_max,births_per_game_year,litter_heads,males_share\n"
         "horse,1,1,0,10,22,0.5,2,12,6,8,1,1,0.07\n"
         "cow,1,0,0,10,22,0.5,2,12,6,8,1,1,0.07\n";
  std::ofstream(root / "unit_types.csv")
      << "key,storage_capacity_t,capacity_by_plot\nhorse_yard,0,0\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto system = tables == nullptr
                          ? nullptr
                          : core::CreateProductionSystem(*tables, core::StubTables::kAllowed);
  if (Expect(system != nullptr, "the stable table set builds a production system") != 0) {
    std::cout << error << '\n';
    return 1;
  }

  const auto burning = [&](const core::WorldState& world) {
    std::vector<core::Alarm> alarms;
    system->CollectAlarms(world, alarms);
    std::int64_t heads = -1;
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind == core::AlarmKind::kHerdWithoutStable) {
        heads = alarm.amount;
      }
    }
    return heads;
  };

  core::WorldState world;
  core::RefreshCalendarCaches(world.calendar);
  core::UnitRow yard;
  yard.type = core::UnitTypeId{0};
  yard.level = 0;
  core::AppendRow(world.units, yard);
  failures += Expect(burning(world) < 0, "a farm with no horses is not asked for a stable");

  // A family's own mare is not the chairman's business, and neither is
  // anybody's cow.
  core::HerdRow mare;
  mare.kind = core::LivestockKindId{0};
  mare.adult_count = 2;
  mare.household_owned = 1;
  core::AppendRow(world.herds, mare);
  core::HerdRow cow;
  cow.kind = core::LivestockKindId{1};
  cow.adult_count = 9;
  core::AppendRow(world.herds, cow);
  failures += Expect(burning(world) < 0, "nor a farm whose only horses are somebody's own");

  // The team: two rows, as the start really keeps it — sixteen billets, not
  // one herd — and the alarm is ONE line counting all of it.
  core::HerdRow team;
  team.kind = core::LivestockKindId{0};
  team.adult_count = 5;
  const core::HerdId first = core::AppendRow(world.herds, team);
  team.adult_count = 3;
  team.juvenile_count = 1;
  core::AppendRow(world.herds, team);
  failures += Expect(burning(world) == 9, "the team's own head count, over all its rows");
  {
    std::vector<core::Alarm> alarms;
    system->CollectAlarms(world, alarms);
    std::uint32_t lines = 0;
    core::HerdId subject;
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind == core::AlarmKind::kHerdWithoutStable) {
        ++lines;
        subject = alarm.herd;
      }
    }
    failures += Expect(lines == 1, "one line for the team, not one per billet");
    failures += Expect(subject.value == first.value, "and it names the team's first row");
  }

  world.units.rows[0].level = 1;
  failures += Expect(burning(world) == 9, "a yard at step one is a pen: the ask stands");
  // The groom's appointment is what silences kYardWithoutGroom. It changes
  // nothing here, and that is the defect this kind exists for.
  world.chairman.horses_stabled = 1;
  failures += Expect(burning(world) == 9, "and stabling the horses does not answer it either");
  world.units.rows[0].level = 2;
  failures += Expect(burning(world) < 0, "the second step answers it, and only the second step");
  return failures;
}

/// A HEAP UNDER THE OPEN SKY IS A STORE, and the whole village turns on it.
///
/// Five outline-bounded types exist (threshing floor, manure heap, silage
/// trench, firewood yard, summer camp), and until 2026-09-07 StoresGoods
/// dropped every one of them: it read the LADDER, an outline leaves that
/// cell blank on purpose, and a blank read as a zero is exactly what
/// StorageCapacityGrams forbids in as many words. Two readers of one
/// property, one doing what the other forbids.
///
/// Nothing in the shipped thirty-year run moved when this was repaired —
/// the run is identical to the byte — which is the reason this test had to
/// be written rather than a run pointed at. The settlement it takes to see
/// the defect is one the tables do not build today: a numbered store that
/// is FULL, standing beside a heap that holds the very thing being carried.
/// THE DOOR COUNTS WHAT LANDED, NOT WHAT IT MEANT TO PUT IN.
///
/// DeliverToStores used to add its own `take` to the running total the
/// moment it called AddToStock, and AddToStock refuses an unnamed resource
/// in silence — rightly, because there is no column for it. The two together
/// answered "delivered" for a load that was never written, and the caller
/// then destroyed it against that number: SettleHauling subtracts the answer
/// from the field, DeliverHarvest books the rest as lost.
///
/// The parse now refuses the misspelt key that produced the unnamed resource
/// (core_catalog/table_lookup.h). This is the second lock, and it is the one
/// that survives the next reason to refuse a write: the cause went away, the
/// habit would not have.
int CheckTheDoorCountsWhatLanded() {
  int failures = 0;
  core::ProductionConfig config;
  config.unit_types.resize(1);
  SetStorageKg(config.unit_types[0], 1000.0F);  // one tonne, in KILOGRAMS

  core::WorldState world;
  core::UnitRow barn;
  barn.type = core::UnitTypeId{0};
  barn.level = 1;
  barn.stock.assign(2, 0);
  core::AppendRow(world.units, barn);

  // An UNNAMED resource: nothing can be written, so nothing may be claimed.
  const core::Grams unnamed =
      core::DeliverToStores(world, config, core::ResourceId{}, 500 * core::kGramsPerKilogram);
  failures += Expect(unnamed == 0, "a load with no resource is not delivered anywhere");
  failures += Expect(world.units.rows[0].stock[0] == 0 && world.units.rows[0].stock[1] == 0,
                     "and nothing was written under any id");

  // A named one still goes in, and the count is the same number as before.
  const core::Grams named =
      core::DeliverToStores(world, config, core::ResourceId{0}, 500 * core::kGramsPerKilogram);
  failures += Expect(named == 500 * core::kGramsPerKilogram &&
                         world.units.rows[0].stock[0] == 500 * core::kGramsPerKilogram,
                     "a named load goes in, and the door reports exactly what went in");

  // And the ceiling still binds: half a tonne fits, the rest is refused.
  const core::Grams over =
      core::DeliverToStores(world, config, core::ResourceId{0}, 900 * core::kGramsPerKilogram);
  failures += Expect(over == 500 * core::kGramsPerKilogram,
                     "the ceiling refuses the remainder, and the door says how much it took");
  return failures;
}

/// A NUMBERED STORE TAKES ITS HOMES ONLY (boss, parcels 399-405): with
/// resource_stores.csv read, a granary keeps grain and a food store potatoes,
/// and neither counts as room for the other's load — the demand and the door
/// ask the same question. Before, the homes bound outlines alone and 143 t of
/// potatoes lay in granaries.
int CheckANumberedStoreTakesItsHomesOnly() {
  int failures = 0;
  core::ProductionConfig config;
  config.resource_stores_read = 1;
  config.unit_types.resize(2);
  SetStorageKg(config.unit_types[0], 1000.0F);  // the granary: a tonne, rye's home
  config.unit_types[0].home_of = {core::ResourceId{0}};
  SetStorageKg(config.unit_types[1], 400.0F);  // the food store: 400 kg, potato's home
  config.unit_types[1].home_of = {core::ResourceId{1}};

  core::WorldState world;
  core::UnitRow granary;
  granary.type = core::UnitTypeId{0};
  granary.level = 1;
  core::AppendRow(world.units, granary);
  core::UnitRow food_store;
  food_store.type = core::UnitTypeId{1};
  food_store.level = 1;
  core::AppendRow(world.units, food_store);
  const core::Grams kKilo = core::kGramsPerKilogram;

  failures += Expect(core::ReceivableRoom(config, world, core::ResourceId{1}) == 400 * kKilo,
                     "a potato load's room is the food store's alone, not the granary's tonne");
  const core::Grams potatoes =
      core::DeliverToStores(world, config, core::ResourceId{1}, 600 * kKilo);
  failures +=
      Expect(potatoes == 400 * kKilo &&
                 core::AmountOf(world.units.rows[0].stock, core::ResourceId{1}) == 0 &&
                 core::AmountOf(world.units.rows[1].stock, core::ResourceId{1}) == 400 * kKilo,
             "the door puts potatoes into the food store only, and refuses the rest past "
             "an empty granary");
  const core::Grams rye = core::DeliverToStores(world, config, core::ResourceId{0}, 300 * kKilo);
  failures += Expect(rye == 300 * kKilo && core::AmountOf(world.units.rows[0].stock,
                                                          core::ResourceId{0}) == 300 * kKilo,
                     "and rye into the granary");
  // A resource no store is the home of: nowhere to go, not the first store.
  failures += Expect(core::ReceivableRoom(config, world, core::ResourceId{2}) == 0 &&
                         core::DeliverToStores(world, config, core::ResourceId{2}, kKilo) == 0,
                     "a load with no home among the stores has no room and goes in nowhere");
  return failures;
}

int CheckAHeapIsAStore() {
  int failures = 0;

  // Two types: a barn of one tonne by the ladder, and a haystack bounded by
  // the outline the player draws — no ladder at all, which is the correct
  // table row for it and not a gap in one.
  core::ProductionConfig config;
  config.unit_types.resize(2);
  SetStorageKg(config.unit_types[0], 1.0F);
  config.unit_types[1].capacity_by_plot = 1;

  // The haystack stands FIRST in row order, and that is the point of it: a
  // delivery needs a destination with a number to clamp against, and the
  // only thing that used to keep FindStorageRow off the heap was the defect.
  core::WorldState world;
  core::UnitRow stack;
  stack.type = core::UnitTypeId{1};
  stack.level = 1;
  stack.stock.assign(2, 0);
  stack.stock[0] = 500 * core::kGramsPerKilogram;  // half a tonne of rye lies in it
  const core::UnitId stack_id = core::AppendRow(world.units, stack);
  core::UnitRow barn;
  barn.type = core::UnitTypeId{0};
  barn.level = 1;
  barn.stock.assign(2, 0);
  barn.stock[0] = 1000 * core::kGramsPerKilogram;  // and the barn is full to its tonne
  const core::UnitId barn_id = core::AppendRow(world.units, barn);

  failures += Expect(core::StoresGoods(world.units.rows[0], config),
                     "a heap the player outlined keeps goods for the settlement");
  failures +=
      Expect(core::FindStorageRow(world, config) == 1,
             "but a DELIVERY still goes to the barn: a heap has no number to clamp against");

  // Rye already lies in the heap, so a load of rye has somewhere to go even
  // with every numbered store shut — this is the state the hauling cap was
  // written to answer, and the one it could not reach.
  failures += Expect(core::ReceivableRoom(config, world, core::ResourceId{0}) ==
                         std::numeric_limits<core::Grams>::max(),
                     "with rye in the heap, a load of rye has somewhere to go");
  // And the same heap is no room at all for oats: the door takes a load into
  // an outline only when that resource already lies in it, so the demand
  // must ask the door's question and not a wider one.
  failures += Expect(core::ReceivableRoom(config, world, core::ResourceId{1}) == 0,
                     "and no room at all for oats, which the heap is not the home of");

  // The door itself, unchanged by any of this and standing here as the
  // subject the two answers above have to agree with.
  const core::Grams placed =
      core::DeliverToStores(world, config, core::ResourceId{0}, 200 * core::kGramsPerKilogram);
  failures += Expect(placed == 200 * core::kGramsPerKilogram &&
                         world.units.rows[0].stock[0] == 700 * core::kGramsPerKilogram,
                     "and the door puts the load in that heap, past the barn that is full");
  failures += Expect(stack_id.value != barn_id.value, "the two units are two units");
  return failures;
}

// THE DISTRICT'S VERDICT ON THE YEAR (district design §9, epochs design §8).
//
// What these checks are really about: until 2026-09-12 the plan accrued as a
// share of the settlement's own reaping, so what was owed WAS what had been
// cut, every year was met by construction, and a counter of failed years on
// such a plan would have been a structural zero wearing the clothes of a
// check. So the first thing asserted here is that a year CAN fail — and each
// check below is shaped so that a plan which cannot fail would redden it.
// THE CHAIRMAN'S DOOR OUT OF A SEALED FUND (kUnsealFund; resources design
// §6: "Распечатать фонд не по назначению может только председатель —
// отдельным решением, в чрезвычайной ситуации").
//
// The verb exists because the reserve became real and the way out of it did
// not: a village with grain it may not touch and no order that touches it
// breaks "никаких безвыходных ситуаций" literally.
int CheckTheChairmanCanUnsealAFund() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  if (Expect(tables != nullptr, "the shipped tables load for the fund door") != 0) {
    return 1;
  }
  const auto system = core::CreateProductionSystem(*tables, core::StubTables::kRefused);
  if (Expect(system != nullptr, "and they build a production system") != 0) {
    return 1;
  }
  const core::ITable* const resources = tables->FindTable("resources");
  const core::ResourceId rye{static_cast<std::uint16_t>(resources->FindRowByKey("rye"))};

  const auto order_unseal = [&](core::FundKind fund, core::Grams owed, core::Grams asked) {
    core::WorldState previous;
    previous.calendar.tick = 10U * core::kTicksPerDay;
    core::RefreshCalendarCaches(previous.calendar);
    if (owed > 0) {
      previous.plan.due.assign(static_cast<std::size_t>(rye.value) + 1U, 0);
      previous.plan.due[rye.value] = owed;
    }
    core::OrderRow order;
    order.kind = core::OrderKind::kUnsealFund;
    order.fund = fund;
    order.resource = rye;
    order.amount = asked;
    core::AppendRow(previous.orders, order);

    core::WorldState current = previous;
    current.calendar.tick += 1;
    core::RefreshCalendarCaches(current.calendar);
    system->RunProductionDecisions(previous, current);
    return current;
  };

  // -- a winter unsealing survives the spring -------------------------------
  //
  // The spring is when the district names the new norm, and for one
  // afternoon it also wiped the releases — so a seed fund opened in the
  // hungry end of winter was forgotten on the very day the sowing year it
  // was opened for began. Found by the delivery cycle, not by reading.
  {
    core::WorldState previous;
    previous.calendar.tick = (2U * core::kDaysPerMonth * core::kTicksPerDay) - 1U;
    core::RefreshCalendarCaches(previous.calendar);
    previous.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kSeed)].assign(
        static_cast<std::size_t>(rye.value) + 1U, 0);
    previous.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kSeed)][rye.value] = 500'000;
    core::WorldState current = previous;
    current.calendar.tick += 1;
    core::RefreshCalendarCaches(current.calendar);
    failures += Expect(current.calendar.season == core::Season::kSpring &&
                           previous.calendar.season != core::Season::kSpring,
                       "the fixture crosses into spring");
    system->RunProductionDecisions(previous, current);
    failures += Expect(
        current.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kSeed)][rye.value] ==
            500'000,
        "a seed fund opened in winter is still open when the sowing begins");
  }

  // A settlement WITH a kolkhoz horse herd: only then does the fodder fund
  // hold anything at all, because its size is the harness's year of work
  // ration. A fixture with no herds is the sharpest form of the other check
  // below: whatever the chairman names, there is no fund to name it out of.
  const auto order_unseal_with_horses = [&](core::FundKind fund, core::Grams asked) {
    core::WorldState previous;
    previous.calendar.tick = 10U * core::kTicksPerDay;
    core::RefreshCalendarCaches(previous.calendar);
    const core::ITable* const livestock = tables->FindTable("livestock");
    core::HerdRow horses;
    horses.kind =
        core::LivestockKindId{static_cast<std::uint16_t>(livestock->FindRowByKey("horse"))};
    horses.adult_count = 40;
    core::AppendRow(previous.herds, horses);
    core::OrderRow order;
    order.kind = core::OrderKind::kUnsealFund;
    order.fund = fund;
    order.resource = rye;
    order.amount = asked;
    core::AppendRow(previous.orders, order);
    core::WorldState current = previous;
    current.calendar.tick += 1;
    core::RefreshCalendarCaches(current.calendar);
    system->RunProductionDecisions(previous, current);
    return current;
  };

  // -- the plan reserve opens, and by the figure the chairman named --------
  {
    const core::WorldState after = order_unseal(core::FundKind::kPlanReserve, 900'000, 400'000);
    failures += Expect(after.orders.rows[0].status == core::OrderStatus::kDone,
                       "the chairman may open the plan reserve in an emergency");
    const core::Grams opened =
        rye.value < after.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kPlanReserve)]
                        .size()
            ? after.unsealed
                  .by_fund[static_cast<std::size_t>(core::FundKind::kPlanReserve)][rye.value]
            : 0;
    failures += Expect(opened == 400'000,
                       "and exactly the figure he named comes out, not as much as is there — "
                       "the design calls the alternative a leak, not a decision");
    failures += Expect(
        after.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kSeed)].empty() ||
            after.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kSeed)][rye.value] == 0,
        "and the seed fund he did not name stays shut");
  }

  // -- past what the district asked for, the door refuses ------------------
  {
    const core::WorldState after = order_unseal(core::FundKind::kPlanReserve, 900'000, 1'000'000);
    failures += Expect(after.orders.rows[0].status == core::OrderStatus::kRefused,
                       "the plan reserve cannot be opened past what the district asked for");
    failures += Expect(after.orders.rows[0].refusal == core::OrderRefusal::kRuleForbids,
                       "and the chairman is told why rather than quietly given what there is");
  }

  // -- before the district has named the plan, the reserve says so ---------
  //
  // THE WINDOW IS REAL AND IT IS EIGHT DAYS OF FORTY-EIGHT: JudgePlan clears
  // plan.due at the year's turn, AnnouncePlan fills it again on the first day
  // of spring, and in between the share this door is measured by has no
  // number behind it. Until 2026-09-12 the answer was kRuleForbids, which
  // sent a chairman looking for a rule that does not exist — the fund is not
  // empty and the design says plainly that he may open it in a hungry winter.
  {
    const core::WorldState after = order_unseal(core::FundKind::kPlanReserve, 0, 400'000);
    failures += Expect(after.orders.rows[0].status == core::OrderStatus::kRefused,
                       "a plan reserve that has no plan behind it yet opens nothing");
    failures +=
        Expect(after.orders.rows[0].refusal == core::OrderRefusal::kNoPlanYet,
               "and the reason is that the plan has not been named, not that a rule forbids — "
               "the move it teaches is to wait for the spring announcement");
  }

  // -- but a plan that simply asks for something else is still a plan -------
  //
  // The two are told apart by the WHOLE vector and not by this resource, and
  // that is the half a narrower check would have got wrong: a district that
  // names oats and no rye HAS named a plan, and answering "no plan yet" to
  // the rye would be a lie about it. Here the plan names rye at 900 kg, the
  // chairman asks for wheat, and the answer must be the ordinary ceiling.
  {
    const core::ResourceId other{static_cast<std::uint16_t>(resources->FindRowByKey("wheat"))};
    core::WorldState previous;
    previous.calendar.tick = 10U * core::kTicksPerDay;
    core::RefreshCalendarCaches(previous.calendar);
    previous.plan.due.assign(static_cast<std::size_t>(rye.value) + 1U, 0);
    previous.plan.due[rye.value] = 900'000;
    core::OrderRow order;
    order.kind = core::OrderKind::kUnsealFund;
    order.fund = core::FundKind::kPlanReserve;
    order.resource = other;
    order.amount = 400'000;
    core::AppendRow(previous.orders, order);
    core::WorldState current = previous;
    current.calendar.tick += 1;
    core::RefreshCalendarCaches(current.calendar);
    system->RunProductionDecisions(previous, current);
    failures += Expect(current.orders.rows[0].refusal == core::OrderRefusal::kRuleForbids,
                       "a plan that names another crop is still a plan, and the door refuses by "
                       "the ceiling rather than by 'no plan yet'");
  }

  // -- a fund nobody named opens nothing ------------------------------------
  //
  // kNone reaches the consumer: the codecs accept it, because it is the value
  // every other kind of order carries, and only the boundary refuses it — so
  // an order replayed out of a journal never passes ShapeIsValid at all. The
  // quiet answer would have been the plan reserve, which is what an `else`
  // gives you.
  {
    const core::WorldState after = order_unseal(core::FundKind::kNone, 900'000, 100'000);
    failures += Expect(after.orders.rows[0].status == core::OrderStatus::kRefused,
                       "an unsealing that names no fund opens none");
    failures += Expect(
        after.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kPlanReserve)].empty() ||
            after.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kPlanReserve)]
                                  [rye.value] == 0,
        "and above all does not quietly open the plan reserve instead");
  }

  // -- a resource the roster does not carry is refused, and grows nothing ---
  //
  // Dense-by-ResourceId is a contract, and an id no table row backs would
  // stretch these vectors to 65535 cells — a length the save codec refuses,
  // which turns one mistyped order into a campaign that can never be saved.
  {
    core::WorldState previous;
    previous.calendar.tick = 10U * core::kTicksPerDay;
    core::RefreshCalendarCaches(previous.calendar);
    core::OrderRow order;
    order.kind = core::OrderKind::kUnsealFund;
    order.fund = core::FundKind::kSeed;
    order.resource = core::ResourceId{60000};  // no such row in resources.csv
    order.amount = 1'000;
    core::AppendRow(previous.orders, order);
    core::WorldState current = previous;
    current.calendar.tick += 1;
    core::RefreshCalendarCaches(current.calendar);
    system->RunProductionDecisions(previous, current);
    failures += Expect(current.orders.rows[0].status == core::OrderStatus::kRefused,
                       "an unsealing of a resource the roster does not carry is refused");
    failures += Expect(
        current.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kSeed)].size() < 60000,
        "and the dense vector is not stretched to hold it");
  }

  // -- a refused order leaves the state the size it found it ----------------
  {
    const core::WorldState after = order_unseal(core::FundKind::kPlanReserve, 900'000, 1'000'000);
    failures += Expect(
        after.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kPlanReserve)].empty(),
        "a refused unsealing grows nothing: the vector used to be resized "
        "before the refusal that turned the order down");
  }

  // -- the seed fund's running total stays a number -------------------------
  //
  // The seed fund has no ceiling in this module and honestly cannot: its size
  // is the sowing norms over the fields still to be sown, which another
  // module computes. What is checked is the one thing that CAN be checked
  // from here — that a second order does not wrap the running total, which a
  // signed 64-bit `+=` does silently and undefinedly.
  {
    core::WorldState previous;
    previous.calendar.tick = 10U * core::kTicksPerDay;
    core::RefreshCalendarCaches(previous.calendar);
    previous.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kSeed)].assign(
        static_cast<std::size_t>(rye.value) + 1U, 0);
    previous.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kSeed)][rye.value] =
        std::numeric_limits<core::Grams>::max() - 10;
    core::OrderRow order;
    order.kind = core::OrderKind::kUnsealFund;
    order.fund = core::FundKind::kSeed;
    order.resource = rye;
    order.amount = 1'000'000;
    core::AppendRow(previous.orders, order);
    core::WorldState current = previous;
    current.calendar.tick += 1;
    core::RefreshCalendarCaches(current.calendar);
    system->RunProductionDecisions(previous, current);
    failures += Expect(current.orders.rows[0].status == core::OrderStatus::kRefused,
                       "an unsealing that would wrap the running total is refused, not wrapped");
    failures += Expect(
        current.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kSeed)][rye.value] ==
            std::numeric_limits<core::Grams>::max() - 10,
        "and the total it was refused by is untouched");
  }

  // -- and so is the fodder fund, the third rung ----------------------------
  //
  // THE POINT OF THE ARRAY, checked rather than asserted in prose: a third
  // fund arrived the same day the second was written, and it needed no field
  // of its own. A fourth will need none either.
  {
    // Rye is a fodder grain for the horse in the shipped roster (feed_links
    // marks it work_only), so it may come out of this fund.
    const core::WorldState after = order_unseal_with_horses(core::FundKind::kFodder, 120'000);
    failures += Expect(after.orders.rows[0].status == core::OrderStatus::kDone,
                       "the fodder fund opens by the same verb as the other two");
    const auto slot = static_cast<std::size_t>(core::FundKind::kFodder);
    const core::Grams opened = rye.value < after.unsealed.by_fund[slot].size()
                                   ? after.unsealed.by_fund[slot][rye.value]
                                   : 0;
    failures += Expect(opened == 120'000, "and by the figure the chairman named");
    const auto plan_slot = static_cast<std::size_t>(core::FundKind::kPlanReserve);
    failures += Expect(after.unsealed.by_fund[plan_slot].empty() ||
                           after.unsealed.by_fund[plan_slot][rye.value] == 0,
                       "and the funds he did not name stay shut — the index is the enum, and "
                       "an off-by-one there would open the neighbour");
  }

  // -- and a fund with nothing in it opens nothing --------------------------
  //
  // The fodder fund's size is the working stock's work ration for the YEAR,
  // so a settlement with no horses holds none of it. The ceiling is on the
  // amount and not only on the resource: without it the door would open onto
  // a fund that does not exist.
  {
    const core::WorldState after = order_unseal(core::FundKind::kFodder, 0, 1'000);
    failures += Expect(after.orders.rows[0].status == core::OrderStatus::kRefused,
                       "a settlement with no working stock holds no fodder fund to open");
    failures += Expect(after.orders.rows[0].refusal == core::OrderRefusal::kRuleForbids,
                       "and is told it is a rule, not a missing resource");
  }

  // -- but the fodder fund opens FODDER GRAIN and nothing else --------------
  //
  // Its ceiling is on WHAT, not on how much. A door that let hay out of the
  // fodder fund would free four hundred tonnes nobody but the animals eat —
  // a release with no cost, and a release with no cost is what this whole
  // verb exists not to be.
  {
    const core::ITable* const resources_table = tables->FindTable("resources");
    const core::ResourceId hay{static_cast<std::uint16_t>(resources_table->FindRowByKey("hay"))};
    core::WorldState previous;
    previous.calendar.tick = 10U * core::kTicksPerDay;
    core::RefreshCalendarCaches(previous.calendar);
    core::OrderRow order;
    order.kind = core::OrderKind::kUnsealFund;
    order.fund = core::FundKind::kFodder;
    order.resource = hay;
    order.amount = 50'000;
    core::AppendRow(previous.orders, order);
    core::WorldState current = previous;
    current.calendar.tick += 1;
    core::RefreshCalendarCaches(current.calendar);
    system->RunProductionDecisions(previous, current);
    failures += Expect(current.orders.rows[0].status == core::OrderStatus::kRefused,
                       "hay does not come out of the fodder fund");
    failures += Expect(current.orders.rows[0].refusal == core::OrderRefusal::kRuleForbids,
                       "and the chairman is told it is a rule and not a shortage");
  }

  // -- the seed fund is the same door ---------------------------------------
  //
  // One verb for both funds: the design gives the same emergency to each,
  // and this check is what would redden if the seed half were ever split off
  // into a second verb that nobody wired up.
  {
    const core::WorldState after = order_unseal(core::FundKind::kSeed, 0, 250'000);
    failures += Expect(after.orders.rows[0].status == core::OrderStatus::kDone,
                       "the seed fund opens by the same verb");
    const core::Grams opened =
        rye.value < after.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kSeed)].size()
            ? after.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kSeed)][rye.value]
            : 0;
    failures += Expect(opened == 250'000, "and by the figure the chairman named");
  }
  return failures;
}

int CheckThePlanIsJudgedAtTheYearsTurn() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  if (Expect(tables != nullptr, "the shipped tables load for the plan verdict") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const auto system = core::CreateProductionSystem(*tables, core::StubTables::kRefused);
  if (Expect(system != nullptr, "and they build a production system") != 0) {
    return 1;
  }
  const core::ITable* const resources = tables->FindTable("resources");
  const core::ITable* const types = tables->FindTable("unit_types");
  const core::ResourceId wheat{static_cast<std::uint16_t>(resources->FindRowByKey("wheat"))};
  const core::UnitTypeId granary{static_cast<std::uint16_t>(types->FindRowByKey("granary"))};

  // One year turn, driven at the tick that crosses it. `stocked` is what lies
  // in the granary when the collector comes.
  const auto turn = [&](core::Grams due, core::Grams stocked, core::PlanState before) {
    core::WorldState previous;
    previous.calendar.tick = (core::kDaysPerYear * core::kTicksPerDay) - 1;
    core::RefreshCalendarCaches(previous.calendar);
    previous.plan = before;
    if (due > 0) {
      previous.plan.due.assign(static_cast<std::size_t>(wheat.value) + 1U, 0);
      previous.plan.due[wheat.value] = due;
    }
    core::UnitRow barn;
    barn.type = granary;
    barn.level = 1;
    barn.stock.assign(static_cast<std::size_t>(wheat.value) + 1U, 0);
    barn.stock[wheat.value] = stocked;
    core::AppendRow(previous.units, barn);

    core::WorldState current = previous;
    current.calendar.tick += 1;
    core::RefreshCalendarCaches(current.calendar);
    system->RunProductionDecisions(previous, current);
    return current;
  };

  // -- THE TURN WRITES DOWN THE WORKED ARABLE, and next spring reads it -----
  //
  // The figure the norm is computed from is state now, and it is taken at the
  // TURN: what the chairman does between January and the announcement must
  // not move it, because moving it was the defect — a plan priced off today's
  // fields is a plan he can zero on the morning it is read.
  //
  // This is also the assertion that was missing when the write was first
  // made: the damage "the turn records nothing" left the whole suite green,
  // because the other plan tests set the area by hand and no run looks at it.
  {
    core::WorldState previous;
    previous.calendar.tick = (core::kDaysPerYear * core::kTicksPerDay) - 1;
    core::RefreshCalendarCaches(previous.calendar);
    core::FieldRow worked;
    worked.kind = core::LandKind::kArable;
    worked.area_ga = 12.0F;
    worked.rotation_assigned = 1;  // a chain was given: this is worked land
    core::AppendRow(previous.fields, worked);
    core::FieldRow untold = worked;
    untold.rotation_assigned = 0;  // nobody has told this ground anything
    core::AppendRow(previous.fields, untold);
    core::FieldRow meadow = worked;
    meadow.kind = core::LandKind::kMeadow;
    core::AppendRow(previous.fields, meadow);
    core::WorldState current = previous;
    current.calendar.tick += 1;
    core::RefreshCalendarCaches(current.calendar);
    system->RunProductionDecisions(previous, current);
    failures +=
        Expect(current.plan.worked_ha_last_year > 11.9F && current.plan.worked_ha_last_year < 12.1F,
               "the year's turn writes down the arable that was WORKED — twelve "
               "hectares, not the thirty-six that count ground nobody told and a meadow");

    // -- AND A RELEASE ON THE TURN'S OWN TICK CANNOT EMPTY IT --------------
    //
    // THE ESCAPE THE ANALYSIS FOUND, and it was two orders per field per
    // year. The order book is consumed at the TOP of the production slot and
    // the year's turn runs lower down the same call, so a chairman who
    // withdrew every chain on the last tick of December had the figure taken
    // as zero — and re-issued the chains the next morning at no cost at all:
    // nothing is sown in January anyway, the mark that holds a fresh chain is
    // spent in spring, the harvest is untouched, and the district asks
    // nothing for ever. Against the YEAR'S MAXIMUM the same escape costs the
    // whole harvest, because the fields have to stay released all year.
    core::WorldState held = previous;
    held.plan.worked_ha_this_year = 12.0F;  // the year has seen twelve worked hectares
    for (core::FieldRow& field : held.fields.rows) {
      field.rotation_assigned = 0;  // ...and the chairman withdraws every chain today
    }
    core::WorldState turned = held;
    turned.calendar.tick += 1;
    core::RefreshCalendarCaches(turned.calendar);
    system->RunProductionDecisions(held, turned);
    failures +=
        Expect(turned.plan.worked_ha_last_year > 11.9F && turned.plan.worked_ha_last_year < 12.1F,
               "a chain withdrawn on the turn's own tick does not empty the year's figure: the "
               "district counts the LARGEST area the year held");

    // -- AND A WHOLE YEAR RELEASED DOES NOT LOWER IT EITHER ----------------
    //
    // The escape that survived the yearly maximum: every chain withdrawn for
    // a WHOLE year costs that year's harvest and one failed plan, and then
    // the district asked nothing for ever (host, 0.32.4: no plan from day
    // 144). The base only grows (register 222). Twenty hectares on the books,
    // a year that worked none: the base stays twenty.
    core::WorldState idle_year = previous;
    idle_year.plan.worked_ha_last_year = 20.0F;
    idle_year.plan.worked_ha_this_year = 0.0F;
    for (core::FieldRow& field : idle_year.fields.rows) {
      field.rotation_assigned = 0;
    }
    core::WorldState idle_turned = idle_year;
    idle_turned.calendar.tick += 1;
    core::RefreshCalendarCaches(idle_turned.calendar);
    system->RunProductionDecisions(idle_year, idle_turned);
    failures += Expect(idle_turned.plan.worked_ha_last_year > 19.9F &&
                           idle_turned.plan.worked_ha_last_year < 20.1F,
                       "a year with every chain withdrawn does not lower the plan's base: "
                       "sowing less fails the plan, it does not shrink it");
  }

  // -- a year delivered in full ---------------------------------------------
  {
    const core::WorldState after = turn(1'000'000, 5'000'000, core::PlanState{});
    failures += Expect(after.plan.last_verdict == core::PlanVerdict::kMet,
                       "a plan delivered in full closes the year as met");
    // THE FIGURE GOES INTO THE BOOK BEFORE IT IS CLEARED (M12): the judged
    // year's book keeps what was asked, beside what was shipped.
    failures += Expect(after.ledger.current.plan_due.size() > wheat.value &&
                           after.ledger.current.plan_due[wheat.value] == 1'000'000 &&
                           after.plan.due[wheat.value] == 0,
                       "the judged year's book keeps what the district asked, and the plan "
                       "is cleared");
    // "and the failed run stands at nothing" stood here and was REMOVED: the
    // counter starts at nothing, so the claim held whether the verdict ran
    // or not — true under both implementations, which is decoration and not
    // a check (§6.2а of the delivery cycle). What it meant to say is said in
    // the block below, from a fixture that carries two failed years, where
    // it can fail.
    failures += Expect(after.plan.met_years_in_a_row == 1, "while the met run has begun");
    failures += Expect(after.chairman.raikom_reputation > 50.0F,
                       "and the district thinks better of the chairman for it");
  }

  // -- a year short by ONE GRAM ---------------------------------------------
  //
  // By one gram deliberately: a check written against an empty store would
  // pass on an implementation that judged "delivered nothing at all" instead
  // of "delivered less than was asked".
  {
    const core::WorldState after = turn(1'000'000, 999'999, core::PlanState{});
    failures += Expect(after.plan.last_verdict == core::PlanVerdict::kFailed,
                       "one gram short of the plan is a failed year, not a rounded one");
    failures += Expect(after.plan.failed_years_in_a_row == 1, "and the failed run has begun");
    failures += Expect(after.chairman.raikom_reputation < 50.0F,
                       "and the district thinks worse of the chairman for it");
  }

  // -- a met year clears a run of failures ----------------------------------
  {
    core::PlanState carried;
    carried.last_verdict = core::PlanVerdict::kFailed;
    carried.failed_years_in_a_row = 2;
    const core::WorldState after = turn(1'000'000, 5'000'000, carried);
    failures += Expect(after.plan.failed_years_in_a_row == 0,
                       "a met year wipes the run of failed ones: the trigger is three IN A ROW, "
                       "not three in a campaign");
  }

  // -- the third failure raises the condition, and only the third -----------
  {
    core::PlanState carried;
    carried.last_verdict = core::PlanVerdict::kFailed;
    carried.failed_years_in_a_row = 2;
    const core::WorldState third = turn(1'000'000, 0, carried);
    std::uint32_t raised = 0;
    for (const core::SimEvent& event : third.step_events) {
      raised += event.kind == core::EventKind::kPlanTrialDue ? 1U : 0U;
    }
    failures += Expect(third.plan.failed_years_in_a_row == 3, "three failed years stand in a row");
    failures += Expect(raised == 1, "and the third of them raises the trial condition");

    carried.failed_years_in_a_row = 3;
    const core::WorldState fourth = turn(1'000'000, 0, carried);
    std::uint32_t again = 0;
    for (const core::SimEvent& event : fourth.step_events) {
      again += event.kind == core::EventKind::kPlanTrialDue ? 1U : 0U;
    }
    failures += Expect(again == 0,
                       "and a fourth failed year does not raise it a second time: a condition "
                       "that re-announces itself yearly is an alarm, not an event");
  }

  // -- a district that asked for nothing judges nothing ---------------------
  //
  // The check that keeps the four above from being a machine which always
  // says something. A world whose tables carry no plan is a world with no
  // district, and it must not accumulate triumphs nobody asked it for.
  {
    const core::WorldState after = turn(0, 5'000'000, core::PlanState{});
    failures += Expect(after.plan.last_verdict == core::PlanVerdict::kNone,
                       "a year the district asked nothing of is not a year it was pleased with");
    failures += Expect(after.plan.met_years_in_a_row == 0, "and no run of met years begins");
  }

  // -- the norm is announced in the spring, off the land that was worked ----
  //
  // THE CHECK THAT KEEPS THE PLAN FROM BEING A SHARE OF THE REAPING AGAIN.
  // It asserts the figure against area x normal yield x share — numbers a
  // plan accrued from the harvest could not produce, because in this fixture
  // nothing has been reaped at all.
  {
    const core::ITable* const crops = tables->FindTable("crops");
    const core::CropId oat{static_cast<std::uint16_t>(crops->FindRowByKey("oat"))};

    core::WorldState previous;
    // The last day of winter OF THE SECOND YEAR: the step below crosses into
    // spring. The first year's norm is off the start stock (below), so the
    // arable's rule is asserted from the second (boss, parcel 399).
    previous.calendar.tick =
        (static_cast<core::Tick>(core::kDaysPerYear + (2U * core::kDaysPerMonth)) *
         core::kTicksPerDay) -
        1U;
    core::RefreshCalendarCaches(previous.calendar);
    // THE AREA THE NORM IS COMPUTED FROM IS LAST YEAR'S, and it is state now
    // rather than a walk over today's fields: ten worked hectares, written at
    // the year's turn. What this fixture's fields carry TODAY must not enter
    // the figure at all — that is the whole repair (world_state.h).
    previous.plan.worked_ha_last_year = 10.0F;
    core::FieldRow worked;
    worked.kind = core::LandKind::kArable;
    worked.area_ga = 10.0F;
    worked.rotation_year0 = oat;
    worked.rotation_assigned = 1;
    core::AppendRow(previous.fields, worked);
    // AND AN UNWORKED FIELD BESIDE IT, of the same size and the same land.
    // Land nobody worked owes nothing — a norm that counted it would punish
    // the chairman for ground he never touched.
    //
    // WHAT MAKES IT UNWORKED IS THAT NOBODY ASSIGNED IT, and this pair has
    // been renamed twice in one evening as the tree learned what it was
    // really testing. The field was LandKind::kDerelict here first, and the
    // kind was never what excused it; then it was an empty rotation slot,
    // and that could not tell "no chain" from "a fallow year"; now it is the
    // fact itself (land_state.h, rotation_assigned).
    core::FieldRow resting = worked;
    resting.rotation_year0 = core::CropId{};
    resting.rotation_assigned = 0;
    resting.overgrown = 1;
    core::AppendRow(previous.fields, resting);

    core::WorldState current = previous;
    current.calendar.tick += 1;
    core::RefreshCalendarCaches(current.calendar);
    const bool crossed = previous.calendar.season != current.calendar.season &&
                         current.calendar.season == core::Season::kSpring;
    failures += Expect(crossed, "the fixture really does cross into spring");
    system->RunProductionDecisions(previous, current);

    core::Grams asked = 0;
    for (const core::Grams due : current.plan.due) {
      asked += due;
    }
    failures += Expect(asked > 0,
                       "the district names the year's norm in the spring, off land that has "
                       "reaped nothing yet — a norm a share of the harvest could not produce");
    // THE FIGURE IS THE DISTRICT'S POSITIONS ON LAST YEAR'S TEN HECTARES,
    // and every number in it is read back out of the tables: a second copy of
    // balance data in a test is a copy that drifts, and this project has been
    // bitten by that more than once.
    const core::ITable* const campaign = tables->FindTable("campaign");
    const float share_percent = std::stof(std::string(campaign->CellText(
        campaign->FindRowByKey("plan_grain_share_percent"), campaign->FindColumn("value"))));
    const std::string positions(campaign->CellText(campaign->FindRowByKey("plan_positions"),
                                                   campaign->FindColumn("value")));
    double expected = 0.0;
    std::uint32_t position_count = 0;
    std::string token;
    for (std::size_t index = 0; index <= positions.size(); ++index) {
      if (index < positions.size() && positions[index] != ' ') {
        token += positions[index];
        continue;
      }
      if (!token.empty()) {
        const std::size_t equals = token.find('=');
        failures += Expect(equals != std::string::npos,
                           "every plan position carries its share of the worked arable");
        const std::string key = token.substr(0, equals);
        const double area_share = std::stod(token.substr(equals + 1)) / 100.0;
        const double yield_kg_per_ha = std::stod(std::string(
            crops->CellText(crops->FindRowByKey(key), crops->FindColumn("yield_kg_per_ha"))));
        expected += yield_kg_per_ha * 10.0 * area_share * 1000.0 *
                    (static_cast<double>(share_percent) / 100.0);
        ++position_count;
        token.clear();
      }
    }
    failures += Expect(position_count > 0, "campaign.csv names the plan's positions");
    failures += Expect(static_cast<double>(asked) > expected * 0.99 &&
                           static_cast<double>(asked) < expected * 1.01,
                       "and the norm is the district's positions on last year's ten hectares — "
                       "not a share of what happens to be standing in the fields today");

    // -- AND NEITHER FALLOW NOR A WITHDRAWN CHAIN CAN MOVE IT --------------
    //
    // THE HOLE THIS CLOSES WAS A BUTTON THAT TURNED THE EPOCH OFF, and it had
    // two doors. The norm used to be priced off the crop in each field's
    // year0 slot: a fallow YEAR is an empty slot and cost nothing, and a
    // field whose chain has been WITHDRAWN — the move the order book allows
    // on purpose, to forgive a layout mistake — cost nothing either. Either
    // way a chairman could owe the district NOTHING on the morning the norm
    // was read: no figure, no verdict, no failed year, no trial.
    //
    // Both doors are shut by the same change, and this asserts both at once:
    // the fields are laid to fallow AND released, and the figure does not
    // move, because it is last year's area and the district's positions and
    // neither is his to touch today.
    core::WorldState fallow_previous = previous;
    for (core::FieldRow& field : fallow_previous.fields.rows) {
      field.rotation_year0 = core::CropId{};
      field.rotation_year1 = core::CropId{};
      field.rotation_year2 = core::CropId{};
      field.rotation_assigned = 0;
    }
    core::WorldState fallow_current = fallow_previous;
    fallow_current.calendar.tick += 1;
    core::RefreshCalendarCaches(fallow_current.calendar);
    system->RunProductionDecisions(fallow_previous, fallow_current);
    core::Grams asked_on_fallow = 0;
    for (const core::Grams due : fallow_current.plan.due) {
      asked_on_fallow += due;
    }
    failures += Expect(asked_on_fallow == asked,
                       "a chairman who rests every field, or withdraws every chain, owes the "
                       "district exactly what he owed: undersowing is a way to FAIL the plan, "
                       "not to shrink it");
    failures += Expect(fallow_current.plan.announced == 1,
                       "and the district is on record as having spoken, which a tonnage of zero "
                       "cannot say");

    // -- THE FIRST YEAR ASKS BY THE START STOCK (boss, parcel 399) -------------
    //
    // The same fixture one year earlier: the start's derelict arable is not
    // what the first norm is priced off, the start stock is — each position a
    // share of the stock of its produce, and a position with no start stock
    // asks nothing. Every figure read back from the tables.
    core::WorldState first_previous = previous;
    first_previous.calendar.tick -=
        static_cast<core::Tick>(core::kDaysPerYear) * core::kTicksPerDay;
    core::RefreshCalendarCaches(first_previous.calendar);
    core::WorldState first_current = first_previous;
    first_current.calendar.tick += 1;
    core::RefreshCalendarCaches(first_current.calendar);
    system->RunProductionDecisions(first_previous, first_current);
    const float first_percent = std::stof(std::string(campaign->CellText(
        campaign->FindRowByKey("first_plan_start_stock_percent"), campaign->FindColumn("value"))));
    const core::ITable* const stock = tables->FindTable("start_stock");
    const core::ITable* const resource_table = tables->FindTable("resources");
    bool first_matches = true;
    std::uint32_t first_positions = 0;
    std::string first_token;
    for (std::size_t index = 0; index <= positions.size(); ++index) {
      if (index < positions.size() && positions[index] != ' ') {
        first_token += positions[index];
        continue;
      }
      if (first_token.empty()) {
        continue;
      }
      const std::string key = first_token.substr(0, first_token.find('='));
      first_token.clear();
      const std::string resource_key(
          crops->CellText(crops->FindRowByKey(key), crops->FindColumn("resource")));
      double stock_kg = 0.0;
      for (std::uint32_t row = 0; row < stock->RowCount(); ++row) {
        if (stock->CellText(row, stock->FindColumn("resource")) == resource_key) {
          stock_kg +=
              std::stod(std::string(stock->CellText(row, stock->FindColumn("amount")))) *
              std::stod(std::string(stock->CellText(row, stock->FindColumn("kg_per_unit"))));
        }
      }
      const std::uint32_t resource = resource_table->FindRowByKey(resource_key);
      const double due = resource < first_current.plan.due.size()
                             ? static_cast<double>(first_current.plan.due[resource])
                             : 0.0;
      const double wanted = stock_kg * 1000.0 * static_cast<double>(first_percent) / 100.0;
      first_matches = first_matches && due > wanted - 1.0 && due < wanted + 1.0;
      ++first_positions;
    }
    failures += Expect(first_positions > 0 && first_matches && first_current.plan.announced == 1,
                       "the first year's norm is each position's share of the start stock of its "
                       "produce, not the derelict arable's");
  }
  return failures;
}

/// RESTING FALLOW RECOVERS; UNWORKED GROUND KEEPS WHAT IT HAS — and nothing
/// in this suite said so until 2026-09-12, which is how the repair of one
/// defect resurrected another.
///
/// While LandKind::kDerelict existed, the year's fertility recovery was kept
/// off unworked land by a filter on the KIND. The kind was removed because
/// the design says an overgrown field is a look and not a state, and the
/// recovery started paying six points a year to ninety-three hectares nobody
/// has ever ploughed: 65 to 100 in six years, and 100 from the sixth year to
/// the thirtieth. That is defect D5 of the reconciliation under a new name,
/// it poisons every area-weighted fertility reading in the project, and it
/// would have handed a player who raised the land a hundred-point field
/// instead of the canon's sixty-five.
///
/// THE WHOLE SUITE STAYED GREEN THROUGH IT. The analysis found it by walking
/// the readers of a removed enum, and the field sheet confirmed it — but
/// neither is a check, and a defect that can come back twice needs one.
int CheckUnworkedGroundDoesNotRecover() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  if (Expect(tables != nullptr, "the shipped tables load for the fallow check") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const auto system = core::CreateProductionSystem(*tables, core::StubTables::kRefused);
  if (Expect(system != nullptr, "and they build a production system") != 0) {
    return 1;
  }
  const core::ITable* const crops = tables->FindTable("crops");
  const core::ITable* const resources = tables->FindTable("resources");
  const core::ITable* const types = tables->FindTable("unit_types");
  const core::CropId rye{static_cast<std::uint16_t>(crops->FindRowByKey("rye_winter"))};
  const core::ResourceId manure{static_cast<std::uint16_t>(resources->FindRowByKey("manure"))};
  const core::UnitTypeId heap_type{static_cast<std::uint16_t>(types->FindRowByKey("manure_pile"))};

  // TWO FIELDS THAT DIFFER IN ONE THING ONLY: the fallow one is on a fallow
  // YEAR — a chain with a gap in it — and the unworked one has no chain at
  // all. Same phase, same fertility, same area. If the recovery read
  // anything but the chain, both would move together.
  core::WorldState previous;
  previous.calendar.tick = (core::kDaysPerYear * core::kTicksPerDay) - 1;
  core::RefreshCalendarCaches(previous.calendar);
  core::FieldRow fallow;
  fallow.kind = core::LandKind::kArable;
  fallow.area_ga = 10.0F;
  fallow.fertility = 65.0F;
  fallow.phase = core::FieldPhase::kIdle;
  fallow.rotation_year1 = rye;   // this year fallow, rye the year after
  fallow.rotation_assigned = 1;  // and the chain was GIVEN, which is the point
  core::AppendRow(previous.fields, fallow);
  core::FieldRow unworked = fallow;
  unworked.rotation_year1 = core::CropId{};
  // AND THE CHAIN WAS NEVER GIVEN, which since 2026-09-12 is a fact of its
  // own and not an inference from three empty slots. Clearing the slot alone
  // would now leave this field "assigned to three fallow years", which is a
  // legal rotation and the opposite of what this pair is testing.
  unworked.rotation_assigned = 0;
  unworked.overgrown = 1;
  core::AppendRow(previous.fields, unworked);
  // AND A HEAP WITH MANURE IN IT, so the same turn exercises the SECOND
  // guard. The winter's manure plan takes the poorest land first, and the
  // unworked ground is deliberately the poorest here: without the guard it
  // heads the queue and takes the dose, which is manure spread on ground
  // that is never ploughed and so never turned in.
  core::UnitRow heap;
  heap.type = heap_type;
  heap.level = 1;
  heap.stock.assign(static_cast<std::size_t>(manure.value) + 1U, 0);
  heap.stock[manure.value] = 400'000'000;
  core::AppendRow(previous.units, heap);
  previous.fields.rows[1].fertility = 10.0F;  // the poorest on the farm

  core::WorldState current = previous;
  current.calendar.tick += 1;
  core::RefreshCalendarCaches(current.calendar);
  system->RunProductionDecisions(previous, current);

  failures += Expect(current.fields.rows[0].fertility > 65.0F,
                     "a field on a fallow YEAR recovers over the winter: it was ploughed bare "
                     "and left to stand, which is what fallow is for");
  failures += Expect(current.fields.rows[1].fertility == 10.0F,
                     "and ground nobody has told anything KEEPS what it has — it is not resting "
                     "fallow, it is land the plough has never touched (defect D5)");
  failures += Expect(current.fields.rows[0].manure_applied > 0,
                     "the winter's manure plan gives the fallow field its dose");
  failures += Expect(current.fields.rows[1].manure_applied == 0,
                     "and none of it to ground that is never ploughed, which would simply lose "
                     "it — the poorest-first queue would otherwise hand it the lot");
  return failures;
}

/// THE PLAYER'S ONE DECISION ABOUT A FIELD, and until 2026-09-12 the core
/// could not take it: OrderKind::kSetRotation existed from the first day and
/// had no consumer, so a field could only ever carry the chain genesis gave
/// it. That is why ninety-three of the start's hundred and sixty-three
/// hectares lay unworked through every thirty-year run this project has
/// measured — work is opened off the rotation, and nothing could give a
/// field one.
int CheckTheChairmanSetsARotation() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  if (Expect(tables != nullptr, "the shipped tables load for the rotation order") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const auto system = core::CreateProductionSystem(*tables, core::StubTables::kRefused);
  if (Expect(system != nullptr, "and they build a production system") != 0) {
    return 1;
  }
  const core::ITable* const crops = tables->FindTable("crops");
  const core::CropId oat{static_cast<std::uint16_t>(crops->FindRowByKey("oat"))};

  // One field, one order, one step. `land` is what the field is made of and
  // `slots` what the chairman names.
  const auto order_rotation = [&](core::LandKind land, std::array<core::CropId, 3> slots) {
    core::WorldState previous;
    previous.calendar.tick = 10U * core::kTicksPerDay;
    core::RefreshCalendarCaches(previous.calendar);
    core::FieldRow field;
    field.kind = land;
    field.area_ga = 10.0F;
    field.fertility = 65.0F;
    const core::FieldId id = core::AppendRow(previous.fields, field);
    core::OrderRow order;
    order.kind = core::OrderKind::kSetRotation;
    order.field = id;
    order.rotation_year0 = slots[0];
    order.rotation_year1 = slots[1];
    order.rotation_year2 = slots[2];
    core::AppendRow(previous.orders, order);
    core::WorldState current = previous;
    current.calendar.tick += 1;
    core::RefreshCalendarCaches(current.calendar);
    system->RunProductionDecisions(previous, current);
    return current;
  };

  // A SECOND ORDER ON A WORLD THAT ALREADY RAN ONE: the release case needs a
  // field that was told something first, and telling it is the other lambda's
  // job. The order book is emptied by the events slot, which is not run here,
  // so the fresh row is appended and read on the next step.
  const auto order_again = [&](core::WorldState world, std::array<core::CropId, 3> slots) {
    world.orders.rows.clear();
    world.orders.row_ids.clear();
    core::OrderRow order;
    order.kind = core::OrderKind::kSetRotation;
    order.field = world.fields.row_ids[0];
    order.rotation_year0 = slots[0];
    order.rotation_year1 = slots[1];
    order.rotation_year2 = slots[2];
    core::AppendRow(world.orders, order);
    core::WorldState current = world;
    current.calendar.tick += 1;
    core::RefreshCalendarCaches(current.calendar);
    system->RunProductionDecisions(world, current);
    return current;
  };

  // -- the chain lands on the field ----------------------------------------
  {
    const core::WorldState after =
        order_rotation(core::LandKind::kArable, {oat, core::CropId{}, oat});
    failures += Expect(after.orders.rows[0].status == core::OrderStatus::kDone,
                       "the chairman may tell a field what to grow");
    const core::FieldRow& field = after.fields.rows[0];
    failures += Expect(field.rotation_year0.value == oat.value &&
                           field.rotation_year1.value == core::kInvalidDefIdValue &&
                           field.rotation_year2.value == oat.value,
                       "and all three seasons land as he named them, gap and all");
    failures += Expect(core::HasRotation(field),
                       "and the field now says it HAS been told, which is a fact of its own");
  }

  // -- AN EMPTY CHAIN IS THE CHAIRMAN TAKING HIS WORD BACK -------------------
  //
  // Boss's ruling of 2026-09-12, and the door is this order rather than a
  // new kind. One or two empty slots are fallow YEARS and the field stays
  // worked (asserted above, gap and all); all three empty and the field goes
  // back to ground nobody has spoken to. Without it a field told once was
  // worked for ever — the nearest release being an all-fallow chain, which
  // is still ploughed, recovered and manured every year — so a layout
  // mistake cost work for the rest of the campaign.
  {
    const core::WorldState after = order_rotation(core::LandKind::kArable, {oat, oat, oat});
    failures += Expect(core::HasRotation(after.fields.rows[0]), "a field told what to grow");
    const core::WorldState released =
        order_again(after, {core::CropId{}, core::CropId{}, core::CropId{}});
    failures += Expect(released.orders.rows[0].status == core::OrderStatus::kDone,
                       "an empty chain is an order and not a malformed one");
    const core::FieldRow& field = released.fields.rows[0];
    failures += Expect(!core::HasRotation(field),
                       "and the field is released: told once is no longer told for ever");
    failures += Expect(field.rotation_year0.value == core::kInvalidDefIdValue &&
                           field.rotation_year1.value == core::kInvalidDefIdValue &&
                           field.rotation_year2.value == core::kInvalidDefIdValue,
                       "and the slots go with the byte — a released field keeps no crops of the "
                       "chain it no longer has");
  }

  // -- a meadow is mown where it grew and is never sown ---------------------
  {
    const core::WorldState after = order_rotation(core::LandKind::kMeadow, {oat, oat, oat});
    failures += Expect(after.orders.rows[0].refusal == core::OrderRefusal::kWrongLand,
                       "a meadow carries no rotation, and never will");
    failures += Expect(!core::HasRotation(after.fields.rows[0]),
                       "and a refused order leaves the field as it found it");
  }

  // -- a crop this build does not know is refused, not written --------------
  //
  // An id that names no row is not a fallow year: it is a layer and a core
  // disagreeing about the crop table. Writing it in would put a subject into
  // the rotation that every reader of crop norms would then question.
  {
    const core::WorldState after = order_rotation(
        core::LandKind::kArable, {oat, core::CropId{static_cast<std::uint16_t>(60000)}, oat});
    failures += Expect(after.orders.rows[0].refusal == core::OrderRefusal::kNoSuchCrop,
                       "a crop this build has never heard of is refused, and refused by its own "
                       "word: 'no such field' and 'no such crop' have different repairs");
    failures += Expect(!core::HasRotation(after.fields.rows[0]),
                       "and NOTHING of the order is written — the first slot was valid and it "
                       "is not in the field either");
  }

  // -- a field that is not there is still kNoSuchSubject --------------------
  //
  // The other half of the split: one code for both was the defect, and a
  // test that only checked the new word would not notice the old one going
  // with it.
  {
    core::WorldState previous;
    previous.calendar.tick = 10U * core::kTicksPerDay;
    core::RefreshCalendarCaches(previous.calendar);
    core::OrderRow order;
    order.kind = core::OrderKind::kSetRotation;
    order.field = core::FieldId{4242};
    order.rotation_year0 = oat;
    core::AppendRow(previous.orders, order);
    core::WorldState current = previous;
    current.calendar.tick += 1;
    core::RefreshCalendarCaches(current.calendar);
    system->RunProductionDecisions(previous, current);
    failures += Expect(current.orders.rows[0].refusal == core::OrderRefusal::kNoSuchSubject,
                       "a field that is not there is refused as a missing SUBJECT, not as a "
                       "missing crop");
  }

  // -- THE FIRST NAMED CROP IS WHAT THE NEXT SOWING PUTS IN ------------------
  //
  // Boss's ruling of 2026-09-12. A chairman lays his three years out in
  // November, with the harvest in and time to think — and the year's turn
  // would rotate his first crop into the third slot before any window
  // opened. Three years late for giving the order on time, and nothing
  // anywhere would have told him. The chain is a cycle, so the phase is the
  // whole decision: an order after the last spring window is written one
  // turn back, and January brings the named first crop to the top.
  {
    const core::ITable* const crops_table = tables->FindTable("crops");
    const core::CropId rye{static_cast<std::uint16_t>(crops_table->FindRowByKey("rye_winter"))};
    const core::CropId potato{static_cast<std::uint16_t>(crops_table->FindRowByKey("potato"))};
    core::WorldState previous;
    // Day 42 of forty-eight: four days to a month, so month 10 of twelve,
    // 0-based — November, past every spring window. (The first draft wrote
    // day 44 and called it November in the comment; day 44 is December, and
    // the assertions would have passed either way. A comment that names the
    // month is the only thing here that can be wrong out loud.)
    previous.calendar.tick = 42U * core::kTicksPerDay;
    core::RefreshCalendarCaches(previous.calendar);
    core::FieldRow field;
    field.kind = core::LandKind::kArable;
    field.area_ga = 10.0F;
    field.fertility = 65.0F;
    const core::FieldId id = core::AppendRow(previous.fields, field);
    core::OrderRow order;
    order.kind = core::OrderKind::kSetRotation;
    order.field = id;
    order.rotation_year0 = oat;
    order.rotation_year1 = potato;
    order.rotation_year2 = rye;
    core::AppendRow(previous.orders, order);
    core::WorldState current = previous;
    current.calendar.tick += 1;
    core::RefreshCalendarCaches(current.calendar);
    system->RunProductionDecisions(previous, current);
    const core::FieldRow& told = current.fields.rows[0];
    // Written AS NAMED — the chairman's sheet and the field say the same
    // thing — and the coming turn is marked to leave it alone.
    failures += Expect(told.rotation_year0.value == oat.value &&
                           told.rotation_year1.value == potato.value &&
                           told.rotation_year2.value == rye.value,
                       "a November chain is written exactly as the chairman named it");
    failures += Expect(told.rotation_skips_turn == 1,
                       "and the coming year's turn is marked to leave it standing, because the "
                       "first crop he named can no longer go in this year");
    failures += Expect(core::HasRotation(told), "and the field is assigned all the same");

    // AND NOW THE TURN ITSELF, because a bit nobody spends is a bit that
    // does nothing. The year rolls: the chain must stand still, the bit must
    // clear, and the crop named first must be what year0 offers the spring.
    core::WorldState before_turn = current;
    before_turn.calendar.tick = (48U * core::kTicksPerDay) - 1U;
    core::RefreshCalendarCaches(before_turn.calendar);
    before_turn.orders.rows.clear();
    before_turn.orders.row_ids.clear();
    core::WorldState turned = before_turn;
    turned.calendar.tick = 48U * core::kTicksPerDay;
    core::RefreshCalendarCaches(turned.calendar);
    // A FROZEN JANUARY, AND IT IS THE POINT OF THE FIXTURE RATHER THAN
    // SCENERY. What is asserted below is that THE TURN does not spend the
    // mark — so the turn must be the only thing that could have. Since the
    // plough gate was separated from the sowing gate (2026-09-13,
    // field_work.h) a field opens work on the thaw, and this fixture left the
    // temperature at its default of exactly 0.0 °C: warm enough to break
    // ground. The field then opened its ploughing in this very call and spent
    // the mark legitimately, and the assertion went red while the rule it
    // guards was intact. Below zero, nothing but the turn can move.
    before_turn.weather.air_temperature_celsius = -12.0F;
    turned.weather.air_temperature_celsius = -12.0F;
    system->RunProductionDecisions(before_turn, turned);
    const core::FieldRow& after_turn = turned.fields.rows[0];
    failures += Expect(after_turn.rotation_year0.value == oat.value,
                       "the year turns and the chain stands still: the spring sows the crop he "
                       "named first, not the one he named second");
    failures += Expect(after_turn.rotation_skips_turn == 1,
                       "and the mark is NOT spent by the turn: it stands until the field opens "
                       "work from the chain, so a January that arrives while the ground is busy "
                       "cannot carry the first named crop away either");
  }

  // -- THE WINDOW THAT DECIDES IS THE NAMED CROP'S OWN ----------------------
  //
  // Oats and peas close in April while potatoes and barley run to May. Ask
  // "is spring over" of the TABLE and the answer in May is no; ask it of the
  // oats the chairman actually named and it is yes. The first reading writes
  // his chain as named, lets January carry the oats into the third season,
  // and hides the trap behind another crop's calendar.
  {
    core::WorldState previous;
    previous.calendar.tick = 18U * core::kTicksPerDay;  // month 4 of twelve, 0-based: May
    core::RefreshCalendarCaches(previous.calendar);
    core::FieldRow field;
    field.kind = core::LandKind::kArable;
    field.area_ga = 10.0F;
    field.fertility = 65.0F;
    const core::FieldId id = core::AppendRow(previous.fields, field);
    core::OrderRow order;
    order.kind = core::OrderKind::kSetRotation;
    order.field = id;
    order.rotation_year0 = oat;  // sown to April, so May is already too late
    core::AppendRow(previous.orders, order);
    core::WorldState current = previous;
    current.calendar.tick += 1;
    core::RefreshCalendarCaches(current.calendar);
    system->RunProductionDecisions(previous, current);
    failures += Expect(current.fields.rows[0].rotation_skips_turn == 1,
                       "oats named in May wait for the next spring: the window that decides is "
                       "the named crop's own, not the latest in the table");
  }

  // -- AND THE YEAR'S FIRST DAY, WHERE THE TURN IS STILL AHEAD ---------------
  //
  // The order book is read at the top of the production slot and the year's
  // turn a few lines below it, in the same call. A chain settled on that
  // tick, with every window of the year ahead of it, would be advanced past
  // its own first season within the minute — so the turn that is about to
  // run is the one this mark holds off.
  {
    core::WorldState previous;
    previous.calendar.tick = (48U * core::kTicksPerDay) - 1U;
    core::RefreshCalendarCaches(previous.calendar);
    core::FieldRow field;
    field.kind = core::LandKind::kArable;
    field.area_ga = 10.0F;
    field.fertility = 65.0F;
    const core::FieldId id = core::AppendRow(previous.fields, field);
    core::WorldState current = previous;
    current.calendar.tick = 48U * core::kTicksPerDay;
    core::RefreshCalendarCaches(current.calendar);
    core::OrderRow order;
    order.kind = core::OrderKind::kSetRotation;
    order.field = id;
    order.rotation_year0 = oat;
    order.rotation_year1 = core::CropId{};
    order.rotation_year2 = core::CropId{};
    core::AppendRow(current.orders, order);
    system->RunProductionDecisions(previous, current);
    failures += Expect(current.fields.rows[0].rotation_year0.value == oat.value,
                       "a chain settled on the year's first day is not advanced past its own "
                       "first season by the turn that runs in the same call");
  }

  // -- THE FIELD SPENDS THE MARK WHEN IT USES THE CHAIN ---------------------
  //
  // A winter rye named first in August has its own window that fortnight, so
  // the ploughing opens the same day the order lands — and THAT is what
  // clears the mark. The month never enters into it: what is asked is
  // whether the chain has been used, and the one place every use passes
  // through is OpenPlowing.
  {
    const core::ITable* const crops_table = tables->FindTable("crops");
    const core::CropId rye{static_cast<std::uint16_t>(crops_table->FindRowByKey("rye_winter"))};
    core::WorldState previous;
    // The day BEFORE, so the step below turns the day and the field walk
    // runs: RunFields is daily, and a step inside one day never reaches it.
    previous.calendar.tick = (30U * core::kTicksPerDay) - 1U;  // month 7, 0-based: August
    core::RefreshCalendarCaches(previous.calendar);
    previous.weather.air_temperature_celsius = 12.0F;
    core::FieldRow field;
    field.kind = core::LandKind::kArable;
    field.area_ga = 10.0F;
    field.fertility = 65.0F;
    const core::FieldId id = core::AppendRow(previous.fields, field);
    core::WorldState current = previous;
    current.calendar.tick = 30U * core::kTicksPerDay;
    core::RefreshCalendarCaches(current.calendar);
    core::OrderRow order;
    order.kind = core::OrderKind::kSetRotation;
    order.field = id;
    order.rotation_year0 = rye;
    order.rotation_year1 = oat;
    order.rotation_year2 = oat;
    core::AppendRow(current.orders, order);
    system->RunProductionDecisions(previous, current);
    const core::FieldRow& sown = current.fields.rows[0];
    failures += Expect(sown.rotation_year0.value == rye.value,
                       "the chain is written as the chairman named it");
    failures += Expect(sown.phase == core::FieldPhase::kPlowing,
                       "and the rye's own window is this fortnight, so the ploughing opens the "
                       "same day the order lands");
    failures += Expect(sown.rotation_skips_turn == 0,
                       "and THAT is what spends the mark: the chain has been used, so the "
                       "coming turn may carry it on");
  }
  return failures;
}

int CheckPauseAndResume() {
  int failures = 0;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_production_pause";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  // No capacity column at all: this test is about the order book, and a
  // capacity with no level row behind it is now a refusal (CheckPauseStore).
  // The class decides whether a standing unit may be paused (units rules §5):
  // a barn produces, a byre is a farm that produces, a school does not.
  std::ofstream(root / "unit_types.csv")
      << "key,class\nbarn,production\nschool,social\nbyre,livestock\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto system = tables == nullptr
                          ? nullptr
                          : core::CreateProductionSystem(*tables, core::StubTables::kAllowed);
  if (Expect(system != nullptr, "a thin table set still builds a production system") != 0) {
    return 1;
  }

  core::WorldState world;
  core::UnitRow barn;
  barn.type = core::UnitTypeId{0};
  barn.level = 1;
  const core::UnitId barn_id = core::AppendRow(world.units, barn);
  core::UnitRow site;
  site.type = core::UnitTypeId{0};
  site.level = 0;  // pegs and string: there is no production here to stop
  site.construction.phase = core::ConstructionPhase::kMarked;
  const core::UnitId site_id = core::AppendRow(world.units, site);
  // A STARTED BUILDING AND A DEMOLITION ARE PAUSED (construction design §6;
  // the human's word of 2026-09-14): work goes on at both, and a level-0 row
  // was refused whatever it was until then.
  core::UnitRow building = site;
  building.construction.phase = core::ConstructionPhase::kBuilding;
  const core::UnitId building_id = core::AppendRow(world.units, building);
  core::UnitRow taking_down = site;
  taking_down.construction.phase = core::ConstructionPhase::kDemolishing;
  const core::UnitId taking_down_id = core::AppendRow(world.units, taking_down);

  const auto give = [&world](core::OrderKind kind, core::UnitId unit) {
    core::OrderRow row;
    row.kind = kind;
    row.status = core::OrderStatus::kPending;
    row.unit = unit;
    return core::AppendRow(world.orders, row);
  };
  const core::OrderId stop = give(core::OrderKind::kPauseUnit, barn_id);
  const core::OrderId on_site = give(core::OrderKind::kPauseUnit, site_id);
  const core::OrderId nowhere = give(core::OrderKind::kPauseUnit, core::UnitId{99});
  const core::OrderId pause_building = give(core::OrderKind::kPauseUnit, building_id);
  const core::OrderId pause_demolition = give(core::OrderKind::kPauseUnit, taking_down_id);

  const core::WorldState before = world;
  system->RunProductionDecisions(before, world);

  const auto refusal = [&world](core::OrderId id) {
    const std::uint32_t row = core::FindRow(world.orders, id);
    return row == core::kNoRow ? core::OrderRefusal::kNone : world.orders.rows[row].refusal;
  };
  const auto status = [&world](core::OrderId id) {
    const std::uint32_t row = core::FindRow(world.orders, id);
    return row == core::kNoRow ? core::OrderStatus::kPending : world.orders.rows[row].status;
  };
  failures += Expect(status(stop) == core::OrderStatus::kDone && world.units.rows[0].paused != 0,
                     "the unit is stopped in the step the order is read");
  failures += Expect(refusal(on_site) == core::OrderRefusal::kRuleForbids,
                     "a marked site has no production to stop");
  failures += Expect(status(pause_building) == core::OrderStatus::kDone &&
                         world.units.rows[core::FindRow(world.units, building_id)].paused != 0,
                     "a started building is paused");
  failures += Expect(status(pause_demolition) == core::OrderStatus::kDone &&
                         world.units.rows[core::FindRow(world.units, taking_down_id)].paused != 0,
                     "and so is a demolition");
  failures += Expect(refusal(nowhere) == core::OrderRefusal::kNoSuchSubject,
                     "and a unit that does not exist is refused for the unit");

  // ONLY WHAT PRODUCES IS PAUSED (units rules §5; boss on econ's audit, П5 /
  // R1). A standing school is refused — a pause there would only stop its
  // wear — while a school being BUILT is work and pauses like any site, and a
  // byre is a farm that produces.
  {
    core::UnitRow school;
    school.type = core::UnitTypeId{1};
    school.level = 1;
    const core::UnitId school_id = core::AppendRow(world.units, school);
    core::UnitRow school_site = school;
    school_site.level = 0;
    school_site.construction.phase = core::ConstructionPhase::kBuilding;
    const core::UnitId school_site_id = core::AppendRow(world.units, school_site);
    core::UnitRow byre;
    byre.type = core::UnitTypeId{2};
    byre.level = 1;
    const core::UnitId byre_id = core::AppendRow(world.units, byre);
    const core::OrderId pause_school = give(core::OrderKind::kPauseUnit, school_id);
    const core::OrderId pause_school_site = give(core::OrderKind::kPauseUnit, school_site_id);
    const core::OrderId pause_byre = give(core::OrderKind::kPauseUnit, byre_id);
    const core::WorldState previous = world;
    system->RunProductionDecisions(previous, world);
    failures += Expect(refusal(pause_school) == core::OrderRefusal::kNotEligible &&
                           world.units.rows[core::FindRow(world.units, school_id)].paused == 0,
                       "a standing school is not paused: it produces nothing to stop");
    failures += Expect(status(pause_school_site) == core::OrderStatus::kDone,
                       "a school being built is work, and work at a site pauses");
    failures += Expect(status(pause_byre) == core::OrderStatus::kDone,
                       "a byre is a farm that produces, and pauses");
    // A school paused before the rule is still let go: resume is never
    // refused for the class.
    world.units.rows[core::FindRow(world.units, school_id)].paused = 1;
    const core::OrderId resume_school = give(core::OrderKind::kResumeUnit, school_id);
    const core::WorldState paused_before = world;
    system->RunProductionDecisions(paused_before, world);
    failures += Expect(status(resume_school) == core::OrderStatus::kDone &&
                           world.units.rows[core::FindRow(world.units, school_id)].paused == 0,
                       "and a school paused before the rule can be resumed");
  }

  // Pausing the paused is refused rather than swallowed: it is not a
  // harmless repeat, it means the chairman is looking at something stale.
  const core::OrderId again = give(core::OrderKind::kPauseUnit, barn_id);
  const core::OrderId start = give(core::OrderKind::kResumeUnit, barn_id);
  {
    const core::WorldState previous = world;
    system->RunProductionDecisions(previous, world);
  }
  failures += Expect(refusal(again) == core::OrderRefusal::kRuleForbids,
                     "stopping what already stands is refused, not silently agreed with");
  failures += Expect(status(start) == core::OrderStatus::kDone && world.units.rows[0].paused == 0,
                     "and resuming starts it again");
  std::filesystem::remove_all(root);
  return failures;
}

/// A table set with nothing in it builds only for a caller that SAYS it wants
/// the documented defaults (core_tables/stub_tables.h).
///
/// One assertion per factory and not one on the assembled simulation, which
/// is what this check first was: the assembly refuses if ANY of the five
/// refuses, so damaging one guard is masked by the other four. A guard has
/// to name its own subject.
int CheckStubTablesMustBeDeclared() {
  int failures = 0;
  const test::FakeTableSet nothing;
  failures += Expect(core::CreateProductionSystem(nothing, core::StubTables::kRefused) == nullptr,
                     "production: a caller that did not allow the defaults is refused");
  failures += Expect(core::CreateProductionSystem(nothing, core::StubTables::kAllowed) != nullptr,
                     "production: and one that did gets them");
  return failures;
}

/// A MEADOW IN FLOWER IS A STATE, AND THE CUT ENDS IT FOR THE YEAR.
///
/// ue took butterflies and had nowhere to put them: MowMeadow returned the
/// field straight to kGrowing, so mown grass and standing grass were the same
/// state, and reading kHarvest as "in flower" would have meant guessing the
/// core's semantics — there kHarvest means "the work is not done", which is
/// about the work order, not the grass.
///
/// The guard walks TWO years rather than sampling a point, because the whole
/// claim is a shape in time: in flower from May until the day of the cut,
/// dark for the rest of that year, and standing again the following spring. A
/// single "is it flowering today" would pass on an implementation that always
/// says yes, and a one-year walk would pass on one that never lets go.
///
/// AND THE MOWING HAPPENS DURING THE WALK, not before it. The first version of
/// this guard set last_mown_day for the whole year up front, which asks a
/// question the game never asks — "what does a meadow mown in July look like
/// in May?" — and under the year rule it made every cut date identical. The
/// choice the design is about only exists if the meadow is standing until the
/// day it is cut.
int CheckTheMeadowFlowersAndTheAftermathComesBack() {
  int failures = 0;
  const test::FakeTableSet tables;
  const auto system = core::CreateProductionSystem(tables, core::StubTables::kAllowed);
  if (Expect(system != nullptr, "the meadow fixture builds a production system") != 0) {
    return 1;
  }

  // A year is 48 days, four to a month, so the window — May, June, July,
  // months 4..6 — is days 16..27, twelve of them. Counted from the calendar,
  // not guessed: an earlier draft called day 20 "the start of May" and failed
  // on the calendar rather than on the code.
  constexpr core::SimDay kWindowFirstDay = 16;
  constexpr core::SimDay kWindowLength = 12;

  // `mown_on` is the day the scythe goes through, or kNeverMownDay. Before
  // it the meadow stands; from it on it carries the cut.
  const auto flowering_days = [&system](core::SimDay mown_on) {
    core::WorldState world;
    core::FieldRow meadow;
    meadow.kind = core::LandKind::kMeadow;
    meadow.area_ga = 10.0F;
    meadow.phase = core::FieldPhase::kGrowing;
    meadow.last_mown_day = core::kNeverMownDay;
    core::AppendRow(world.fields, meadow);
    std::vector<core::SimDay> flowering;
    for (core::SimDay day = 0; day < core::kDaysPerYear * 2; ++day) {
      world.calendar.tick = day * core::kTicksPerDay;
      core::RefreshCalendarCaches(world.calendar);
      if (mown_on != core::kNeverMownDay && day == mown_on) {
        world.fields.rows[0].last_mown_day = mown_on;
      }
      const core::WorldState before = world;
      system->ProductionPhase().RunItemRange(before, world, 0, 1);
      if (world.fields.rows[0].in_flower) {
        flowering.push_back(day);
      }
    }
    return flowering;
  };

  const auto count_in_year = [](const std::vector<core::SimDay>& days, core::SimDay year) {
    std::size_t count = 0;
    for (const core::SimDay day : days) {
      count += (day / core::kDaysPerYear == year) ? 1U : 0U;
    }
    return count;
  };

  // Never mown: it stands through the whole window, both years.
  const auto untouched = flowering_days(core::kNeverMownDay);
  failures += Expect(
      count_in_year(untouched, 0) == kWindowLength && count_in_year(untouched, 1) == kWindowLength,
      "a meadow nobody has cut flowers through the whole window, every year");

  // Cut on the first day of the window: nothing flowers that year at all.
  const auto earliest = flowering_days(kWindowFirstDay);
  failures += Expect(count_in_year(earliest, 0) == 0,
                     "a meadow cut on the first day of the window does not flower that year");

  // Cut two months in: the eight days before the scythe stand, and not one
  // day after it.
  const auto late = flowering_days(kWindowFirstDay + 8);
  failures += Expect(count_in_year(late, 0) == 8,
                     "and a late cut keeps the days BEFORE it — the nectar flow is the timing of "
                     "the mowing, not a switch");
  failures += Expect(count_in_year(late, 0) > count_in_year(earliest, 0),
                     "so a later cut leaves more flowering days than an early one");
  failures += Expect(count_in_year(untouched, 0) > count_in_year(late, 0),
                     "while cutting at all costs flowering days: otherwise the two above would "
                     "agree with a rule that ignores the cut");

  // AND THE LATCH LETS GO IN THE SPRING, not in the winter and not never.
  // This is the half a one-year walk cannot see, and the half that a "days
  // of regrowth" number got wrong: the aftermath does not flower in the year
  // of the cut, and the stand comes back the next.
  failures +=
      Expect(count_in_year(earliest, 1) == kWindowLength && count_in_year(late, 1) == kWindowLength,
             "and the year after a cut the meadow stands again, whenever it was cut");

  // No day outside the window flowers, in either year — otherwise every
  // count above could be satisfied by a rule that ignores the months.
  bool only_inside_the_window = true;
  for (const core::SimDay day : untouched) {
    const core::SimDay in_year = day % core::kDaysPerYear;
    only_inside_the_window = only_inside_the_window && in_year >= kWindowFirstDay &&
                             in_year < kWindowFirstDay + kWindowLength;
  }
  failures += Expect(only_inside_the_window,
                     "and nothing flowers outside May-July: August belongs to the yards, not to "
                     "the meadow");
  return failures;
}

/// The reaping gate in one home: front edge, ripeness, back edge. The case the
/// static analysis named (UB-001, a late same-year sowing ripening past its
/// window) is deliberately NOT asserted either way — the repair is held for
/// boss's decision, and an assertion holding the defect would be a claim
/// invented to give the test one.
int CheckTheReapingGate() {
  int failures = 0;
  core::ProductionConfig config;
  core::CropDef spring;
  spring.sow_from_month = 3;
  spring.sow_to_month = 4;
  spring.harvest_from_month = 7;
  spring.harvest_to_month = 8;
  core::CropDef winter = spring;
  winter.is_winter = true;
  winter.harvest_from_month = 6;
  winter.harvest_to_month = 6;
  config.crops = {spring, winter};

  const std::int32_t ripen = core::RipenDays(config, core::CropId{0});
  failures += Expect(ripen == 9, "reaping gate: the fixture crop ripens in nine days");

  core::FieldRow in_time;
  in_time.crop = core::CropId{0};
  in_time.sown_day = (4U * core::kDaysPerMonth) + 3U;  // last sowing day: ripe on day 28
  failures += Expect(core::ReapingMayOpen(config, in_time, 7, 7U * core::kDaysPerMonth),
                     "reaping gate: a ripe crop opens inside its window");
  failures += Expect(!core::ReapingMayOpen(config, in_time, 6, 6U * core::kDaysPerMonth),
                     "reaping gate: the front edge holds");
  core::FieldRow just_sown = in_time;
  just_sown.sown_day = 7U * core::kDaysPerMonth;
  failures += Expect(!core::ReapingMayOpen(config, just_sown, 7, just_sown.sown_day + 8U),
                     "reaping gate: an unripe crop does not open inside its window");
  // UB-001: a spring crop that ripened after its window stands and may still
  // be reaped. Reddens on the old gate, which closed every crop at the back edge.
  core::FieldRow late = in_time;
  late.sown_day = (8U * core::kDaysPerMonth) + 2U;
  failures += Expect(core::ReapingMayOpen(config, late, 10, late.sown_day + 9U),
                     "reaping gate: a late spring crop ripe past its window may still be reaped");
  failures += Expect(!core::ReapingMayOpen(config, late, 10, late.sown_day + 8U),
                     "reaping gate: past its window a spring crop still has to be ripe");

  core::FieldRow winter_field;
  winter_field.crop = core::CropId{1};
  failures += Expect(core::ReapingMayOpen(config, winter_field, 6, 6U * core::kDaysPerMonth),
                     "reaping gate: a winter crop opens inside its window");
  failures += Expect(!core::ReapingMayOpen(config, winter_field, 7, 7U * core::kDaysPerMonth),
                     "reaping gate: a winter crop keeps the window's back edge");
  return failures;
}

/// kFellingUnreachable (boss, parcel 308): a stand marked for felling that the
/// brigade's ride from the nearest lived-in house does not reach — past the
/// road limit, or with no daylight left after the ride there and back — is
/// named; a near one, an unmarked one and an empty house are not.
int CheckFellingUnreachable() {
  int failures = 0;
  core::ProductionConfig config;
  config.harness_speed_kmh = 12.0F;  // one game hour a kilometre
  config.travel_limit_hours = 4.0F;
  config.min_usable_hours = 1.0F;
  core::WorldState world;
  world.weather.daylight_hours = 12.0F;
  core::UnitRow house;
  house.level = 1;
  house.household = core::FamilyId{1};
  core::AppendRow(world.units, house);
  core::UnitRow empty_house;  // right beside the far stand, and nobody lives in it
  empty_house.level = 1;
  empty_house.position = core::Vec2{.x = 4400.0F, .y = 0.0F};
  core::AppendRow(world.units, empty_house);
  const auto stand_at = [&world](float x, float marked) {
    core::TimberStandRow stand;
    stand.position = core::Vec2{.x = x, .y = 0.0F};
    stand.stock_m3 = 100.0F;
    stand.marked_m3 = marked;
    stand.work_days_remaining = marked > 0.0F ? 1.0F : 0.0F;
    return core::AppendRow(world.stands, stand);
  };
  const core::TimberStandId near = stand_at(2000.0F, 10.0F);
  const core::TimberStandId far = stand_at(4500.0F, 10.0F);
  stand_at(6000.0F, 0.0F);  // far and unmarked: nothing asked of anybody
  const core::TimberStandId edge = stand_at(3500.0F, 10.0F);

  const auto named = [](const std::vector<core::Alarm>& alarms, core::TimberStandId stand) {
    std::int64_t hours = -1;
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind == core::AlarmKind::kFellingUnreachable && alarm.stand.value == stand.value) {
        hours = alarm.amount;
      }
    }
    return hours;
  };
  std::vector<core::Alarm> summer;
  core::CollectTimberAlarms(config, world, summer);
  failures += Expect(summer.size() == 1 && named(summer, far) == 4,
                     "felling out of reach: in summer only the stand 4.5 km out is named, with "
                     "its ride of 4 hours — not the near one, not the unmarked one, and the empty "
                     "house beside it is nobody's road");
  failures += Expect(named(summer, near) < 0 && named(summer, edge) < 0,
                     "felling out of reach: 2 and 3.5 hours of ride fit a summer day");
  world.weather.daylight_hours = 7.0F;
  std::vector<core::Alarm> winter;
  core::CollectTimberAlarms(config, world, winter);
  failures += Expect(named(winter, edge) == 3 && named(winter, near) < 0,
                     "felling out of reach: in a 7-hour winter day 3.5 hours there and back "
                     "leave nothing, and the stand is named; the near one is not");
  return failures;
}

/// Felling (timber design §8a, 2026-09-13): the mark, its refusals, the logs
/// laid down when the crew is done, and the old forest's ceiling.
int CheckFelling() {
  int failures = 0;
  core::ProductionConfig config;
  core::TimberCatalog& timber = config.timber;
  timber.log_m3 = 0.25F;
  timber.log_grams = 200000;
  timber.felling_days_per_m3 = 0.05F;
  timber.forest_old_m3_per_ha_year = 0.05F;
  timber.old_log_share_factor = 0.5F;
  timber.fallen_vanish_years = 2.0F;
  timber.stands = {{.kind = core::TimberStandKind::kGrove,
                    .position = core::Vec2{.x = 0.0F, .y = 0.0F},
                    .area_ha = 10.0F,
                    .log_share = 0.25F},
                   {.kind = core::TimberStandKind::kForestOld,
                    .position = core::Vec2{.x = 0.0F, .y = 0.0F},
                    .area_ha = 100.0F,
                    .log_share = 0.5F}};

  core::WorldState world;
  core::TimberStandRow grove;
  grove.table_row = 0;
  grove.kind = core::TimberStandKind::kGrove;
  grove.stock_m3 = 300.0F;
  const core::TimberStandId grove_id = core::AppendRow(world.stands, grove);
  core::TimberStandRow forest;
  forest.table_row = 1;
  forest.kind = core::TimberStandKind::kForestOld;
  core::AppendRow(world.stands, forest);

  core::OrderRow order;
  order.kind = core::OrderKind::kMarkFelling;
  order.stand = core::TimberStandId{999};
  order.volume_m3 = 10.0F;
  failures += Expect(core::MarkFelling(config, world, order) == core::OrderRefusal::kNoSuchSubject,
                     "felling: a stand that is not there is no such subject");
  order.stand = grove_id;
  order.volume_m3 = 301.0F;
  failures += Expect(core::MarkFelling(config, world, order) == core::OrderRefusal::kRuleForbids,
                     "felling: more than the stand holds is refused");
  order.volume_m3 = 40.0F;
  failures += Expect(core::MarkFelling(config, world, order) == core::OrderRefusal::kNone,
                     "felling: a volume the stand holds is marked");
  core::TimberStandRow& marked = world.stands.rows[0];
  failures += Expect(marked.marked_m3 == 40.0F && marked.work_days_remaining == 2.0F,
                     "felling: the mark opens a seam of volume x days per m3");
  failures +=
      Expect(core::MarkFelling(config, world, order) == core::OrderRefusal::kConflictsWithActive,
             "felling: a second mark on the stand being felled is a conflict");
  // AS MANY FELLINGS AT ONCE AS THE CHAIRMAN MARKS (the human, 2026-09-14): a
  // second grove is marked while the first is being felled.
  core::TimberStandRow second_grove;
  second_grove.table_row = 0;
  second_grove.kind = core::TimberStandKind::kGrove;
  second_grove.stock_m3 = 50.0F;
  const core::TimberStandId second_id = core::AppendRow(world.stands, second_grove);
  core::OrderRow second_order = order;
  second_order.stand = second_id;
  second_order.volume_m3 = 20.0F;
  failures += Expect(core::MarkFelling(config, world, second_order) == core::OrderRefusal::kNone,
                     "felling: another stand is marked while the first is still being felled");
  core::RemoveRow(world.stands, second_id);

  core::FellFinishedStands(config, world);
  failures +=
      Expect(world.stands.rows[0].stock_m3 == 300.0F && world.stands.rows[0].load_grams == 0,
             "felling: nothing is felled while the crew still has work");
  world.stands.rows[0].work_days_remaining = 0.0F;
  core::FellFinishedStands(config, world);
  // 40 m3 x 0.25 of logs / 0.25 m3 a log = 40 logs of 200 kg.
  failures +=
      Expect(world.stands.rows[0].stock_m3 == 260.0F && world.stands.rows[0].marked_m3 == 0.0F &&
                 world.stands.rows[0].load_grams == 40 * 200000,
             "felling: the finished mark leaves the stock and lies as logs");

  for (int turn = 0; turn < 3; ++turn) {
    core::GrowOldForest(config, world);
  }
  // 100 ha x 0.05 = 5 m3 a year, two years at most: 10, not 15.
  failures += Expect(world.stands.rows[1].stock_m3 == 10.0F,
                     "felling: the old forest holds no more than the vanish years of trunks");
  failures += Expect(world.stands.rows[0].stock_m3 == 260.0F,
                     "felling: the old forest's turn leaves a grove alone");

  timber.tool_resource = core::ResourceId{3};
  timber.tool_grams = 3000;
  timber.tools_per_feller = 1.0F;
  failures +=
      Expect(core::FellingCrewCap(timber, 5 * 3000) == 5 && core::FellingCrewCap(timber, 2999) == 0,
             "felling: the crew is capped by whole tools in the stores");
  return failures;
}

/// Digging clay, stone and sand (construction design §3; boss, parcel 270):
/// the mark's refusals, the seam by the material's labour per tonne, the dug
/// mass laid on the site, the exhausted site told once and never marked
/// again, the crew capped by tools, and the shipped catalogue's six plots.
int CheckExtraction() {
  int failures = 0;
  core::ProductionConfig config;
  core::ExtractionCatalog& extraction = config.extraction;
  extraction.resources = {core::ResourceId{5}, core::ResourceId{6}, core::ResourceId{7}};
  extraction.days_per_t = {0.05F, 0.25F, 0.03F};

  core::WorldState world;
  core::ExtractionSiteRow quarry;
  quarry.resource = core::ResourceId{6};  // stone: 0.25 man-days a tonne
  quarry.stock_grams = 30'000'000;        // 30 t
  const core::ExtractionSiteId quarry_id = core::AppendRow(world.extraction_sites, quarry);
  core::ExtractionSiteRow foreign;
  foreign.resource = core::ResourceId{9};  // a resource nobody digs
  foreign.stock_grams = 30'000'000;
  const core::ExtractionSiteId foreign_id = core::AppendRow(world.extraction_sites, foreign);

  core::OrderRow order;
  order.kind = core::OrderKind::kMarkExtraction;
  order.extraction_site = core::ExtractionSiteId{999};
  order.amount = 10'000'000;
  failures +=
      Expect(core::MarkExtraction(config, world, order) == core::OrderRefusal::kNoSuchSubject,
             "digging: a site that is not there is no such subject");
  order.extraction_site = foreign_id;
  failures += Expect(core::MarkExtraction(config, world, order) == core::OrderRefusal::kRuleForbids,
                     "digging: a site of a resource the build does not dig is refused");
  order.extraction_site = quarry_id;
  order.amount = 30'000'001;
  failures += Expect(core::MarkExtraction(config, world, order) == core::OrderRefusal::kRuleForbids,
                     "digging: more than the site holds is refused");
  order.amount = 0;
  failures += Expect(core::MarkExtraction(config, world, order) == core::OrderRefusal::kRuleForbids,
                     "digging: a mass of nothing is refused");
  order.amount = 20'000'000;
  failures += Expect(core::MarkExtraction(config, world, order) == core::OrderRefusal::kNone,
                     "digging: a mass the site holds is marked");
  failures += Expect(world.extraction_sites.rows[0].marked_grams == 20'000'000 &&
                         world.extraction_sites.rows[0].work_days_remaining == 5.0F,
                     "digging: 20 t of stone opens a seam of 20 x 0.25 = 5 man-days");
  failures +=
      Expect(core::MarkExtraction(config, world, order) == core::OrderRefusal::kConflictsWithActive,
             "digging: a second mark on a site still marked is a conflict");

  core::DigFinishedSites(world);
  failures += Expect(world.extraction_sites.rows[0].load_grams == 0 &&
                         world.extraction_sites.rows[0].stock_grams == 30'000'000,
                     "digging: nothing is laid down while the crew still has work");
  world.extraction_sites.rows[0].work_days_remaining = 0.0F;
  core::DigFinishedSites(world);
  failures += Expect(world.extraction_sites.rows[0].load_grams == 20'000'000 &&
                         world.extraction_sites.rows[0].stock_grams == 10'000'000 &&
                         world.extraction_sites.rows[0].marked_grams == 0,
                     "digging: the finished mark leaves the stock and lies as a load");

  // The last ten tonnes: the site runs out, is told of once, and refuses.
  order.amount = 10'000'000;
  failures += Expect(core::MarkExtraction(config, world, order) == core::OrderRefusal::kNone,
                     "digging: the rest of the stock may be marked");
  world.extraction_sites.rows[0].work_days_remaining = 0.0F;
  world.step_events.clear();
  core::DigFinishedSites(world);
  core::DigFinishedSites(world);
  std::uint32_t told = 0;
  bool names_the_site = false;
  for (const core::SimEvent& event : world.step_events) {
    if (event.kind == core::EventKind::kExtractionSiteExhausted) {
      ++told;
      names_the_site =
          event.resource.value == 6 && event.amount == static_cast<std::int64_t>(quarry_id.value);
    }
  }
  failures += Expect(told == 1 && names_the_site && world.extraction_sites.rows[0].exhausted == 1,
                     "digging: an exhausted site is told of once, with its resource and id");
  order.amount = 1;
  failures += Expect(core::MarkExtraction(config, world, order) == core::OrderRefusal::kRuleForbids,
                     "digging: an exhausted site is never marked again");

  extraction.tool_resource = core::ResourceId{3};
  extraction.tool_grams = 3000;
  extraction.tools_per_worker = 1.0F;
  failures += Expect(core::DiggingCrewCap(extraction, 4 * 3000) == 4 &&
                         core::DiggingCrewCap(extraction, 2999) == 0,
                     "digging: the crew is capped by whole tools in the stores");
  return failures;
}

/// The sawmill's day (timber design §8б): what was worked becomes boards out
/// of the logs in the stores, tomorrow's demand is what the logs could still
/// give up to the room the boards need, and a sawmill that cannot saw — paused
/// itself, or its yard paused — asks for nobody.
int CheckSawing() {
  int failures = 0;
  constexpr core::Grams kLog = 200 * core::kGramsPerKilogram;
  constexpr core::Grams kTolerance = 1;
  core::ProductionConfig config;
  config.unit_types.resize(3);
  SetStorageKg(config.unit_types[0], 10000.0F);  // row 0: a store of ten tonnes
  core::TimberCatalog& timber = config.timber;
  timber.log_m3 = 0.25F;
  timber.log_grams = kLog;
  timber.board_yield = 0.55F;
  timber.sawing_days_per_board_m3 = 1.0F;
  timber.board_grams_per_m3 = 600 * core::kGramsPerKilogram;
  timber.log_resource = core::ResourceId{0};
  timber.board_resource = core::ResourceId{1};
  timber.sawmill_type = core::UnitTypeId{2};

  core::WorldState world;
  core::UnitRow store;
  store.type = core::UnitTypeId{0};
  store.level = 1;
  store.stock.assign(2, 0);
  store.stock[0] = 10 * kLog;
  core::AppendRow(world.units, store);
  core::UnitRow yard;
  yard.type = core::UnitTypeId{1};
  yard.level = 1;
  const core::UnitId yard_id = core::AppendRow(world.units, yard);
  core::UnitRow sawmill;
  sawmill.type = core::UnitTypeId{2};
  sawmill.level = 1;
  sawmill.parent = yard_id;
  core::AppendRow(world.units, sawmill);
  const auto near = [](float value, float expected) {
    return value > expected - 0.001F && value < expected + 0.001F;
  };

  core::SettleUnitProduction(config, world);
  // Ten logs of 0.25 m3 at 0.55 are 1.375 m3 of boards, a man-day each.
  failures += Expect(near(world.units.rows[2].production_days_remaining, 1.375F) &&
                         near(world.units.rows[2].production_days_written, 1.375F),
                     "sawing: the demand is the boards the logs in the stores make");
  failures += Expect(world.units.rows[0].stock[0] == 10 * kLog && world.units.rows[0].stock[1] == 0,
                     "sawing: writing the demand saws nothing");

  // The sawyers drained 0.55 man-days: 0.55 m3 of boards out of 1 m3 of logs.
  world.units.rows[2].production_days_remaining -= 0.55F;
  core::SettleUnitProduction(config, world);
  failures += Expect(world.units.rows[0].stock[0] == 6 * kLog,
                     "sawing: the man-days worked take their logs out of the store");
  const core::Grams boards = world.units.rows[0].stock[1];
  failures += Expect(boards > 330 * core::kGramsPerKilogram - kTolerance &&
                         boards < 330 * core::kGramsPerKilogram + kTolerance,
                     "sawing: and put their boards into it — 0.55 m3 at 600 kg");
  failures += Expect(near(world.units.rows[2].production_days_remaining, 0.825F),
                     "sawing: tomorrow's demand is what the six logs left can give");

  // -- WEAR TAKES ITS SHARE OF WHAT THE SAME WORK TURNS OUT ------------------
  // Unit rules §15: a production unit's output falls with its wear. Until
  // 2026-09-17 it did not, anywhere — the contract of unit_state.h declared
  // that rather than hid it — so a hundred and seventy buildings at a mean
  // wear of 23% turned out what new ones did.
  //
  // THE SAME MAN-DAYS, TWICE, AT DIFFERENT WEAR. Anything else would compare
  // two different amounts of work and call the difference wear.
  {
    core::ProductionConfig worn_config = config;
    worn_config.farming.wear_output_loss_at_full = 0.5F;
    const auto saw_at = [&](float wear) {
      core::WorldState fresh = world;
      fresh.units.rows[0].stock[0] = 10 * kLog;
      fresh.units.rows[0].stock[1] = 0;
      fresh.units.rows[2].wear = wear;
      fresh.units.rows[2].production_days_written = 1.0F;
      fresh.units.rows[2].production_days_remaining = 0.0F;
      core::SettleUnitProduction(worn_config, fresh);
      return fresh.units.rows[0].stock[1];
    };
    const core::Grams sound = saw_at(0.0F);
    const core::Grams half_worn = saw_at(50.0F);
    const core::Grams ruined = saw_at(100.0F);
    failures += Expect(sound > 0, "wear: a sound saw turns the man-days into boards");
    // Linear from the first per cent: half the scale is half the loss, so a
    // quarter off. The bands are wide enough to survive rounding in grams and
    // narrow enough to fail on a multiplier applied with the wrong sign or to
    // the wrong quantity.
    failures += Expect(half_worn * 4 > sound * 3 - 1000 && half_worn * 4 < sound * 3 + 1000,
                       "wear: at half the scale a quarter of the output is gone — linear, with no "
                       "dead zone");
    failures += Expect(ruined * 2 > sound - 1000 && ruined * 2 < sound + 1000,
                       "wear: and at the top of the scale HALF — a ruin still works, which is why "
                       "the share is a half and not all of it");
    // THE DEMAND IS NOT TOUCHED, and that is the decision the rule rests on:
    // how many days the stores could feed is about the logs and the room, not
    // about the state of the saw. Charging the wear there too would charge it
    // twice.
    core::WorldState sound_world = world;
    sound_world.units.rows[0].stock[0] = 10 * kLog;
    sound_world.units.rows[0].stock[1] = 0;
    sound_world.units.rows[2].wear = 0.0F;
    core::SettleUnitProduction(worn_config, sound_world);
    core::WorldState worn_world = world;
    worn_world.units.rows[0].stock[0] = 10 * kLog;
    worn_world.units.rows[0].stock[1] = 0;
    worn_world.units.rows[2].wear = 100.0F;
    core::SettleUnitProduction(worn_config, worn_world);
    failures += Expect(near(sound_world.units.rows[2].production_days_remaining,
                            worn_world.units.rows[2].production_days_remaining),
                       "wear: and the DEMAND is untouched — a worn saw is not asked for fewer "
                       "hands, it gives less for the same ones");
  }

  world.units.rows[2].paused = 1;
  core::SettleUnitProduction(config, world);
  failures += Expect(world.units.rows[2].production_days_remaining == 0.0F,
                     "sawing: a paused sawmill asks for nobody");
  world.units.rows[2].paused = 0;
  world.units.rows[1].paused = 1;
  core::SettleUnitProduction(config, world);
  failures += Expect(world.units.rows[2].production_days_remaining == 0.0F,
                     "sawing: nor does one whose yard does not stand sound");
  world.units.rows[1].paused = 0;

  // Room for 60 kg of anything: a tenth of a cubic metre of boards.
  world.units.rows[0].stock[1] =
      10000 * core::kGramsPerKilogram - 6 * kLog - 60 * core::kGramsPerKilogram;
  core::SettleUnitProduction(config, world);
  failures += Expect(near(world.units.rows[2].production_days_remaining, 0.1F),
                     "sawing: nobody is sent to saw more boards than there is room for");

  // PAST THE GRAMS CEILING THE ANSWER IS ZERO, NOT A WRAPPED INT (UB-001,
  // UB-201): on x86 the undefined cast came out as INT64_MIN, which is what
  // these two would read without the guard.
  failures += Expect(core::LogGramsForBoardM3(timber, 3.0e38F) == 0,
                     "sawing: boards past any mass the core counts ask for no logs");
  const core::TimberStandDef huge_grove{.kind = core::TimberStandKind::kGrove,
                                        .position = core::Vec2{.x = 0.0F, .y = 0.0F},
                                        .area_ha = 1.0F,
                                        .log_share = 1.0F};
  failures += Expect(core::LogGramsFromVolume(timber, huge_grove, 1.0e30F) == 0,
                     "felling: a volume past any mass the core counts lays no logs");
  return failures;
}

/// AN UPGRADE'S RECIPE IS NOBODY ELSE'S (boss, parcel 294): a store raising
/// its next level keeps working as a store, and the logs its works hold back
/// (ConstructionState::reserved) are neither taken nor counted as held — not
/// by a plain take, not by the saw's demand, not by the saw's own taking.
int CheckAnUpgradesRecipeIsNobodysElse() {
  int failures = 0;
  constexpr core::Grams kLog = 200 * core::kGramsPerKilogram;
  core::ProductionConfig config;
  config.unit_types.resize(3);
  SetStorageKg(config.unit_types[0], 10000.0F);
  core::TimberCatalog& timber = config.timber;
  timber.log_m3 = 0.25F;
  timber.log_grams = kLog;
  timber.board_yield = 0.55F;
  timber.sawing_days_per_board_m3 = 1.0F;
  timber.board_grams_per_m3 = 600 * core::kGramsPerKilogram;
  timber.log_resource = core::ResourceId{0};
  timber.board_resource = core::ResourceId{1};
  timber.sawmill_type = core::UnitTypeId{2};

  core::WorldState world;
  core::UnitRow store;
  store.type = core::UnitTypeId{0};
  store.level = 1;
  store.stock.assign(2, 0);
  store.stock[0] = 10 * kLog;
  store.construction.phase = core::ConstructionPhase::kBuilding;
  store.construction.target_level = 2;
  store.construction.reserved.assign(1, 6 * kLog);
  core::AppendRow(world.units, store);
  core::UnitRow yard;
  yard.type = core::UnitTypeId{1};
  yard.level = 1;
  const core::UnitId yard_id = core::AppendRow(world.units, yard);
  core::UnitRow sawmill;
  sawmill.type = core::UnitTypeId{2};
  sawmill.level = 1;
  sawmill.parent = yard_id;
  core::AppendRow(world.units, sawmill);

  failures += Expect(core::HeldEverywhere(world, timber.log_resource) == 4 * kLog,
                     "reserved: of ten logs in a store raising its level, four are held for "
                     "anybody");
  core::SettleUnitProduction(config, world);
  // Four logs of 0.25 m3 at 0.55 are 0.55 m3 of boards, a man-day each.
  const float demand = world.units.rows[2].production_days_remaining;
  failures += Expect(demand > 0.549F && demand < 0.551F,
                     "reserved: the saw asks for the boards of the four free logs, not of ten");
  // The sawyers worked 1.375 man-days, a demand written before the works
  // held their logs back: ten logs' worth, and only four are anybody's.
  world.units.rows[2].production_days_written = 1.375F;
  world.units.rows[2].production_days_remaining = 0.0F;
  core::SettleUnitProduction(config, world);
  failures += Expect(world.units.rows[0].stock[0] == 6 * kLog,
                     "reserved: the saw's day takes the four free logs and leaves the six");

  world.units.rows[0].stock[0] = 10 * kLog;
  failures +=
      Expect(core::TakeFromStorage(world, config, timber.log_resource, 10 * kLog) == 4 * kLog &&
                 world.units.rows[0].stock[0] == 6 * kLog,
             "reserved: a take of ten gets four, and the works keep their six");
  failures += Expect(core::TakeFromUnit(world.units.rows[0], timber.log_resource, kLog) == 0,
                     "reserved: nor does a take straight from the unit reach them");
  world.units.rows[0].stock[0] = 5 * kLog;  // the stock has fallen under the line
  failures += Expect(core::UnreservedOf(world.units.rows[0], timber.log_resource) == 0,
                     "reserved: a stock under its reserved line gives nothing, not a debt");
  return failures;
}

/// The district's limit (district design §1, §4; boss, parcels 208, 211):
/// what may be bought, the year's grant, buying, the cart, the year's turn.
/// The district's visits (characters design §2; boss, parcel 324): a regular
/// visit announced its notice ahead and arriving on the first day of its
/// month, Korenev the day after a failed plan, none doubled.
int CheckDistrictVisits() {
  int failures = 0;
  core::ProductionConfig config;  // the catalogue's defaults: June, December, two days
  core::WorldState world;
  const auto count = [&world](core::EventKind kind) {
    std::uint32_t seen = 0;
    for (const core::SimEvent& event : world.step_events) {
      seen += event.kind == kind ? 1U : 0U;
    }
    return seen;
  };
  const auto day = [&world](std::uint32_t value) {
    world.calendar.day = value;
    world.step_events.clear();
  };
  constexpr std::uint32_t kJuneFirst = 5U * core::kDaysPerMonth;  // day 20
  constexpr std::uint32_t kDecemberFirst = 11U * core::kDaysPerMonth;

  day(kJuneFirst - 3U);
  core::AnnounceRegularVisits(config, world);
  failures += Expect(
      world.district_visits.rows.empty() && count(core::EventKind::kDistrictVisitAnnounced) == 0,
      "visits: three days before June nothing is announced yet");
  day(kJuneFirst - 2U);
  core::AnnounceRegularVisits(config, world);
  core::AnnounceRegularVisits(config, world);
  core::DistrictVisitOutcome announced;
  failures += Expect(
      world.district_visits.rows.size() == 1 &&
          world.district_visits.rows[0].face == core::DistrictFace::kKarasev &&
          world.district_visits.rows[0].arrive_day == kJuneFirst &&
          count(core::EventKind::kDistrictVisitAnnounced) == 1 &&
          core::UnpackDistrictVisit(world.step_events[0].amount, announced) &&
          announced.face == core::DistrictFace::kKarasev &&
          announced.kind == core::DistrictVisitKind::kRegular,
      "visits: two days before June Karasev's regular visit is announced, once however often "
      "the day asks");
  day(kJuneFirst - 1U);
  core::ArriveDistrictVisits(config, world);
  failures +=
      Expect(world.district_visits.rows.size() == 1 && count(core::EventKind::kDistrictVisit) == 0,
             "visits: the day before, he has not arrived");
  day(kJuneFirst);
  core::ArriveDistrictVisits(config, world);
  core::DistrictVisitOutcome arrived;
  failures +=
      Expect(world.district_visits.rows.empty() && count(core::EventKind::kDistrictVisit) == 1 &&
                 world.step_events[0].severity == core::EventSeverity::kNotable &&
                 core::UnpackDistrictVisit(world.step_events[0].amount, arrived) &&
                 arrived.face == core::DistrictFace::kKarasev &&
                 arrived.kind == core::DistrictVisitKind::kRegular &&
                 arrived.found == core::DistrictVisitFinding::kNone,
             "visits: on June's first day Karasev arrives, notable, and finds nothing");

  day(core::kDaysPerYear + kDecemberFirst - 2U);
  core::AnnounceRegularVisits(config, world);
  failures +=
      Expect(world.district_visits.rows.size() == 1 &&
                 world.district_visits.rows[0].face == core::DistrictFace::kPolushkina &&
                 world.district_visits.rows[0].arrive_day == core::kDaysPerYear + kDecemberFirst,
             "visits: in the second year Polushkina is announced for December");
  world.district_visits = core::DistrictVisitTable{};

  day(2U * core::kDaysPerYear);
  core::CallPlanFailedVisit(world);
  core::CallPlanFailedVisit(world);
  core::ArriveDistrictVisits(config, world);
  failures +=
      Expect(world.district_visits.rows.size() == 1 &&
                 world.district_visits.rows[0].face == core::DistrictFace::kKorenev &&
                 world.district_visits.rows[0].kind == core::DistrictVisitKind::kExtraordinary &&
                 world.district_visits.rows[0].cause == core::DistrictVisitCause::kPlanFailed &&
                 count(core::EventKind::kDistrictVisit) == 0 &&
                 count(core::EventKind::kDistrictVisitAnnounced) == 0,
             "visits: a failed plan calls Korenev once, unannounced, not for today");
  day((2U * core::kDaysPerYear) + 1U);
  core::ArriveDistrictVisits(config, world);
  core::DistrictVisitOutcome korenev;
  failures +=
      Expect(world.district_visits.rows.empty() && count(core::EventKind::kDistrictVisit) == 1 &&
                 world.step_events[0].severity == core::EventSeverity::kInterrupting &&
                 core::UnpackDistrictVisit(world.step_events[0].amount, korenev) &&
                 korenev.face == core::DistrictFace::kKorenev &&
                 korenev.kind == core::DistrictVisitKind::kExtraordinary,
             "visits: the next morning Korenev's extraordinary visit interrupts");

  // A notice of zero days: the announcement and the arrival share one tick,
  // the announcement first.
  config.district_visits.notice_days = 0;
  day(kJuneFirst);
  core::AnnounceRegularVisits(config, world);
  core::ArriveDistrictVisits(config, world);
  failures += Expect(world.step_events.size() == 2 &&
                         world.step_events[0].kind == core::EventKind::kDistrictVisitAnnounced &&
                         world.step_events[1].kind == core::EventKind::kDistrictVisit,
                     "visits: with no notice the visit is announced and arrives the same tick");

  failures += Expect(
      core::SeniorOfChannel(core::DistrictFace::kKarasev) == core::DistrictFace::kStozharov &&
          core::SeniorOfChannel(core::DistrictFace::kPolushkina) == core::DistrictFace::kZhernova &&
          core::SeniorOfChannel(core::DistrictFace::kKorenev) == core::DistrictFace::kKorenev,
      "visits: a junior's senior is the senior of the channel");
  return failures;
}

/// THE ACCUMULATION LIMIT (district §9; register 234; boss seq 76 and 81):
/// named with the plan off (next year's seed + the figure + last year's eaten
/// and fed) × the share, absent in the first year; a finance auditor finds a
/// surplus above it and the district seizes it whole, reputation down; the
/// agronomist does not count stores, and a store under the limit is clean.
int CheckTheAccumulationLimit() {
  int failures = 0;
  constexpr core::Grams kTonne = core::kGramsPerTonne;
  core::ProductionConfig config;
  config.unit_types.resize(1);
  config.unit_types[0].level_storage_capacity_kg = {100'000.0F};
  core::CropDef rye;
  rye.resource = core::ResourceId{0};
  rye.yield_kg_per_ha = 1000.0F;
  rye.sowing_norm_kg_per_ha = 100.0F;
  config.crops = {rye};
  config.plan_positions = {
      core::ProductionConfig::PlanPosition{.crop = core::CropId{0}, .area_share = 0.5F}};
  config.limit.accumulation_share = 1.5F;
  config.limit.seizure_reputation_loss = 10.0F;

  core::WorldState world;
  core::FieldRow next_rye;
  next_rye.kind = core::LandKind::kArable;
  next_rye.area_ga = 10.0F;  // 1 t of seed next year
  next_rye.rotation_assigned = 1;
  next_rye.rotation_year1 = core::CropId{0};
  core::AppendRow(world.fields, next_rye);
  world.plan.due = {5 * kTonne};
  world.ledger.closed.eaten = {3 * kTonne};
  world.ledger.closed.feed = {1 * kTonne};

  core::NameAccumulationLimit(config, world, true);
  failures += Expect(world.plan.accumulation_limit.empty() || world.plan.accumulation_limit[0] == 0,
                     "limit: the first year has none — the district has no book to size it");
  core::NameAccumulationLimit(config, world, false);
  // (1 seed + 5 figure + 3 eaten + 1 fed) × 1.5 = 15 t.
  failures += Expect(
      world.plan.accumulation_limit.size() == 1 && world.plan.accumulation_limit[0] == 15 * kTonne,
      "limit: seed, figure, eaten and fed of the year gone, times the share");

  core::UnitRow barn;
  barn.type = core::UnitTypeId{0};
  barn.level = 1;
  barn.stock = {14 * kTonne};
  const core::UnitId barn_id = core::AppendRow(world.units, barn);
  const auto visit_of = [](core::DistrictFace face) {
    core::DistrictVisitRow row;
    row.face = face;
    row.kind = core::DistrictVisitKind::kRegular;
    return row;
  };
  failures += Expect(core::InspectVisit(world, visit_of(core::DistrictFace::kPolushkina)).found ==
                         core::DistrictVisitFinding::kNone,
                     "limit: 14 t under a 15 t limit is clean");
  world.units.rows[core::FindRow(world.units, barn_id)].stock = {20 * kTonne};
  failures += Expect(core::InspectVisit(world, visit_of(core::DistrictFace::kPolushkina)).found ==
                             core::DistrictVisitFinding::kDiscrepancy &&
                         core::InspectVisit(world, visit_of(core::DistrictFace::kKarasev)).found ==
                             core::DistrictVisitFinding::kNone,
                     "limit: the finance auditor finds 5 t over; the agronomist counts no store");

  world.chairman.raikom_reputation = 50.0F;
  const core::Grams seized = core::SeizeAboveLimit(config, world);
  failures += Expect(
      seized == 5 * kTonne &&
          world.units.rows[core::FindRow(world.units, barn_id)].stock[0] == 15 * kTonne &&
          world.ledger.current.seized.size() == 1 && world.ledger.current.seized[0] == 5 * kTonne,
      "limit: the surplus is seized whole and booked, the limit left standing");
  failures += Expect(world.chairman.raikom_reputation == 40.0F,
                     "limit: a seizure costs the raikom's reputation");
  failures += Expect(world.plan.delivered.empty() || world.plan.delivered[0] == 0,
                     "limit: what is seized is not delivered and pays no overfulfilment");
  return failures;
}

/// THE TEAM'S OATS ARE HUSBANDRY, NOT A HOARD (boss seq 83): on the shipped
/// tables, forty working horses raise the oat limit by exactly the share of
/// their fodder fund — the term that kept the bare village from being seized
/// 3.0 t and 7.9 t of oats it was merely feeding a growing team with.
int CheckTheLimitKeepsTheTeamsOats() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "limit: the shipped tables give a production configuration") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const core::ITable* const resources = tables->FindTable("resources");
  const core::ITable* const livestock = tables->FindTable("livestock");
  const auto oat = core::ResourceId{static_cast<std::uint16_t>(resources->FindRowByKey("oat"))};
  const auto limit_of_oats = [&config, oat](const core::WorldState& world) {
    core::WorldState named = world;
    core::NameAccumulationLimit(config, named, false);
    return oat.value < named.plan.accumulation_limit.size()
               ? named.plan.accumulation_limit[oat.value]
               : core::Grams{0};
  };
  core::WorldState without;
  without.calendar.tick = 16U * core::kTicksPerDay;  // May, the plan's spring
  core::RefreshCalendarCaches(without.calendar);
  without.ledger.closed.eaten.assign(resources->RowCount(), 1'000'000);
  core::WorldState with = without;
  core::HerdRow team;
  team.kind = core::LivestockKindId{static_cast<std::uint16_t>(livestock->FindRowByKey("horse"))};
  team.adult_count = 40;
  core::AppendRow(with.herds, team);
  const core::Grams fund = core::FodderFundGrams(config, with, oat);
  const core::Grams raised = limit_of_oats(with) - limit_of_oats(without);
  const auto expected = static_cast<core::Grams>(std::llround(
      static_cast<double>(fund) * static_cast<double>(config.limit.accumulation_share)));
  failures += Expect(fund > 0 && std::llabs(raised - expected) <= 1,
                     "limit: forty horses raise the oat limit by the share of their fodder fund");
  return failures;
}

/// «СДАТЬ СЕЙЧАС» (kDeliverPlan; econ's audit M2, Л1): the chairman ships
/// what is owed before the turn, and the turn ships only the rest.
int CheckDeliverPlanNow() {
  int failures = 0;
  constexpr core::Grams kTonne = 1'000'000;
  core::ProductionConfig config;
  config.unit_types.resize(1);
  config.unit_types[0].level_storage_capacity_kg = {100'000.0F};
  const auto make_world = [](core::Grams in_store) {
    core::WorldState world;
    core::UnitRow barn;
    barn.type = core::UnitTypeId{0};
    barn.level = 1;
    barn.stock = {in_store, 0};
    core::AppendRow(world.units, barn);
    world.plan.announced = 1;
    world.plan.due = {10 * kTonne, 2 * kTonne};
    world.plan.delivered = {0, 0};
    return world;
  };

  core::WorldState before_spring;
  failures += Expect(core::DeliverPlanNow(config, before_spring, core::ResourceId{}, 0) ==
                         core::OrderRefusal::kNoPlanYet,
                     "before the spring's figure there is nothing to ship");

  // Six tonnes in the barn of ten owed: all six go now, the plan counts them.
  core::WorldState early = make_world(6 * kTonne);
  failures += Expect(
      core::DeliverPlanNow(config, early, core::ResourceId{0}, 0) == core::OrderRefusal::kNone &&
          early.plan.delivered[0] == 6 * kTonne && early.units.rows[0].stock[0] == 0,
      "shipping now takes what the barn holds of what is owed");
  failures += Expect(early.plan.delivered[1] == 0, "and only the position named: the other waits");
  failures += Expect(core::DeliverPlanNow(config, early, core::ResourceId{0}, 0) ==
                         core::OrderRefusal::kRuleForbids,
                     "a shipment that moves nothing is refused, not done");

  // The harvest brings eight more; the turn ships the four still owed, not ten.
  early.units.rows[0].stock[0] = 8 * kTonne;
  core::DeliverPlan(config, early);
  failures +=
      Expect(early.plan.delivered[0] == 10 * kTonne && early.units.rows[0].stock[0] == 4 * kTonne,
             "the turn ships what is still owed and not the figure twice");

  // OVER THE DEBT (district §1): the position is paid in full, and three more
  // tonnes of it go by quantity — the surplus the limit points are for.
  failures += Expect(core::DeliverPlanNow(config, early, core::ResourceId{0}, 3 * kTonne) ==
                             core::OrderRefusal::kNone &&
                         early.plan.delivered[0] == 13 * kTonne &&
                         early.units.rows[0].stock[0] == 1 * kTonne,
                     "a quantity ships over the debt: delivered stands above due");
  failures += Expect(core::DeliverPlanNow(config, early, core::ResourceId{5}, kTonne) ==
                         core::OrderRefusal::kRuleForbids,
                     "a resource the plan asks nothing of is no position to deliver to");
  failures += Expect(core::DeliverPlanNow(config, early, core::ResourceId{1}, kTonne) ==
                             core::OrderRefusal::kRuleForbids &&
                         early.plan.delivered[1] == 0,
                     "a quantity of what the barn does not hold moves nothing and is refused");

  // THE TONNES OVER, IN GRAIN (district §1; boss seq 70): each position's
  // surplus weighed by its calories against grain, and nothing while any
  // position is short.
  config.food_kcal_per_gram = {3.3F, 0.77F};  // rye, potato
  failures +=
      Expect(core::PlanOverfulfilGrainTonnes(config, early) == 0.0F,
             "overfulfilment: three tonnes of rye over do not cover a potato not delivered");
  early.plan.delivered[1] = 6 * kTonne;
  const float over = core::PlanOverfulfilGrainTonnes(config, early);
  // Rye 3 t over, potato 4 t over at 0.77 / 3.3: 3 + 0.933 = 3.933 t of grain.
  failures += Expect(over > 3.93F && over < 3.94F,
                     "overfulfilment: a tonne of potato over weighs its calories in grain");
  return failures;
}

int CheckDistrictLimit() {
  int failures = 0;
  constexpr core::Grams kPane = 5 * core::kGramsPerKilogram;
  core::ProductionConfig config;
  config.unit_types.resize(1);
  SetStorageKg(config.unit_types[0], 100.0F);  // a store of 100 kg
  core::LimitCatalog& limit = config.limit;
  // Row 0 glass (goods, Epoch I), row 1 a horse (livestock), row 2 a tractor
  // lot of Epoch II, row 3 goods with no price, row 4 goods with no amount.
  // THE LOTS ARE BUILT FIELD BY FIELD AND NOT BY DESIGNATED INITIALISER since
  // the livestock window's contract (2026-09-16). LimitLotDef grew four
  // fields, and a designated initialiser must name EVERY field or refuse to
  // compile — which is a good tripwire on a struct that decides what the
  // district sells, and a bad one to answer with eight lines of `{}` padding.
  const auto lot = [](std::int32_t points,
                      std::uint8_t era,
                      core::LimitLotKind kind,
                      core::ResourceAmounts goods) {
    core::LimitLotDef def;
    def.points = points;
    def.era = era;
    def.kind = kind;
    def.goods = goods;
    return def;
  };
  limit.lots.resize(6);
  limit.lots[0] = lot(25, 1, core::LimitLotKind::kGoods, {24 * kPane});
  // A LIVESTOCK LOT WHOSE HEAD COUNT IS NOT WRITTEN — the piglet and chick
  // batches of the shipped tables. It carries an amount of goods on purpose:
  // with none, the "nothing written" rule of the GOODS branch would refuse it
  // too and the livestock branch would go untested (damage run D10,
  // 2026-09-13, made exactly that mistake in the other direction).
  limit.lots[1] = lot(70, 1, core::LimitLotKind::kLivestock, {kPane});
  limit.lots[2] = lot(45, 2, core::LimitLotKind::kGoods, {kPane});
  limit.lots[3] = lot(-1, 1, core::LimitLotKind::kGoods, {kPane});
  limit.lots[4] = lot(20, 1, core::LimitLotKind::kGoods, {0});
  // And a horse: priced, of epoch I, one grown head, the sex asked for.
  limit.lots[5] = lot(70, 1, core::LimitLotKind::kLivestock, {});
  limit.lots[5].livestock = core::LivestockKindId{0};
  limit.lots[5].head_count = 1;
  limit.lots[5].sex_choice = true;
  const auto orderable = [&limit](std::uint16_t lot) {
    return core::LotOrderable(limit, core::LimitLotId{lot}, core::Epoch::kOne);
  };
  failures += Expect(orderable(0) == core::OrderRefusal::kNone &&
                         orderable(9) == core::OrderRefusal::kNoSuchSubject &&
                         orderable(2) == core::OrderRefusal::kGateClosed,
                     "limit: glass is bought in Epoch I, an unknown lot is no subject, a later "
                     "epoch's gate is shut");
  failures += Expect(orderable(1) == core::OrderRefusal::kRuleForbids &&
                         orderable(3) == core::OrderRefusal::kRuleForbids &&
                         orderable(4) == core::OrderRefusal::kRuleForbids,
                     "limit: a livestock lot whose head count is not written, an unpriced lot and "
                     "a lot with no written amount are not bought here");
  // AND STOCK IS BOUGHT HERE SINCE 2026-09-16. This line said the opposite
  // for three days, and the opposite had got as far as a named rejection in
  // the design — «район живое не покупает» — on the strength of a refusal in
  // the core that called itself a STUB in its own comment. The catalogue has
  // priced a horse at 70 points from epoch I all along.
  failures += Expect(orderable(5) == core::OrderRefusal::kNone,
                     "limit: a priced livestock lot with a head count IS bought here — it is the "
                     "design's «страховка от тупика» and the only way out of a dead team");

  failures += Expect(core::LimitReputationMultiplier(10.0F) == 0.7F &&
                         core::LimitReputationMultiplier(50.0F) == 1.0F &&
                         core::LimitReputationMultiplier(90.0F) == 1.4F,
                     "limit: the raikom's reputation multiplies the grant by the design's bands");
  failures += Expect(
      core::YearLimitPoints(limit, core::FarmStatusTier::kLagging, false, 0.0F, 50.0F) == 350 &&
          core::YearLimitPoints(limit, core::FarmStatusTier::kLagging, true, 0.0F, 50.0F) == 500 &&
          core::YearLimitPoints(limit, core::FarmStatusTier::kLagging, true, 0.0F, 10.0F) == 350 &&
          core::YearLimitPoints(limit, core::FarmStatusTier::kLeading, false, 35.0F, 50.0F) == 590,
      "limit: base by tier, +150 for a plan in full, x0.7 at a poor reputation, and 35 t of "
      "grain over on the falling scale, 100 + 200 + 40");

  core::WorldState world;
  world.epoch = core::Epoch::kOne;
  world.calendar.day = 100;
  core::UnitRow store;
  store.type = core::UnitTypeId{0};
  store.level = 1;
  store.stock.assign(1, 0);
  core::AppendRow(world.units, store);
  world.limit.points = 30;

  core::OrderRow order;
  order.kind = core::OrderKind::kOrderLimitLot;
  order.lot = core::LimitLotId{0};
  failures += Expect(core::OrderLimitLot(config, world, order) == core::OrderRefusal::kNone &&
                         world.limit.points == 5 && world.ledger.current.limit_points_spent == 25,
                     "limit: buying glass takes its 25 points and books them as spent");
  failures += Expect(core::OrderLimitLot(config, world, order) == core::OrderRefusal::kLimitShort &&
                         world.limit.points == 5 && world.limit_deliveries.rows.size() == 1,
                     "limit: a second lot the points no longer cover is refused and costs nothing");
  const std::uint32_t arrive = world.limit_deliveries.rows[0].arrive_day;
  failures += Expect(arrive >= 102 && arrive <= 104,
                     "limit: the cart is due in two days plus a delay of zero to two");

  world.calendar.day = arrive - 1;
  core::ArriveLimitDeliveries(config, world);
  failures +=
      Expect(world.units.rows[0].stock[0] == 0, "limit: nothing comes before the cart's day");
  world.calendar.day = arrive;
  core::ArriveLimitDeliveries(config, world);
  // 24 panes of 5 kg into a 100 kg store: 20 go in, 4 wait at the gate.
  failures += Expect(world.units.rows[0].stock[0] == 100 * core::kGramsPerKilogram &&
                         world.limit_deliveries.rows.size() == 1 &&
                         world.limit_deliveries.rows[0].goods[0] == 4 * kPane,
                     "limit: what fits goes in, what does not waits on the cart");
  world.units.rows[0].stock[0] = 0;
  world.calendar.day = arrive + 1;
  core::ArriveLimitDeliveries(config, world);
  failures +=
      Expect(world.units.rows[0].stock[0] == 4 * kPane && world.limit_deliveries.rows.empty(),
             "limit: the rest comes the next day and the empty cart leaves");

  // -- the livestock window (2026-09-16) -------------------------------------
  //
  // THE ROOM FIRST, because a refusal must cost nothing. The world above has
  // one unit with no livestock capacity and NO FAMILIES at all, so it has
  // nowhere at all to put a head — the state a refusal has to name.
  config.livestock.resize(1);
  config.livestock[0].adult_from_game_months = 24.0F;
  world.limit.points = 200;
  core::OrderRow buy;
  buy.kind = core::OrderKind::kOrderLimitLot;
  buy.lot = core::LimitLotId{5};
  buy.male = 1;
  failures +=
      Expect(core::OrderLimitLot(config, world, buy) == core::OrderRefusal::kNoRoomForStock &&
                 world.limit.points == 200 && world.livestock_arrivals.rows.empty(),
             "stock: with no roof and no yard the head is refused by name, and the refusal costs "
             "not one point");

  // ONE YARD IS ENOUGH, at two head a yard. The ceiling is the design's
  // «некуда поставить — нельзя заказать» and the number is measured, not
  // chosen: the model never billets more than 0.95 head per yard.
  config.farming.billet_heads_per_yard = 2.0F;
  core::AppendRow(world.families, core::FamilyRow{});
  failures += Expect(core::OrderLimitLot(config, world, buy) == core::OrderRefusal::kNone &&
                         world.limit.points == 130 && world.livestock_arrivals.rows.size() == 1,
                     "stock: a yard with room buys the head and pays its seventy points");
  // GUARDED, AND THE GUARD IS NOT DEFENSIVE HABIT. The first damage run on
  // this block shut the window again, the purchase was refused, and the
  // indexing below dumped core instead of reddening — a crash that a count of
  // FAIL lines reads as zero failures. A check that turns into a crash under
  // damage is a check that cannot be measured.
  const bool bought = world.livestock_arrivals.rows.size() == 1;
  const core::LivestockArrivalRow coming =
      bought ? world.livestock_arrivals.rows[0] : core::LivestockArrivalRow{};
  failures +=
      Expect(bought && coming.kind.value == 0 && coming.head_count == 1 && coming.male == 1 &&
                 coming.stage == core::LivestockArrivalStage::kAdultStart,
             "stock: the head travels knowing its kind, its number, its age and the sex "
             "the chairman asked for");
  failures += Expect(bought && coming.arrive_day >= world.calendar.day + 2 &&
                         coming.arrive_day <= world.calendar.day + 4,
                     "stock: and it is due in the district's own days — the same pair of knobs the "
                     "carts use, not a second pair for the same sentence");

  // AND IT STANDS IN A VILLAGE THAT HAD NO HERD OF THAT KIND AT ALL, which is
  // precisely the case the window exists for: a farm whose team died to the
  // last head has no horse row left to add to.
  world.calendar.day = bought ? coming.arrive_day - 1 : world.calendar.day;
  core::ArriveLivestock(config, world);
  failures +=
      Expect(bought && world.herds.rows.empty() && world.livestock_arrivals.rows.size() == 1,
             "stock: nothing stands in the village before the head's day");
  world.calendar.day += 1;
  core::ArriveLivestock(config, world);
  const bool stands = world.herds.rows.size() == 1;
  failures +=
      Expect(stands && world.herds.rows[0].adult_count == 1 &&
                 world.herds.rows[0].adult_male_count == 1 && world.livestock_arrivals.rows.empty(),
             "stock: on its day a herd is founded with the head in it, and the row leaves");
  // THE AGE IS THE WHOLE OF «в начале взрослого возраста»: a head entered at
  // nil would be a free extra lifetime bought for the same seventy points,
  // and the age total is what the death draw reads.
  failures += Expect(stands && world.herds.rows[0].adult_age_game_years_total > 1.9F &&
                         world.herds.rows[0].adult_age_game_years_total < 2.1F,
                     "stock: and it arrives grown — two years on the herd's age total, not nil");
  // The points put back where the livestock block found them: the year's turn
  // below counts what BURNS, and a balance this block left behind would be
  // measured as the district's arithmetic rather than as this block's litter.
  world.limit.points = 5;

  world.chairman.raikom_reputation = 50.0F;
  const std::int32_t granted_before = world.limit.points_granted_total;
  core::TurnLimitYear(config, world, true, 0.0F);
  failures += Expect(world.ledger.current.limit_points_burned == 5 && world.limit.points == 500,
                     "limit: at the year's turn the unspent points burn and a plan in full earns "
                     "base plus 150");
  // THE RUNNING TOTAL RISES BY THE GRANT AND BY NOTHING ELSE. It is what the
  // design weighs electrification against — «сумма баллов лимита, полученных
  // с начала партии» — so a year that burns five points and is granted five
  // hundred must add five hundred, not four hundred and ninety-five.
  failures += Expect(world.limit.points_granted_total - granted_before == 500,
                     "limit: and every point granted goes on the running total, the burn not "
                     "subtracted from it");
  // THE OVERFULFILMENT TERM REACHES THE GRANT, on the falling scale: 15.6 t
  // of grain over is 5 at 20 and 10.6 at 10, 206 points; 40 t is 5 at 20, 20
  // at 10 and 15 at 4, 360 — and no cap stops it.
  const std::int32_t points_saved = world.limit.points;
  core::TurnLimitYear(config, world, true, 15.6F);
  failures += Expect(world.limit.points == 706,
                     "limit: 15.6 t of grain over the plan add 206 on the falling scale");
  core::TurnLimitYear(config, world, true, 40.0F);
  failures += Expect(world.limit.points == 860,
                     "limit: and 40 t add 360, the third tier at its own price, uncapped");
  world.limit.points = points_saved;
  // AND A SALE DOES NOT TOUCH IT. Handing stock back pays into this year's
  // points; if it reached the running total it would be a pump — buy a head
  // at full price, hand it back at a fraction, lose points on every turn of
  // the handle and drive the total up all the same.
  const std::int32_t total_before_sale = world.limit.points_granted_total;
  const std::int32_t points_before_sale = world.limit.points;
  core::OrderRow sale;
  sale.kind = core::OrderKind::kHandStock;
  sale.herd = world.herds.row_ids[0];
  sale.amount = 1;
  failures += Expect(core::OrderHandStock(config, world, sale) == core::OrderRefusal::kNone &&
                         world.limit.points > points_before_sale,
                     "limit: handing a head back pays into this year's points");
  failures += Expect(world.limit.points_granted_total == total_before_sale,
                     "limit: and NOT into the running total — a total a sale can raise is a pump");
  return failures;
}

/// At the year's turn a field still being prepared for the year that ended
/// lets its crop go (oat_balance, 2026-09-13: a cabbage harrowed too late to
/// sow went into the next year's oat slot). Finished ploughing is kept as
/// autumn ploughing; a fallow being ploughed and a sown field are untouched.
/// Man-days held to a share come out of a float product: 0.6 × 25 is not 15.
bool ManDaysNear(float value, float expected) {
  return value > expected - 1.0e-3F && value < expected + 1.0e-3F;
}

/// The events of one kind the step has emitted so far.
std::vector<core::SimEvent> EventsOf(const core::WorldState& world, core::EventKind kind) {
  std::vector<core::SimEvent> found;
  for (const core::SimEvent& event : world.step_events) {
    if (event.kind == kind) {
      found.push_back(event);
    }
  }
  return found;
}

/// A column's world: a 1000 t store (type 0), the camp's type (1), a bare
/// fallow's norms of 1 man-day a hectare to plough, and a crop of 1 t a
/// hectare reaped at 2 man-days. Spring lot 0, autumn lot 1.
core::ProductionConfig MakeColumnConfig() {
  core::ProductionConfig config;
  config.unit_types.resize(2);
  SetStorageKg(config.unit_types[0], 1.0e6F);
  config.field_camp_type = core::UnitTypeId{1};
  config.farming.traction_hungry_factor = 0.0F;
  config.farming.plow_days_per_ha = 1.0F;
  config.farming.harrow_days_per_ha = 0.5F;
  config.crops.resize(1);
  config.crops[0].resource = core::ResourceId{0};
  config.crops[0].yield_kg_per_ha = 1000.0F;
  config.crops[0].harvest_days_per_ha = 2.0F;
  // Field by field, for the reason given at the other lot table above.
  const auto service_lot = [](std::int32_t points) {
    core::LimitLotDef def;
    def.points = points;
    def.era = 1;
    def.kind = core::LimitLotKind::kService;
    return def;
  };
  config.limit.lots = {service_lot(120), service_lot(150), service_lot(10)};
  config.limit.mts_spring_lot = core::LimitLotId{0};
  config.limit.mts_autumn_lot = core::LimitLotId{1};
  config.limit.mts_column_ha_limit = 30.0F;
  return config;
}

core::WorldState MakeColumnWorld(core::SimDay day) {
  core::WorldState world;
  world.epoch = core::Epoch::kOne;
  world.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);
  world.limit.points = 500;
  core::UnitRow store;
  store.type = core::UnitTypeId{0};
  store.level = 1;
  store.stock.assign(1, 0);
  core::AppendRow(world.units, store);
  return world;
}

/// The column's day at its last tick, as production calls it.
void EndColumnDay(const core::ProductionConfig& config, core::WorldState& world, core::SimDay day) {
  world.calendar.tick = (static_cast<core::Tick>(day) * core::kTicksPerDay) + 23U;
  core::RefreshCalendarCaches(world.calendar);
  core::RunMtsColumn(config, world);
}

core::FieldRow ColumnField(float x, float area, core::FieldPhase phase, float work) {
  core::FieldRow field;
  field.center = core::Vec2{.x = x, .y = 0.0F};
  field.area_ga = area;
  field.kind = core::LandKind::kArable;
  field.phase = phase;
  field.work_days_remaining = work;
  return field;
}

/// THE DISTRICT MTS'S COLUMN (MTS design §1; boss, parcels 449-451): bought
/// as a service lot one at a time and not after its season; on the road until
/// a camp stands in its window; 10 ha a working day on the fields nearest the
/// camp, the crew owing only the hectares the column left; a field it finishes
/// goes on at once; it leaves at its limit with the hectares in the event.
int CheckTheMtsColumn() {
  int failures = 0;
  const core::ProductionConfig config = MakeColumnConfig();
  const auto order = [](core::LimitLotId lot) {
    core::OrderRow row;
    row.kind = core::OrderKind::kOrderLimitLot;
    row.lot = lot;
    return row;
  };
  failures += Expect(core::LotOrderable(config.limit, core::LimitLotId{0}, core::Epoch::kOne) ==
                             core::OrderRefusal::kNone &&
                         core::LotOrderable(config.limit, core::LimitLotId{2}, core::Epoch::kOne) ==
                             core::OrderRefusal::kRuleForbids,
                     "mts: the column's service is bought, another service is not");

  // Spring, bought in February (day 4), due on day 6.
  core::WorldState world = MakeColumnWorld(4);
  failures += Expect(
      core::OrderLimitLot(config, world, order(core::LimitLotId{0})) == core::OrderRefusal::kNone &&
          world.limit.points == 380 && world.mts_column.phase == core::MtsColumnPhase::kOnTheRoad &&
          world.mts_column.arrive_day == 6,
      "mts: the spring column is bought for its points and put on the road");
  failures += Expect(core::OrderLimitLot(config, world, order(core::LimitLotId{1})) ==
                             core::OrderRefusal::kRuleForbids &&
                         world.limit.points == 380,
                     "mts: a second column while one is out is refused and costs nothing");
  EndColumnDay(config, world, 6);
  EndColumnDay(config, world, 8);
  failures += Expect(world.mts_column.phase == core::MtsColumnPhase::kOnTheRoad,
                     "mts: before its window and with no camp in it, the column is still out");

  core::UnitRow camp;
  camp.type = core::UnitTypeId{1};
  camp.level = 1;
  const core::UnitId camp_id = core::AppendRow(world.units, camp);
  const core::FieldId near =
      core::AppendRow(world.fields, ColumnField(100.0F, 25.0F, core::FieldPhase::kPlowing, 25.0F));
  const core::FieldId far =
      core::AppendRow(world.fields, ColumnField(2000.0F, 30.0F, core::FieldPhase::kPlowing, 30.0F));
  core::FieldRow meadow = ColumnField(50.0F, 40.0F, core::FieldPhase::kHarvest, 80.0F);
  meadow.kind = core::LandKind::kMeadow;
  const core::FieldId meadow_id = core::AppendRow(world.fields, meadow);
  EndColumnDay(config, world, 8);
  const std::vector<core::SimEvent> arrived = EventsOf(world, core::EventKind::kMtsColumnArrived);
  failures += Expect(world.mts_column.phase == core::MtsColumnPhase::kWorking &&
                         arrived.size() == 1 && arrived[0].unit == camp_id,
                     "mts: in its window with a camp standing, the column arrives at the camp");

  const auto field = [&world](core::FieldId id) -> core::FieldRow& {
    return world.fields.rows[core::FindRow(world.fields, id)];
  };
  int working_days = 0;
  for (core::SimDay day = 9; day < 20 && working_days < 3; ++day) {
    const bool rest = core::IsRestDay(day, world.calendar.day_zero_weekday, world.epoch);
    EndColumnDay(config, world, day);
    if (rest) {
      continue;
    }
    ++working_days;
    if (working_days == 1) {
      failures += Expect(ManDaysNear(field(near).work_days_remaining, 15.0F) &&
                             world.mts_column.field == near && world.mts_column.worked_ha == 10.0F,
                         "mts: the first day works 10 ha of the nearest field, the crew owes 15");
      // A crew's phase opened afresh at full demand is held to the share left
      // at the very next tick, whatever the hour.
      field(near).work_days_remaining = 25.0F;
      world.calendar.tick = (static_cast<core::Tick>(day + 1U) * core::kTicksPerDay) + 5U;
      core::RefreshCalendarCaches(world.calendar);
      core::RunMtsColumn(config, world);
      failures += Expect(ManDaysNear(field(near).work_days_remaining, 15.0F),
                         "mts: at any tick the crew is held to the hectares the column left");
    }
  }
  failures += Expect(working_days == 3, "mts: three working days lie inside the spring window");
  failures += Expect(
      field(near).phase == core::FieldPhase::kGrowing && field(near).work_days_remaining == 0.0F,
      "mts: the field it finished went through harrowing to the end of its sowing");
  failures +=
      Expect(ManDaysNear(field(far).work_days_remaining, 25.0F) &&
                 field(meadow_id).area_ga == 40.0F && field(meadow_id).work_days_remaining == 80.0F,
             "mts: the rest went to the next arable field, and the meadow is not its");
  const std::vector<core::SimEvent> left = EventsOf(world, core::EventKind::kMtsColumnLeft);
  failures += Expect(world.mts_column.phase == core::MtsColumnPhase::kGone && left.size() == 1 &&
                         left[0].amount == 30,
                     "mts: at its 30 ha it leaves, and says how many");
  failures += Expect(world.mts_column.field == far,
                     "mts: gone, it keeps the field it left half done for the crew's share");

  // Bought too late: due in June for a window that ended in May.
  core::WorldState late = MakeColumnWorld(19);
  failures += Expect(core::OrderLimitLot(config, late, order(core::LimitLotId{0})) ==
                             core::OrderRefusal::kRuleForbids &&
                         late.limit.points == 500,
                     "mts: a column that would come after its season is refused");

  // Autumn with no camp: out until the window's end, then it never comes.
  core::WorldState campless = MakeColumnWorld(26);
  core::OrderLimitLot(config, campless, order(core::LimitLotId{1}));
  EndColumnDay(config, campless, 39);
  failures += Expect(campless.mts_column.phase == core::MtsColumnPhase::kOnTheRoad,
                     "mts: in October with no camp the column still waits");
  EndColumnDay(config, campless, 40);
  failures += Expect(campless.mts_column.phase == core::MtsColumnPhase::kNotArrived &&
                         EventsOf(campless, core::EventKind::kMtsColumnNotArrived).size() == 1 &&
                         campless.limit.points == 350,
                     "mts: past its window it never comes, and the points are not returned");

  // Autumn at work: a 10 ha field reaped and carried in one day.
  core::WorldState autumn = MakeColumnWorld(28);
  core::OrderLimitLot(config, autumn, order(core::LimitLotId{1}));
  core::AppendRow(autumn.units, camp);
  core::FieldRow rye = ColumnField(300.0F, 10.0F, core::FieldPhase::kHarvest, 20.0F);
  rye.crop = core::CropId{0};
  core::AppendRow(autumn.fields, rye);
  EndColumnDay(config, autumn, 30);
  core::SimDay day = 31;
  while (core::IsRestDay(day, autumn.calendar.day_zero_weekday, autumn.epoch)) {
    ++day;
  }
  EndColumnDay(config, autumn, day);
  const core::FieldRow& reaped = autumn.fields.rows[0];
  failures += Expect(reaped.phase != core::FieldPhase::kHarvest && reaped.reaped_grams == 0 &&
                         autumn.units.rows[0].stock[0] == 10000 * core::kGramsPerKilogram &&
                         autumn.mts_column.worked_ha == 10.0F,
                     "mts: in autumn the column reaps its field and carries the 10 t home");
  return failures;
}

int CheckAnUnsownFieldLetsItsCropGoAtTheTurn() {
  int failures = 0;
  core::WorldState world;
  const core::CropId cabbage{2};
  const auto add_field = [&world](core::CropId crop, core::FieldPhase phase) {
    core::FieldRow row;
    row.crop = crop;
    row.phase = phase;
    core::AppendRow(world.fields, row);
  };
  add_field(cabbage, core::FieldPhase::kHarrowing);
  add_field(cabbage, core::FieldPhase::kPlowing);
  add_field(core::CropId{}, core::FieldPhase::kPlowing);
  add_field(cabbage, core::FieldPhase::kGrowing);
  core::FieldRow& harrowed = world.fields.rows[0];
  core::FieldRow& half_ploughed = world.fields.rows[1];
  core::FieldRow& fallow = world.fields.rows[2];
  core::FieldRow& sown = world.fields.rows[3];

  failures += Expect(core::ReleaseUnsownPreparation(world, harrowed),
                     "turn release: a harrowed, unsown field is released");
  failures += Expect(
      harrowed.phase == core::FieldPhase::kIdle && harrowed.crop.value == core::kInvalidDefIdValue,
      "turn release: the stale crop is gone and the field is idle");
  failures += Expect(harrowed.autumn_plowed == 1,
                     "turn release: the finished furrow is kept as autumn ploughing");
  failures += Expect(
      core::ReleaseUnsownPreparation(world, half_ploughed) && half_ploughed.autumn_plowed == 0,
      "turn release: an unfinished ploughing is released without the credit");
  failures += Expect(
      !core::ReleaseUnsownPreparation(world, fallow) && fallow.phase == core::FieldPhase::kPlowing,
      "turn release: a fallow being ploughed is left alone");
  failures +=
      Expect(!core::ReleaseUnsownPreparation(world, sown) && sown.crop.value == cabbage.value,
             "turn release: a sown field keeps its crop");
  return failures;
}

/// kPlanPositionUncovered (boss, 2026-09-13): a district position that no
/// chain grows in one of its three years stands as an alarm naming the
/// produce and the year, and goes out once any chain grows it then. By
/// produce, so a second crop of the same produce covers it; fallow and
/// unassigned ground cover nothing.
int CheckAnUncoveredPlanPositionIsAnAlarm() {
  int failures = 0;
  core::ProductionConfig config;
  core::CropDef oat;
  oat.resource = core::ResourceId{2};
  core::CropDef potato;
  potato.resource = core::ResourceId{6};
  core::CropDef oat_again = oat;  // a second crop yielding the same produce
  config.crops = {oat, potato, oat_again};
  config.plan_grain_share = 0.5F;
  config.plan_positions = {{.crop = core::CropId{0}, .area_share = 0.2F},
                           {.crop = core::CropId{1}, .area_share = 0.4F}};

  // Owed hectares = priced area × share × plan share. This year is priced off
  // LAST year's 20 ha (oat 2, potato 4); the two after off today's worked 13 ha
  // (oat 1.3, potato 2.6).
  core::WorldState world;
  world.plan.worked_ha_last_year = 20.0F;
  core::FieldRow wide;
  wide.area_ga = 10.0F;
  wide.rotation_assigned = 1;
  wide.rotation_year0 = core::CropId{0};
  wide.rotation_year1 = core::CropId{1};
  wide.rotation_year2 = core::CropId{};  // a rested season
  core::AppendRow(world.fields, wide);
  core::FieldRow small;  // potato this year on 3 ha against 4 owed
  small.area_ga = 3.0F;
  small.rotation_assigned = 1;
  small.rotation_year0 = core::CropId{1};
  core::AppendRow(world.fields, small);
  core::FieldRow unassigned;  // grows oats in every slot, but nobody assigned it
  unassigned.area_ga = 45.0F;
  unassigned.rotation_year0 = unassigned.rotation_year1 = unassigned.rotation_year2 =
      core::CropId{0};
  core::AppendRow(world.fields, unassigned);

  const auto uncovered = [&config, &world]() {
    std::vector<core::Alarm> alarms;
    core::CollectPlanAlarms(config, world, alarms);
    std::vector<std::pair<std::uint16_t, std::int64_t>> found;
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind == core::AlarmKind::kPlanPositionUncovered) {
        found.emplace_back(alarm.resource.value, alarm.amount);
      }
    }
    return found;
  };

  const auto before = uncovered();
  // Typed pairs: from bare ints MSVC's std::pair narrows int to uint16_t in
  // its converting constructor and warns (C4244) under /WX.
  using Uncovered = std::pair<std::uint16_t, std::int64_t>;
  const std::vector<Uncovered> expected = {Uncovered{std::uint16_t{2}, std::int64_t{1}},
                                           Uncovered{std::uint16_t{2}, std::int64_t{2}},
                                           Uncovered{std::uint16_t{6}, std::int64_t{0}},
                                           Uncovered{std::uint16_t{6}, std::int64_t{2}}};
  failures += Expect(before == expected,
                     "plan alarm: oat missing in years 1 and 2, potato in year 2, potato in "
                     "year 0 on 3 ha against 4 owed, and the unassigned field covers nothing");

  world.fields.rows[0].rotation_year2 = core::CropId{2};
  world.fields.rows[1].area_ga = 4.0F;
  const auto after = uncovered();
  const std::vector<Uncovered> expected_after = {Uncovered{std::uint16_t{2}, std::int64_t{1}},
                                                 Uncovered{std::uint16_t{6}, std::int64_t{2}}};
  failures += Expect(after == expected_after,
                     "plan alarm: the owed hectares put it out, and another crop of the same "
                     "produce covers the year");
  return failures;
}

/// kRemoveField (2026-09-14): the start quest's first gesture, and the one
/// construction design §12 teaches every removal by. Free and at once; refused
/// only where bread stands; the reserve's mark rides out on the event.
int CheckTheChairmanRemovesAField() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  if (Expect(tables != nullptr, "the shipped tables load for the field removal") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const auto system = core::CreateProductionSystem(*tables, core::StubTables::kRefused);
  if (Expect(system != nullptr, "and they build a production system") != 0) {
    return 1;
  }

  // Two fields — the one the order names first, a bystander second — and one
  // order naming `target` (the first field's id unless told otherwise).
  const auto remove = [&](const core::FieldRow& named, std::optional<core::FieldId> target) {
    core::WorldState previous;
    previous.calendar.tick = 10U * core::kTicksPerDay;
    core::RefreshCalendarCaches(previous.calendar);
    const core::FieldId id = core::AppendRow(previous.fields, named);
    core::FieldRow bystander;
    bystander.area_ga = 7.0F;
    core::AppendRow(previous.fields, bystander);
    core::OrderRow order;
    order.kind = core::OrderKind::kRemoveField;
    order.field = target.value_or(id);
    core::AppendRow(previous.orders, order);
    core::WorldState current = previous;
    current.calendar.tick += 1;
    core::RefreshCalendarCaches(current.calendar);
    current.step_events.clear();
    system->RunProductionDecisions(previous, current);
    return current;
  };
  const auto removed_events = [](const core::WorldState& world) {
    std::vector<const core::SimEvent*> found;
    for (const core::SimEvent& event : world.step_events) {
      if (event.kind == core::EventKind::kFieldRemoved) {
        found.push_back(&event);
      }
    }
    return found;
  };

  // -- the reserve goes, free and at once, and says it was the reserve ------
  {
    core::FieldRow reserve;
    reserve.area_ga = 3.0F;
    reserve.overgrown = 1;
    reserve.start_reserve = 1;
    const core::WorldState after = remove(reserve, std::nullopt);
    failures += Expect(after.orders.rows[0].status == core::OrderStatus::kDone,
                       "remove: the chairman may let the reserve field go");
    failures += Expect(after.fields.rows.size() == 1 && after.fields.rows[0].area_ga == 7.0F,
                       "remove: the named field is gone and the bystander stays");
    const auto events = removed_events(after);
    failures +=
        Expect(events.size() == 1 && events[0]->field.value != core::kInvalidEntityIdValue &&
                   events[0]->amount == 1,
               "remove: one kFieldRemoved names the field and carries the reserve's mark");
  }
  // -- an ordinary field growing its crop goes too, and says it was not -----
  {
    core::FieldRow growing;
    growing.area_ga = 10.0F;
    growing.phase = core::FieldPhase::kGrowing;
    const core::WorldState after = remove(growing, std::nullopt);
    failures += Expect(
        after.orders.rows[0].status == core::OrderStatus::kDone && after.fields.rows.size() == 1,
        "remove: a growing field goes with what was put into it");
    const auto events = removed_events(after);
    failures += Expect(events.size() == 1 && events[0]->amount == 0,
                       "remove: and its event says it was not the start's reserve");
  }
  // -- where bread stands, nothing moves -------------------------------------
  {
    core::FieldRow harvest;
    harvest.area_ga = 10.0F;
    harvest.phase = core::FieldPhase::kHarvest;
    const core::WorldState after = remove(harvest, std::nullopt);
    failures += Expect(after.orders.rows[0].refusal == core::OrderRefusal::kNotEmpty &&
                           after.fields.rows.size() == 2 && removed_events(after).empty(),
                       "remove: a field being reaped is refused kNotEmpty and stays");
  }
  {
    core::FieldRow reaped;
    reaped.area_ga = 10.0F;
    reaped.reaped_grams = 5'000'000;
    const core::WorldState after = remove(reaped, std::nullopt);
    failures += Expect(after.orders.rows[0].refusal == core::OrderRefusal::kNotEmpty &&
                           after.fields.rows.size() == 2 && removed_events(after).empty(),
                       "remove: grain reaped and not yet carted holds the field too");
  }
  // -- a meadow, and a field that is not there -------------------------------
  {
    core::FieldRow meadow;
    meadow.area_ga = 20.0F;
    meadow.kind = core::LandKind::kMeadow;
    const core::WorldState after = remove(meadow, std::nullopt);
    failures += Expect(after.orders.rows[0].refusal == core::OrderRefusal::kWrongLand &&
                           after.fields.rows.size() == 2,
                       "remove: a meadow is not a contour anybody drew, kWrongLand");
  }
  {
    const core::WorldState after = remove(core::FieldRow{}, core::FieldId{9999});
    failures += Expect(after.orders.rows[0].refusal == core::OrderRefusal::kNoSuchSubject &&
                           after.fields.rows.size() == 2,
                       "remove: a field that is not there, kNoSuchSubject");
  }
  return failures;
}

int main() {
  int failures = 0;
  failures += CheckTheChairmanRemovesAField();
  failures += CheckAnUncoveredPlanPositionIsAnAlarm();
  failures += CheckAnUnsownFieldLetsItsCropGoAtTheTurn();
  failures += CheckTheReapingGate();
  failures += CheckFelling();
  failures += CheckFellingUnreachable();
  failures += CheckExtraction();
  failures += CheckSawing();
  failures += CheckAnUpgradesRecipeIsNobodysElse();
  failures += CheckDistrictLimit();
  failures += CheckDeliverPlanNow();
  failures += CheckTheAccumulationLimit();
  failures += CheckTheLimitKeepsTheTeamsOats();
  failures += CheckTheMtsColumn();
  failures += CheckDistrictVisits();
  failures += CheckStubTablesMustBeDeclared();
  failures += CheckStoreCeilingAndAlarms();
  const test::FakeTableSet tables;
  const auto system = core::CreateProductionSystem(tables, core::StubTables::kAllowed);
  failures += Expect(system != nullptr, "factory yields a system");

  const core::WorldState previous;
  core::WorldState current = previous;
  failures += Expect(system->ProductionPhase().ParallelItemCount(previous) == 0,
                     "production stub has zero items");
  system->ProductionPhase().RunItemRange(previous, current, 0, 0);
  system->RunProductionDecisions(previous, current);
  failures +=
      Expect(current.calendar.tick == previous.calendar.tick && current.epoch == previous.epoch,
             "stubs leave the world unchanged");

  failures += CheckFeeding();
  failures += CheckTheHerdsMilkGoesToItsHome();
  failures += CheckTheHerdDoesNotEatThePlan();
  failures += CheckFeedCaps();
  failures += CheckFeedLightCountsTheWinter();
  failures += CheckFeedLightRespectsTheCeiling();
  failures += CheckFeedLightNeverRunsOut();
  failures += CheckSeedLightMeasuresCoverage();
  failures += CheckSeedLightAsksAboutTheNearestCampaign();
  failures += CheckFeedLightKeepsTheKindsApart();
  failures += CheckSeedLightDoesNotNetCropsOff();
  failures += CheckHaulingIsNotFree();
  failures += CheckBilletingAndProduce();
  failures += CheckCohortFlows();
  failures += CheckAutumnPigs();
  failures += CheckSelfFedYard();
  failures += CheckWorkOnlyFeed();
  failures += CheckMangerReach();
  failures += CheckStableGate();
  failures += CheckNightPasture();
  failures += CheckHandingStockBack();
  failures += CheckElectrification();
  failures += CheckTheMeadowFlowersAndTheAftermathComesBack();
  failures += CheckAgeSpread();
  failures += CheckDroughtReadsTheAfternoon();
  failures += CheckABrokenPlanPositionsLineIsNamedNotFatal();
  failures += CheckHorsesComeInWhenAGroomIsAppointed();
  failures += CheckTheHarvestWarningComesBeforeTheHarvest();
  failures += CheckTheRoomIsSpentInHarvestOrder();
  failures += CheckTheSowingWillNotFit();
  failures += CheckTheHarvestWillNotBeGathered();
  failures += CheckTheSnowBooksWhatItTakes();
  failures += CheckAReapedFieldStillSpendsTheRoom();
  failures += CheckTheWarningBurnsUntilTheHarvestIsResolved();
  failures += CheckTheStrawClaimsRoomToo();
  failures += CheckCapacityWithoutALadderIsRefused();
  failures += CheckTheTeamWithoutARoofSaysSo();
  failures += CheckAHeapIsAStore();
  failures += CheckANumberedStoreTakesItsHomesOnly();
  failures += CheckTheDoorCountsWhatLanded();
  failures += CheckPauseAndResume();
  failures += CheckThePlanIsJudgedAtTheYearsTurn();
  failures += CheckUnworkedGroundDoesNotRecover();
  failures += CheckTheChairmanSetsARotation();
  failures += CheckTheChairmanCanUnsealAFund();

  if (failures == 0) {
    std::cout << "unit_core_production: all checks passed\n";
  }
  return failures;
}
