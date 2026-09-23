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
#include "../common/timber_flow_tally.h"
#include "core_common/calendar.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/era_readiness.h"
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

/// The core's answers to the transition order, as slots of a tally: open,
/// the seven refusals in the order's own order, and not eligible.
constexpr std::size_t kDoorAnswerSlots = 9;

constexpr std::array<const char*, kDoorAnswerSlots> kDoorAnswerNames = {"open                ",
                                                                        "indices_not_held    ",
                                                                        "no_own_traction     ",
                                                                        "wintering_not_closed",
                                                                        "office_not_repaired ",
                                                                        "food_variety_short  ",
                                                                        "social_objects_short",
                                                                        "units_below_level   ",
                                                                        "not_eligible        "};

/// The slot of an answer. The seven refusals are consecutive enumerators
/// from kIndicesNotHeld (order_state.h), so their slot is their distance
/// from it plus one; anything else the function never returns reads as
/// "not eligible", the one answer left.
std::size_t DoorAnswerSlot(core::OrderRefusal answer) {
  if (answer == core::OrderRefusal::kNone) {
    return 0;
  }
  const auto first = static_cast<std::size_t>(core::OrderRefusal::kIndicesNotHeld);
  const auto value = static_cast<std::size_t>(answer);
  if (value >= first && value < first + kDoorAnswerSlots - 2) {
    return value - first + 1;
  }
  return kDoorAnswerSlots - 1;
}

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
  /// The day the first resident of THIS village crossed the filth threshold
  /// downwards, or 0 if none ever did. Not judged here — see the note over
  /// the ensemble line in main() for why the number is printed and not
  /// banded.
  std::uint32_t first_disease_day = 0;
  /// Days hot enough for the heat surcharge before that crossing. Zero means
  /// the surcharge and the dirty-work multiplier have never once met in the
  /// same day, which is a different statement from "the heat is small".
  std::uint32_t hot_days_before = 0;
  /// Readiness at the last turn this village lived, and the longest run of
  /// years its two indices BOTH stood above their thresholds. The run is the
  /// number the transition is opened on; the two indices are what boss reads
  /// to see how far off it is.
  float economic_index = 0.0F;
  float social_index = 0.0F;
  std::uint8_t longest_both_above = 0;
  float satisfaction_stub_points = 0.0F;
  /// The eight component scores as the FIRST year closed and as the
  /// thirty-third did, in the order of kComponentNames below.
  ///
  /// TWO YEARS AND NOT A MEAN, because the pair answers a question no average
  /// can: a component already near a hundred in year one has a scale that
  /// cannot tell a ruined farm from a sound one, and a component that has
  /// barely moved by year thirty-three is not measuring development at all.
  /// Either way the fault is the scale and not the threshold, and only the
  /// two columns side by side say which.
  std::array<float, 8> year1_components = {};
  std::array<float, 8> year33_components = {};
  /// How many of the thirty-three closed years each of the six transition
  /// blocks stood open in. A block decides the transition as hard as either
  /// index does, and until this line existed only the indices were visible —
  /// so a settlement could be read as "sixteen years above the thresholds"
  /// while a block nobody printed kept the door shut the whole time.
  std::array<std::uint32_t, 6> block_years = {};
  /// WAS IT EVER ASKED FOR. A nought in a block is two different worlds —
  /// "the fixture never ordered it" and "it was ordered and never came" —
  /// and the two have opposite repairs: the first is a hole in the run, the
  /// second a hole in the world. Counting SITES as well as buildings is what
  /// tells them apart, because a marked plot is an order nobody filled.
  std::uint32_t social_sites_ever = 0;
  std::uint32_t social_built_ever = 0;
  /// The highest level any unit of the village ever reached. One means no
  /// upgrade was ever finished; if no upgrade is ever ORDERED either, the
  /// level block is measuring a verb the fixture does not use.
  std::uint8_t highest_unit_level = 0;
  /// The village's mean categories in the WORST season of the last year —
  /// the number the variety block is compared against. Boss asks whether the
  /// measured world produces winter variety at all before he calls the
  /// threshold high.
  float worst_season_variety = 0.0F;
  std::uint8_t variety_seasons_seen = 0;
  /// The denominator beside the answer: how many units the village stands at
  /// the end, and how many of them have reached the level the transition
  /// asks. "Nought years open" says nothing about whether the shortfall is
  /// one stubborn type or the whole farm.
  std::uint32_t units_standing = 0;
  std::uint32_t units_at_level = 0;
  /// WAS IT EVER ASKED FOR, applied to the fixture's own policy. The first
  /// upgrade policy ordered nothing at all and the world looked exactly as it
  /// had; without this count the next reader would weigh the design instead
  /// of the veto.
  std::uint32_t upgrades_ordered = 0;
  /// Years in which ALL SIX blocks stood open at once — the only one of these
  /// numbers the transition actually turns on. Six blockers open 33, 32, 30,
  /// 7, 1 and 0 years say nothing about whether any YEAR had all six.
  std::uint32_t all_six_years = 0;
  /// Years in which exactly one block was shut, by which one: the true narrow
  /// place is the blocker that is most often the ONLY one closed, not the one
  /// with the fewest open years.
  std::array<std::uint32_t, 6> sole_holdout = {};
  /// How many years had exactly 0, 1, 2 … 6 blocks shut. The door needs all
  /// six, so the SHAPE of the shortfall decides whether any single repair can
  /// help: a year short by three is a year three repairs away.
  std::array<std::uint32_t, 7> shut_counts = {};
  /// How many years each unordered PAIR of blocks was shut together, indexed
  /// by (i, j) with i < j. The blocker in the most pairs is the real narrow
  /// place even when it never holds the door alone.
  std::array<std::uint32_t, 36> pair_years = {};
  /// What became of the upgrade orders, read at the UNIT (upgrade_policy.h).
  /// The tally that stood here read the order book a day after the events
  /// slot had swept it, and saw not one order of any status in nine villages
  /// over thirty-three years (retracted, 3af834f).
  run::UpgradePolicy::Fates upgrade_fates;
  /// WHY the upgrades were refused, by OrderRefusal code, read off the
  /// kOrderRefused event of the step that consumed the order. The verdicts
  /// at the unit said "refused, the rung is this era's" 26.7 times a village
  /// and could not say why (2026-09-23, the Epoch II diagnosis).
  std::array<std::uint32_t, static_cast<std::size_t>(core::OrderRefusal::kOrderRefusalCount)>
      upgrade_refusals = {};
  /// The material an upgrade's kMaterialsShort named, by resource row (the
  /// event carries it since 0.34.29), and the resources' keys to print it by.
  std::array<std::uint32_t, 256> upgrade_short_by_resource = {};
  std::vector<std::string> resource_keys;
  /// What held the social objects' marking (social_objects_policy.h, Held).
  run::SocialObjectsPolicy::Held social_held;
  std::vector<run::SocialObjectsPolicy::Fate> social_fates;
  std::vector<std::string> social_keys;
  /// Where the logs went, year by year (timber_flow_tally.h).
  std::vector<run::TimberFlowTally::Year> timber;
  std::int64_t first_sawmill_day = -1;
  /// The standing kolkhoz buildings by the era their SECOND rung opens in:
  /// Epoch I, a later era, or no second rung at all. Three counts that must
  /// sum to `units_standing`, printed beside it.
  std::uint32_t units_at_required = 0;
  std::uint32_t rung2_this_era = 0;
  std::uint32_t rung2_later_era = 0;
  std::uint32_t rung2_none = 0;
  /// The campaign year the chairman's order took the village into Epoch II,
  /// or 0 when it never did (transition_policy.h). Printed for EVERY village
  /// and not as a median (boss, epoch1-next seq 17): a median of "never" and
  /// three years says nothing about either.
  std::uint16_t transition_year = 0;
  /// The same event by the policy's own clock (calendar year + 1), printed
  /// beside the run's year so the two clocks are compared, not assumed.
  std::uint16_t transition_calendar_year = 0;
  /// Years by what the CORE answers the transition order that year — the
  /// same function the chairman's order is refused by (era_readiness.h,
  /// TransitionRefusal), asked at the yearly sample. Slot 0 is "open",
  /// 1..7 the seven refusals in the order's own order, 8 "not eligible"
  /// (already in Epoch II). A village that never went is explained here
  /// by the refusal that held it, and a year counted open that did NOT
  /// produce a transition would be a hole in the run's chairman.
  std::array<std::uint32_t, kDoorAnswerSlots> door_answers = {};
  /// The first campaign year whose sample found the door open, or 0. Printed
  /// beside the transition year: an open door with no transition after it
  /// is either the last year of the run or a hole in the chairman.
  std::uint16_t door_first_open_year = 0;
  /// For a door first open on the LAST sample: 1 when one more step took
  /// the village into Epoch II, 2 when it did not, 0 when not asked.
  std::uint8_t went_after_the_end = 0;
};

