// Unit test of core_residents: stage-3 demography on a hand-built world —
// births only in fertile marriages, deaths by age, marriages of singles,
// migration arithmetic, link integrity — plus the family-satisfaction
// metrics phase with the epoch weights and the low-component law.

#include <cstdint>
#include <iostream>
#include <string_view>
#include <type_traits>

#include "core_common/calendar.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_residents/residents_system.h"
#include "core_tables/tables.h"

static_assert(std::is_abstract_v<core::IResidentsSystem>, "IResidentsSystem is a contract");
static_assert(std::has_virtual_destructor_v<core::IResidentsSystem>,
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

/// Appends an adult of the given biological age (life speedup 4).
core::ResidentId AddAdult(core::WorldState& world,
                          core::FamilyId family,
                          core::Sex sex,
                          float age_years) {
  core::ResidentRow person;
  person.family = family;
  person.sex = sex;
  person.birth_day = -static_cast<std::int32_t>(age_years * 12.0F);  // 12 days = 1 bio year
  return AppendRow(world.residents, person);
}

void Marry(core::WorldState& world, core::ResidentId wife, core::ResidentId husband) {
  world.residents.rows[FindRow(world.residents, wife)].spouse = husband;
  world.residents.rows[FindRow(world.residents, husband)].spouse = wife;
}

/// Runs the demography sub-step over `days` days the way the decisions slot
/// does: one call per day boundary.
void RunDays(core::IResidentsSystem& system, core::WorldState& world, std::uint32_t days) {
  for (std::uint32_t day = 0; day < days; ++day) {
    core::WorldState previous = world;
    world.calendar.tick += core::kTicksPerDay;
    core::RefreshCalendarCaches(world.calendar);
    system.RunDemographyDecisions(previous, world);
  }
}

}  // namespace

int main() {
  int failures = 0;
  const EmptyTableSet tables;  // canonical defaults compiled into the config
  const auto system = core::CreateResidentsSystem(tables);
  failures += Expect(system != nullptr, "factory yields a system");

  // A hand-built village: three fertile couples, one old man, one single.
  core::WorldState world;
  world.world_seed = 77;
  world.rng = core::SeedRngState(77, 0);
  for (int couple = 0; couple < 3; ++couple) {
    const core::FamilyId yard = AppendRow(world.families, core::FamilyRow{});
    const core::ResidentId wife =
        AddAdult(world, yard, core::Sex::kFemale, 22.0F + static_cast<float>(couple) * 4.0F);
    const core::ResidentId husband =
        AddAdult(world, yard, core::Sex::kMale, 24.0F + static_cast<float>(couple) * 4.0F);
    Marry(world, wife, husband);
  }
  const core::FamilyId old_yard = AppendRow(world.families, core::FamilyRow{});
  AddAdult(world, old_yard, core::Sex::kMale, 74.0F);
  const core::FamilyId single_yard = AppendRow(world.families, core::FamilyRow{});
  AddAdult(world, single_yard, core::Sex::kFemale, 20.0F);

  const std::uint32_t start_population = static_cast<std::uint32_t>(world.residents.rows.size());
  const std::uint32_t start_ids = world.residents.next_id_value;

  // Two game years of village life.
  RunDays(*system, world, 2 * core::kDaysPerYear);

  // Births happened, and every child has married parents and a live family.
  std::uint32_t births = 0;
  bool children_have_parents = true;
  bool families_hold = true;
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const core::ResidentRow& resident = world.residents.rows[row];
    families_hold = families_hold && FindRow(world.families, resident.family) != core::kNoRow;
    if (resident.birth_day >= 0) {
      ++births;
      children_have_parents = children_have_parents &&
                              resident.mother.value != core::kInvalidEntityIdValue &&
                              resident.father.value != core::kInvalidEntityIdValue;
    }
  }
  failures += Expect(births > 0, "fertile couples give births within two years");
  failures += Expect(children_have_parents, "every newborn has both parents recorded");
  failures += Expect(families_hold, "every resident's family exists");
  failures += Expect(world.residents.next_id_value >= start_ids + births,
                     "ids grow monotonically, never reused");

  // The 74-year-old (20%/year at 4x life speed) is gone within two game
  // years — eight biological years beyond the old-age band.
  bool old_man_alive = false;
  for (const core::ResidentRow& resident : world.residents.rows) {
    if (resident.birth_day <= -880) {
      old_man_alive = true;
    }
  }
  failures += Expect(!old_man_alive, "the old man died of age within two years");

  // The single woman married (a migrant or widower): spouse symmetry holds
  // for every married resident.
  bool spouses_symmetric = true;
  std::uint32_t married = 0;
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const core::ResidentRow& resident = world.residents.rows[row];
    if (resident.spouse.value == core::kInvalidEntityIdValue) {
      continue;
    }
    ++married;
    const std::uint32_t spouse_row = FindRow(world.residents, resident.spouse);
    spouses_symmetric =
        spouses_symmetric && spouse_row != core::kNoRow &&
        world.residents.rows[spouse_row].spouse.value == world.residents.row_ids[row].value;
  }
  failures += Expect(spouses_symmetric, "spouse links are symmetric");
  failures += Expect(married >= 6, "the starting couples are still married");

  // Migration: the canonical 8 a year arrived over two years (minus anyone
  // who died — so at least most of them).
  failures += Expect(world.residents.rows.size() + 5 >= start_population + births + 10,
                     "migrants arrived at about 8 a year");

  // The metrics phase: weighted satisfaction and the low-component law.
  {
    core::WorldState previous = world;
    core::WorldState current = world;
    current.families.rows[0].component_satiety = 60.0F;
    current.families.rows[0].component_common_cause = 60.0F;
    current.families.rows[0].component_needs = 60.0F;
    current.families.rows[0].component_rest = 60.0F;
    current.families.rows[1].component_satiety = 10.0F;  // starving
    current.families.rows[1].component_common_cause = 90.0F;
    current.families.rows[1].component_needs = 90.0F;
    current.families.rows[1].component_rest = 90.0F;
    // The rest component is no longer free-standing: since labor exists it
    // is the members' own rest, averaged, and the metrics phase recomputes
    // it (stage 5). Set the people, not the component.
    for (core::ResidentRow& resident : current.residents.rows) {
      if (resident.family.value == current.families.row_ids[0].value) {
        resident.rest = 60.0F;
      } else if (resident.family.value == current.families.row_ids[1].value) {
        resident.rest = 90.0F;
      }
    }
    core::IParallelPhase& metrics = system->MetricsPhase();
    const std::uint32_t count = metrics.ParallelItemCount(current);
    failures += Expect(count == static_cast<std::uint32_t>(current.families.rows.size()),
                       "the metrics phase iterates every family of the current state");
    metrics.RunItemRange(previous, current, 0, count);
    failures += Expect(current.families.rows[0].satisfaction == 60.0F,
                       "equal components give their value (Epoch I weights sum to 100)");
    failures += Expect(current.families.rows[1].satisfaction <= 20.0F,
                       "a component below 20 caps satisfaction at twice itself");
  }

  if (failures == 0) {
    std::cout << "unit_core_residents: all checks passed\n";
  }
  return failures;
}
