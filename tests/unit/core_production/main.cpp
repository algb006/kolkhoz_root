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
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "../../common/fake_tables.h"
#include "core_common/calendar.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_production/production_system.h"
#include "core_tables/tables.h"
#include "field_haul.h"
#include "herd_system.h"
#include "production_config.h"
#include "stock_lights.h"

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
    return static_cast<std::uint32_t>(herd.newborn_count + herd.juvenile_count);
  };
  failures += Expect(foals_at_level(1) == 0, "a summer yard brings no foals");
  failures += Expect(foals_at_level(2) > 0, "a stable does");
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

int CheckPauseAndResume() {
  int failures = 0;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_production_pause";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  // No capacity column at all: this test is about the order book, and a
  // capacity with no level row behind it is now a refusal (CheckPauseStore).
  std::ofstream(root / "unit_types.csv") << "key\nbarn\n";
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
  const core::UnitId site_id = core::AppendRow(world.units, site);

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
  failures += Expect(refusal(nowhere) == core::OrderRefusal::kNoSuchSubject,
                     "and a unit that does not exist is refused for the unit");

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

int main() {
  int failures = 0;
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
  failures += CheckAgeSpread();
  failures += CheckDroughtReadsTheAfternoon();
  failures += CheckHorsesComeInWhenAGroomIsAppointed();
  failures += CheckTheHarvestWarningComesBeforeTheHarvest();
  failures += CheckTheRoomIsSpentInHarvestOrder();
  failures += CheckAReapedFieldStillSpendsTheRoom();
  failures += CheckTheWarningBurnsUntilTheHarvestIsResolved();
  failures += CheckTheStrawClaimsRoomToo();
  failures += CheckCapacityWithoutALadderIsRefused();
  failures += CheckTheTeamWithoutARoofSaysSo();
  failures += CheckPauseAndResume();

  if (failures == 0) {
    std::cout << "unit_core_production: all checks passed\n";
  }
  return failures;
}
