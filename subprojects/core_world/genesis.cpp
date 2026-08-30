// World genesis (include/core_world/world.h, stage 3): the designed start —
// 80 residents in 21 yards (campaign table) with the age pyramid of the
// reference run (demography.py start distribution: 15.2% under 7, 22%
// school age, 49.2% working age, 13.6% old), deterministic from the seed.
//
// Household composition, derived from the start canon: old-timers keep
// their own yards (pairs, then a single); every other yard is a married
// couple; children and unmarried grown children live with their parents
// (families design §1, life-cycle §1). Fields the later stages own (houses,
// education mechanics) stay at their defaults.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "campaign_tables.h"
#include "core_common/calendar.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace core {
namespace {

/// Stream id of the world's sequential RNG (world.cpp uses the same).
constexpr std::uint64_t kWorldRngStream = 0;

/// Start pyramid shares, the reference run's distribution (demography.py).
constexpr float kShareUnderSeven = 0.152F;
constexpr float kShareSchool = 0.220F;
constexpr float kShareOld = 0.136F;

float DrawInRange(RngState& rng, float low, float high) {
  return low + NextRandomUnitFloat(rng) * (high - low);
}

/// @brief Birth day (negative: before day 0) for a biological age in years.
std::int32_t BirthDayForAge(float age_years, float life_speedup, RngState& rng) {
  const float game_days = age_years / life_speedup * static_cast<float>(kDaysPerYear);
  // A little jitter so a cohort does not share one birthday.
  return -static_cast<std::int32_t>(game_days + DrawInRange(rng, 0.0F, 6.0F));
}

/// @brief The shared shape of every starting person; kin and family are set
/// by the caller.
ResidentRow RollPerson(RngState& rng, Sex sex, float age_years, float life_speedup) {
  ResidentRow person;
  person.sex = sex;
  person.birth_day = BirthDayForAge(age_years, life_speedup, rng);
  person.intellect = DrawInRange(rng, 20.0F, 80.0F);
  person.stamina = DrawInRange(rng, 20.0F, 80.0F);
  person.optimism = DrawInRange(rng, 20.0F, 80.0F);
  person.ideology = DrawInRange(rng, 30.0F, 70.0F);
  person.health = DrawInRange(rng, 60.0F, 90.0F) - (age_years > 55.0F ? 15.0F : 0.0F);
  person.mood = DrawInRange(rng, 50.0F, 70.0F);
  // Most Epoch-I adults are illiterate; some finished primary school
  // (education design §2; the exact share is an ASSUMPTION until playtests).
  if (age_years >= 16.0F && NextRandomUnitFloat(rng) < 0.3F) {
    person.education_stage = EducationStage::kPrimary;
    person.education_grade = DrawInRange(rng, 3.0F, 5.0F);
  }
  return person;
}

FamilyRow RollFamily(RngState& rng) {
  FamilyRow family;
  family.component_satiety = DrawInRange(rng, 50.0F, 65.0F);
  family.component_common_cause = DrawInRange(rng, 45.0F, 60.0F);
  family.component_needs = DrawInRange(rng, 45.0F, 60.0F);
  family.component_rest = DrawInRange(rng, 50.0F, 65.0F);
  return family;
}

UnitTypeId TypeByKey(const ITable* unit_types, std::string_view key) {
  if (unit_types == nullptr) {
    return UnitTypeId{};
  }
  const std::uint32_t row = unit_types->FindRowByKey(key);
  return row == kNoTableRow ? UnitTypeId{} : UnitTypeId{static_cast<std::uint16_t>(row)};
}

ResourceId GenesisResource(const ITable* resources, std::string_view key) {
  if (resources == nullptr) {
    return ResourceId{};
  }
  const std::uint32_t row = resources->FindRowByKey(key);
  return row == kNoTableRow ? ResourceId{} : ResourceId{static_cast<std::uint16_t>(row)};
}

CropId CropByKey(const ITable* crops, std::string_view key) {
  if (crops == nullptr) {
    return CropId{};
  }
  const std::uint32_t row = crops->FindRowByKey(key);
  return row == kNoTableRow ? CropId{} : CropId{static_cast<std::uint16_t>(row)};
}

void PutStock(UnitRow& unit, ResourceId resource, float kilograms) {
  if (resource.value == kInvalidDefIdValue) {
    return;
  }
  if (unit.stock.size() <= resource.value) {
    unit.stock.resize(resource.value + 1U, 0);
  }
  unit.stock[resource.value] = static_cast<Grams>(kilograms) * kGramsPerKilogram;
}

/// @brief Puts kilograms of a resource into a family's own larder.
void PutPantry(FamilyRow& family, ResourceId resource, float kilograms) {
  if (resource.value == kInvalidDefIdValue) {
    return;
  }
  if (family.pantry.size() <= resource.value) {
    family.pantry.resize(resource.value + 1U, 0);
  }
  family.pantry[resource.value] = static_cast<Grams>(kilograms) * kGramsPerKilogram;
}

UnitId PlaceUnit(WorldState& world, UnitTypeId type, float x_meters, float y_meters) {
  UnitRow unit;
  unit.type = type;
  unit.position = Vec2{.x = x_meters, .y = y_meters};
  return AppendRow(world.units, unit);
}

FieldId PlaceField(
    WorldState& world, float area_ga, CropId rotation, Vec2 center, Metric fertility) {
  FieldRow field;
  field.center = center;
  field.area_ga = area_ga;
  field.fertility = fertility;
  field.rotation_year0 = rotation;
  field.rotation_year1 = rotation;
  field.rotation_year2 = rotation;
  return AppendRow(world.fields, field);
}

/// @brief Appends one herd; returns nothing, because genesis never needs the
/// id back.
void AddHerd(WorldState& world,
             const ITable& livestock,
             std::string_view key,
             std::uint16_t adults,
             std::uint16_t males,
             UnitId unit,
             FamilyId household,
             bool household_owned) {
  const std::uint32_t row = livestock.FindRowByKey(key);
  if (row == kNoTableRow) {
    return;  // a table set without this kind simply has none of it
  }
  HerdRow herd;
  herd.kind = LivestockKindId{static_cast<std::uint16_t>(row)};
  herd.unit = unit;
  herd.household = household;
  herd.household_owned = household_owned ? 1U : 0U;
  herd.adult_count = adults;
  herd.adult_male_count = males;
  // Adults start at the age they became adults: the start herd is a young
  // working herd, not one about to die of old age.
  AppendRow(world.herds, herd);
}

/// The start's animals (start canon §10-§11 and the household canon of
/// livestock design §2, boss answer 2026-08-30 to registry question 148).
///
/// Two lines that look alike and are not: the cows stand at the stock yard
/// and the horses stand in private yards, and BOTH are the kolkhoz's. The
/// horses are billeted, not given away — the farm feeds them and the farm
/// works them. What the families own is what the canon gives them: two goats
/// and eight hens each, a pig at every twentieth yard, and no cow at all
/// ("the cows are handed over, every last one"). The canonical ~1.2 t of
/// milk a yard is TWO GOATS, which is what made the coverage figure add up
/// all along.
void PlaceHerds(WorldState& world, const ITableSet& tables, UnitId stock_yard) {
  const ITable* livestock = tables.FindTable("livestock");
  if (livestock == nullptr) {
    return;
  }
  // Two bulls to 37 cows is the 4% the design keeps; without a sire the barn
  // could never grow, and growing it is the whole first-epoch arc.
  AddHerd(world, *livestock, "cow", 39, 2, stock_yard, FamilyId{}, false);
  const auto yards = static_cast<std::uint32_t>(world.families.rows.size());
  const std::uint32_t horse_yards = yards < 16 ? yards : 16U;
  for (std::uint32_t yard = 0; yard < horse_yards; ++yard) {
    AddHerd(world, *livestock, "horse", 1, 0, UnitId{}, world.families.row_ids[yard], false);
  }
  for (std::uint32_t yard = 0; yard < yards; ++yard) {
    const FamilyId home = world.families.row_ids[yard];
    AddHerd(world, *livestock, "goat", 2, 0, UnitId{}, home, true);
    AddHerd(world, *livestock, "chicken", 8, 0, UnitId{}, home, true);
  }
  // A pig at every twentieth yard: "only the well-off, and no more than a
  // fifth of the yards" — a sow and a boar, or it is not a herd.
  for (std::uint32_t yard = 0; yard + 1U < yards; yard += 20U) {
    AddHerd(world, *livestock, "pig", 2, 1, UnitId{}, world.families.row_ids[yard], true);
  }
}

/// @brief The start economy of the canon (start.md §10-§11): the surviving
/// units with the stores in the church, 160 ha of arable land with the
/// suggested first-year plan of the reference run (70 ha sown: 62% grain,
/// 18% potatoes, 8% flax, 12% fodder), 80 ha of meadows and the animals of
/// PlaceHerds above. A table-less world (unit tests) gets none of this and
/// stays people-only.
/// Start fertility 65 = soil factor 1.3 of the reference runs; stock
/// amounts are ASSUMPTION sized to the first sowing plus a food margin.
void BuildStartEconomy(WorldState& world, const ITableSet& tables) {
  const ITable* unit_types = tables.FindTable("unit_types");
  const ITable* resources = tables.FindTable("resources");
  const ITable* crops = tables.FindTable("crops");
  if (unit_types == nullptr || resources == nullptr || crops == nullptr) {
    return;  // people-only world until the tables exist
  }
  constexpr Metric kStartFertility = 65.0F;

  // Units of the start set, by the keys of the design db. The kolkhoz yard
  // (horse_yard) is deliberately absent — the first build of the campaign,
  // and its SECOND level is the stable, which is why horse breeding stays
  // blocked at the start exactly as the canon asks.
  const UnitId church = PlaceUnit(world, TypeByKey(unit_types, "church_store"), 0, 0);
  PlaceUnit(world, TypeByKey(unit_types, "well"), 50, 0);
  const UnitId stock_yard = PlaceUnit(world, TypeByKey(unit_types, "cattle_yard"), 200, 100);
  PlaceUnit(world, TypeByKey(unit_types, "build_yard"), 300, 0);
  PlaceUnit(world, TypeByKey(unit_types, "log_pile"), 100, 50);
  PlaceUnit(world, TypeByKey(unit_types, "stone_pile"), 150, 50);
  PlaceUnit(world, TypeByKey(unit_types, "clay_pile"), 200, 50);
  const UnitId compost = PlaceUnit(world, TypeByKey(unit_types, "manure_pile"), 400, 200);

  // One decrepit house per starting family, in a compact village south of
  // the yard: three rows of seven, 40 m between houses and 80 m between
  // rows. ~5 ha of built-up land, the share the start map gives it
  // (49-simulations §2в). Distances matter from stage 5 on: every worker's
  // day is measured from his own door.
  constexpr std::uint32_t kHousesPerRow = 7;
  constexpr float kHouseStepMeters = 40.0F;
  constexpr float kRowStepMeters = 80.0F;
  const UnitTypeId old_house = TypeByKey(unit_types, "old_house");
  for (std::uint32_t family_row = 0; family_row < world.families.rows.size(); ++family_row) {
    UnitRow house;
    house.type = old_house;
    const std::uint32_t village_row = family_row / kHousesPerRow;
    const std::uint32_t place_in_row = family_row % kHousesPerRow;
    house.position = Vec2{.x = -120.0F + (static_cast<float>(place_in_row) * kHouseStepMeters),
                          .y = -80.0F - (static_cast<float>(village_row) * kRowStepMeters)};
    house.household = world.families.row_ids[family_row];
    world.families.rows[family_row].house = AppendRow(world.units, house);
  }

  // The stores in the church and the inherited compost (start.md §7).
  UnitRow& church_row = world.units.rows[FindRow(world.units, church)];
  PutStock(church_row, GenesisResource(resources, "oat"), 5000);
  PutStock(church_row, GenesisResource(resources, "barley"), 3500);
  PutStock(church_row, GenesisResource(resources, "wheat"), 2500);
  PutStock(church_row, GenesisResource(resources, "rye"), 8000);
  PutStock(church_row, GenesisResource(resources, "potato"), 35000);
  // Fodder in the barn on day zero. The canon hands the kolkhoz a live herd
  // in January, and a live herd in January has been eating something since
  // the autumn — a start with empty mangers would kill the cows before the
  // first cut, which is not hardship but an unwinnable opening.
  // How much: measured from the first cut, not rounded (difficulty design
  // §4, boss answer 2026-08-31). The farm is handed over in March, the
  // scythes go out in June, and everything the herd eats between those dates
  // was put in the barn before the chairman arrived. On the normal level
  // that is EXACTLY enough to reach the cut, which the run measures at about
  // 165 t: the herd eats 7.3 t a game day and the scythes go out on day 22.
  // Sixty was a round number, and round numbers lie here because they do not
  // know when help arrives: at sixty the herd lost eight cows to a decision
  // the player never made, and a hundred still left it nine days short.
  //
  // The rule generalises, and it is worth keeping: the size of any start
  // stock that gets consumed is measured from the nearest moment the farm
  // replenishes it itself — the cut for fodder, the harvest for food, the
  // first felling for firewood.
  PutStock(church_row, GenesisResource(resources, "hay"), 165000);

  // What the households still have of their own. The village was living
  // before the kolkhoz was declared, and it is declared in January: yards
  // with bare shelves would spend the whole first spring on nothing at all,
  // because trudodni are earned by work and there is no field work until
  // April. That is not a hard start, it is an empty one.
  // ASSUMPTION on the amounts: potatoes as the peasant staple, a little
  // grain and what is left of the cellar — about two months to the first
  // issue (start canon §7 gives the stores, not the larders).
  for (FamilyRow& family : world.families.rows) {
    PutPantry(family, GenesisResource(resources, "potato"), 400);
    PutPantry(family, GenesisResource(resources, "oat"), 80);
    PutPantry(family, GenesisResource(resources, "vegetables"), 60);
  }
  PutStock(world.units.rows[FindRow(world.units, compost)],
           GenesisResource(resources, "manure"),
           250000);

  // 160 ha of arable land: the reference first-year mix sown, the rest
  // fallow; 80 ha of meadows in permanent grass. The land lies in a half
  // ring north of the village, and the radii reproduce the start map's own
  // measurements (49-simulations §2в): the arable averages 1.0 km from the
  // village and nothing lies farther than 1.5 km, which is what makes the
  // road eat 37-56% of a spring day there. The potato patch sits nearest,
  // being the crop walked to most often. Coordinates are written out rather
  // than computed — genesis must land bit for bit on every compiler and trig
  // library results do not (daylight_table.h says the same) — and they are
  // polar around the village centre (0, -160), which is why the y values
  // look shifted.
  PlaceField(
      world, 20.0F, CropByKey(crops, "oat"), Vec2{.x = 752.0F, .y = 114.0F}, kStartFertility);
  PlaceField(
      world, 13.4F, CropByKey(crops, "barley"), Vec2{.x = 611.0F, .y = 568.0F}, kStartFertility);
  PlaceField(world,
             10.0F,
             CropByKey(crops, "wheat_spring"),
             Vec2{.x = 182.0F, .y = 874.0F},
             kStartFertility);
  PlaceField(
      world, 12.6F, CropByKey(crops, "potato"), Vec2{.x = -222.0F, .y = 451.0F}, kStartFertility);
  PlaceField(
      world, 5.6F, CropByKey(crops, "flax"), Vec2{.x = -843.0F, .y = 547.0F}, kStartFertility);
  PlaceField(world,
             8.4F,
             CropByKey(crops, "fodder_beet"),
             Vec2{.x = -799.0F, .y = 131.0F},
             kStartFertility);
  PlaceField(world, 45.0F, CropId{}, Vec2{.x = 1106.0F, .y = 614.0F}, kStartFertility);
  PlaceField(world, 45.0F, CropId{}, Vec2{.x = -774.0F, .y = 946.0F}, kStartFertility);
  // Meadows and pasture. The map gives about 15% of its hundred square
  // kilometres to grass (terrain design §1) — some fifteen hundred hectares
  // — so the fodder base is not limited by LAND at all. It is limited by
  // hands and by the mowing window: 8 real man-days a hectare means the
  // village mows what it has crews and days for, and hay becomes a decision
  // rather than a given. Two hundred hectares are laid out here, which is
  // more than the first years can cut and rather less than the map holds.
  constexpr std::array<Vec2, 10> kMeadowCenters = {{{.x = 1401.0F, .y = 215.0F},
                                                    {.x = 725.0F, .y = 1096.0F},
                                                    {.x = -725.0F, .y = 1096.0F},
                                                    {.x = -1401.0F, .y = 215.0F},
                                                    {.x = 1300.0F, .y = -160.0F},
                                                    {.x = 1032.0F, .y = 631.0F},
                                                    {.x = 170.0F, .y = 1129.0F},
                                                    {.x = -170.0F, .y = 1129.0F},
                                                    {.x = -1032.0F, .y = 631.0F},
                                                    {.x = -1300.0F, .y = -160.0F}}};
  const CropId timothy = CropByKey(crops, "timothy");
  for (const Vec2 center : kMeadowCenters) {
    FieldRow& field =
        world.fields.rows[FindRow(world.fields, PlaceField(world, 20.0F, timothy, center, 55.0F))];
    // Meadows are standing grass from day one: no sowing year needed.
    field.crop = timothy;
    field.phase = FieldPhase::kGrowing;
  }

  PlaceHerds(world, tables, stock_yard);
}

}  // namespace

