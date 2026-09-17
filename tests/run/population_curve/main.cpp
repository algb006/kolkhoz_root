// Simulation run: 33 years of demography over the standard wiring, checked
// against the reference-curve milestones (stage-3 criterion, plan §5:
// ~200 by year 7, ~500 by year 14, ~1500 by year 33; reference run
// manual/balance/sim/demography.py at x4 acceleration, migration 8/year).
// Tolerances are wide on purpose: the core is a per-person stochastic model
// of the same rates, not the cohort arithmetic itself.
//
// NINE SEEDS, JUDGED ON THE MEDIAN (2026-09-17, boss's order). Until today
// every band here judged ONE trajectory, and the comment below the bands said
// in as many words why that is not enough: births are drawn against the
// world's RNG in row order, so a hair's difference in one family's satiety
// moves one draw and thirty-three years later the village is built of
// different people. The day that stopped being a caveat and became a wrong
// answer is recorded: the night pasture's gate gave 153 residents at year 33
// against 183 without it — and the SAME pair had the gated world HIGHER at
// year 14, 364 against 343. Less fodder cannot make more people; what stood
// between the two runs was a reshuffled random stream, and a band on one
// trajectory called that a regression.
//
// The median of nine is what the bands judge now. It buys the right to
// NARROW them, not to widen them: a measure that steadies is a measure that
// must catch more, and a band kept wide over a steadier measure would be the
// same instrument with a better excuse.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "../common/building_chairman.h"
#include "../common/run_harness.h"
#include "core_common/calendar.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {

/// The nine seeds. Nine and not five because nine already stands in two
/// instruments beside this one — `thirty_years` and the lean-season band of
/// `food_year` — and a third count in a third run would be a third home for
/// one decision. 1931 leads: it is the seed every earlier reading of this
/// curve was taken on, so the recorded numbers stay comparable.
constexpr std::array<std::uint64_t, 9> kSeeds = {
    1931, 1932, 1933, 1934, 1935, 1936, 1937, 1938, 1939};

constexpr std::uint32_t kYears = 33;

/// What one trajectory says. Everything the bands judge is here, so that the
/// walk below has no opinion about any of it: the walk lives a village, the
/// judging happens once, over nine of these.
struct Trajectory {
  std::uint64_t seed = 0;
  std::uint32_t start_population = 0;
  std::uint32_t start_families = 0;
  std::uint32_t year7 = 0;
  std::uint32_t year14 = 0;
  std::uint32_t year33 = 0;
  float male_share = 0.0F;
  std::uint32_t lived_years = 0;
  core::Epoch epoch = core::Epoch::kOne;
  bool families_hold = true;
  bool spouses_hold = true;
};

/// @brief Lives one village for `kYears` years and fills `out`.
/// @param print_years Prints the year-by-year line; true for the lead seed
///                    only, because nine times thirty-three lines of flows
///                    is a wall nobody reads.
/// @return false if the world would not start at all.
bool Walk(std::uint64_t seed, bool print_years, Trajectory& out) {
  const run::Simulation world = run::Start(seed);
  if (!world) {
    return false;
  }
  core::ISimulation* simulation = world.simulation.get();
  out.seed = seed;
  out.start_population =
      static_cast<std::uint32_t>(simulation->CompletedState().residents.rows.size());
  out.start_families =
      static_cast<std::uint32_t>(simulation->CompletedState().families.rows.size());

  // THE BUILDING CHAIRMAN (building_chairman.h). This run had no chairman at
  // all while weddings got a free house from a stub; the stub is gone (boss,
  // parcel 257), and a curve measured without anybody building is a curve of
  // a village leaving in its first winters — 36 at year 7, nobody at 14.
  run::BuildingChairman builder(*world.tables);

  for (std::uint32_t year = 1; year <= kYears; ++year) {
    for (std::uint32_t day = 0; day < core::kDaysPerYear; ++day) {
      run::AdvanceDays(*world, 1);
      builder.RunDay(*simulation);
    }
    const core::WorldState& state = simulation->CompletedState();
    const auto population = static_cast<std::uint32_t>(state.residents.rows.size());
    if (year == 7) {
      out.year7 = population;
    }
    if (year == 14) {
      out.year14 = population;
    }
    if (!print_years) {
      continue;
    }
    // THE FLOWS BESIDE THE STOCK, because a head count cannot say what moved
    // it. Read on 2026-09-17 to answer "who dies": the curve peaks at 395 in
    // year 17 and falls to 153 by 33, and this line is what told us that
    // NOBODY leaves and nobody dies of hunger — deaths are ordinary age
    // mortality (RunDeaths reads age and nothing else), departures are zero
    // every year, and BIRTHS GO TO ZERO for eight straight years once the
    // families' year-mean satiety drops under `birth_satiety_stop`. The
    // village does not starve to death; it stops being born and ages out.
    // Without these five numbers the same collapse reads as a famine, which
    // is the wrong repair.
    std::uint32_t men = 0;
    for (const core::ResidentRow& who : state.residents.rows) {
      men += who.sex == core::Sex::kMale ? 1 : 0;
    }
    std::cout << "year " << year << ": " << population << " residents, "
              << state.families.rows.size() << " families, epoch " << static_cast<int>(state.epoch)
              << ", men " << men << " women " << population - men << " | year "
              << state.ledger.closed.year << " closed: births " << state.ledger.closed.births
              << ", deaths " << state.ledger.closed.deaths << ", departures "
              << state.ledger.closed.departures << ", arrivals " << state.ledger.closed.arrivals
              << ", weddings " << state.ledger.closed.weddings << '\n';
  }

  const core::WorldState& final_state = simulation->CompletedState();
  out.year33 = static_cast<std::uint32_t>(final_state.residents.rows.size());
  out.lived_years = static_cast<std::uint32_t>(final_state.calendar.date.year) + 1;
  out.epoch = final_state.epoch;

  // Integrity after three decades of births, deaths, weddings and moves:
  // every resident's family exists, spouses point at each other. These are
  // not bands and are asked of EVERY seed — a broken link is broken however
  // the dice fell.
  for (std::uint32_t row = 0; row < final_state.residents.rows.size(); ++row) {
    const core::ResidentRow& resident = final_state.residents.rows[row];
    out.families_hold =
        out.families_hold && core::FindRow(final_state.families, resident.family) != core::kNoRow;
    if (resident.spouse.value != core::kInvalidEntityIdValue) {
      const std::uint32_t spouse_row = core::FindRow(final_state.residents, resident.spouse);
      out.spouses_hold = out.spouses_hold && spouse_row != core::kNoRow &&
                         final_state.residents.rows[spouse_row].spouse.value ==
                             final_state.residents.row_ids[row].value;
    }
  }

  std::uint32_t men = 0;
  for (const core::ResidentRow& resident : final_state.residents.rows) {
    men += resident.sex == core::Sex::kMale ? 1 : 0;
  }
  out.male_share =
      out.year33 == 0 ? 0.0F : static_cast<float>(men) / static_cast<float>(out.year33);
  return true;
}

