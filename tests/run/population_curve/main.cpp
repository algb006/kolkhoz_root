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
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <streambuf>
#include <string>
#include <vector>

#include "../common/births_herd_tally.h"
#include "../common/building_chairman.h"
#include "../common/run_harness.h"
#include "../common/timber_flow_tally.h"
#include "../common/transition_tally.h"
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
  std::uint32_t year10 = 0;
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
  /// THE DAY, NOT THE YEAR (boss-core-epoch1-queue [15]): the transition is
  /// ordered the first DAY the door opens, and the yearly sample above could
  /// not say what held it — "all six open together: 0 years in 0 villages"
  /// on villages that went. Per condition in TransitionPolicy's order (the
  /// indices, then the six blocks): the first Epoch I day it stood met (-1
  /// never), and the Epoch I days it ALONE held the door (SoleHoldout).
  std::array<std::int64_t, run::TransitionPolicy::kConditions> condition_first_met_day = {
      -1, -1, -1, -1, -1, -1, -1, -1};
  std::array<std::uint32_t, run::TransitionPolicy::kConditions> sole_holdout_days = {};
  /// Days walked in Epoch I: what the two above are counted against.
  std::uint32_t epoch_one_days = 0;
  /// WHAT SHUTS THE TWO STANDING BLOCKS (boss-core-epoch1-queue [18]): from
  /// the first day each was met, the Epoch I days it stood shut, and on those
  /// days, by unit type row, the kolkhoz types below the era's level and the
  /// social types not standing. Seed 1931 met every condition by year 6 and
  /// never all on one day; these say which building shut which.
  std::uint32_t units_shut_days = 0;
  std::map<std::uint16_t, std::uint32_t> units_below_days;
  std::uint32_t social_shut_days = 0;
  std::map<std::uint16_t, std::uint32_t> social_missing_days;
  std::vector<std::string> unit_type_keys;
  /// Why no upgrade was ordered on the days a kolkhoz unit stood below the
  /// era's level (upgrade_policy.h, Held).
  run::UpgradePolicy::Held upgrade_held;
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
  /// Marks made while a house site stood, which the old veto held.
  std::uint32_t social_marked_past_a_house_site = 0;
  std::vector<run::SocialObjectsPolicy::Fate> social_fates;
  std::vector<std::string> social_keys;
  /// Where the logs went, year by year (timber_flow_tally.h).
  std::vector<run::TimberFlowTally::Year> timber;

  /// The run's planting at the turn of year 20 and at the end
  /// (planting_policy.h).
  run::PlantingPolicy::Summary planting20;
  run::PlantingPolicy::Summary planting_end;
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
  /// The day Epoch II opened (-1 never) and the residents on it
  /// (transition_tally.h).
  std::int64_t epoch2_day = -1;
  std::uint32_t epoch2_population = 0;
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
                Trajectory& out,
                run::BirthsHerdTally* births_herd = nullptr) {
  bool hot_at_any_tick = false;
  const core::UnitId awaiting = upgrades.Awaiting();
  for (std::uint32_t tick = 0; tick < core::kTicksPerDay; ++tick) {
    simulation.AdvanceStep();
    const core::WorldState& state = simulation.CompletedState();
    // econ's work sample (births_herd_tally.h), once a day at noon.
    if (births_herd != nullptr && tick == run::BirthsHerdTally::kSampleHour) {
      births_herd->SampleWork(state);
    }
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
/// `--no-planting`: the control arm of the planting measurement (0.34.41) —
/// the building chairman without the one policy under test.
bool g_no_planting = false;

/// `--epoch-one-forever`: econ's branch (boss-core-epoch1-queue [34]) — the
/// run's chairman never orders the transition, and the transition print runs
/// all thirty-three years: population, traction, the indices with their
/// components and the gates at every turn. The world's rules are untouched;
/// the era stays because nobody asks. Its verdicts are printed, not trusted:
/// the curve's bands were read on villages that go on.
bool g_epoch_one_forever = false;

/// `--horses-half`: the parameter with a KNOWN DIRECTION for the traction
/// share (boss-core-epoch1-queue [42]): half the team on day 0 must move
/// the mechanisation component down, or the instrument is dead. Of each age
/// band the village keeps half its horses, rounded down, counted over
/// ALL horse herds: the start stables them one head a row, and halving
/// each row took all sixteen (the first probe, 16 -> 0).
bool g_horses_half = false;

/// `--horses-loss-year=N`: econ's pair for own traction's 70 % (boss-core-
/// epoch1-queue [57]; econ, proposals/own-traction.md §4): at the start of
/// year N the village keeps a quarter of its horses, each age band, as a
/// murrain would leave it. Lost before the door (year 10) the gate must shut
/// for 2-4 years and open by itself as the team grows back; lost long before
/// (year 3) nothing moves. 0: no loss.
std::uint32_t g_horses_loss_year = 0;

/// Keeps `keep_num`/`keep_den` of the world's horses, rounded down, each age
/// band counted over all horse herds and taken from the last herd row up,
/// and prints the adults before and after with `when`.
void KeepHorsesShare(const run::Simulation& world,
                     std::uint32_t keep_num,
                     std::uint32_t keep_den,
                     const char* flag,
                     const char* when) {
  core::WorldState halved = world.State();
  const core::ITable* const kinds = world.tables->FindTable("livestock");
  const std::uint32_t horse = kinds == nullptr ? core::kNoTableRow : kinds->FindRowByKey("horse");
  std::uint32_t adults = 0;
  std::uint32_t juveniles = 0;
  std::uint32_t newborns = 0;
  for (const core::HerdRow& herd : halved.herds.rows) {
    if (horse != core::kNoTableRow && herd.kind.value == horse) {
      adults += herd.adult_count;
      juveniles += herd.juvenile_count;
      newborns += herd.newborn_count;
    }
  }
  std::uint32_t adults_to_take = adults - (adults * keep_num / keep_den);
  std::uint32_t juveniles_to_take = juveniles - (juveniles * keep_num / keep_den);
  std::uint32_t newborns_to_take = newborns - (newborns * keep_num / keep_den);
  const auto take = [](std::uint16_t& count, std::uint32_t& wanted) {
    const std::uint32_t taken = std::min<std::uint32_t>(count, wanted);
    count = static_cast<std::uint16_t>(count - taken);
    wanted -= taken;
    return taken;
  };
  for (auto row = halved.herds.rows.size(); row > 0; --row) {
    core::HerdRow& herd = halved.herds.rows[row - 1];
    if (horse == core::kNoTableRow || herd.kind.value != horse) {
      continue;
    }
    const std::uint16_t adults_before = herd.adult_count;
    take(herd.adult_count, adults_to_take);
    take(herd.juvenile_count, juveniles_to_take);
    take(herd.newborn_count, newborns_to_take);
    herd.adult_male_count = std::min(herd.adult_male_count, herd.adult_count);
    // The mean age of the adults is kept: the total follows the head.
    herd.adult_age_game_years_total = adults_before == 0
                                          ? 0.0F
                                          : herd.adult_age_game_years_total *
                                                static_cast<float>(herd.adult_count) /
                                                static_cast<float>(adults_before);
  }
  std::uint32_t after = 0;
  for (const core::HerdRow& herd : halved.herds.rows) {
    after += horse != core::kNoTableRow && herd.kind.value == horse ? herd.adult_count : 0U;
  }
  const std::uint32_t before = adults;
  world.simulation->ResetWorld(halved);
  std::cout << "population_curve: " << flag << " — adult horses " << before << " -> " << after
            << ' ' << when << '\n';
}

/// `--seed-offset=N`: every seed of kSeeds moved by N — a SECOND sample of
/// nine villages for a comparison, not the canonical curve (its bands are
/// read on kSeeds; a verdict on another sample is printed, not trusted).
std::uint64_t g_seed_offset = 0;

/// One Epoch I day of the two STANDING blocks, from the first day each was
/// met: shut, and by what (Trajectory::units_below_days, social_missing_days).
/// The lists are the core's door's own (TransitionPolicy::Catalog), the level
/// its RequiredUnitLevel.
void CountStandingShut(const run::TransitionPolicy& transition,
                       const core::WorldState& today,
                       const std::array<bool, run::TransitionPolicy::kConditions>& met,
                       Trajectory& out) {
  constexpr std::size_t kSocial = 5;
  constexpr std::size_t kUnits = 6;
  const core::ReadinessCatalog& catalog = transition.Catalog();
  if (out.condition_first_met_day[kUnits] >= 0 && !met[kUnits]) {
    ++out.units_shut_days;
    std::vector<std::uint16_t> below;
    for (const core::UnitRow& unit : today.units.rows) {
      const bool kolkhoz = std::ranges::any_of(
          catalog.kolkhoz_types,
          [&unit](core::UnitTypeId type) { return type.value == unit.type.value; });
      if (unit.level == 0 || unit.dead != 0 || !kolkhoz ||
          unit.level >= core::RequiredUnitLevel(catalog, unit.type, today.epoch, today)) {
        continue;
      }
      if (std::ranges::find(below, unit.type.value) == below.end()) {
        below.push_back(unit.type.value);
      }
    }
    for (const std::uint16_t type : below) {
      ++out.units_below_days[type];
    }
  }
  if (out.condition_first_met_day[kSocial] >= 0 && !met[kSocial]) {
    ++out.social_shut_days;
    for (const core::UnitTypeId type : catalog.social_objects) {
      const bool stands = std::ranges::any_of(today.units.rows, [type](const core::UnitRow& unit) {
        return unit.type.value == type.value && unit.level >= 1 && unit.dead == 0;
      });
      if (!stands) {
        ++out.social_missing_days[type.value];
      }
    }
  }
}

bool Walk(std::uint64_t seed, bool print_years, Trajectory& out) {
  const run::Simulation world = run::Start(seed);
  if (!world) {
    return false;
  }
  if (g_horses_half) {
    KeepHorsesShare(world, 1, 2, "--horses-half", "on day 0");
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
  if (g_no_planting) {
    builder.planting.Disable();
  }
  if (g_epoch_one_forever) {
    builder.transition.Disable();
  }
  run::TimberFlowTally timber(*world.tables);
  // econ's births and horses prints: the "forever" branch only.
  run::BirthsHerdTally births_herd(*world.tables);
  run::BirthsHerdTally* const births_herd_on = g_epoch_one_forever ? &births_herd : nullptr;
  run::TransitionTally transition_tally(
      *world.tables, g_epoch_one_forever ? kYears : run::TransitionTally::kYearsDecomposedDefault);
  if (const core::ITable* const types = world.tables->FindTable("unit_types")) {
    const std::uint32_t key_column = types->FindColumn("key");
    for (std::uint32_t row = 0; row < types->RowCount(); ++row) {
      out.unit_type_keys.emplace_back(key_column == core::kNoTableColumn
                                          ? std::to_string(row)
                                          : std::string(types->CellText(row, key_column)));
    }
  }
  if (const core::ITable* const resources = world.tables->FindTable("resources")) {
    const std::uint32_t key_column = resources->FindColumn("key");
    for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
      out.resource_keys.emplace_back(key_column == core::kNoTableColumn
                                         ? std::to_string(row)
                                         : std::string(resources->CellText(row, key_column)));
    }
  }

  for (std::uint32_t year = 1; year <= kYears; ++year) {
    if (g_horses_loss_year != 0 && year == g_horses_loss_year) {
      KeepHorsesShare(world, 1, 4, "--horses-loss-year", "at the start of the year");
    }
    for (std::uint32_t day = 0; day < core::kDaysPerYear; ++day) {
      // THE DAY IS WALKED TICK BY TICK AND NOT BY AdvanceDays, for one
      // reason: the outbox is cleared every step, so a loop that advances a
      // whole day and reads the events afterwards sees the last tick and
      // calls the rest silence (event_journal's own header says the same).
      // `AdvanceDays(1)` IS this loop — the order of `builder.RunDay` after
      // the day is untouched, and the bands below are the proof.
      LiveOneDay(*simulation, builder.upgrades, out, births_herd_on);
      builder.RunDay(*simulation);
      timber.CountDay(simulation->CompletedState());
      // What held the transition TODAY, by the order's own door.
      const core::WorldState& today = simulation->CompletedState();
      if (today.epoch == core::Epoch::kOne) {
        ++out.epoch_one_days;
        const auto met = builder.transition.ConditionsMet(today);
        for (std::size_t condition = 0; condition < met.size(); ++condition) {
          if (met[condition] && out.condition_first_met_day[condition] < 0) {
            out.condition_first_met_day[condition] = static_cast<std::int64_t>(today.calendar.day);
          }
        }
        const int sole = builder.transition.SoleHoldout(today);
        if (sole >= 0) {
          ++out.sole_holdout_days[static_cast<std::size_t>(sole)];
        }
        CountStandingShut(builder.transition, today, met, out);
      }
      transition_tally.CountDay(builder.transition, today);
      if (births_herd_on != nullptr) {
        // The team's two alarms, through the simulation's own door (0.37.8).
        std::vector<core::Alarm> alarms;
        simulation->CollectAlarms(alarms);
        births_herd_on->CountAlarms(alarms);
        births_herd_on->CountDay(today);
      }
    }
    const core::WorldState& state = simulation->CompletedState();
    const auto population = static_cast<std::uint32_t>(state.residents.rows.size());
    if (year == 7) {
      out.year7 = population;
    }
    if (year == 10) {
      out.year10 = population;
    }
    if (year == 14) {
      out.year14 = population;
    }
    // THE PLANTING AT THE TURN OF YEAR 20 (boss, boss-core-epoch1-3 seq 18):
    // the groves are felled to nothing by about then, and this is the year
    // the planted zones have to be carrying the timber.
    if (year == 20) {
      out.planting20 = builder.planting.Summarize(state);
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
        unit.level >= core::RequiredUnitLevel(catalog, unit.type, final_state.epoch, final_state)
            ? 1U
            : 0U;
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
  out.upgrade_held = builder.upgrades.held();
  out.epoch2_day = transition_tally.OpenedDay();
  out.epoch2_population = transition_tally.OpenedPopulation();
  transition_tally.Print("population_curve", seed);
  if (births_herd_on != nullptr) {
    births_herd_on->Print("population_curve", seed);
  }
  out.social_marked_past_a_house_site = builder.social.marked_past_a_house_site();
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
  out.planting_end = builder.planting.Summarize(final_state);
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

/// @brief A page of text a walk, so nine villages walking at once print as
/// nine walking in turn (0.37.208). While open, everything written to
/// std::cout and std::cerr — by this run, its policies and the core's log —
/// goes to the page of the walk whose thread writes it; a thread that named
/// no walk writes to the page past the last, which nobody prints.
///
/// BOTH STREAMS INTO ONE PAGE, in the order they were written: left to the
/// process, the two reach a file by two bufferings, and their order in it is
/// the buffers' and not the run's.
class WalkPages final : public std::streambuf {
 public:
  explicit WalkPages(std::size_t walks) : pages_(walks + 1) {}

  /// @brief Names the walk the calling thread writes for, from here on.
  static void WriteTo(std::size_t walk) { t_walk = walk; }

  /// @brief Takes std::cout and std::cerr; Close gives them back.
  void Open() {
    cout_ = std::cout.rdbuf(this);
    cerr_ = std::cerr.rdbuf(this);
  }

  void Close() {
    std::cout.rdbuf(cout_);
    std::cerr.rdbuf(cerr_);
  }

  const std::string& Page(std::size_t walk) const { return pages_[walk]; }

 protected:
  int_type overflow(int_type character) override {
    if (!traits_type::eq_int_type(character, traits_type::eof())) {
      Own().push_back(traits_type::to_char_type(character));
    }
    return traits_type::not_eof(character);
  }

  std::streamsize xsputn(const char* text, std::streamsize count) override {
    Own().append(text, static_cast<std::size_t>(count));
    return count;
  }

 private:
  /// The calling thread's page: each walk has its own, so no two threads
  /// write one string.
  std::string& Own() { return pages_[t_walk < pages_.size() ? t_walk : pages_.size() - 1]; }

  static thread_local std::size_t t_walk;

  std::vector<std::string> pages_;
  std::streambuf* cout_ = nullptr;
  std::streambuf* cerr_ = nullptr;
};

thread_local std::size_t WalkPages::t_walk = static_cast<std::size_t>(-1);

}  // namespace

int main(int argc, char** argv) {
  int failures = 0;
  run::BuildingChairman::Declare("population_curve");
  std::uint64_t probe_seed = 0;
  bool sequential = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--sequential") {
      sequential = true;
    } else if (argument == "--no-planting") {
      g_no_planting = true;
    } else if (argument == "--epoch-one-forever") {
      g_epoch_one_forever = true;
    } else if (argument == "--horses-half") {
      g_horses_half = true;
    } else if (argument.starts_with("--horses-loss-year=")) {
      g_horses_loss_year = static_cast<std::uint32_t>(std::strtoul(
          std::string(argument.substr(std::string_view("--horses-loss-year=").size())).c_str(),
          nullptr,
          10));
    } else if (argument.starts_with("--seed-offset=")) {
      g_seed_offset = std::strtoull(
          std::string(argument.substr(std::string_view("--seed-offset=").size())).c_str(),
          nullptr,
          10);
    } else {
      probe_seed = std::strtoull(argv[index], nullptr, 10);
    }
  }
  if (g_no_planting) {
    std::cout << "population_curve: CONTROL ARM — --no-planting: the building chairman "
                 "plants nothing (the declaration above does not hold)\n";
  }
  if (g_epoch_one_forever) {
    std::cout << "population_curve: ECON'S BRANCH — --epoch-one-forever: the run's chairman "
                 "never orders Epoch II (the declaration above does not hold for the "
                 "transition); every verdict below is printed, not trusted\n";
  }
  if (g_horses_loss_year != 0) {
    std::cout << "population_curve: CONTROL ARM — --horses-loss-year=" << g_horses_loss_year
              << ": a quarter of the horses kept, each age band, at the start of that year "
                 "(econ's pair for own traction's 70 %); the bands below are read on the "
                 "whole team, so their verdict is printed, not trusted\n";
  }
  if (g_horses_half) {
    std::cout << "population_curve: CONTROL ARM — --horses-half: every horse herd halved on day "
                 "0, adults and young alike; the bands below are read on the whole team, so "
                 "their verdict is printed, not trusted\n";
  }
  if (g_seed_offset != 0) {
    std::cout << "population_curve: NOT THE CANONICAL SAMPLE — --seed-offset=" << g_seed_offset
              << ": seeds " << kSeeds.front() + g_seed_offset << ".."
              << kSeeds.back() + g_seed_offset
              << "; the bands are read on the canonical nine, so their verdict here is printed, "
                 "not trusted\n";
  }

  // ONE SEED ON THE COMMAND LINE IS A PROBE, NOT A VERDICT: it prints the
  // year-by-year flows of a village nobody has looked at and judges nothing.
  // A band read on the median of nine cannot be applied to a sample of one,
  // and a run that pretended otherwise would hand back a red on a question it
  // never asked.
  if (probe_seed != 0) {
    Trajectory one;
    if (!Walk(probe_seed, true, one)) {
      return 1;
    }
    std::cout << "population_curve: PROBE seed " << one.seed << " — year 7 = " << one.year7
              << ", year 14 = " << one.year14 << ", year 33 = " << one.year33 << ", male share "
              << one.male_share << ", first filth disease day " << one.first_disease_day
              << " (printed, not judged)\n";
    return 0;
  }

  // THE NINE VILLAGES WALK AT ONCE (0.37.208): each is a simulation of its own
  // and shares nothing with the others, and one after another they were 84
  // minutes of a suite whose every other test fits in ten — the test that
  // bounded the suite whatever the machine gave it.
  //
  // EACH WALK WRITES INTO ITS OWN PAGE (WalkPages, above) — its policies'
  // lines, the core's log, the first village's years — and the pages are
  // printed here in the seeds' order, each before its village's line. So
  // the text is the same whichever way they walked: `--sequential` walks
  // them one after another through the same pages, and the job that
  // compares the two texts byte for byte is the proof of «shares nothing».
  std::vector<Trajectory> walked(kSeeds.size());
  std::vector<char> walked_well(kSeeds.size(), 0);
  WalkPages pages(kSeeds.size());
  const auto walk_one = [&walked, &walked_well](std::size_t index) {
    WalkPages::WriteTo(index);
    walked_well[index] = Walk(kSeeds[index] + g_seed_offset, index == 0, walked[index]) ? 1 : 0;
  };
  std::cout.flush();
  std::cerr.flush();
  pages.Open();
  if (sequential) {
    for (std::size_t index = 0; index < kSeeds.size(); ++index) {
      walk_one(index);
    }
  } else {
    std::vector<std::future<void>> pending;
    for (std::size_t index = 0; index < kSeeds.size(); ++index) {
      pending.push_back(std::async(std::launch::async, walk_one, index));
    }
    for (std::future<void>& one : pending) {
      one.get();
    }
  }
  pages.Close();
  // What a thread that named no walk wrote (a worker of a simulation's own):
  // said, not dropped — its order among the walks is not known.
  if (!pages.Page(kSeeds.size()).empty()) {
    std::cout << "population_curve: WRITTEN BY NO WALK'S OWN THREAD, in the order it came:\n"
              << pages.Page(kSeeds.size());
  }
  std::vector<Trajectory> walks;
  for (std::size_t index = 0; index < kSeeds.size(); ++index) {
    std::cout << pages.Page(index);
    if (walked_well[index] == 0) {
      return 1;
    }
    const Trajectory& walk = walked[index];
    std::cout << "population_curve: seed " << walk.seed << " — year 7 = " << walk.year7
              << ", year 14 = " << walk.year14 << ", year 33 = " << walk.year33 << ", male share "
              << walk.male_share << ", first filth disease day " << walk.first_disease_day
              << " (hot days up to it, upper bound, " << walk.hot_days_before << ")\n";
    walks.push_back(walk);
  }

  std::vector<std::uint32_t> year7;
  std::vector<std::uint32_t> year10;
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
    year10.push_back(walk.year10);
    year14.push_back(walk.year14);
    year33.push_back(walk.year33);
    male_shares.push_back(walk.male_share);
  }

  const std::uint32_t median_year7 = Median(year7);
  const std::uint32_t median_year10 = Median(year10);
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
  // RESTORED TO AN ASSERTION on 0.34.45 (boss, boss-core-epoch1-5 seq 2): the
  // harness had printed "KNOWN GAP CLOSED" since at least 0.34.41. The claim
  // is the MEDIAN, and it holds with room: 441 on 0.34.41, 431 on 0.34.44 and
  // 430 on 0.34.45, against 380. The weakest seed does not hold it (338 on
  // 0.34.45, seed 1937), and nothing here says it should.
  // THE DESIGN'S PAIR, NOT A POPULATION OF A YEAR (boss-core-epoch1-queue
  // [28]; econ, boss-econ-pop14-threshold [2], econ/manual/audit/
  // pop14-threshold.md). «Population by year 14 >= 380» was born on 28
  // August (48ff005) a demography check with no farm behind it, and its
  // 430-450 were taken in a world where the fourth social object was never
  // marked; no line of the design asks a population of a year. What the
  // design pairs is the village the era opens with and the year it opens
  // in: the population on the day Epoch II opens is asked, the year printed.
  // SINCE THE SEVENTH BLOCK (the human's word of 2026-09-27, «По числу
  // жителей согласен»; era_readiness.h, kPopulationRequired) the population
  // on the opening day is 380 or more by construction, and asking it would be
  // a tautology: the gap that asked it is gone. What keeps its meaning is the
  // YEAR against the design's ~14 — printed beside econ's row (V1: year 13,
  // 11..13, at 387 residents, 9 villages of 9). The year-14 population line
  // was a known gap while the era opened in year 4; it is asked again below.
  std::vector<std::uint32_t> opened_populations;
  std::vector<std::uint32_t> opened_years;
  for (const Trajectory& walk : walks) {
    if (walk.epoch2_day >= 0) {
      opened_populations.push_back(walk.epoch2_population);
      opened_years.push_back(static_cast<std::uint32_t>(walk.epoch2_day / core::kDaysPerYear + 1));
    }
  }
  const std::uint32_t median_opened_population =
      opened_populations.empty() ? 0U : Median(opened_populations);
  const std::uint32_t median_opened_year = opened_years.empty() ? 0U : Median(opened_years);
  std::cout << "population_curve: Epoch II opened in " << opened_years.size() << " villages of "
            << walks.size() << ", median year " << median_opened_year
            << ", median residents on the day " << median_opened_population
            << "; population by year 14, printed only: median " << median_year14
            << "; econ's row (V1): year 13 (11..13), 387 residents, 9 of 9; the design ~14\n";
  // RESTORED on 0.37.1: with the seventh block the median transition year
  // is 13, the harness printed "KNOWN GAP CLOSED — restore the assertion"
  // (median year-14 435 against 380), and a gap left standing after it
  // closes cannot report the day it reopens.
  //
  // THE TWO GATES ON THE MEDIAN WERE TAKEN DOWN on 0.37.97 (boss, econ-boss-
  // hay-term-2026-10-01 [15]): «the median of nine is at 380 or more by year
  // 14» and «the median village opens Epoch II by year 14». THE THRESHOLD SAT
  // INSIDE THE MEDIAN'S SCATTER BETWEEN TREES. Residents at year 14, the
  // median of the same nine villages, on four trees in a row: 390 (0.37.91),
  // 415 (0.37.93), 379 (0.37.94), 386 (0.37.96) — and the year the median
  // village opens the era 14, 14, 15, 15. A delivery that touched the winter
  // hay of three villages moved them by +21, +35 and +18 residents at year
  // 14; the feeding order and the hay lamp moved single villages by 50 to 110
  // either way. Free houses run out in years 6 to 7 in every one of those
  // worlds, and in that bottleneck the year the era opens is a draw between
  // 13 and 20. A gate on the median measured that draw: green on 0.37.93,
  // red on 0.37.94, half red on 0.37.96, and it named no author.
  //
  // WHAT IS ASKED INSTEAD is what the design asks and the scatter does not
  // reach: every village opens Epoch II, and opens it in years 12 to 20. THE
  // MEDIANS ARE PRINTED beside a baseline that names its tree, so that a
  // reader sees which way the world moved without a gate deciding it for him.
  //
  // THE BAND IS 11 TO 20 SINCE 0.37.135, AND IT IS THE BAND OF A NAMED WORLD
  // (boss, boss-all-carts-carry-people-go-2026-10-02 [63]; econ [62]): «мир
  // без дров и без наряда от конного двора, дерево c1d4131» (0.37.133). On
  // the cart of 1.8 t (0.37.126) with the meadow's hay carted by the
  // manger's need (0.37.132-133) the village builds and grows faster: the
  // era opens in years 11 to 14, the median 12, where the baseline of
  // 0.37.96 had 13 to 18 and 15. The design's «about 14» is NOT rewritten:
  // econ's prediction is that the firewood and the horse job starting at the
  // horse yard bring the median back to 13-14, and it is checked on those
  // deliveries, with the condition of return there (the lever is the
  // arrivals and a house's price, not the horses and not the cart's load).
  // ECON'S CONDITION OF ACCEPTANCE stands beside the band as its own three
  // assertions: the earliest village not before year 11, the median village
  // not before year 12, and the residents at the end of year 10 at 290 to
  // 360 by the median.
  std::uint32_t earliest_opened = 0;
  std::uint32_t latest_opened = 0;
  for (const std::uint32_t year : opened_years) {
    earliest_opened = earliest_opened == 0 || year < earliest_opened ? year : earliest_opened;
    latest_opened = year > latest_opened ? year : latest_opened;
  }
  std::cout << "population_curve: the year Epoch II opens, " << earliest_opened << ".."
            << latest_opened << " over " << opened_years.size() << " villages, median "
            << median_opened_year << "; residents at year 10, median " << median_year10
            << "; at year 14, median " << median_year14
            << " | BASELINE, tree 0.37.133 (c1d4131), the world with no firewood and no horse job "
               "from the horse yard: years 11..14, median 12; residents at year 14, median 446 "
               "| the baseline before the cart of 1.8 t, tree 0.37.96 (3baefce): years 13..18, "
               "median 15; residents at year 14, median 386\n";
  const std::string all_claim = "and every village opens Epoch II — now " +
                                std::to_string(opened_years.size()) + " of " +
                                std::to_string(walks.size());
  failures += run::Expect(opened_years.size() == walks.size(), all_claim.c_str());
  // OF THE THREE BANDS BELOW THE MEDIAN'S IS RED SINCE 0.37.208 (the felling
  // cure) — a balance finding at econ, the band NOT re-taken (boss, 10
  // October 2026): the stuck felling mark had been braking growth, and with
  // it gone Epoch II opens in years 11..13, median 11 (0.37.207: 11..14,
  // median 12) and the residents at year 10 are 356 by the median (341) —
  // four short of the third band's edge. They are ECON'S ACCEPTANCE BANDS OF
  // 0.37.133, a tripwire on one delivery — not the design's (the design's
  // are «500 residents — Epoch II» at year 14 and the door's «residents >=
  // 380»); re-taken by her as the band of a named world after the colts'
  // delivery and the bot's arm — not stale.
  // THE OTHER TWO ARE ONE TABLE EXPORT AWAY: on this tree with boss's three
  // exports of 9-10 October laid on it and not yet committed (the start
  // layout's fallow cell, the repair's materials, world_params) the run
  // read 10..12 and 361, and all three were red.
  const std::string year_claim = "and opens it in years 11 to 20 — now " +
                                 std::to_string(earliest_opened) + " to " +
                                 std::to_string(latest_opened) + " (0 = no village opens it)";
  failures += run::Expect(earliest_opened >= 11 && latest_opened <= 20, year_claim.c_str());
  // Red since 0.37.208 (the felling cure) — a balance finding at econ, band not re-taken.
  const std::string median_claim = "and the median village opens it not before year 12 — now " +
                                   std::to_string(median_opened_year);
  failures += run::Expect(median_opened_year >= 12, median_claim.c_str());
  const std::string year10_claim =
      "and the residents at the end of year 10 are 290 to 360 by the median — now " +
      std::to_string(median_year10);
  failures += run::Expect(median_year10 >= 290 && median_year10 <= 360, year10_claim.c_str());
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
  // RESTORED TO AN ASSERTION on 0.34.45 (boss, boss-core-epoch1-5 seq 2). Since
  // 2026-09-18 the era moves by the chairman's order and not by population, so
  // the comment above describes a rule that is gone. The claim is still about
  // the first seed, 1931, and it opens in year 5 on 0.34.41, 0.34.44 and
  // 0.34.45 alike (8 villages of 9 on 0.34.45).
  // A KNOWN GAP FROM 0.34.51 TO 0.35.0, RESTORED: the horses counted once
  // cost the first seed its era (the median village 197 / 394 / 424 in
  // years 7 / 14 / 33 against 257 / 516 / 345 on 0.34.50). The district's
  // seed loan of 0.35.0 gave it back — the gap printed CLOSED — and the
  // median stands at 202 / 406 / 441.
  // ASKED OF THE NINE, NOT OF THE FIRST SEED (0.37.33). The founders' schooling
  // by counts reshuffled which villages open the era and which do not: on
  // 0.37.32 (aedaeea) 6 of 9 opened, 1933, 1937 and 1938 never; on 0.37.33
  // 7 of 9, 1931 and 1934 never; the median year 21 on both. The claim on
  // seed 1931 alone held by the luck of which three missed. The floor is the
  // lower of the two trees' counts; two or three villages of nine never
  // opening in 33 years is the finding, sent to boss with 0.37.33.
  constexpr std::size_t kEraOpenedAtLeast = 6;
  const std::string era_claim =
      "Epoch II is reached on the way in " + std::to_string(kEraOpenedAtLeast) + " villages of " +
      std::to_string(walks.size()) + " or more (" + std::to_string(opened_years.size()) + ")";
  failures += run::Expect(opened_years.size() >= kEraOpenedAtLeast, era_claim.c_str());
  // THE CLAIM WAS RED FROM 0.37.53 TO 0.37.80, FOR TWO CAUSES IN TURN, and
  // both notes that explained it were taken out when it held again (0.37.81,
  // the suite of k03781b): from 0.37.53 the elder's swap of houses was a new
  // sample of the same villages, 5 of 9 opening (108 villages, sign test
  // p = 0.405 — noise by the rule written before the reading; the floor was
  // not lowered); from 0.37.78 the era opened in NO village — unlike yards
  // left a third of them on three counted categories and the transition's
  // food variety gate was met on no seed. 0.37.81 put a little of every
  // category that keeps on the table and the gate is met. A red here now is
  // a new finding and has no note to stand behind.
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
              << (open_years / static_cast<float>(walks.size())) << "  by village:";
    // BY VILLAGE, the mean's place (2026-09-27): seed 1931 alone never
    // reached Epoch II under the houses-first veto, and the mean could not
    // say which block held it.
    for (const Trajectory& walk : walks) {
      std::cout << ' ' << walk.block_years[index];
    }
    std::cout << '\n';
  }
  // THE DAY'S ANSWER, BY VILLAGE (boss-core-epoch1-queue [15]): the first
  // campaign year each condition stood met ("-" never), and the Epoch I days
  // it alone held the door. The office's days are the fixture's own: the run
  // repairs it only once the other six are met (office_policy.h).
  constexpr std::array<const char*, run::TransitionPolicy::kConditions> kConditionNames = {
      "индексы 3 года    ",
      "своя тяга/база    ",
      "зимовка 2 года    ",
      "правление ≤1%     ",
      "разнообразие пищи ",
      "4 соцобъекта из 6 ",
      "юниты на уровне   ",
      "жителей ≥ 380     "};
  std::cout << "population_curve: transition conditions by village, first year met / Epoch I days "
               "it ALONE held the door (villages "
            << walks.size() << "; Epoch I days by village:";
  for (const Trajectory& walk : walks) {
    std::cout << ' ' << walk.epoch_one_days;
  }
  std::cout << ")\n";
  for (std::size_t condition = 0; condition < kConditionNames.size(); ++condition) {
    std::cout << "  " << kConditionNames[condition] << ':';
    std::uint32_t villages_held = 0;
    for (const Trajectory& walk : walks) {
      const std::int64_t day = walk.condition_first_met_day[condition];
      std::cout << ' '
                << (day < 0 ? std::string("-") : std::to_string(day / core::kDaysPerYear + 1))
                << '/' << walk.sole_holdout_days[condition];
      villages_held += walk.sole_holdout_days[condition] > 0 ? 1U : 0U;
    }
    std::cout << "  — alone in " << villages_held << " villages\n";
  }
  // AND WHAT SHUT THE TWO STANDING BLOCKS, village by village (boss-core-
  // epoch1-queue [18]-[19]), from the first day each was met: the days shut,
  // and on them the types below the era's level / the social types not
  // standing, by days.
  for (const Trajectory& walk : walks) {
    const auto key_of = [&walk](std::uint16_t type) {
      return type < walk.unit_type_keys.size() ? walk.unit_type_keys[type] : std::to_string(type);
    };
    std::cout << "population_curve: seed " << walk.seed
              << " after first met — юниты на уровне shut " << walk.units_shut_days
              << " days, below level:";
    for (const auto& [type, days] : walk.units_below_days) {
      std::cout << ' ' << key_of(type) << 'x' << days;
    }
    std::cout << "; 4 соцобъекта shut " << walk.social_shut_days << " days, not standing:";
    for (const auto& [type, days] : walk.social_missing_days) {
      std::cout << ' ' << key_of(type) << 'x' << days;
    }
    std::cout << '\n';
    // AND WHY THE RAISE DID NOT COME (upgrade_policy.h, Held), every day a
    // kolkhoz unit stood below the era's level, the first reason of the day.
    const run::UpgradePolicy::Held& held = walk.upgrade_held;
    std::cout << "population_curve: seed " << walk.seed
              << " upgrade not ordered, days below level — farm first " << held.farm_first
              << ", yard not up " << held.farm_not_standing << ", another raise building "
              << held.one_going_up << ", unit a site already " << held.site_open << ", gate closed "
              << held.gate_closed << ", next rung short " << held.materials_short
              << " (first short:";
    for (const auto& [resource, days] : held.short_by_resource) {
      std::cout << ' '
                << (resource < walk.resource_keys.size() ? walk.resource_keys[resource]
                                                         : std::to_string(resource))
                << 'x' << days;
    }
    std::cout << "), UNEXPLAINED " << held.unexplained << '\n';
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
  float past_house_site = 0.0F;
  for (const Trajectory& walk : walks) {
    nothing_left += static_cast<float>(walk.social_held.nothing_left);
    going_up += static_cast<float>(walk.social_held.one_going_up);
    farm_first += static_cast<float>(walk.social_held.farm_first);
    house_waits += static_cast<float>(walk.social_held.a_house_waits);
    past_house_site += static_cast<float>(walk.social_marked_past_a_house_site);
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
            << " (a roofless family or a waiting couple; days before the farm stood are not "
               "counted); marks made past a house site ahead of need "
            << (past_house_site / villages) << "\n";
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
    // THE PLANTING, by village (planting_policy.h): zones accepted by the
    // core / planted / grown, the timber the grown ones held at maturity and
    // what stands in plantings now — at the turn of year 20 and at the end.
    // Refusals, years held back by a waiting zone and years with no place
    // left are printed beside, so a zero reads as what it is.
    for (const auto& [label, pick] : {std::pair{"year 20", &Trajectory::planting20},
                                      std::pair{"the end", &Trajectory::planting_end}}) {
      std::cout << "population_curve: planting at " << label
                << " — ordered, EVER planted/grown/m3 felled out of them; rows NOW "
                   "planted/grown, m3 standing; refused, held back, no place — by village:";
      for (const Trajectory& walk : walks) {
        const run::PlantingPolicy::Summary& s = walk.*pick;
        std::cout << " | " << s.ordered << ", " << s.planted_ever << '/' << s.grown_ever << '/'
                  << std::lround(s.felled_ever_m3) << "; " << s.planted << '/' << s.grown << ' '
                  << std::lround(s.standing_m3) << "; " << s.refused << ' ' << s.held_back << ' '
                  << s.no_place;
      }
      std::cout << '\n';
    }
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
