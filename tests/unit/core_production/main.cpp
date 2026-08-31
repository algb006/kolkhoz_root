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
#include <vector>

#include "core_common/calendar.h"
#include "core_common/quantities.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_production/production_system.h"
#include "core_tables/tables.h"
#include "herd_system.h"
#include "production_config.h"

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

class EmptyTableSet final : public core::ITableSet {
 public:
  const core::ITable* FindTable(std::string_view /*name*/) const override { return nullptr; }

  std::uint32_t TableCount() const override { return 0; }

  std::string_view TableName(std::uint32_t /*index*/) const override { return {}; }
};

// --- stage 6, task O3: the herd day ---------------------------------------

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
  config.unit_types[0].storage_capacity_kg = 1.0e6F;
  config.unit_types[0].livestock_capacity_head = 10.0F;
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
  config.unit_types[1].storage_capacity_kg = 0.0F;  // a barn, not a store
  config.unit_types[1].livestock_capacity_head = 10.0F;

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
         "rye,rye,0,0,4,5,5,5,8,8,2,850,180,-1,1,0,3,8,0\n";
  std::ofstream(root / "farming.csv")
      << "key,value\nfertility_neutral,50\nmanure_norm_kg_per_ha,20000\n"
         "manure_fertility_bonus,10\nfallow_recovery,6\nrepeat_penalty_per_year,3\n"
         "drought_temp_c,25\nstress_per_day,0.02\nstress_cap,0.3\n";
  std::ofstream(root / "weather.csv")
      << "key,temp_mean_c,temp_spread_c,temp_amplitude_c,precipitation_chance_percent\n"
         "winter,-10,2,3,35\nspring,5,7,5,35\nsummer,19,5,6,25\nautumn,6,7,5,45\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto system = tables == nullptr ? nullptr : core::CreateProductionSystem(*tables);
  if (Expect(system != nullptr, "the drought table set builds a production system") != 0) {
    std::cout << error << '\n';
    return 1;
  }

  const auto stress_after_a_day = [&](float mean_celsius) {
    core::WorldState world;
    world.calendar.tick = 30 * core::kTicksPerDay;  // late July: summer
    core::RefreshCalendarCaches(world.calendar);
    world.weather.air_temperature_celsius = mean_celsius;
    world.weather.precipitation = core::Precipitation::kNone;
    core::FieldRow field;
    field.area_ga = 10.0F;
    field.phase = core::FieldPhase::kGrowing;
    field.crop = core::CropId{0};
    core::AppendRow(world.fields, field);
    const core::WorldState previous = world;
    system->ProductionPhase().RunItemRange(previous, world, 0, 1);
    return world.fields.rows[0].weather_stress;
  };
  failures += Expect(stress_after_a_day(18.0F) == 0.0F,
                     "a summer day with a mean of 18 reads 24 in the afternoon: no drought");
  failures += Expect(stress_after_a_day(19.0F) > 0.0F,
                     "a mean of 19 reads 25 in the afternoon, and the field starts to burn");
  failures += Expect(stress_after_a_day(24.0F) == stress_after_a_day(19.0F),
                     "hotter is not more stress a day: the rate is the crop's, the gate is heat");
  std::filesystem::remove_all(root);
  return failures;
}

}  // namespace

int main() {
  int failures = 0;
  const EmptyTableSet tables;
  const auto system = core::CreateProductionSystem(tables);
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
  failures += CheckBilletingAndProduce();
  failures += CheckCohortFlows();
  failures += CheckAutumnPigs();
  failures += CheckSelfFedYard();
  failures += CheckWorkOnlyFeed();
  failures += CheckMangerReach();
  failures += CheckStableGate();
  failures += CheckAgeSpread();
  failures += CheckDroughtReadsTheAfternoon();

  if (failures == 0) {
    std::cout << "unit_core_production: all checks passed\n";
  }
  return failures;
}
