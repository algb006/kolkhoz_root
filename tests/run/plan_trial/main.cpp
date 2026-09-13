// Simulation run: IS «ПОД СУД» REACHABLE, and by what kind of bad play?
//
// Boss's order of 2026-09-13, and the question behind it is his sentence:
// "конец, до которого нельзя дойти, это не милосердие, а отсутствующий конец".
// The district takes the chairman to court after three failed plan years IN A
// ROW (campaign.csv, plan_failed_years_to_trial). On the canonical run the plan
// is failed in eleven years of thirty and the longest run is TWO — so the
// ending exists in the code and has never once been reached.
//
// "Is it reachable at all" is the wrong question, and the right one is boss's
// own: BY WHAT PLAY. So this measures ordinary chairman's mistakes and not
// sabotage — the three he named, each on its own and then together:
//
//   ALL THE LAND AT ONCE   every derelict field given a rotation in the first
//                          spring. Measured on 2026-09-13 to cost the entire
//                          first harvest and a decade of births; the question
//                          here is whether it also costs the plan three years
//                          running.
//   FALLOW EVERYWHERE      a third of the arable laid to fallow, every year.
//                          The mistake of a chairman who read that fallow
//                          restores fertility and did not read what it costs.
//   THE FUND UNSEALED      the seed fund opened in the first hungry winter.
//                          The one the design calls "осознанный выбор" — and
//                          it is exactly the choice a chairman makes when his
//                          people are thin and the spring is far away.
//
// ONE LEVER AT A TIME AND THEN ALL THREE, because "bad play fails" is not an
// answer boss can act on. If the trial needs all three at once, the threshold
// is too slack; if one is enough, it may be too tight. The run reports the
// longest failed run each lever produces and says which.
//
// IT ASSERTS ALMOST NOTHING ON PURPOSE. The threshold is boss's to set (his
// words: "если и там подряд не больше двух — порог смягчаю я, и это моя
// работа"), so this run MEASURES and does not judge. Its one gate is that the
// canonical play still cannot reach the trial — the claim 0.17.99 shipped, and
// the one thing here that would be a regression rather than a balance opinion.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "../common/fixture_policy.h"
#include "../common/repair_policy.h"
#include "../common/run_harness.h"
#include "../common/sowing_policy.h"
#include "../common/yard_policy.h"
#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {

/// Years each variant walks. TWENTY AND NOT THIRTY, and the reason is the
/// question rather than the budget: the trial needs three consecutive failures,
/// and every failure the canonical run has is in its first two decades — the
/// village is poorest early and the oat lattice that drives the failures runs
/// from the first year. A variant that cannot reach three in twenty years has
/// answered; carrying it to thirty would cost half the run's time to re-ask a
/// question already settled. Said out loud because it is a choice, not a given.
constexpr std::uint32_t kYears = 20;

/// Mean satiety at which a chairman reaches for the seed fund. Not a model
/// constant — a reading of "his people are thin": the canonical year's mean is
/// about 70 and its leanest day about 19 (food_year), so fifty is a winter that
/// is plainly going badly and not merely a lean week.
constexpr float kHungryWinter = 50.0F;

/// The obvious chairman's agronomy, and it is deliberately ONE pair of figures
/// rather than a table: the oat gap between the end of sowing and the start of
/// reaping (day 15 to day 28), and the last day a standing crop is safe from
/// the snow on the shipped climate. A man who can see that the ground is not
/// ready and that the season is running out knows this much and no more.
constexpr std::int32_t kObviousRipenDays = 13;
constexpr std::uint32_t kObviousSeasonLastDay = 42;

/// What is being done wrong, as a set of independent levers.
struct BadPlay {
  const char* name;
  bool all_land_at_once = false;
  bool fallow_everywhere = false;
  bool unseal_the_fund = false;
  /// THE OBVIOUS CHAIRMAN, and this flag is what turns the run's FLOOR into a
  /// game (boss, 2026-09-13). Every arm without it plays a village in which
  /// nobody ever touches the land — not one kSetRotation in thirty years — so
  /// reaching the trial there is the correct answer to "what becomes of a farm
  /// nobody steers", and not a broken threshold. The canon is measured on the
  /// arm that has it.
  bool obvious_chairman = false;
  /// ONE RULE SMARTER, and it exists to tell two things apart that the obvious
  /// chairman alone cannot: a layout he loses on because he is deliberately
  /// dim, and a layout NOBODY could have held. The first is ordinary
  /// difficulty; the second is a start with no way out, which the design
  /// forbids outright ("никаких безвыходных ситуаций"). Until somebody
  /// cleverer has tried, the two look identical.
  bool looks_ahead = false;
};