/// @brief The middle value of an odd-sized sample, by sorting a copy.
/// @param values Taken by value on purpose: the caller's order is the order
///               the seeds were run in, and that order is what gets printed.
template <typename Number>
Number Median(std::vector<Number> values) {
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

}  // namespace

int main(int argc, char** argv) {
  int failures = 0;
  run::BuildingChairman::Declare("population_curve");

  // ONE SEED ON THE COMMAND LINE IS A PROBE, NOT A VERDICT: it prints the
  // year-by-year flows of a village nobody has looked at and judges nothing.
  // A band read on the median of nine cannot be applied to a sample of one,
  // and a run that pretended otherwise would hand back a red on a question it
  // never asked.
  if (argc > 1) {
    Trajectory one;
    if (!Walk(std::strtoull(argv[1], nullptr, 10), true, one)) {
      return 1;
    }
    std::cout << "population_curve: PROBE seed " << one.seed << " — year 7 = " << one.year7
              << ", year 14 = " << one.year14 << ", year 33 = " << one.year33 << ", male share "
              << one.male_share << " (printed, not judged)\n";
    return 0;
  }

  std::vector<Trajectory> walks;
  for (const std::uint64_t seed : kSeeds) {
    Trajectory walk;
    if (!Walk(seed, seed == kSeeds.front(), walk)) {
      return 1;
    }
    std::cout << "population_curve: seed " << walk.seed << " — year 7 = " << walk.year7
              << ", year 14 = " << walk.year14 << ", year 33 = " << walk.year33 << ", male share "
              << walk.male_share << '\n';
    walks.push_back(walk);
  }

  std::vector<std::uint32_t> year7;
  std::vector<std::uint32_t> year14;
  std::vector<std::uint32_t> year33;
  std::vector<float> male_shares;
  for (const Trajectory& walk : walks) {
    failures += run::Expect(walk.start_population == 80, "genesis seats 80 residents");
    failures += run::Expect(walk.start_families == 21, "genesis builds 21 yards");
    failures += run::Expect(walk.families_hold, "every resident's family exists");
    failures += run::Expect(walk.spouses_hold, "spouse links are symmetric");
    // THE FLOOR UNDER EVERY CEILING, which is not a band and cannot be a
    // matter of balance: the years were lived and the village is not the
    // genesis standing still. Every hard assertion below is an upper bound,
    // and the lower ones are KnownGap by design — so on 2026-09-16, with
    // every phase of the step removed, this run passed in 2.6 seconds against
    // the usual 25: eighty residents never born, never dead, never married,
    // under every ceiling (boss, standstill parcel 9).
    failures += run::Expect(walk.lived_years >= kYears, "the curve lived its thirty-three years");
    failures += run::Expect(walk.year33 != walk.start_population,
                            "and the village it drew is not the genesis standing still");
    year7.push_back(walk.year7);
    year14.push_back(walk.year14);
    year33.push_back(walk.year33);
    male_shares.push_back(walk.male_share);
  }

  const std::uint32_t median_year7 = Median(year7);
  const std::uint32_t median_year14 = Median(year14);
  const std::uint32_t median_year33 = Median(year33);
  const float median_male_share = Median(male_shares);

  // Reference milestones: 199 (year 7), ~500 (year 14), ~1500 (year 33).
  //
  // The upper ends also carry a known inflation: weddings get a free house
  // from a stub, so the canon's brake ("build houses or the village ages")
  // has never been applied (69-reconciliation.md §11).
  //
  // KNOWN GAPS, NOT MOVED BANDS (boss, parcel 301: "Цели оставить, облегчить
  // стройку"). The gap is the building chain; with free materials
  // thirty_years' nine seeds reach 486 by year 14. The upper ends still fail:
  // a village that explodes is not a gap, it is a fault.
  //
  // KNOWN GAP, MEASURED 2026-09-17 AND NOT THE ONE THIS LINE USED TO NAME.
  // It said "the gap is the building chain", meaning houses. The year-by-year
  // flows say otherwise, and say it flatly: the exodus for want of a roof
  // fires in ONE year of thirty-three, departures are zero in every other,
  // and the gate that actually closes is FOOD — the families' year-mean
  // satiety falls under `birth_satiety_stop` around year 17 and the village
  // has no births at all for the eight years after. The settlement does not
  // fail to be housed. It fails to be fed, and then ages out. Houses are a
  // gap; they are not THIS gap.
  // RESTORED FROM KnownGap TO A HARD ASSERTION 2026-09-17, because the
  // harness had been saying "KNOWN GAP CLOSED — restore the assertion" on
  // every run and nobody had. The nine seeds are what makes it safe to obey:
  // the median is 223 and the WEAKEST of the nine is 172, both clear of 150.
  // A gap left standing after it closes is the same defect as a guard that
  // cannot redden — it stops being able to report the day it reopens.
  failures += run::Expect(median_year7 >= 150, "the settlement is growing by year 7, not stalled");
  failures += run::Expect(median_year7 <= 320, "and not exploding by year 7");
  failures += run::KnownGap(median_year14 >= 380,
                            "and is past the Epoch II mark by year 14",
                            std::to_string(median_year14) + " against 380");
  failures += run::Expect(median_year14 <= 800, "and not exploding by year 14");
  failures += run::KnownGap(median_year33 >= 1150,
                            "and lands in the canon's order of magnitude by year 33",
                            std::to_string(median_year33) + " against 1150");
  failures += run::Expect(median_year33 <= 2300, "and not exploding by year 33");

  // The epoch switch is a population-threshold STUB (residents_system.cpp:
  // the designed era events — the readiness index, the ceremonies — are a
  // later phase). It flips at exactly 500, so asserting it at year 14 is a
  // HARDER claim about the same number the line above calls "about 500" with
  // a band of 380 to 650. Two assertions about one number, one banded and
  // one exact, is a contradiction in the test rather than in the model: a
  // run at 447 satisfies "about 500" and fails "the threshold was crossed".
  // What survives is the claim that matters — the settlement reaches Epoch II
  // on the way, not that it does so in a particular year of a stubbed rule.
  // The epochs follow the population threshold, so they are the same gap.
  failures += run::KnownGap(walks.front().epoch >= core::Epoch::kTwo,
                            "Epoch II is reached on the way",
                            "epoch " + std::to_string(static_cast<int>(walks.front().epoch)));
  failures += run::KnownGap(walks.front().epoch == core::Epoch::kThree,
                            "Epoch III has come by year 33",
                            "epoch " + std::to_string(static_cast<int>(walks.front().epoch)));

  // THE SEX BALANCE, ON THE MEDIAN OF NINE AND NARROWER FOR IT. The old band
  // was 0.42 to 0.58 over one trajectory — plus or minus eight points, which
  // is what a single village of a hundred and fifty souls needs before its
  // shot noise stops reddening it.
  //
  // MEASURED 2026-09-17, the nine as they came: 0.412, 0.452, 0.528, 0.512,
  // 0.503, 0.481, 0.480, 0.479, 0.508 — median 0.481, spread 0.412 to 0.528.
  // THE LEAD SEED IS THE EXTREME LOW OF THE NINE, and that is the whole
  // lesson of the day recorded as a number: 1931 alone read 0.412 and failed
  // the old band, and a morning went into explaining a skew that eight other
  // villages do not have. A median of nine has roughly a third of a single
  // draw's scatter, so the band below is half the old width and still four
  // times the median's own noise: it is the same claim, able to catch twice
  // as much.
  //
  // The claim itself is unchanged and worth restating, because it is the one
  // the model can break: newborn sex is a fair draw and nothing in the world
  // kills or removes one sex faster than the other — RunDeaths reads age
  // alone, RunOutflow reads age and marriage, and the exodus takes whole
  // families. A lasting skew here would mean one of those three stopped
  // being true.
  failures +=
      run::Expect(median_male_share > 0.45F && median_male_share < 0.55F, "the sex balance held");

  std::cout << "population_curve: nine seeds, MEDIAN year 7 = " << median_year7
            << ", year 14 = " << median_year14 << ", year 33 = " << median_year33 << ", male share "
            << median_male_share << '\n';
  if (failures == 0) {
    std::cout << "population_curve: all checks passed\n";
  }
  return failures;
}