constexpr std::array<const char*, 6> kBlockNames = {"разнообразие пищи ",
                                                    "4 соцобъекта из 6",
                                                    "своя тяга/база   ",
                                                    "зимовка 2 года   ",
                                                    "юниты на уровне  ",
                                                    "правление ≤1%    "};

/// The eight, in the order they are gathered and printed.
constexpr std::array<const char*, 8> kComponentNames = {"план           (25)",
                                                        "продовольствие (20)",
                                                        "механизация    (20)",
                                                        "фонды          (15)",
                                                        "довольство     (35)",
                                                        "доля усилий    (25)",
                                                        "соцобъекты     (20)",
                                                        "демография     (10)"};

/// @brief The eight scores of a scored year, in the printed order.
std::array<float, 8> ComponentsOf(const core::ReadinessState& readiness) {
  return {readiness.economy.plan.score,
          readiness.economy.winter_stocks.score,
          readiness.economy.mechanisation.score,
          readiness.economy.funds.score,
          readiness.society.satisfaction.score,
          readiness.society.kolkhoz_effort.score,
          readiness.society.social_objects.score,
          readiness.society.demography.score};
}

/// The hot day, as `hot_afternoon_c` in weather_params.csv spells it and as
/// both the weather and hygiene read it: the AFTERNOON, which is the day's
/// mean plus its swing.
///
/// WRITTEN THE WRONG WAY FIRST, and the wrong way is worth keeping in the
/// record: this counter compared the day's MEAN, which is exactly the defect
/// it had been added to find in the hygiene rule. It printed zero for nine
/// villages twice — once truthfully, while the rule shared its mistake, and
/// once falsely, after the rule was fixed and the crossing day moved two days
/// without the counter noticing. An instrument that repeats the defect it
/// looks for agrees with the bug and reports a clean world.
constexpr float kHotAfternoonCelsius = 25.0F;