/// How the year came out, for one variant.
struct Verdict {
  std::uint32_t failed_years = 0;
  std::uint32_t worst_run = 0;
  std::uint32_t worst_run_year = 0;
  bool reached_trial = false;
  std::uint32_t residents_at_end = 0;
  /// WHAT THE LEVER ACTUALLY DID, and it is reported because it once did
  /// nothing: the seed-fund variant returned numbers identical to the
  /// canonical play down to the resident count, and identical numbers mean the
  /// world never diverged. A lever that did not fire and a lever that costs
  /// nothing are the same row in a results table and opposite findings, so the
  /// run says which rather than leaving it to be inferred.
  core::Grams grams_unsealed = 0;
  std::uint32_t lever_pulled_on_day = 0;
};

/// Fields with no chain at all — the derelict ground nobody has told anything.
/// HasRotation is the core's own test (land_state.h) and this is its mirror on
/// the test side: a field is "raised" once it has been given a chain.
std::vector<core::FieldId> UnassignedArable(const core::WorldState& world) {
  std::vector<core::FieldId> fields;
  for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
    const core::FieldRow& field = world.fields.rows[row];
    if (field.kind == core::LandKind::kArable && field.rotation_assigned == 0) {
      fields.push_back(world.fields.row_ids[row]);
    }
  }
  return fields;
}

/// The crops of the canonical chain, read off the START LAYOUT rather than
/// named here: a run that writes "oat, rye, potato" into itself is a second
/// home for the canon, and the canon is boss's.
std::vector<core::CropId> CropsInUse(const core::WorldState& world) {
  std::vector<core::CropId> crops;
  for (const core::FieldRow& field : world.fields.rows) {
    for (const core::CropId slot :
         {field.rotation_year0, field.rotation_year1, field.rotation_year2}) {
      if (slot.value != core::kInvalidDefIdValue && std::ranges::find(crops, slot) == crops.end()) {
        crops.push_back(slot);
      }
    }
  }
  return crops;
}

/// LEVER ONE: every derelict field gets a chain in the first spring.
void RaiseEverything(core::ISimulation& simulation, const core::WorldState& world) {
  const std::vector<core::FieldId> fields = UnassignedArable(world);
  const std::vector<core::CropId> crops = CropsInUse(world);
  if (fields.empty() || crops.empty()) {
    return;
  }
  std::vector<core::OrderRow> orders;
  orders.reserve(fields.size());
  for (std::size_t index = 0; index < fields.size(); ++index) {
    core::OrderRow order;
    order.kind = core::OrderKind::kSetRotation;
    order.field = fields[index];
    // The same three-season chain the canon uses, rolled by field so the new
    // ground does not all want the same crop in the same window.
    order.rotation_year0 = crops[index % crops.size()];
    order.rotation_year1 = crops[(index + 1) % crops.size()];
    order.rotation_year2 = crops[(index + 2) % crops.size()];
    orders.push_back(order);
  }
  simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
}

/// LEVER TWO: a third of the worked arable laid to bare fallow, in every slot.
/// An empty rotation slot IS fallow (order_state.h), so the order is a chain of
/// three invalid crops — which the book accepts and which costs the field its
/// whole year.
void FallowEverywhere(core::ISimulation& simulation, const core::WorldState& world) {
  std::vector<core::OrderRow> orders;
  std::uint32_t seen = 0;
  for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
    const core::FieldRow& field = world.fields.rows[row];
    if (field.kind != core::LandKind::kArable || field.rotation_assigned == 0) {
      continue;
    }
    if (seen++ % 3 != 0) {
      continue;  // one field in three
    }
    core::OrderRow order;
    order.kind = core::OrderKind::kSetRotation;
    order.field = world.fields.row_ids[row];
    orders.push_back(order);  // all three slots left invalid: bare fallow
  }
  if (!orders.empty()) {
    simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
  }
}

/// Everything the village holds of one resource, across every store. The funds
/// are NOTIONAL — a computation over one heap of grain, not a separate pile
/// (world_state.h) — so there is no "fund size" to read, and the honest figure
/// for "the chairman opened it" is what is actually there to be eaten.
core::Grams VillageStock(const core::WorldState& world, core::ResourceId resource) {
  core::Grams held = 0;
  for (const core::UnitRow& unit : world.units.rows) {
    if (resource.value < unit.stock.size()) {
      held += unit.stock[resource.value];
    }
  }
  return held;
}

