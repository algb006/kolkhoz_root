// Simulation run: where the idle hours come from, year by year.
//
// The census says the settlement idles nearly twice as many hours as it
// works, and on some seeds the working hours reach ZERO by the tenth year.
// There is no player in a run, so the village hands out its own orders, and
// "nobody was given anything to do" is therefore a statement about the farm
// and not about a missing chairman.
//
// WHAT THIS MEASURES, AND WHY IT IS NOT THE SAME QUESTION. Idleness has two
// possible causes and they need opposite cures:
//
//   THE HANDING OUT BROKE — there is work standing undone and nobody was
//   sent to it. That is a defect in the assignment.
//
//   THE WORK RAN OUT — the farm's demand is what twenty fields and a
//   handful of sites ask for, and the village grew past it. That is not a
//   defect at all; it is the shape of the model, and the cure is more to
//   do, not better assignment.
//
// The two are told apart by ONE number the activity census does not carry:
// the WORK STANDING UNDONE, in man-days, on the same day the idleness is
// counted. Every seam the labour sub-step can drain is summed — fields,
// hauling, sites, herds — because a seam is exactly "work somebody could be
// sent to right now".
//
// So: if idleness rises while the seams are FULL, the assignment is broken.
// If idleness rises while the seams are EMPTY, the village has outgrown its
// work. Nothing here guesses which; it prints both curves side by side.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "../common/fixture_policy.h"
#include "../common/orders_policy.h"
#include "../common/repair_policy.h"
#include "../common/run_harness.h"
#include "../common/yard_policy.h"
#include "core_common/calendar.h"
#include "core_common/resident_activity.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {

constexpr std::uint32_t kYears = 12;

core::ActivityRules RulesOfRun(const core::ITableSet& tables) {
  core::ActivityRules rules;
  rules.travel_hours = 0.5F;
  const core::ITable* const life = tables.FindTable("life");
  const std::uint32_t row =
      life == nullptr ? core::kNoTableRow : life->FindRowByKey("life_speedup");
  const std::uint32_t column = life == nullptr ? core::kNoTableColumn : life->FindColumn("value");
  if (row != core::kNoTableRow && column != core::kNoTableColumn) {
    rules.life_speedup = std::strtof(std::string(life->CellText(row, column)).c_str(), nullptr);
  }
  return rules;
}

/// @brief Work standing undone right now, in game man-days: every seam the
/// labour sub-step is able to drain.
///
/// THIS IS THE HALF THE CENSUS CANNOT SEE. A man is idle either because
/// nobody sent him or because there was nowhere to send him, and only this
/// number separates the two.
float SeamsStanding(const core::WorldState& world) {
  float days = 0.0F;
  for (const core::FieldRow& field : world.fields.rows) {
    days += field.work_days_remaining > 0.0F ? field.work_days_remaining : 0.0F;
    days += field.haul_days_remaining > 0.0F ? field.haul_days_remaining : 0.0F;
  }
  for (const core::UnitRow& unit : world.units.rows) {
    days += unit.construction.labor_days_remaining > 0.0F ? unit.construction.labor_days_remaining
                                                          : 0.0F;
  }
  for (const core::HerdRow& herd : world.herds.rows) {
    days += herd.care_days_remaining > 0.0F ? herd.care_days_remaining : 0.0F;
  }
  return days;
}

}  // namespace

