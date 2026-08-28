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

}  // namespace

WorldState CreateStartWorld(const ITableSet& tables, std::uint64_t world_seed) {
  WorldState world;
  world.world_seed = world_seed;
  world.rng = SeedRngState(world_seed, kWorldRngStream);
  world.calendar.day_zero_weekday = CampaignDayZeroWeekday(tables);
  RefreshCalendarCaches(world.calendar);

  const auto population =
      static_cast<std::uint32_t>(CampaignValue(tables, "start_population", 80.0F));
  const auto households =
      static_cast<std::uint32_t>(CampaignValue(tables, "start_households", 21.0F));
  const float life_speedup = LifeSpeedupFromTables(tables);
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
  return world;
}

}  // namespace core