/// LEVER THREE: the seed fund opened in a hungry winter, for every resource the
/// district's plan asks for — which is the grain, and which is also the seed.
/// The amount is the chairman's by design ("осознанный выбор, а не незаметная
/// утечка"), and a chairman whose people are thin asks for what is there.
void UnsealTheSeed(core::ISimulation& simulation, const core::WorldState& world) {
  std::vector<core::OrderRow> orders;
  for (std::uint32_t index = 0; index < world.plan.due.size(); ++index) {
    const core::ResourceId resource = core::DefIdFromIndex<core::ResourceIdTag>(index);
    const core::Grams held = VillageStock(world, resource);
    if (world.plan.due[index] <= 0 || held <= 0) {
      continue;
    }
    core::OrderRow order;
    order.kind = core::OrderKind::kUnsealFund;
    order.fund = core::FundKind::kSeed;
    order.resource = resource;
    order.amount = held;
    orders.push_back(order);
  }
  if (!orders.empty()) {
    simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
  }
}

/// Mean satiety across the village — the test for "a hungry winter", asked the
/// way the food model asks it.
float MeanSatiety(const core::WorldState& world) {
  if (world.residents.rows.empty()) {
    return 100.0F;
  }
  float total = 0.0F;
  for (const core::ResidentRow& resident : world.residents.rows) {
    total += resident.satiety;
  }
  return total / static_cast<float>(world.residents.rows.size());
}

/// Which one-shot levers have already been pulled in this variant.
struct Pulled {
  bool land = false;
  bool fund = false;
};

/// THE LEVERS, each pulled once and at the moment its mistake is made. The
/// land is raised in the first spring, the fallow is re-ordered every New
/// Year, and the fund is opened the first time the village is plainly hungry.
void PullLevers(const BadPlay& play,
                core::ISimulation& simulation,
                const core::WorldState& world,
                Pulled& pulled,
                Verdict& verdict) {
  if (play.all_land_at_once && !pulled.land && world.calendar.day >= 1) {
    RaiseEverything(simulation, world);
    pulled.land = true;
  }
  if (play.fallow_everywhere && world.calendar.day % core::kDaysPerYear == 1) {
    FallowEverywhere(simulation, world);
  }
  if (play.unseal_the_fund && !pulled.fund && MeanSatiety(world) < kHungryWinter) {
    UnsealTheSeed(simulation, world);
    pulled.fund = true;
    verdict.lever_pulled_on_day = world.calendar.day;
  }
}

Verdict Play(const BadPlay& play, std::uint64_t seed, std::uint32_t trial_threshold) {
  Verdict verdict;
  run::Simulation started = run::Start(seed);
  if (!started) {
    return verdict;
  }
  run::YardPolicy yard(*started.tables);
  run::FixturePolicy fixture(*started.tables);
  run::RepairPolicy repairs(*started.tables);
  // The ripening span and the season's end, read the way the core reads them:
  // the oat gap (last sowing day to first reaping day) and the day before the
  // seasonal mean falls to freezing. One figure for every crop is the obvious
  // chairman's whole agronomy.
  run::SowingPolicy chairman(
      kObviousRipenDays, kObviousSeasonLastDay, play.looks_ahead, started.tables.get());

  Pulled pulled;
  std::uint32_t run_length = 0;
  std::uint8_t failed_before = 0;
  for (std::uint32_t year = 0; year < kYears; ++year) {
    for (std::uint32_t day = 0; day < core::kDaysPerYear; ++day) {
      run::AdvanceDays(*started, 1);
      yard.RunDay(*started.simulation);
      fixture.RunDay(*started.simulation);
      repairs.RunDay(*started.simulation);
      if (play.obvious_chairman) {
        chairman.RunDay(*started.simulation);
      }
      const core::WorldState& world = started.State();

      PullLevers(play, *started.simulation, world, pulled, verdict);

      // THE FAILED-YEAR COUNTER IS READ AS A RISING EDGE, not as a level: the
      // district judges once a year and the byte then stands for the whole of
      // the next one, so a level test would count one failure forty-eight
      // times. Watching it RISE counts the judgement, which is the event.
      const std::uint8_t failed_now = world.plan.failed_years_in_a_row;
      if (failed_now > failed_before) {
        ++verdict.failed_years;
        run_length = failed_now;
        if (run_length > verdict.worst_run) {
          verdict.worst_run = run_length;
          verdict.worst_run_year = year + 1;
        }
        verdict.reached_trial = verdict.reached_trial || failed_now >= trial_threshold;
      }
      failed_before = failed_now;

      // DID THE UNSEALING ACTUALLY LAND? Asked of the STATE the order writes,
      // and not of the order.
      //
      // Two earlier drafts asked it of the wrong table twice. The order book is
      // swept the same step an order settles (core_world/world.cpp,
      // SweepOrderBook), so walking `world.orders` after the day counts zero
      // for every variant — including the one that plainly worked. The events
      // it leaves behind live in `step_events`, which holds ONE STEP, so
      // reading that once a day misses twenty-three ticks in twenty-four. Both
      // readings reported a confident zero, which is what an instrument does
      // when it is looking at the wrong place: it does not fail, it agrees.
      //
      // FundReleaseState stands until the year's turn, so it cannot be missed.
      const auto seed_slot = static_cast<std::size_t>(core::FundKind::kSeed);
      if (seed_slot < world.unsealed.by_fund.size()) {
        for (const core::Grams opened : world.unsealed.by_fund[seed_slot]) {
          verdict.grams_unsealed = std::max(verdict.grams_unsealed, opened);
        }
      }
    }
  }
  verdict.residents_at_end = static_cast<std::uint32_t>(started.State().residents.rows.size());
  // WHAT THE CHAIRMAN BUILT, printed because "is the store lever even
  // reachable by the run" was asked and could only be answered by looking. It
  // is: FixturePolicy runs on every arm and always has, so a granary and a
  // cattle yard go up in all of them — the store lever is not missing from the
  // instrument, it is already pulled.
  fixture.Report(started.State());
  if (play.obvious_chairman) {
    // SAID OUT LOUD, and boss made it the condition of the prosthetic existing
    // at all: the giving-back is a crutch for a verb the seam does not have,
    // and a line in the report is the only thing that stops it being read as a
    // mechanic in a month's time.
    chairman.Report();
  }
  return verdict;
}

}  // namespace