int main(int argc, char** argv) {
  const std::uint32_t seed = argc > 1 ? static_cast<std::uint32_t>(std::atoi(argv[1])) : 1930;
  bool raise_derelict = false;
  // ONE ARM, ONE VARIABLE. The other instrument runs without a chairman and
  // finds winter the QUIET season; this one runs with one and finds winter
  // among the busiest. The chairman builds, and building is winter work, so
  // he was the obvious candidate — and he PREDICTS THE WRONG SIGN: his work
  // should have lowered winter idleness here, and winter idleness here is
  // higher. A candidate that predicts the opposite of what was measured
  // explains something else. Switching him off settles it in one run.
  bool no_chairman = false;
  // And the chairman is four policies, so "the chairman" is not yet an
  // answer: --yard-only keeps the one that raises the horse yard and
  // appoints a groom, and drops the other three.
  bool yard_only = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    raise_derelict = raise_derelict || argument == "--raise-derelict";
    no_chairman = no_chairman || argument == "--no-chairman";
    yard_only = yard_only || argument == "--yard-only";
  }
  const run::Simulation world = run::Start(seed);
  if (!world) {
    return 1;
  }
  // THE CONTROL ARM, and it answers the question the two curves alone
  // cannot. Ninety of the start canon's hundred and sixty hectares are
  // DERELICT — two fields of forty-five — and nothing in the core can raise
  // them: LandKind::kDerelict is skipped whole, and the work that would
  // clear it is phase-2 (land_state.h). So the farm's labour demand is
  // bounded by seventy hectares for ever.
  //
  // If the handing out of work were broken, giving the village more land
  // would change nothing. If the demand is the ceiling, the working hours
  // must rise. Raising them here is a PROBE and ships nowhere: it hands the
  // two fields the rotation of the field nearest them in size, because a
  // field with no rotation is sown with nothing and would measure the
  // rotation instead of the land.
  if (raise_derelict) {
    core::WorldState raised = world.State();
    core::CropId slots[3]{};
    for (const core::FieldRow& field : raised.fields.rows) {
      if (field.kind == core::LandKind::kArable && field.rotation_year0.value != 0) {
        slots[0] = field.rotation_year0;
        slots[1] = field.rotation_year1;
        slots[2] = field.rotation_year2;
        break;
      }
    }
    std::uint32_t lifted = 0;
    for (core::FieldRow& field : raised.fields.rows) {
      if (field.kind != core::LandKind::kDerelict) {
        continue;
      }
      field.kind = core::LandKind::kArable;
      field.rotation_year0 = slots[0];
      field.rotation_year1 = slots[1];
      field.rotation_year2 = slots[2];
      ++lifted;
    }
    world.simulation->ResetWorld(raised);
    std::cout << "idle_curve: CONTROL ARM — " << lifted
              << " derelict fields raised to arable. A PROBE, not a mechanic: nothing in the "
                 "core can do this, and nothing of it ships\n";
  }
  int failures = 0;
  const core::ActivityRules rules = RulesOfRun(*world.tables);
  run::YardPolicy yard(*world.tables);
  run::FixturePolicy fixture(*world.tables);
  run::OrdersPolicy orders;
  run::RepairPolicy repairs(*world.tables);

  std::cout << "idle_curve: seed " << seed << ", " << kYears << " years"
            << (no_chairman ? ", БЕЗ ПРЕДСЕДАТЕЛЯ" : (yard_only ? ", ТОЛЬКО КОНЮШНЯ" : "")) << '\n';
  std::cout << "idle_curve: год | жителей | рабочего возраста | работали | бездельничали | "
               "стояло работы, чел-дней (среднее за год)\n";
  // BY SEASON, because boss's second instrument found idleness sitting on
  // the busy season and asked whether that is a distribution defect. It is
  // only a distribution defect if there was work for those hands: the
  // standing seam against the working-age count in the SAME season is what
  // says so, and comparing one season with another cannot.
  std::array<double, 4> seam_by_season{};
  std::array<std::uint64_t, 4> idle_by_season{};
  std::array<std::uint64_t, 4> work_by_season{};
  std::array<std::uint64_t, 4> hands_by_season{};
  // HIS DENOMINATOR, not a second one. The other instrument counts the
  // share of an able adult's WORKING DAY: the hours that belonged to
  // idleness, work, the road, "nothing to work with" and truancy, and no
  // others. Two instruments that disagree because their denominators differ
  // have not disagreed about the world at all, and saying so would be a
  // false alarm dressed as a finding.
  std::array<std::uint64_t, 4> day_by_season{};
  // BY AGE BAND, because the other instrument found the young idling twice
  // as much as the old on all four seeds and that is not explained by there
  // being too little work: too little work falls on everybody.
  static constexpr std::array<float, 5> kBandFrom = {16.0F, 26.0F, 36.0F, 46.0F, 56.0F};
  std::array<std::uint64_t, 5> idle_by_band{};
  std::array<std::uint64_t, 5> day_by_band{};
  // AND BY ROW, which is the discriminator. At placement_level 0 — the
  // shipped value, "the start has no accountant and the chairman places
  // naively" — every candidate scores zero and the sort falls through to
  // its stable tiebreaker: the RESIDENT'S ROW, ascending. Rows are appended
  // at birth, so a fixed queue in birth order is exactly what a village of
  // growing population would see as "the young never work".
  //
  // Age and row are correlated, so a gradient by age proves nothing on its
  // own. If the gradient follows the ROW at least as tightly, the cause is
  // the queue and not the years.
  std::array<std::uint64_t, 5> idle_by_fifth{};
  std::array<std::uint64_t, 5> day_by_fifth{};
  for (std::uint32_t year = 0; year < kYears; ++year) {
    std::uint64_t worked = 0;
    std::uint64_t idled = 0;
    std::uint64_t blocked = 0;
    std::uint64_t walking = 0;
    std::uint64_t truant = 0;
    double seam_sum = 0.0;
    std::uint32_t samples = 0;
    std::uint32_t of_age = 0;
    for (std::uint32_t day = 0; day < core::kDaysPerYear; ++day) {
      if (!no_chairman) {
        yard.RunDay(*world.simulation);
        if (!yard_only) {
          fixture.RunDay(*world.simulation);
          orders.RunDay(*world.simulation);
          repairs.RunDay(*world.simulation);
        }
      }
      for (std::uint32_t tick = 0; tick < core::kTicksPerDay; ++tick) {
        world->AdvanceStep();
        const core::WorldState& state = world.State();
        for (std::uint32_t row = 0; row < state.residents.rows.size(); ++row) {
          const core::ResidentActivity what = core::ActivityOfResident(state, row, rules).activity;
          worked += what == core::ResidentActivity::kWorking ? 1U : 0U;
          idled += what == core::ResidentActivity::kIdle ? 1U : 0U;
          blocked += what == core::ResidentActivity::kBlocked ? 1U : 0U;
          walking += what == core::ResidentActivity::kWalking ? 1U : 0U;
          truant += what == core::ResidentActivity::kTruant ? 1U : 0U;
          const auto now = static_cast<std::size_t>(state.calendar.season);
          if (now < idle_by_season.size()) {
            idle_by_season[now] += what == core::ResidentActivity::kIdle ? 1U : 0U;
            work_by_season[now] += what == core::ResidentActivity::kWorking ? 1U : 0U;
            const float years = core::BiologicalAgeYears(
                rules.life_speedup, state.residents.rows[row].birth_day, state.calendar.day);
            std::size_t band = kBandFrom.size();
            for (std::size_t index = 0; index < kBandFrom.size(); ++index) {
              if (years >= kBandFrom[index]) {
                band = index;
              }
            }
            const bool counted =
                what == core::ResidentActivity::kIdle || what == core::ResidentActivity::kWorking ||
                what == core::ResidentActivity::kWalking ||
                what == core::ResidentActivity::kBlocked || what == core::ResidentActivity::kTruant;
            if (band < kBandFrom.size() && counted && years < 70.0F) {
              ++day_by_band[band];
              idle_by_band[band] += what == core::ResidentActivity::kIdle ? 1U : 0U;
              const std::size_t fifth =
                  std::min<std::size_t>(4,
                                        (static_cast<std::size_t>(row) * 5) /
                                            std::max<std::size_t>(1, state.residents.rows.size()));
              ++day_by_fifth[fifth];
              idle_by_fifth[fifth] += what == core::ResidentActivity::kIdle ? 1U : 0U;
            }
            day_by_season[now] += what == core::ResidentActivity::kIdle ||
                                          what == core::ResidentActivity::kWorking ||
                                          what == core::ResidentActivity::kWalking ||
                                          what == core::ResidentActivity::kBlocked ||
                                          what == core::ResidentActivity::kTruant
                                      ? 1U
                                      : 0U;
          }
        }
      }
      const core::WorldState& evening = world.State();
      const auto season = static_cast<std::size_t>(evening.calendar.season);
      if (season < seam_by_season.size()) {
        seam_by_season[season] += static_cast<double>(SeamsStanding(evening));
        ++hands_by_season[season];
      }
      seam_sum += static_cast<double>(SeamsStanding(evening));
      ++samples;
      of_age = 0;
      for (const core::ResidentRow& resident : evening.residents.rows) {
        const float age =
            core::BiologicalAgeYears(rules.life_speedup, resident.birth_day, evening.calendar.day);
        of_age += age >= rules.work_from_bio_years && age < rules.work_to_bio_years ? 1U : 0U;
      }
    }
    const core::WorldState& done = world.State();
    // THE ONE BINDING CLAIM, and it is the trap this run found: a year in
    // which NOBODY worked while work stood is a village that cannot get
    // out. Ploughing needs a horse and a man cannot pull a plough
    // (assignment.cpp), so a settlement that loses its last draught horse
    // stalls every arable field in kPlowing for ever — no ploughing, no
    // sowing, no harvest, no oats, no horses. Nothing in the model breaks
    // that circle, and "no way out" is a red line of the design.
    if (worked == 0 && seam_sum / (samples == 0 ? 1 : samples) > 1.0) {
      failures += run::Expect(false,
                              "a year with work standing and nobody working at all: the village "
                              "has no way out of this, and that is a red line");
    }
    std::cout << "idle_curve: " << (year + 1) << " | " << done.residents.rows.size() << " | "
              << of_age << " | " << worked << " | " << idled << " | "
              << (seam_sum / (samples == 0 ? 1 : samples)) << " | назначено-но-нечем " << blocked
              << " | в дороге " << walking << " | прогул " << truant << '\n';
  }
  // THE SEASONAL ARITHMETIC, and it answers the question boss's second
  // instrument raised: is the busy season's idleness a matter of who was
  // picked, or was there never work for those hands in the first place?
  static constexpr std::array<std::string_view, 4> kSeasons = {"зима", "весна", "лето", "осень"};
  std::cout << "idle_curve: сезон | стояло работы, чел-дней | работали, чел-часов | "
               "бездельничали | доля простоя\n";
  for (std::size_t index = 0; index < kSeasons.size(); ++index) {
    const double days = hands_by_season[index] == 0
                            ? 0.0
                            : seam_by_season[index] / static_cast<double>(hands_by_season[index]);
    const double total = static_cast<double>(day_by_season[index]);
    const double share = total == 0.0 ? 0.0 : static_cast<double>(idle_by_season[index]) / total;
    std::cout << "idle_curve: " << kSeasons[index] << " | " << days << " | "
              << work_by_season[index] << " | " << idle_by_season[index] << " | " << share << '\n';
  }

  static constexpr std::array<std::string_view, 5> kBandName = {
      "16-25", "26-35", "36-45", "46-55", "56-70"};
  std::cout << "idle_curve: полоса | доля простоя\n";
  for (std::size_t index = 0; index < kBandName.size(); ++index) {
    const double total = static_cast<double>(day_by_band[index]);
    std::cout << "idle_curve: " << kBandName[index] << " | "
              << (total == 0.0 ? 0.0 : static_cast<double>(idle_by_band[index]) / total) << '\n';
  }

  std::cout << "idle_curve: пятина строк | доля простоя\n";
  for (std::size_t index = 0; index < day_by_fifth.size(); ++index) {
    const double total = static_cast<double>(day_by_fifth[index]);
    std::cout << "idle_curve: " << (index + 1) << " | "
              << (total == 0.0 ? 0.0 : static_cast<double>(idle_by_fifth[index]) / total) << '\n';
  }

  // WHAT IS STANDING AT THE END, field by field. A seam that neither
  // drains nor changes for years is not "work waiting" — it is work nobody
  // can be sent to, and the difference is the whole question.
  const core::WorldState& last = world.State();
  const core::ITableSet* const last_tables = world.tables.get();
  // DID ANYTHING ACTUALLY GET BUILT? The other instrument reports that a
  // site never finishes on any core, ever. This run's chairman raises the
  // horse yard THROUGH THE ORDER BOOK — kBuildUnit, then kStartBuild, then
  // kUpgradeUnit — so if construction never completed, the yard would still
  // be at level 0 and there would be no draught horses at all.
  const core::ITable* const types = last_tables->FindTable("unit_types");
  const std::uint32_t yard_type =
      types == nullptr ? core::kNoTableRow : types->FindRowByKey("horse_yard");
  for (const core::UnitRow& unit : last.units.rows) {
    if (yard_type != core::kNoTableRow && unit.type.value == yard_type) {
      std::cout << "idle_curve: конюшня — ступень " << static_cast<int>(unit.level)
                << ", фаза стройки " << static_cast<int>(unit.construction.phase) << ", осталось "
                << unit.construction.labor_days_remaining << " чел-дней\n";
    }
  }
  std::uint32_t built = 0;
  for (const core::UnitRow& unit : last.units.rows) {
    built += unit.level > 0 ? 1U : 0U;
  }
  std::cout << "idle_curve: юнитов стоит " << built << " из " << last.units.rows.size() << '\n';

  // Draught horses specifically: ploughing cannot be done without one, and
  // a job that merely PREFERS a horse still takes one (assignment.cpp).
  const core::ITable* const livestock = last_tables->FindTable("livestock");
  const std::uint32_t horse_row =
      livestock == nullptr ? core::kNoTableRow : livestock->FindRowByKey("horse");
  std::uint32_t horses = 0;
  std::uint32_t all_head = 0;
  for (const core::HerdRow& herd : last.herds.rows) {
    all_head += herd.adult_count;
    if (horse_row != core::kNoTableRow && herd.kind.value == horse_row) {
      horses += herd.adult_count;
    }
  }
  std::uint32_t mowing = 0;
  std::uint32_t ploughing = 0;
  for (const core::FieldRow& field : last.fields.rows) {
    const bool meadow =
        field.kind == core::LandKind::kMeadow || field.kind == core::LandKind::kFloodplainMeadow;
    mowing += meadow && field.work_days_remaining > 0.0F ? 1U : 0U;
    ploughing += field.kind == core::LandKind::kArable &&
                         field.phase == core::FieldPhase::kPlowing &&
                         field.work_days_remaining > 0.0F
                     ? 1U
                     : 0U;
  }
  std::cout << "idle_curve: в конце — голов всего " << all_head << ", ТЯГЛОВЫХ ЛОШАДЕЙ " << horses
            << "; лугов под косой " << mowing << ", пашни под плугом " << ploughing << '\n';
  // WHY NOBODY IS SENT is the question, and there are only so many ways to
  // be unsendable: no house (a man with no home has no day to travel from),
  // too young, or a job that needs a horse and no horse free.
  std::uint32_t homeless = 0;
  std::uint32_t of_age = 0;
  for (const core::ResidentRow& resident : last.residents.rows) {
    const float age =
        core::BiologicalAgeYears(rules.life_speedup, resident.birth_day, last.calendar.day);
    if (!(age >= rules.work_from_bio_years && age < rules.work_to_bio_years)) {
      continue;
    }
    ++of_age;
    core::Vec2 home{};
    homeless += core::HomePositionOf(last, resident.family, home) ? 0U : 1U;
  }
  std::cout << "idle_curve: в конце — " << last.herds.rows.size() << " стад, взрослых голов "
            << horses << "; рабочего возраста " << of_age << ", из них БЕЗ ДОМА " << homeless
            << '\n';
  for (std::uint32_t row = 0; row < last.fields.rows.size(); ++row) {
    const core::FieldRow& field = last.fields.rows[row];
    if (field.work_days_remaining <= 0.0F && field.haul_days_remaining <= 0.0F) {
      continue;
    }
    std::cout << "idle_curve:   поле " << row << " вид " << static_cast<int>(field.kind) << " фаза "
              << static_cast<int>(field.phase) << " га " << field.area_ga << " работы "
              << field.work_days_remaining << " подвоза " << field.haul_days_remaining << " лежит "
              << field.reaped_grams << '\n';
  }
  std::cout << (failures == 0 ? "idle_curve: all checks passed\n" : "idle_curve: FAILED\n");
  return failures;
}