WorldState CreateStartWorld(const ITableSet& tables, std::uint64_t world_seed) {
  WorldState world;
  world.world_seed = world_seed;
  world.rng = SeedRngState(world_seed, kWorldRngStream);
  world.calendar.day_zero_weekday = CampaignDayZeroWeekday(tables);
  RefreshCalendarCaches(world.calendar);

  // Value validation (UB-002): the float-to-uint cast of a negative or huge
  // table value is UB — validate before casting, fall back loudly.
  float population_value = CampaignValue(tables, "start_population", 80.0F);
  if (!(population_value >= 1.0F && population_value <= 100000.0F)) {
    LogError("campaign: start_population out of range; using 80");
    population_value = 80.0F;
  }
  float households_value = CampaignValue(tables, "start_households", 21.0F);
  if (!(households_value >= 1.0F && households_value <= 10000.0F)) {
    LogError("campaign: start_households out of range; using 21");
    households_value = 21.0F;
  }
  const auto population = static_cast<std::uint32_t>(population_value);
  const auto households = static_cast<std::uint32_t>(households_value);
  float life_speedup = LifeSpeedupFromTables(tables);
  // Positive test so that NaN falls back too, and a floor that keeps the
  // age-to-day division from producing values no int32 can hold.
  if (!(life_speedup >= 0.1F && life_speedup <= 1000.0F)) {
    LogError("life: life_speedup out of range; using 4");
    life_speedup = 4.0F;
  }
  RngState& rng = world.rng;

  // The pyramid in whole people.
  const auto old_count =
      static_cast<std::uint32_t>(static_cast<float>(population) * kShareOld + 0.5F);
  const auto under_seven =
      static_cast<std::uint32_t>(static_cast<float>(population) * kShareUnderSeven + 0.5F);
  const auto school_age =
      static_cast<std::uint32_t>(static_cast<float>(population) * kShareSchool + 0.5F);
  const std::uint32_t children = under_seven + school_age;
  const std::uint32_t adults = population - old_count - children;

  // Old-timers first: pairs in their own yards, then a single.
  std::uint32_t old_households = 0;
  for (std::uint32_t placed = 0; placed < old_count; placed += 2) {
    const FamilyId yard = AppendRow(world.families, RollFamily(rng));
    ++old_households;
    const float age = DrawInRange(rng, 62.0F, 74.0F);
    ResidentRow first = RollPerson(rng, Sex::kMale, age, life_speedup);
    first.family = yard;
    const ResidentId first_id = AppendRow(world.residents, first);
    if (placed + 1 < old_count) {
      ResidentRow second = RollPerson(rng, Sex::kFemale, age - 2.0F, life_speedup);
      second.family = yard;
      second.spouse = first_id;
      const ResidentId second_id = AppendRow(world.residents, second);
      world.residents.rows[FindRow(world.residents, first_id)].spouse = second_id;
    }
  }

  // Working couples fill the remaining yards.
  const std::uint32_t couple_households =
      households > old_households ? households - old_households : 1;
  const std::uint32_t couples = couple_households < adults / 2 ? couple_households : adults / 2;
  std::array<ResidentId, 64> family_mothers = {};
  std::array<ResidentId, 64> family_fathers = {};
  for (std::uint32_t couple = 0; couple < couples; ++couple) {
    const FamilyId yard = AppendRow(world.families, RollFamily(rng));
    // Spread over the whole working band, as the reference pyramid does —
    // an all-fertile start would overheat the early growth.
    const float age = DrawInRange(rng, 20.0F, 58.0F);
    ResidentRow husband = RollPerson(rng, Sex::kMale, age, life_speedup);
    husband.family = yard;
    const ResidentId husband_id = AppendRow(world.residents, husband);
    ResidentRow wife = RollPerson(rng, Sex::kFemale, age - 2.0F, life_speedup);
    wife.family = yard;
    wife.spouse = husband_id;
    const ResidentId wife_id = AppendRow(world.residents, wife);
    world.residents.rows[FindRow(world.residents, husband_id)].spouse = wife_id;
    if (couple < family_mothers.size()) {
      family_mothers[couple] = wife_id;
      family_fathers[couple] = husband_id;
    }
  }

  // Unmarried grown-ups live with a couple as grown children; children are
  // dealt to the couples round-robin, with kinship links.
  const std::uint32_t grown = adults - couples * 2;
  for (std::uint32_t index = 0; index < grown + children; ++index) {
    const std::uint32_t host = couples == 0 ? 0 : index % couples;
    const ResidentId mother_id = family_mothers[host % family_mothers.size()];
    const std::uint32_t mother_row = FindRow(world.residents, mother_id);
    if (mother_row == kNoRow) {
      break;  // degenerate start parameters; genesis stays valid, just small
    }
    float age = 0.0F;
    if (index < grown) {
      age = DrawInRange(rng, 16.0F, 21.0F);
    } else if (index - grown < under_seven) {
      age = DrawInRange(rng, 0.5F, 6.5F);
    } else {
      age = DrawInRange(rng, 7.0F, 15.5F);
    }
    const Sex sex = NextRandomUnitFloat(rng) < 0.5F ? Sex::kFemale : Sex::kMale;
    ResidentRow child = RollPerson(rng, sex, age, life_speedup);
    child.family = world.residents.rows[mother_row].family;
    child.mother = mother_id;
    child.father = family_fathers[host % family_fathers.size()];
    AppendRow(world.residents, child);
  }

  if (world.residents.rows.size() != population) {
    LogWarning("genesis: start parameters rounded population to " +
               std::to_string(world.residents.rows.size()));
  }
  BuildStartEconomy(world, tables);
  return world;
}

}  // namespace core