int main(int argc, char** argv) {
  const std::uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1929;

  // THE THRESHOLD IS READ AND NOT COPIED — the same rule 0.17.99's gate follows.
  // A run that writes "3" into itself stops measuring the campaign the day boss
  // softens the threshold, which is the very thing this run exists to inform.
  std::uint32_t trial_threshold = 0;
  {
    std::string error;
    const std::unique_ptr<core::ITableSet> tables = core::LoadTableSet("tables", &error);
    if (tables == nullptr) {
      std::cout << "FAIL: tables did not load (" << error << ") — run from the repo root\n";
      return 1;
    }
    const core::ITable* const campaign = tables->FindTable("campaign");
    const std::uint32_t key_col = campaign != nullptr ? campaign->FindColumn("key") : 0;
    const std::uint32_t value_col = campaign != nullptr ? campaign->FindColumn("value") : 0;
    for (std::uint32_t row = 0; campaign != nullptr && row < campaign->RowCount(); ++row) {
      if (campaign->CellText(row, key_col) == "plan_failed_years_to_trial") {
        trial_threshold = static_cast<std::uint32_t>(
            std::strtoul(std::string(campaign->CellText(row, value_col)).c_str(), nullptr, 10));
      }
    }
  }
  if (trial_threshold == 0) {
    std::cout << "FAIL: plan_failed_years_to_trial did not read off campaign.csv\n";
    return 1;
  }
  std::cout << "plan_trial: seed " << seed << ", " << kYears
            << " years a variant, the district takes him to court after " << trial_threshold
            << " failed years in a row\n";

  const std::vector<BadPlay> plays = {
      // THE FLOOR, and its name says what it is. It used to be called "the
      // canonical play", which is how the whole project came to read a
      // village nobody steers as the village a player would have. Not one
      // kSetRotation is issued here in twenty years: the layout genesis laid
      // down is worked unchanged, the derelict ninety hectares are never
      // raised, no field is ever released. The trial IS the expected end.
      {.name = "THE FLOOR: not one land decision in twenty years"},
      // AND THE CANON, which is this one. One rule, the obvious one.
      {.name = "the obvious chairman: releases what will not ripen", .obvious_chairman = true},
      // AND ONE RULE SMARTER, asked only to tell "he was dim" apart from
      // "nobody could have". Same verb, same order, same poorest-first — said
      // on the first day of the spring instead of at the deadline.
      {.name = "one rule smarter: keeps only what the spring can sow",
       .obvious_chairman = true,
       .looks_ahead = true},
      {.name = "all the land at once", .all_land_at_once = true},
      {.name = "a third laid to fallow, every year", .fallow_everywhere = true},
      {.name = "the seed fund opened in the first hungry winter", .unseal_the_fund = true},
      {.name = "all three together",
       .all_land_at_once = true,
       .fallow_everywhere = true,
       .unseal_the_fund = true},
  };

  int failures = 0;
  bool canon_reached_trial = false;
  bool floor_reached_trial = false;
  for (const BadPlay& play : plays) {
    const Verdict verdict = Play(play, seed, trial_threshold);
    std::cout << "plan_trial: " << play.name << " — plan failed in " << verdict.failed_years
              << " years of " << kYears << ", longest run " << verdict.worst_run << " (year "
              << verdict.worst_run_year << "), " << verdict.residents_at_end << " residents; "
              << (verdict.reached_trial ? "REACHED «ПОД СУД»" : "never reached the trial") << "\n";
    if (play.unseal_the_fund) {
      std::string opened =
          "THE FUND WAS NEVER OPENED — the winter never got hungry enough, so "
          "this row measures nothing";
      if (verdict.lever_pulled_on_day != 0 && verdict.grams_unsealed > 0) {
        opened = "the fund was opened on day " + std::to_string(verdict.lever_pulled_on_day) +
                 ", " + std::to_string(verdict.grams_unsealed / 1000) +
                 " kg of the largest "
                 "position released";
      } else if (verdict.lever_pulled_on_day != 0) {
        opened = "the order went in on day " + std::to_string(verdict.lever_pulled_on_day) +
                 " AND NOTHING WAS RELEASED — the order was refused or asked for nothing";
      }
      std::cout << "plan_trial:   " << opened << "\n";
    }
    const bool nothing_done_wrong =
        !play.all_land_at_once && !play.fallow_everywhere && !play.unseal_the_fund;
    // THE CANON IS THE OBVIOUS CHAIRMAN AND ONLY HIM. The look-ahead arm shares
    // his flag because it shares his verb, but it is a PROBE — asked on one
    // layout to tell "he was dim" apart from "nobody could have" — and gating
    // on it would let a probe's answer fail the run.
    if (nothing_done_wrong && play.obvious_chairman && !play.looks_ahead) {
      canon_reached_trial = verdict.reached_trial;
    }
    if (nothing_done_wrong && !play.obvious_chairman) {
      floor_reached_trial = verdict.reached_trial;
    }
  }

  // TWO GATES, ONE AT EACH END, and they used to be one — which is how the
  // run came to be read backwards for a day.
  //
  // The gate 0.17.99 shipped said "a chairman who does nothing wrong is not
  // taken to court", and it was checked on the arm where the chairman does
  // nothing AT ALL. Those are not the same sentence. A village nobody steers
  // SHOULD be tried: the instrument was honest and the reading was not.
  //
  // So the floor is asserted to REACH the trial and the canon to avoid it. The
  // first is the one that would be a finding if it went red: it would mean the
  // game forgives a chairman for never making a decision.
  // RED WITH ITS REASON PRINTED, so that a familiar red is not read as "that
  // one again" (boss, 2026-09-13). Both gates stand on seed 1929 alone, and
  // over nine seeds 1929..1937 neither holds as a property of the game:
  // measured the same day, once the herds stayed below the plan reserve and
  // the chairman answered the uncovered-position alarm, the floor reaches the
  // trial on 4 of 9 and the obvious chairman — answering the alarm on the best
  // fields up to the district's hectares — on 2 of 9, failing 2 to 12 plan
  // years of 20: the potato now grows and lies on the fields for want of room. A 4.4 m move of the
  // well alone moved the floor by one or two years on four seeds of nine. Turning the gates into
  // shares waits for the carting cure: a share set on a world whose harvest lies on the field would
  // record that defect as the norm.
  if (!floor_reached_trial || canon_reached_trial) {
    std::cout << "plan_trial: KNOWN RED — both gates stand on seed 1929 alone; over nine seeds "
                 "the floor reaches the trial on 4 of 9 and the obvious chairman on 2 of 9. "
                 "They wait to become shares after the carting cure\n";
  }
  failures += run::Expect(floor_reached_trial,
                          "a village nobody steers is taken to court: the trial is reachable");
  failures += run::Expect(!canon_reached_trial, "the obvious chairman is never taken to court");
  std::cout << (failures == 0 ? "plan_trial: all checks passed\n"
                              : "plan_trial: FAILURES " + std::to_string(failures) + "\n");
  return failures == 0 ? 0 : 1;
}