/// @brief Advances one whole game day, watching for the village's first
/// filth crossing on the way past.
///
/// TICK BY TICK AND NOT BY `AdvanceDays`, for one reason: the outbox is
/// cleared every step, so a loop that advances a whole day and reads the
/// events afterwards sees the last tick and calls the rest silence
/// (event_journal's header says the same of itself). This IS `AdvanceDays(1)`
/// — the order of the chairman's day after it is untouched, and the
/// population bands are the proof.
void LiveOneDay(core::ISimulation& simulation,
                const run::UpgradePolicy& upgrades,
                Trajectory& out) {
  bool hot_at_any_tick = false;
  const core::UnitId awaiting = upgrades.Awaiting();
  for (std::uint32_t tick = 0; tick < core::kTicksPerDay; ++tick) {
    simulation.AdvanceStep();
    const core::WorldState& state = simulation.CompletedState();
    if (awaiting.value != core::kInvalidEntityIdValue) {
      for (const core::SimEvent& event : state.step_events) {
        if (event.kind == core::EventKind::kOrderRefused && event.unit.value == awaiting.value &&
            event.amount >= 0 &&
            static_cast<std::size_t>(event.amount) < out.upgrade_refusals.size()) {
          ++out.upgrade_refusals[static_cast<std::size_t>(event.amount)];
          if (event.amount == static_cast<std::int64_t>(core::OrderRefusal::kMaterialsShort) &&
              event.resource.value < out.upgrade_short_by_resource.size()) {
            ++out.upgrade_short_by_resource[event.resource.value];
          }
        }
      }
    }
    if (out.first_disease_day != 0) {
      continue;
    }
    const float afternoon =
        state.weather.air_temperature_celsius + state.weather.temperature_swing_celsius;
    hot_at_any_tick = hot_at_any_tick || afternoon >= kHotAfternoonCelsius;
    for (const core::SimEvent& event : state.step_events) {
      if (event.kind == core::EventKind::kHygieneDisease) {
        out.first_disease_day = static_cast<std::uint32_t>(state.calendar.day);
        break;
      }
    }
  }
  // AN UPPER BOUND AND NOT THE COUNT, deliberately. The rule reads the
  // temperature on the demography tick; this run does not know which tick
  // that is, so it asks whether the day was hot at ANY tick. Too high by
  // construction — which is exactly what makes a ZERO here a proof that the
  // heat surcharge and the dirty-work multiplier have never once met in the
  // same day, rather than a hint that the heat is small.
  out.hot_days_before += hot_at_any_tick ? 1U : 0U;
}

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
  run::TimberFlowTally timber(*world.tables);
  if (const core::ITable* const resources = world.tables->FindTable("resources")) {
    const std::uint32_t key_column = resources->FindColumn("key");
    for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
      out.resource_keys.emplace_back(key_column == core::kNoTableColumn
                                         ? std::to_string(row)
                                         : std::string(resources->CellText(row, key_column)));
    }
  }

  for (std::uint32_t year = 1; year <= kYears; ++year) {
    for (std::uint32_t day = 0; day < core::kDaysPerYear; ++day) {
      // THE DAY IS WALKED TICK BY TICK AND NOT BY AdvanceDays, for one
      // reason: the outbox is cleared every step, so a loop that advances a
      // whole day and reads the events afterwards sees the last tick and
      // calls the rest silence (event_journal's own header says the same).
      // `AdvanceDays(1)` IS this loop — the order of `builder.RunDay` after
      // the day is untouched, and the bands below are the proof.
      LiveOneDay(*simulation, builder.upgrades, out);
      builder.RunDay(*simulation);
      timber.CountDay(simulation->CompletedState());
    }
    const core::WorldState& state = simulation->CompletedState();
    const auto population = static_cast<std::uint32_t>(state.residents.rows.size());
    if (year == 7) {
      out.year7 = population;
    }
    if (year == 14) {
      out.year14 = population;
    }
    // THE RUN IS TAKEN AS IT PASSES, not read off the final state. It is
    // reset to nought by the first year that falls short, so a village that
    // held three years in a row at year 20 and slipped at 21 would read as
    // nought at 33 — and "never reached it" and "reached it and lost it" are
    // the two answers boss is asking to tell apart.
    out.longest_both_above = std::max(out.longest_both_above, state.readiness.both_above_run);
    out.economic_index = state.readiness.economic_index;
    out.social_index = state.readiness.social_index;
    out.satisfaction_stub_points = state.readiness.satisfaction_stub_points;
    if (year == 1) {
      out.year1_components = ComponentsOf(state.readiness);
    }
    if (year == kYears) {
      out.year33_components = ComponentsOf(state.readiness);
    }
    const core::TransitionBlocks& blocks = state.readiness.blocks;
    out.block_years[0] += blocks.food_variety;
    out.block_years[1] += blocks.social_objects;
    out.block_years[2] += blocks.own_traction;
    out.block_years[3] += blocks.wintering_two_years;
    out.block_years[4] += blocks.units_at_level;
    out.block_years[5] += blocks.office_repaired;
    const std::array<std::uint8_t, 6> open = {blocks.food_variety,
                                              blocks.social_objects,
                                              blocks.own_traction,
                                              blocks.wintering_two_years,
                                              blocks.units_at_level,
                                              blocks.office_repaired};
    std::uint32_t shut = 0;
    std::size_t last_shut = 0;
    for (std::size_t index = 0; index < open.size(); ++index) {
      if (open[index] == 0) {
        ++shut;
        last_shut = index;
      }
    }
    out.all_six_years += shut == 0 ? 1U : 0U;
    const core::OrderRefusal door = builder.transition.Refusal(state);
    ++out.door_answers[DoorAnswerSlot(door)];
    if (door == core::OrderRefusal::kNone && out.door_first_open_year == 0) {
      out.door_first_open_year = static_cast<std::uint16_t>(year);
    }
    // THE SAME CLOCK AS THE DOOR, the run's own year: the policy's
    // YearTaken counts off the calendar, and two clocks side by side is how
    // a one-year lag reads as a hole (or a hole reads as a lag).
    if (state.epoch != core::Epoch::kOne && out.transition_year == 0) {
      out.transition_year = static_cast<std::uint16_t>(year);
    }
    if (shut == 1) {
      ++out.sole_holdout[last_shut];
    }
    ++out.shut_counts[shut];
    for (std::size_t first = 0; first < open.size(); ++first) {
      for (std::size_t second = first + 1; second < open.size(); ++second) {
        if (open[first] == 0 && open[second] == 0) {
          ++out.pair_years[(first * open.size()) + second];
        }
      }
    }
    for (const core::UnitRow& unit : state.units.rows) {
      out.highest_unit_level = std::max(out.highest_unit_level, unit.level);
    }
    out.worst_season_variety = state.ledger.closed.worst_season_variety;
    // THE COUNT BESIDE THE NUMBER, because the block tests both and yesterday
    // I blamed the count from an armchair. Which of the two shuts the gate is
    // a measurement, not a derivation.
    out.variety_seasons_seen = state.ledger.closed.variety_seasons_seen;
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
  // ORDERED OR NOT ORDERED, asked of the same catalogue the score uses so the
  // two cannot disagree about which six types the list is.
  const core::ReadinessCatalog catalog =
      core::ReadReadinessCatalog(*world.tables, core::Epoch::kOne);
  for (const core::UnitTypeId type : catalog.social_objects) {
    bool site = false;
    bool built = false;
    for (const core::UnitRow& unit : final_state.units.rows) {
      if (unit.type.value != type.value || unit.dead != 0) {
        continue;
      }
      site = true;  // a row at all is an order somebody placed
      built = built || unit.level >= 1;
    }
    out.social_sites_ever += site ? 1U : 0U;
    out.social_built_ever += built ? 1U : 0U;
  }
  for (const core::UnitRow& unit : final_state.units.rows) {
    if (unit.level == 0 || unit.dead != 0) {
      continue;  // a marked plot is not a unit that failed to be upgraded
    }
    // THE KOLKHOZ'S OWN, which is what the blocker now counts: a family's
    // house at level one in good repair is a hundred-per-cent house (housing
    // §6), and the era does not ask for its level.
    bool kolkhoz = false;
    for (const core::UnitTypeId type : catalog.kolkhoz_types) {
      kolkhoz = kolkhoz || type.value == unit.type.value;
    }
    if (!kolkhoz) {
      continue;
    }
    ++out.units_standing;
    out.units_at_level += unit.level >= 2 ? 1U : 0U;
    // And at the level the era REQUIRES — what the block reads since
    // 2026-09-18 — beside the level-2 count, so the two can be compared.
    out.units_at_required +=
        unit.level >= core::RequiredUnitLevel(catalog, unit.type, final_state.epoch) ? 1U : 0U;
    // WHICH OF THEM CAN GET THERE IN THIS ERA AT ALL: the rung 2 of a type
    // opens in the era unit_levels.csv names, and an order for a later era's
    // rung is refused with kGateClosed however often it is repeated.
    const std::uint8_t era = builder.upgrades.RungEra(unit.type, 2);
    out.rung2_this_era += era == 1 ? 1U : 0U;
    out.rung2_later_era += era > 1 ? 1U : 0U;
    out.rung2_none += era == 0 ? 1U : 0U;
  }
  out.upgrades_ordered = builder.upgrades.ordered();
  out.upgrade_fates = builder.upgrades.FatesAtEnd(*simulation);
  out.social_held = builder.social.held();
  out.social_fates = builder.social.fates();
  if (const core::ITable* const types = world.tables->FindTable("unit_types")) {
    const std::uint32_t key_column = types->FindColumn("key");
    for (const core::UnitTypeId id : builder.social.wanted()) {
      out.social_keys.emplace_back(key_column == core::kNoTableColumn
                                       ? std::to_string(id.value)
                                       : std::string(types->CellText(id.value, key_column)));
    }
  }
  out.timber = timber.years();
  out.first_sawmill_day = timber.first_sawmill_day();
  out.transition_calendar_year = builder.transition.YearTaken();
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

  // THE DOOR THAT OPENED ON THE LAST SAMPLE. The chairman has already staged
  // the order after the last day, and the run would end before any step
  // read it — an open door with nobody through it, which reads as a hole
  // in the chairman. One more STEP, after every measurement above is taken,
  // lets the core answer it: the whole chain, order to era, seen in a run
  // rather than only in the unit test.
  if (out.door_first_open_year != 0 && out.transition_year == 0) {
    simulation->AdvanceStep();
    out.went_after_the_end = simulation->CompletedState().epoch != core::Epoch::kOne ? 1 : 2;
  }
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
              << one.male_share << ", first filth disease day " << one.first_disease_day
              << " (printed, not judged)\n";
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
              << walk.male_share << ", first filth disease day " << walk.first_disease_day
              << " (hot days up to it, upper bound, " << walk.hot_days_before << ")\n";
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

  // THE FIRST FILTH DISEASE IS READ OFF THE MINIMUM AND NOT THE MEDIAN, and
  // the reason is the whole of it: "the first" in a village is the EARLIEST
  // over everybody, so the ensemble's answer to it is the earliest over the
  // nine, not the middle one. The bands above are about a typical village;
  // this number is about the unluckiest, and a median here would answer the
  // adjacent question convincingly.
  //
  // THE DAY IS PRINTED AND THE YEAR IS ASSERTED, and the line between them is
  // the line between a balance reading and an intent. Day 57 is a
  // calibration: boss moves two knobs and it moves, and a band on it would
  // be rewritten with every balance edit until nobody read it. "NOT IN THE
  // FIRST YEAR" is the design — «не в первую весну, когда игроку не до бани»
  // — and it has something to fall on: the first measurement, before the
  // knobs moved, put the earliest crossing on day 35, which is the first
  // September. The acceptance in tests/unit/core_residents asserts the rule
  // itself and says nothing about any day, deliberately; this is the other
  // half of that division.
  std::vector<std::uint32_t> first_days;
  for (const Trajectory& walk : walks) {
    if (walk.first_disease_day != 0) {
      first_days.push_back(walk.first_disease_day);
    }
  }
  // THE SIZE OF THE SET BESIDE THE ANSWER. "None of the nine ever crossed"
  // and "the earliest crossed on day N" are different facts, and without the
  // count a minimum over an empty sample prints as silence — and, worse,
  // passes the year assertion below for having nothing to judge. So the
  // count is asserted FIRST, and it is a real claim rather than a
  // formality: no fixture builds a bathhouse, hygiene has no other riser,
  // so every village must reach the threshold inside thirty-three years.
  std::cout << "population_curve: first filth disease — " << first_days.size() << " of "
            << kSeeds.size() << " villages crossed the threshold";
  if (!first_days.empty()) {
    const std::uint32_t earliest = *std::ranges::min_element(first_days);
    const std::uint32_t latest = *std::ranges::max_element(first_days);
    std::cout << ", EARLIEST day " << earliest << " (year "
              << (earliest + core::kDaysPerYear - 1) / core::kDaysPerYear << "), latest day "
              << latest;
  }
  std::cout << '\n';

  // READINESS FOR EPOCH II, printed and not banded — the same division as the
  // filth day above: the thresholds are balance and STUB, the shape is
  // asserted in tests/unit/core_world. What this line answers is boss's
  // question of parcel 138: do both indices hold above their thresholds for
  // three years running in ANY of the nine, or is the transition out of reach
  // today?
  //
  // THE STUB PRICE IS PRINTED AS A NUMBER AND ALWAYS (boss, parcel 132), not
  // only when it is large: a mark that shows up at a threshold teaches its
  // reader that its absence means "all honest".
  std::uint8_t best_run = 0;
  for (const Trajectory& walk : walks) {
    std::cout << "population_curve: seed " << walk.seed << " readiness at year " << kYears
              << " — economy " << walk.economic_index << ", society " << walk.social_index
              << ", longest run of BOTH above threshold "
              << static_cast<int>(walk.longest_both_above) << " years (of 3 needed); "
              << walk.satisfaction_stub_points << " of satisfaction's 100 points are a STUB\n";
    best_run = std::max(best_run, walk.longest_both_above);
  }
  std::cout << "population_curve: best run of both indices above threshold over nine villages — "
            << static_cast<int>(best_run) << " years (printed, not judged)\n";

  // THE EIGHT COMPONENTS AT BOTH ENDS OF THE CAMPAIGN (boss, parcel 142).
  // Means over the nine, year 1 against year 33, so that a scale which cannot
  // tell a ruined farm from a sound one and a scale which does not move in
  // thirty-three years are both visible as themselves rather than as a
  // threshold that needs turning.
  std::cout << "population_curve: readiness components, mean of nine — year 1 | year 33\n";
  for (std::size_t index = 0; index < kComponentNames.size(); ++index) {
    float first = 0.0F;
    float last = 0.0F;
    for (const Trajectory& walk : walks) {
      first += walk.year1_components[index];
      last += walk.year33_components[index];
    }
    const auto count = static_cast<float>(walks.size());
    std::cout << "  " << kComponentNames[index] << "  " << (first / count) << " | "
              << (last / count) << '\n';
  }

  // THE SIX BLOCKS, which decide the transition as hard as either index and
  // were invisible until now. Printed as the mean number of years out of
  // thirty-three that each stood open: a block open NOUGHT years is a door
  // that never unlocks however high the indices climb, and one open all
  // thirty-three is a block that blocks nothing. Either is worth knowing, and
  // neither could be read off the two indices that were printed beside them.
  std::cout << "population_curve: transition blocks, mean years OPEN of " << kYears
            << " (0 = the door never unlocks; " << kYears << " = it never blocks)\n";
  for (std::size_t index = 0; index < kBlockNames.size(); ++index) {
    float open_years = 0.0F;
    for (const Trajectory& walk : walks) {
      open_years += static_cast<float>(walk.block_years[index]);
    }
    std::cout << "  " << kBlockNames[index] << "  "
              << (open_years / static_cast<float>(walks.size())) << '\n';
  }

  // WAS IT EVER ASKED FOR. Three numbers that turn a nought in a block from a
  // verdict into a question with an answer: how many of the six social types
  // were ever MARKED against how many were ever finished, the highest level
  // any unit reached, and the village's worst season of the last year against
  // the variety threshold. "Nobody ordered it" is a hole in the fixture;
  // "ordered and never came" is a hole in the world.
  float sites = 0.0F;
  float built = 0.0F;
  float level = 0.0F;
  float variety = 0.0F;
  float seasons = 0.0F;
  for (const Trajectory& walk : walks) {
    seasons += static_cast<float>(walk.variety_seasons_seen);
    sites += static_cast<float>(walk.social_sites_ever);
    built += static_cast<float>(walk.social_built_ever);
    level += static_cast<float>(walk.highest_unit_level);
    variety += walk.worst_season_variety;
  }
  const auto villages = static_cast<float>(walks.size());
  std::cout << "population_curve: was it ever asked for — social types MARKED "
            << (sites / villages) << " of "
            << (walks.empty() ? 0U : walks.front().social_keys.size())
            << " the era wants (the block asks 4), FINISHED " << (built / villages)
            << "; highest unit level reached " << (level / villages) << "; worst season's variety "
            << (variety / villages) << " categories over " << (seasons / villages)
            << " seasons seen\n";
  // WHAT HELD THE MARKING, day by day after the farm stood, the first reason
  // in the policy's order (social_objects_policy.h, Held); means of nine.
  float nothing_left = 0.0F;
  float going_up = 0.0F;
  float farm_first = 0.0F;
  float house_waits = 0.0F;
  for (const Trajectory& walk : walks) {
    nothing_left += static_cast<float>(walk.social_held.nothing_left);
    going_up += static_cast<float>(walk.social_held.one_going_up);
    farm_first += static_cast<float>(walk.social_held.farm_first);
    house_waits += static_cast<float>(walk.social_held.a_house_waits);
  }
  // THE SIX BY TYPE (boss, boss-core-epoch1-3 seq 7): per village, for each
  // wanted type, the campaign year it was first a site and first stood
  // ("-" never), its site-days, and of those the marked days short of recipe.
  if (!walks.empty()) {
    const std::vector<std::string>& keys = walks.front().social_keys;
    for (std::size_t type = 0; type < keys.size(); ++type) {
      std::cout << "population_curve: social " << keys[type] << " — site/built year by village:";
      std::uint64_t site_days = 0;
      std::uint64_t short_days = 0;
      std::uint32_t built_in = 0;
      for (const Trajectory& walk : walks) {
        if (type >= walk.social_fates.size()) {
          std::cout << " ?";
          continue;
        }
        const run::SocialObjectsPolicy::Fate& fate = walk.social_fates[type];
        const auto year = [](std::int64_t day) {
          return day < 0 ? std::string("-") : std::to_string(day / core::kDaysPerYear + 1);
        };
        std::cout << ' ' << year(fate.first_site_day) << '/' << year(fate.built_day);
        site_days += fate.site_days;
        short_days += fate.materials_short_days;
        built_in += fate.built_day >= 0 ? 1U : 0U;
      }
      std::cout << " — built in " << built_in << " of " << walks.size() << ", site-days "
                << site_days << ", of them short of recipe " << short_days;
      std::map<std::uint16_t, std::uint64_t> first_short;
      for (const Trajectory& walk : walks) {
        if (type < walk.social_fates.size()) {
          for (const auto& [resource, days] : walk.social_fates[type].short_by_resource) {
            first_short[resource] += days;
          }
        }
      }
      std::cout << ", first short:";
      for (const auto& [resource, days] : first_short) {
        const std::vector<std::string>& names = walks.front().resource_keys;
        std::cout << ' ' << (resource < names.size() ? names[resource] : std::to_string(resource))
                  << " x" << days;
      }
      std::cout << '\n';
    }
  }
  std::cout << "population_curve: social marking held, days of " << 33U * core::kDaysPerYear
            << " — every object stands " << (nothing_left / villages) << ", one still going up "
            << (going_up / villages) << ", the farm first " << (farm_first / villages)
            << ", a house waits " << (house_waits / villages)
            << " (days before the farm stood are not counted)\n";
  // WHERE THE LOGS WENT (boss seq 25): the lead village year by year, then
  // the nine villages' thirty-three-year totals, means of nine.
  if (!walks.empty()) {
    run::TimberFlowTally::PrintYears(
        "population_curve (seed " + std::to_string(walks[0].seed) + ")", walks[0].timber);
    run::TimberFlowTally::Year total;
    std::size_t years_seen = 0;
    for (const Trajectory& walk : walks) {
      years_seen += walk.timber.size();
      for (const run::TimberFlowTally::Year& y : walk.timber) {
        total.felled += y.felled;
        total.to_houses += y.to_houses;
        total.to_social += y.to_social;
        total.to_upgrades += y.to_upgrades;
        total.to_other += y.to_other;
        total.spoiled += y.spoiled;
        total.lying_on_stands += y.lying_on_stands;
        total.in_stores += y.in_stores;
        total.lying_no_demand += y.lying_no_demand;
        total.lying_no_carter += y.lying_no_carter;
        total.lying_carted += y.lying_carted;
        total.carters_on_carted_days += y.carters_on_carted_days;
        total.demand_days_on_carted_days += y.demand_days_on_carted_days;
        total.load_tonnes_on_carted_days += y.load_tonnes_on_carted_days;
        total.carted_off_tonnes += y.carted_off_tonnes;
        total.carter_days_before += y.carter_days_before;
        total.sawmill_days += y.sawmill_days;
        total.sawmill_demand_days += y.sawmill_demand_days;
        total.sawmill_dead_days += y.sawmill_dead_days;
        total.sawmill_unbuilt_days += y.sawmill_unbuilt_days;
        total.sawmill_paused_days += y.sawmill_paused_days;
        total.sawyer_days += y.sawyer_days;
        total.boards_to_sites += y.boards_to_sites;
        total.boards_to_upgrades += y.boards_to_upgrades;
        total.boards_in_stores += y.boards_in_stores;
      }
    }
    const double nine = static_cast<double>(walks.size());
    std::cout << "population_curve: logs over 33 years, tonnes, means of " << walks.size() << " ("
              << years_seen << " village-years closed) — felled (a floor) " << total.felled / nine
              << ", to houses " << total.to_houses / nine << ", to social objects "
              << total.to_social / nine << ", to upgrades " << total.to_upgrades / nine
              << ", to the farm's other sites " << total.to_other / nine << ", spoiled "
              << total.spoiled / nine
              << ", sawn NOT MEASURED; at the year's turn, mean over the village-years: "
              << "lying on the stands "
              << (years_seen > 0 ? total.lying_on_stands / static_cast<double>(years_seen) : 0.0)
              << ", in the stores "
              << (years_seen > 0 ? total.in_stores / static_cast<double>(years_seen) : 0.0) << '\n';
    // WHY THE LOGS LAY (boss seq 27, step 2): stand-days with a load, by the
    // first reason — the carting demand is sized to what the stores can take
    // in, so "no demand" reads "no room to receive", not "nobody asked".
    // THE BOARDS (boss, boss-core-epoch1-3 seq 3): means of nine over the
    // 33 years; a sawmill-day is one it stood at level and unpaused.
    std::cout << "population_curve: boards, means of " << walks.size() << " — sawmill-days "
              << static_cast<double>(total.sawmill_days) / nine << " of "
              << 33U * core::kDaysPerYear << ", with sawing demand "
              << static_cast<double>(total.sawmill_demand_days) / nine << ", sawyer-days "
              << total.sawyer_days / nine << ", boards onto sites " << total.boards_to_sites / nine
              << " t (of them upgrades " << total.boards_to_upgrades / nine
              << " t), in the stores at the turn, mean over village-years "
              << (years_seen > 0 ? total.boards_in_stores / static_cast<double>(years_seen) : 0.0)
              << " t; boards made NOT MEASURED (the sawmill books no column)\n";
    std::cout << "population_curve: sawmill row-days that could not work, means of " << walks.size()
              << " — dead " << static_cast<double>(total.sawmill_dead_days) / nine
              << ", a site at level 0 " << static_cast<double>(total.sawmill_unbuilt_days) / nine
              << ", paused " << static_cast<double>(total.sawmill_paused_days) / nine << '\n';
    std::cout << "population_curve: forest standing on the stands at the turns of years 1, 5, 10, "
                 "20, 33, m3 (marked), by village:";
    for (const Trajectory& walk : walks) {
      std::cout << " |";
      for (const std::size_t year : {1U, 5U, 10U, 20U, 33U}) {
        if (year <= walk.timber.size()) {
          const run::TimberFlowTally::Year& y = walk.timber[year - 1];
          std::cout << ' ' << static_cast<long>(y.standing_m3) << '('
                    << static_cast<long>(y.marked_m3) << ')';
        }
      }
    }
    std::cout << '\n';
    std::cout << "population_curve: the first sawmill standing, campaign year per village:";
    for (const Trajectory& walk : walks) {
      if (walk.first_sawmill_day < 0) {
        std::cout << " never";
      } else {
        std::cout << ' ' << (walk.first_sawmill_day / core::kDaysPerYear) + 1;
      }
    }
    std::cout << '\n';
    std::cout << "population_curve: stand-days with logs lying, sum of " << walks.size()
              << " — no carting demand (the stores could take none) " << total.lying_no_demand
              << ", demand and no carter " << total.lying_no_carter << ", demand and carters "
              << total.lying_carted << '\n';
    const double carted_days = static_cast<double>(total.lying_carted);
    std::cout << "population_curve: on the stand-days with carters — carters a stand "
              << (carted_days > 0.0 ? total.carters_on_carted_days / carted_days : 0.0)
              << ", carting demand "
              << (total.load_tonnes_on_carted_days > 0.0
                      ? total.demand_days_on_carted_days / total.load_tonnes_on_carted_days
                      : 0.0)
              << " man-days a tonne of logs lying, load a stand "
              << (carted_days > 0.0 ? total.load_tonnes_on_carted_days / carted_days : 0.0)
              << " t (a demand is written for what the stores can take, so man-days a tonne "
                 "is the road's price only while the stores take it all)\n";
    std::cout << "population_curve: what the carting moved — " << total.carted_off_tonnes
              << " t off the stands over " << total.carter_days_before << " carter-days, "
              << (total.carter_days_before > 0.0
                      ? total.carted_off_tonnes / total.carter_days_before
                      : 0.0)
              << " t a carter-day (a stand's load falls by the carting only; felling the same "
                 "day hides a fall, so this is a floor)\n";
  }
  // THE DENOMINATOR BESIDE THE ANSWER. "Nought years open" says nothing about
  // whether the shortfall is one stubborn type or the whole farm, and the two
  // have different repairs.
  float standing = 0.0F;
  float at_level = 0.0F;
  for (const Trajectory& walk : walks) {
    standing += static_cast<float>(walk.units_standing);
    at_level += static_cast<float>(walk.units_at_level);
  }
  // THE ONLY NUMBER THE DOOR TURNS ON. Six blockers open 33, 33, 32, 8, 1 and
  // 0 years apart say nothing about whether any single YEAR had all six: the
  // office's one good year need not be a year the social objects stood. And
  // the true narrow place is the blocker most often the SOLE one shut, which
  // is not the one with the fewest open years.
  float all_six = 0.0F;
  // And IN HOW MANY VILLAGES, because a mean of 0.9 years is one village with
  // eight or eight villages with one, and the door is a village's door.
  std::uint32_t villages_opened = 0;
  std::array<float, 6> sole = {};
  for (const Trajectory& walk : walks) {
    all_six += static_cast<float>(walk.all_six_years);
    villages_opened += walk.all_six_years > 0 ? 1U : 0U;
    for (std::size_t index = 0; index < sole.size(); ++index) {
      sole[index] += static_cast<float>(walk.sole_holdout[index]);
    }
  }
  // THE TRANSITION, village by village: the year, or "never", for each of
  // the nine, and the count beside it — a list of years alone would hide the
  // villages that are not in it.
  std::uint32_t transitioned = 0;
  std::cout << "population_curve: Epoch II by the chairman's order, year per village:";
  for (const Trajectory& walk : walks) {
    // The year the door was first seen open rides beside it in brackets,
    // so a door that opened with nobody going through it cannot hide.
    const auto opened = [&walk] {
      if (walk.door_first_open_year == 0) {
        return std::string();
      }
      const char* after = walk.went_after_the_end == 1   ? ", went one step after the end"
                          : walk.went_after_the_end == 2 ? ", DID NOT GO one step after the end"
                                                         : "";
      return " (open at " + std::to_string(walk.door_first_open_year) + after + ")";
    };
    if (walk.transition_year == 0) {
      std::cout << " never" << opened();
      continue;
    }
    ++transitioned;
    std::cout << ' ' << walk.transition_year << " [calendar " << walk.transition_calendar_year
              << ']' << opened();
  }
  std::cout << " — " << transitioned << " villages of " << walks.size() << '\n';
  // WHAT HELD THE DOOR, year by year, in the core's own words: the refusal
  // the order would have met at each yearly sample, mean years of 33 per
  // village. Every slot printed, noughts included.
  std::array<float, kDoorAnswerSlots> answers = {};
  for (const Trajectory& walk : walks) {
    for (std::size_t slot = 0; slot < answers.size(); ++slot) {
      answers[slot] += static_cast<float>(walk.door_answers[slot]);
    }
  }
  std::cout << "population_curve: the transition order's answer at the yearly sample, mean years "
               "of "
            << kYears << " per village:\n";
  for (std::size_t slot = 0; slot < answers.size(); ++slot) {
    std::cout << "  " << kDoorAnswerNames[slot] << "  "
              << answers[slot] / static_cast<float>(walks.size()) << '\n';
  }
  std::cout << "population_curve: ALL SIX blocks open together — " << (all_six / villages)
            << " years of " << kYears << ", in " << villages_opened << " villages of "
            << walks.size() << "; years with exactly one shut, by which:\n";
  for (std::size_t index = 0; index < kBlockNames.size(); ++index) {
    std::cout << "  " << kBlockNames[index] << "  " << (sole[index] / villages) << '\n';
  }
  // THE SHAPE OF THE SHORTFALL, because "all six" is nought and the sole
  // holdout is nought too: what decides whether any single repair can help is
  // how many are shut at once, and which travel together.
  std::array<float, 7> shut_shape = {};
  std::array<float, 36> pairs = {};
  for (const Trajectory& walk : walks) {
    for (std::size_t index = 0; index < shut_shape.size(); ++index) {
      shut_shape[index] += static_cast<float>(walk.shut_counts[index]);
    }
    for (std::size_t index = 0; index < pairs.size(); ++index) {
      pairs[index] += static_cast<float>(walk.pair_years[index]);
    }
  }
  std::cout << "population_curve: years by HOW MANY blocks were shut (mean of nine):\n";
  for (std::size_t index = 0; index < shut_shape.size(); ++index) {
    std::cout << "  " << index << " shut: " << (shut_shape[index] / villages) << '\n';
  }
  std::cout << "population_curve: the pairs shut together most often:\n";
  for (std::size_t first = 0; first < kBlockNames.size(); ++first) {
    for (std::size_t second = first + 1; second < kBlockNames.size(); ++second) {
      const float years = pairs[(first * kBlockNames.size()) + second] / villages;
      if (years >= 1.0F) {
        std::cout << "  " << kBlockNames[first] << " + " << kBlockNames[second] << "  " << years
                  << '\n';
      }
    }
  }

  // AND THE OFFICE'S 1.4 IS THE FIXTURE, NOT THE WORLD (boss, blockers round
  // 2). Units rules §11 checks the office's wear "когда вопрос выносят на
  // собрание" — a moment the PLAYER picks, and a living chairman repairs the
  // office before putting the question. This run never puts the question and
  // repairs by its general rule, so the number measures the fixture's habit.
  std::cout << "population_curve: the office block measures the FIXTURE — the run never puts the "
               "question to a meeting, and a living chairman repairs before he does\n";

  float ordered = 0.0F;
  float at_required = 0.0F;
  for (const Trajectory& walk : walks) {
    ordered += static_cast<float>(walk.upgrades_ordered);
    at_required += static_cast<float>(walk.units_at_required);
  }
  std::cout << "population_curve: units at year " << kYears << " — " << (at_level / villages)
            << " of " << (standing / villages) << " kolkhoz buildings have reached level 2, "
            << (at_required / villages) << " stand at the level their era REQUIRES, after "
            << (ordered / villages) << " upgrades ORDERED (means of nine)\n";
  // READ AT THE UNIT, and the four verdicts are printed beside the orders
  // they must sum to, so a reader that saw nothing cannot pass for a world
  // that refused nothing.
  run::UpgradePolicy::Fates sum;
  for (const Trajectory& walk : walks) {
    const run::UpgradePolicy::Fates& fate = walk.upgrade_fates;
    sum.onto_open_site += fate.onto_open_site;
    sum.started += fate.started;
    sum.refused += fate.refused;
    sum.refused_later_era += fate.refused_later_era;
    sum.other += fate.other;
    sum.finished += fate.finished;
    sum.abandoned += fate.abandoned;
    sum.gone += fate.gone;
    sum.days_to_finish += fate.days_to_finish;
    sum.open_delivering += fate.open_delivering;
    sum.open_building += fate.open_building;
    sum.open_other += fate.open_other;
  }
  const auto mean = [villages](std::uint64_t total) {
    return static_cast<float>(total) / villages;
  };
  const std::uint32_t read =
      sum.onto_open_site + sum.started + sum.refused + sum.refused_later_era + sum.other;
  std::cout << "  verdicts at the unit: onto an open site " << mean(sum.onto_open_site)
            << ", started " << mean(sum.started) << ", refused with the rung in THIS era "
            << mean(sum.refused) << ", refused with the rung in a LATER era "
            << mean(sum.refused_later_era) << ", other " << mean(sum.other) << " — sum "
            << mean(read) << " of " << (ordered / villages) << " ordered\n";
  // AND WHY, by the core's own refusal code (order_state.h, OrderRefusal —
  // the number is the enumerator's position). Printed whole, zeros named by
  // their count, so an empty line reads as "no refusal event seen".
  std::array<std::uint64_t, static_cast<std::size_t>(core::OrderRefusal::kOrderRefusalCount)> why =
      {};
  for (const Trajectory& walk : walks) {
    for (std::size_t code = 0; code < why.size(); ++code) {
      why[code] += walk.upgrade_refusals[code];
    }
  }
  std::uint64_t events = 0;
  std::cout << "  refusal events at the upgraded unit, by OrderRefusal code (sum of nine):";
  for (std::size_t code = 0; code < why.size(); ++code) {
    if (why[code] != 0) {
      std::cout << " code " << code << " x" << why[code];
      events += why[code];
    }
  }
  std::cout << " — " << events << " events against " << sum.refused + sum.refused_later_era
            << " refusals read at the unit\n";
  // AND WHICH MATERIAL the kMaterialsShort named (the event's resource).
  std::array<std::uint64_t, 256> short_by = {};
  std::uint64_t short_seen = 0;
  for (const Trajectory& walk : walks) {
    for (std::size_t index = 0; index < short_by.size(); ++index) {
      short_by[index] += walk.upgrade_short_by_resource[index];
      short_seen += walk.upgrade_short_by_resource[index];
    }
  }
  std::cout << "  materials short on the refused upgrades (sum of nine):";
  for (std::size_t index = 0; index < short_by.size(); ++index) {
    if (short_by[index] == 0) {
      continue;
    }
    const std::vector<std::string>& keys = walks.front().resource_keys;
    std::cout << ' ' << (index < keys.size() ? keys[index] : std::to_string(index)) << " x"
              << short_by[index];
  }
  std::cout << " — " << short_seen << " named of "
            << why[static_cast<std::size_t>(core::OrderRefusal::kMaterialsShort)]
            << " kMaterialsShort\n";
  std::uint64_t this_era = 0;
  std::uint64_t later_era = 0;
  std::uint64_t no_rung = 0;
  for (const Trajectory& walk : walks) {
    this_era += walk.rung2_this_era;
    later_era += walk.rung2_later_era;
    no_rung += walk.rung2_none;
  }
  std::cout << "  of the " << (standing / villages)
            << " standing kolkhoz buildings, level 2 opens in Epoch I for " << mean(this_era)
            << ", in a LATER era for " << mean(later_era) << ", and does not exist for "
            << mean(no_rung) << '\n';
  std::cout << "  the started, followed: finished " << mean(sum.finished) << ", abandoned "
            << mean(sum.abandoned) << ", gone " << mean(sum.gone) << ", still open "
            << mean(sum.open_delivering) << " delivering / " << mean(sum.open_building)
            << " building / " << mean(sum.open_other) << " other; days to finish, mean "
            << (sum.finished == 0
                    ? 0.0F
                    : static_cast<float>(sum.days_to_finish) / static_cast<float>(sum.finished))
            << '\n';

  failures += run::Expect(first_days.size() == kSeeds.size(),
                          "every one of the nine villages reached the filth threshold at all");
  failures += run::Expect(
      !first_days.empty() && *std::ranges::min_element(first_days) > core::kDaysPerYear,
      "and not one of them did so in the FIRST year — the design wants the bathhouse buildable "
      "before the filth costs anything (boss, parcel 130)");

  if (failures == 0) {
    std::cout << "population_curve: all checks passed\n";
  }
  return failures;
}
