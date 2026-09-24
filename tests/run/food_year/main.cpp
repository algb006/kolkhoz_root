// Simulation run: the stage-6 criterion (plan §8) — "the village does not go
// hungry under sound management, and does go hungry under bad".
//
// The claim has two halves and a run that checks only the good half proves
// nothing: a model that always feeds everybody would pass it. So the same
// three years are run twice, over the same seed and the same world:
//
//   * with the shipped tables — the settlement's mean satiety holds up, no
//     household sits on the minimum ration month after month, and life
//     expectancy stays at or above its base;
//   * with the distribution norms struck out — the kolkhoz issues nothing
//     for the trudodni it owes. Satiety falls, the ration starts firing,
//     health follows it down, life expectancy drops. And NOBODY STARVES TO
//     DEATH: hunger works through health, never through a death of its own
//     (food model §2). That last one is the design's red line, and it is the
//     reason this run exists in this shape.
//
// The second half is fed a doctored table set rather than a doctored world:
// the point is that the MECHANICS bite, and the shortest honest way to make
// them bite is to take away what the chairman hands out.

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../common/building_chairman.h"
#include "../common/run_harness.h"
#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {

/// What a run had to say about the village at the end of it.
struct Outcome {
  float mean_satiety = 0.0F;
  float mean_health = 0.0F;
  float worst_year_satiety = 100.0F;
  float last_year_satiety = 0.0F;

  /// How many FINISHED years the two figures above are over — 0 means
  /// neither was measured, and the report says so instead of a number.
  std::uint32_t years_measured = 0;

  /// The leanest day the settlement saw, and the most people it ever had
  /// under the health threshold at once. THESE are what the criterion is
  /// measured on: a peasant year is not level, and its average describes
  /// neither the plenty after the harvest nor the empty spring (metrics
  /// design §8, rule of 2026-08-31). A LEVEL satiety curve is a symptom, not
  /// a success — it means food arrives faster than it is eaten and is
  /// piling up somewhere.
  float leanest_day_satiety = 100.0F;

  /// How many days actually went into `leanest_day_satiety`. IT EXISTS
  /// BECAUSE A FLOOR CANNOT TELL A GOOD YEAR FROM AN UNMEASURED ONE: the
  /// figure above starts at 100 and is only ever lowered, so a measurement
  /// that never ran leaves the most-passing value a floor can meet, and the
  /// direction claim and the gap floor clear as well. Asserted in main()
  /// against the number of days the run is supposed to walk, so the hole has
  /// a name and a breakage rather than a hope.
  std::uint32_t leanest_days_measured = 0;
  std::uint32_t most_hungry_at_once = 0;

  /// HOW MANY PEOPLE SPENT A WHOLE YEAR LOSING HEALTH — residents who were
  /// under food.csv's health_loss_satiety_threshold on more than half of the
  /// days they lived through a year, counted at that year's end and kept at
  /// the worst of the years measured.
  ///
  /// WHY A COUNT OF PEOPLE AND NOT A MEAN. The yearly figure this stands
  /// beside is a mean over days of a mean over residents — a settlement
  /// double mean — and a threshold on that asserts nothing about anybody:
  /// the mean is not the median, and hunger is not symmetric, it gathers in
  /// the poor yards. The share of people under a line at a given settlement
  /// mean can be far more than half or far less, and the number does not say
  /// which (boss, 2026-09-12; architecture 8bo — a threshold is a claim
  /// about a DISTRIBUTION).
  ///
  /// So the claim is put where it belongs. The line stays where it was born,
  /// on one resident, and what becomes yearly is the COUNT.
  ///
  /// PRINTED AND ASSERTING NOTHING, deliberately. The count has no edge yet,
  /// and an edge invented here would be one more threshold with no home.
  /// Zero is too strict — one sick old man in a bad year does not make a
  /// settlement collapse — and "no more than N" wants an argument out of the
  /// food and health tables, not out of this run.
  std::uint32_t worst_year_below_health_line = 0;

  /// WHAT THE CHAIRMAN AUTHORISED out of the sealed funds over the run, in
  /// grams, by fund — AND AUTHORISED IS THE WORD. UnsealFund moves no grain:
  /// it adds the amount to this tally and returns, and the keepers elsewhere
  /// read the tally to know how much of the store they may stop protecting.
  /// So a chairman who asks for the same fund on a hundred days records a
  /// hundred permissions, not a hundred loads of rye, and this figure is not
  /// a cost and must never be read as one.
  ///
  /// IT IS STILL WORTH PRINTING, for two reasons. It is the only witness
  /// that the chairman reached at all, which is what the anchor below tests;
  /// and the seed fund has no ceiling in the verb (production_system.cpp
  /// says so and says why — the fund's size lives in core_residents), so the
  /// figure grows without bound and SAYING that is more useful than hiding
  /// it. The run prints the figure rather than this comment quoting one:
  /// a number in prose beside a number the binary computes goes stale on the
  /// first delivery that moves it, and this one already did.
  ///
  /// ACCUMULATED ACROSS YEARS, because the core zeroes the release state at
  /// the year's turn — an unsealing is an emergency of ITS year, not a
  /// standing licence (world_state.h). The run watches the per-fund total
  /// rise and adds the last standing figure whenever it drops, which is the
  /// ordinary way to total a counter that resets.
  std::array<std::int64_t, static_cast<std::size_t>(core::FundKind::kFundKindCount)> released{};

  /// How many times the run watched a fund's tally drop to nothing under
  /// it — the year's turn, and the only thing that can lower it. IT EXISTS
  /// BECAUSE THE RESET RULE HAS NO OTHER WITNESS: the totals above are the
  /// sum of what the rule collected, so a rule that stopped collecting
  /// reports a smaller number and nothing says which of the two happened.
  std::uint32_t fund_wipes_seen = 0;

  /// How many per-fund tally reads the run made: one per fund per tick. IT
  /// EXISTS BECAUSE THE MOVE HAS NO OTHER WITNESS — the tally is read every
  /// TICK and not once a day, so that an authorisation written at hour 1 and
  /// wiped at hour 0 of the next day is seen, and putting the block back
  /// under the daily guard leaves every other assertion in this run green.
  /// This one falls by a factor of twenty-four and says so.
  std::uint32_t tally_reads = 0;

  /// The satiety line every count in this outcome was measured against, read
  /// from food.csv. Zero means the run never found it — the anchor assert in
  /// main() is what turns that into a failure rather than a quiet nought.
  float health_line = 0.0F;
  float life_expectancy = 0.0F;
  std::uint32_t people = 0;
  std::uint32_t hungry = 0;   ///< Under the health-loss threshold at the end.
  std::uint32_t starved = 0;  ///< Deaths the run can attribute to hunger.
  std::uint32_t lowest_people = 0;
};

/// Copies tables/ into `root`, letting the caller rewrite one file on the way.
/// The core reads a DIRECTORY, so a doctored run needs a doctored directory —
/// there is no back door into a loaded table set, by design.
void CopyTables(const std::filesystem::path& root, std::string_view drop_column) {
  namespace fs = std::filesystem;
  fs::remove_all(root);
  fs::create_directories(root);
  for (const fs::directory_entry& entry : fs::directory_iterator("tables")) {
    if (entry.path().extension() != ".csv") {
      continue;
    }
    std::ifstream in(entry.path(), std::ios::binary);
    std::ofstream out(root / entry.path().filename(), std::ios::binary);
    if (entry.path().filename() != "food.csv" || drop_column.empty()) {
      out << in.rdbuf();
      continue;
    }
    // food.csv keeps the issue norms in a column of its own; blanking that
    // column is exactly "the chairman hands out nothing".
    std::string line;
    std::uint32_t issue_column = 0;
    bool header_seen = false;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      std::vector<std::string> cells;
      std::stringstream cell_stream(line);
      std::string cell;
      while (std::getline(cell_stream, cell, ',')) {
        cells.push_back(cell);
      }
      if (!header_seen && !line.empty() && line[0] != '#') {
        header_seen = true;
        for (std::uint32_t index = 0; index < cells.size(); ++index) {
          if (cells[index] == drop_column) {
            issue_column = index;
          }
        }
      } else if (header_seen && issue_column < cells.size()) {
        cells[issue_column].clear();
      }
      for (std::uint32_t index = 0; index < cells.size(); ++index) {
        out << (index == 0 ? "" : ",") << cells[index];
      }
      out << '\n';
    }
  }
}

/// @brief The simplest chairman there can be: ONE rule, no policy.
///
/// WHY THE RUN NEEDS HIM. Without him this run measures a village whose funds
/// are sealed and whose door nobody opens — and a village with no chairman is
/// the one thing the game does not contain for a single minute. The seed fund
/// and the plan reserve are held back from the issue by design (resources
/// design §6), and the design is equally explicit that the chairman may open
/// them when there is nothing to feed people with. Nobody was saying so here,
/// so the run was starving by the shape of the EXPERIMENT and not by the
/// shape of the model (boss, 2026-09-12).
///
/// THE RULE, and it is deliberately not a strategy: on a day when the village
/// is hungry, open the plan reserve; if it is still hungry, open the seed
/// fund. Nothing is optimised and nothing is chosen — this is the LOWER BOUND
/// of sensible behaviour, what any chairman would do, and the design calls it
/// legitimate in as many words.
///
/// AND IT OPENS A PORTION OF WHAT IS DUE, a share given at construction.
///
/// WHY A SHARE AND NOT "WHAT IS MISSING". The run cannot compute what is
/// missing without a second copy of the food model, and a second copy is how
/// a measurement comes to agree with itself rather than with the village. A
/// share needs no model at all: the chairman reaches for a tenth of WHAT THE
/// DISTRICT IS OWED each hungry day, so how much he ends up spending is
/// MEASURED by the run rather than decided by it.
///
/// That is a tenth of the DOOR only at the plan reserve, whose ceiling is
/// that very figure. At the seed fund it is a probe size and nothing more,
/// because the seed fund's size is not visible from here at all. And he does
/// not keep reaching at the same door: kPatienceDays sends him past the
/// reserve to the seed from the second hungry day of a streak, so the reserve
/// gets at most one portion per streak — and none at all from a streak that
/// opened in the mute window below, because hungry_days_ goes on counting
/// while he stages nothing and is already past his patience by the time the
/// district names a figure again.
///
/// AND HE IS MUTE FOR EIGHT DAYS OF EVERY YEAR, which is a finding about
/// the MODEL and deliberately not repaired here. `plan.due` is zeroed by
/// JudgePlan on the first of January and refilled by AnnouncePlan on the
/// first of March, so for the two months between them the chairman has no
/// figure to size a reach by and stages nothing.
///
/// A DRAFT OF THIS RUN GAVE HIM A MEMORY of the district's last figure, and
/// it was withdrawn because it repaired nothing. UnsealFund's plan-reserve
/// ceiling reads the CURRENT plan.due — the same all-zero vector — so every
/// reserve order in that window is refused whatever the chairman remembers.
/// The only door a memory opens is the seed fund, which has no ceiling at
/// all, and there it authorised a whole remembered year per resource per
/// hungry day: 578 t more of a figure that was already meaningless, and the
/// ceiling arm came out very slightly WORSE for it.
///
/// So the window stands, named: the door kept for a hungry winter is bolted
/// through January and February, as a side effect of the plan being cleared
/// at the year's turn and announced again in spring. Whether that is the
/// design (no plan, no reserve) or an accident is boss's to say.
///
/// The whole share, 1.0, is the CEILING arm: the most this chairman can do
/// with what he can see. The small share is the question boss asked —
/// whether the door has to be taken off its hinges or only opened. Both are
/// printed; neither asserts a level.
class MinimalChairman {
 public:
  explicit MinimalChairman(float portion) : portion_(portion) {}

  void RunDay(core::ISimulation& simulation, bool hungry_today) {
    if (!hungry_today) {
      hungry_days_ = 0;
      return;
    }
    ++hungry_days_;
    const core::WorldState& world = simulation.CompletedState();
    // The plan reserve first — it costs the autumn's delivery. The seed fund
    // after, and only if the first did not help, because it costs the spring.
    // That order is the design's ladder read upwards.
    const core::FundKind fund =
        hungry_days_ < kPatienceDays ? core::FundKind::kPlanReserve : core::FundKind::kSeed;
    std::vector<core::OrderRow> orders;
    for (std::uint32_t index = 0; index < world.plan.due.size(); ++index) {
      const core::Grams owed = world.plan.due[index];
      if (owed <= 0) {
        continue;
      }
      // At least one gram whenever anything is owed: a share small enough to
      // round to nothing would make "he reached and the door gave nothing"
      // indistinguishable from "he never reached", and the second is what the
      // no-chairman arm already measures.
      const auto share =
          static_cast<core::Grams>(static_cast<double>(owed) * static_cast<double>(portion_));
      core::OrderRow order;
      order.kind = core::OrderKind::kUnsealFund;
      order.fund = fund;
      order.resource = core::DefIdFromIndex<core::ResourceIdTag>(index);
      order.amount = share > 0 ? share : 1;
      orders.push_back(order);
    }
    if (orders.empty()) {
      return;
    }
    simulation.StageOrders(std::span<const core::OrderRow>(orders.data(), orders.size()), {});
  }

 private:
  /// Days of hunger before he reaches past the plan reserve for the seed. Two
  /// is "he tried the cheaper door and it did not answer", not a balance
  /// figure.
  static constexpr std::uint32_t kPatienceDays = 2;

  float portion_ = 1.0F;
  std::uint32_t hungry_days_ = 0;
};

/// The seed this run's bands were read on. Another seed is another weather,
/// another set of births and another village: the figures move with it, so
/// the verdict below binds here and the sweep only prints.
constexpr std::uint64_t kCanonSeed = 1931;

/// Whether the bands below judge or only print. Set false by a swept seed.
bool g_bands_bind = true;

/// A BAND carries a measured number — a level, a margin, a share — and the
/// number was read on kCanonSeed. On another seed it measures the same
/// quantity and must not pretend the level applies, so it prints instead.
///
/// A MODEL CLAIM carries only a DIRECTION: hunger reaches health, a starved
/// village is still a village. Those bind on every seed and go through
/// run::Expect directly — the first draft of the sweep gated them too, and
/// an automated sweep would have read green over the red line that nobody
/// starves to death.
int ExpectBand(bool holds, const char* label) {
  if (holds || g_bands_bind) {
    return run::Expect(holds, label);
  }
  std::cout << "off-band (other seed): " << label << '\n';
  return 0;
}

/// A RECORDED VALUE IS NOT A DESIGN BAND, AND THE NAME IS THE WHOLE OF THE
/// DIFFERENCE (boss's decision of 2026-09-12; architecture §8бш).
///
/// RE-RECORDED ONCE, AND THE ACT IS WRITTEN DOWN BECAUSE IT IS THE DANGEROUS
/// HALF OF THIS WHOLE IDEA. A value re-recorded on every delivery is a
/// ratchet that follows the model wherever it goes and never says no — it
/// would read as a check and behave as a diary. What keeps it honest is that
/// moving it is a deliberate act with a stated cause, and here is the cause
/// and the cost.
///
/// THE CAUSE: sowing stopped being horse-pulled on 2026-09-12. Three places
/// in the tree said it is hand work and one said it is not; the equipment
/// registry has no seed drill in any era, so the design's "a seed drill or by
/// hand" was a promise with nothing behind it, and boss ruled the prose to
/// match. THE SIZE, stated because the prediction below is compared against
/// it: at an empty fodder fund the three field phases fell from 223.6 to
/// 203.8 game man-days, about a ninth of themselves — and rather less than a
/// twentieth of the settlement's working year, which also carries the
/// harvest, the herd and the hauling. "About a tenth of field work" was the
/// first draft of this line and it was two and a half times too large.
///
/// THE COST, and every one of the four moved the WRONG way: the yearly mean
/// 67.54 to 64.84, the leanest day 20.49 to 19.81, the share hungry at once
/// 0.811 to 0.863, the gap the issue makes 1.61 to 0.78. THIS LINE STANDS
/// BESIDE THE NUMBERS AND NOT IN A COMMIT MESSAGE, because here it outlives
/// the message.
///
/// AND THE REASON IS A PROPERTY OF THE MODEL WITH A PREDICTION ATTACHED
/// (boss, 2026-09-12). Cheaper work did not feed anybody: it freed hands, the
/// hands became two more residents, and the same harvest was spread over
/// them. THE LAND IS THE CEILING — there is nowhere else to work, seventy
/// hectares in the first year and seventy in the thirtieth — so spare labour
/// turns into people and not into food. idle_curve says the same thing from
/// the other side.
///
/// THE PREDICTION, AND IT CAN BE WRONG: once the player can raise the
/// ninety-three unworked hectares, this same experiment must come out the
/// other way — cheaper work should RAISE the table, not lower it. Repeat it
/// then with the same damage, so the comparison is honest. If cheap work
/// still lowers the table after the land opens, the ceiling was never the
/// land and the real one is still unnamed.
///
/// Three checks below carried numbers that had been red for days — 25 on
/// the leanest day from the stage-6 criterion, four fifths of the village
/// hungry at once, and five points of margin between the fed village and the
/// unfed one. Only the first of the three came with the criterion; the other
/// two were written later, beside measurements of the day. The model meets
/// none of them, and boss has ruled that answering them is the MODEL's debt
/// and not this run's — so the band is withdrawn and the regression is kept.
/// A guard that reddens on every run becomes background, and a gate that can
/// never be satisfied stops meaning "broken" at all, which costs more than
/// the guard was worth.
///
/// What stands in their place is what this run measured on kCanonSeed, and
/// the only claim made about it is that the settlement does not get WORSE
/// than that without someone noticing. THAT is an honest instrument
/// precisely because it says so: what would make it a lie is not the
/// comparison with yesterday's number — it is the label "design band" laid
/// on top of one. The debt itself lives in boss's decision register with his
/// name and a date for the re-shoot, and deliberately not here: a floor
/// written down in a test is not a design decision and must never be read as
/// one by whoever comes next.
///
/// A recorded value is a MEASURED number, so it is a band by this file's own
/// rule and goes through the same seed gate: on a swept seed it prints.
///
/// THERE IS NO TOLERANCE, and the first draft's was worse than none. It
/// allowed 2 % and justified itself by the second compiler — Clang here,
/// MSVC on the Windows host — but the host build makes `core.lib` and never
/// runs ctest, so the tolerance guarded a run that does not happen while
/// permitting a fifth of a point of real degradation every time. The run IS
/// deterministic: the same seed over the same tables gives the same float,
/// and the suite already leans on that in `determinism`, which compares one
/// worker against four byte for byte.
///
/// So the recorded values are written to nine figures and compared exactly.
/// If the day comes that the host runs the suite, a tolerance may be owed —
/// and it will be owed a MEASUREMENT of how far the two compilers actually
/// part, not a number chosen in advance to be comfortable.
///
/// @brief Regression check where a HIGHER number is the healthier village.
int ExpectNoLower(float value, float recorded, const char* label) {
  std::cout << "food_year: " << label << " — " << std::setprecision(9) << value << ", recorded "
            << recorded << std::setprecision(6) << '\n';
  return ExpectBand(value >= recorded, label);
}

/// @brief Regression check where a LOWER number is the healthier village.
int ExpectNoHigher(float value, float recorded, const char* label) {
  std::cout << "food_year: " << label << " — " << std::setprecision(9) << value << ", recorded "
            << recorded << std::setprecision(6) << '\n';
  return ExpectBand(value <= recorded, label);
}

Outcome RunYears(const std::filesystem::path& tables_root,
                 std::uint32_t years,
                 float chairman_portion,
                 std::uint64_t seed) {
  Outcome outcome;
  const run::Simulation world = run::Start(seed, 1, tables_root.string());
  if (!world) {
    return outcome;
  }
  core::ISimulation* simulation = world.simulation.get();
  MinimalChairman chairman(chairman_portion);
  // THE BUILDING CHAIRMAN (building_chairman.h). The core raises no house
  // from nothing (boss, parcel 257), and a stranger comes only to a free house
  // (district design §2): without anybody building, the three years had no
  // migrant at all — 86 people in 21 households against 119 in 48 — and the
  // issue, paid by trudodni, came to 0.63 t a head against 0.98 (parcel 304).
  run::BuildingChairman builder(*world.tables);
  std::array<std::int64_t, static_cast<std::size_t>(core::FundKind::kFundKindCount)> standing{};
  // THE LINE IS THE TABLE'S, not a repeat of it here. 40 is
  // health_loss_satiety_threshold in food.csv — the level below which health
  // falls — and a second copy of it in this file would be a number with two
  // homes, which is the drift this run has already been bitten by once.
  //
  // READ THROUGH CellReal, which is this project's CHECKED door: it refuses an
  // unparsable cell and a non-finite one. Reaching past it to strtof was the
  // first draft, and it turned every way of losing the number into the same
  // silent 40 — a renamed column, a blank cell, the text "nan". The last of
  // those is the worst: no satiety is ever below a NaN, so the run would print
  // a fed village and assert on it.
  //
  // AND THE FIXTURE ASSERTS ITS OWN ANCHOR. There is no fallback value here on
  // purpose: a measurement that quietly substitutes a remembered number for
  // the one it could not read is not a measurement.
  float health_line = 0.0F;
  if (const core::ITable* const food = world.tables->FindTable("food")) {
    const std::uint32_t row = food->FindRowByKey("health_loss_satiety_threshold");
    const std::uint32_t column = food->FindColumn("value");
    if (row != core::kNoTableRow && column != core::kNoTableColumn) {
      if (const std::optional<float> cell = food->CellReal(row, column)) {
        health_line = *cell;
        outcome.health_line = health_line;
      }
    }
  }

  struct Days {
    std::uint32_t lived = 0;
    std::uint32_t below = 0;
  };

  std::unordered_map<std::uint32_t, Days> alive_days;
  outcome.lowest_people =
      static_cast<std::uint32_t>(simulation->CompletedState().residents.rows.size());
  for (std::uint32_t tick = 0; tick < years * core::kTicksPerYear; ++tick) {
    simulation->AdvanceStep();
    const core::WorldState& day = simulation->CompletedState();
    // THE FUND TALLY IS READ EVERY TICK, and everything below it once a day.
    // It stood with the daily work at first and lost a day at every year's
    // turn: an order staged at hour 0 is applied at hour 1, and JudgePlan
    // wipes the tally at hour 0 of the first of January — so the last day's
    // authorisation was written and erased between two daily readings, and a
    // year whose only unsealing fell on its last day vanished whole.
    for (std::size_t fund = 0; fund < day.unsealed.by_fund.size(); ++fund) {
      // COUNTED INSIDE THE LOOP BODY, and the first draft counted at the top
      // of the tick instead — which witnessed the tick loop and not this
      // block: move the block back under the daily guard, leave the
      // increment where it was, and the anchor still read 3456. A witness
      // that can be separated from its subject is not a witness.
      ++outcome.tally_reads;
      std::int64_t total = 0;
      for (const core::Grams amount : day.unsealed.by_fund[fund]) {
        // Saturating at BOTH ends, and the first draft guarded only the
        // top — which left it undefined in exactly the case its own comment
        // invoked, a negative amount off a loaded save. Nothing the
        // simulation writes can come near either end (UnsealFund refuses an
        // amount that is not positive), but this shape read off a save has
        // no range check behind it, and a total that wraps is worse than one
        // that pins.
        constexpr std::int64_t kTop = std::numeric_limits<std::int64_t>::max();
        constexpr std::int64_t kBottom = std::numeric_limits<std::int64_t>::min();
        if (amount > 0 && total > kTop - amount) {
          total = kTop;
        } else if (amount < 0 && total < kBottom - amount) {
          total = kBottom;
        } else {
          total += amount;
        }
      }
      if (total < standing[fund]) {
        outcome.released[fund] += standing[fund];  // the year turned, the slate was wiped
        ++outcome.fund_wipes_seen;
      }
      standing[fund] = total;
    }
    if (core::HourFromTick(day.calendar.tick) != 0) {
      continue;
    }
    builder.RunDay(*simulation);
    const auto people = static_cast<std::uint32_t>(day.residents.rows.size());
    outcome.lowest_people = people < outcome.lowest_people ? people : outcome.lowest_people;
    float today_total = 0.0F;
    std::uint32_t today_hungry = 0;
    // THE YEAR IS CLOSED BEFORE TODAY JOINS IT. This block stood below the
    // tally at first, so the first day of the new year was already in the map
    // when the old one was counted — one day in forty-eight attributed to the
    // year it opens rather than the year it closes, against a comment that
    // said "the one that just ended".
    if (day.calendar.date.day_in_month == 0 &&
        static_cast<std::uint32_t>(day.calendar.date.month) == 0) {
      // Count who spent the year that just ended under the line, then start
      // the tally over. More than HALF the days he was alive for — a man who
      // arrived in the autumn is judged on his autumn and not on a year he
      // did not see.
      std::uint32_t below_all_year = 0;
      for (const std::pair<const std::uint32_t, Days>& entry : alive_days) {
        below_all_year += entry.second.below * 2U > entry.second.lived ? 1U : 0U;
      }
      outcome.worst_year_below_health_line = below_all_year > outcome.worst_year_below_health_line
                                                 ? below_all_year
                                                 : outcome.worst_year_below_health_line;
      alive_days.clear();
    }
    for (std::uint32_t row = 0; row < day.residents.rows.size(); ++row) {
      const core::ResidentRow& resident = day.residents.rows[row];
      today_total += resident.satiety;
      today_hungry += resident.satiety < health_line ? 1U : 0U;
      // Kept BY RESIDENT ID, not by row: rows shift as people are born and
      // die, and a tally kept by position would follow the position rather
      // than the person — counting one man's hungry spring against whoever
      // inherits his row in the autumn.
      Days& days = alive_days[day.residents.row_ids[row].value];
      ++days.lived;
      days.below += resident.satiety < health_line ? 1U : 0U;
    }

    if (chairman_portion > 0.0F) {
      // A quarter of the village below the hunger mark is the day a chairman
      // notices. Not a balance figure and not a threshold of the design — a
      // trigger for the experiment, chosen wide enough that he acts before
      // the year is decided rather than after.
      chairman.RunDay(*simulation, people > 0 && today_hungry * 4U >= people);
    }
    // The FIRST year is excluded from the lean-season measures on purpose.
    // The farm is handed over as a ruin in the winter, with a larder that
    // reaches the first cut and no further; its first spring is meant to
    // hurt, and the design says so (difficulty design §4). What the criterion
    // asks about is a working kolkhoz, not the year it stopped being a ruin.
    if (day.calendar.date.year > 1) {
      if (people > 0) {
        const float today_mean = today_total / static_cast<float>(people);
        outcome.leanest_day_satiety =
            today_mean < outcome.leanest_day_satiety ? today_mean : outcome.leanest_day_satiety;
        ++outcome.leanest_days_measured;
      }
      outcome.most_hungry_at_once =
          today_hungry > outcome.most_hungry_at_once ? today_hungry : outcome.most_hungry_at_once;
    }
    // FINISHED YEARS ONLY (boss seq 27: «food_year упёрся в потолок 70»).
    // The window starts as three neutral 70s — the nutrition factor's prior,
    // not a year anybody lived — and the minimum over the whole window read
    // them: "worst year 70" on every arm whose real years were all above it,
    // a number that could not move. A year is in the window once its turn
    // has folded it in; the date's year is 1-based.
    const std::size_t window = day.vitals.satiety_year_means.size();
    const std::size_t finished = std::min<std::size_t>(
        day.calendar.date.year > 0 ? day.calendar.date.year - 1U : 0U, window);
    for (std::size_t index = window - finished; index < window; ++index) {
      const float year_mean = day.vitals.satiety_year_means[index];
      outcome.worst_year_satiety =
          year_mean < outcome.worst_year_satiety ? year_mean : outcome.worst_year_satiety;
    }
    outcome.years_measured = static_cast<std::uint32_t>(finished);
    if (finished > 0) {
      outcome.last_year_satiety = day.vitals.satiety_year_means.back();
    }
  }
  const core::WorldState& state = simulation->CompletedState();
  float satiety_total = 0.0F;
  float health_total = 0.0F;
  for (const core::ResidentRow& resident : state.residents.rows) {
    satiety_total += resident.satiety;
    health_total += resident.health;
    outcome.hungry += resident.satiety < health_line ? 1U : 0U;
  }
  outcome.people = static_cast<std::uint32_t>(state.residents.rows.size());
  const auto count = static_cast<float>(outcome.people);
  outcome.mean_satiety = count > 0.0F ? satiety_total / count : 0.0F;
  outcome.mean_health = count > 0.0F ? health_total / count : 0.0F;
  outcome.life_expectancy = state.vitals.life_expectancy_years;
  for (std::size_t fund = 0; fund < standing.size(); ++fund) {
    // The last stretch, which for a whole number of years is empty: the
    // final tick IS a turn, so the tally has already been collected and
    // zeroed. It is here for a run that stops mid-year, and it adds nothing
    // to this one.
    outcome.released[fund] += standing[fund];
  }
  return outcome;
}

void Report(const char* label, const Outcome& outcome) {
  std::cout << "food_year: " << label << " — " << outcome.people << " residents, mean satiety "
            << outcome.mean_satiety << ", mean health " << outcome.mean_health;
  if (outcome.years_measured == 0) {
    std::cout << ", worst year NOT MEASURED (no year finished)";
  } else {
    std::cout << ", worst year " << outcome.worst_year_satiety << " of " << outcome.years_measured
              << " finished, last year " << outcome.last_year_satiety;
  }
  std::cout << ", worst year with " << outcome.worst_year_below_health_line
            << " under the health line all year"
            << ", life expectancy " << outcome.life_expectancy << ", leanest day "
            << outcome.leanest_day_satiety << ", worst " << outcome.most_hungry_at_once
            << " hungry at once, chairman authorised "
            << static_cast<double>(
                   outcome.released[static_cast<std::size_t>(core::FundKind::kPlanReserve)]) /
                   1000000.0
            << " t of reserve and "
            << static_cast<double>(
                   outcome.released[static_cast<std::size_t>(core::FundKind::kSeed)]) /
                   1000000.0
            << " t of seed (permissions, not grain) over " << outcome.fund_wipes_seen
            << " fund-wipes (per FUND: one turn with two funds open counts twice), "
            << outcome.hungry << " under " << outcome.health_line
            << " at the end, population never "
            << "below " << outcome.lowest_people << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  namespace fs = std::filesystem;
  int failures = 0;
  constexpr std::uint32_t kYears = 3;
  /// A tenth of what is due, per hungry day. Not a balance figure and not a
  /// rule of the design — a probe: small enough that the difference from the
  /// ceiling arm is visible, large enough to reach the village inside a lean
  /// season rather than after it.
  constexpr float kChairmanPortion = 0.1F;
  // A SEED ARGUMENT, so the same binary can be swept. The bands in this file
  // were read on kCanonSeed and are claims about the model, not about the
  // weather of one year — but they are MEASURED on one seed, so on any other
  // the run measures and does not judge (labor_year carries the same rule for
  // the same reason).
  std::uint64_t seed = kCanonSeed;
  if (argc > 1) {
    // REFUSED, not defaulted. strtoull reports failure only through errno and
    // endptr, so "--verbose" or "19e31" would have become seed 0 — a run that
    // judges nothing, exits green, and is indistinguishable from a passing
    // sweep. A typo in a sweep script must not read as a result.
    char* end = nullptr;
    errno = 0;
    seed = std::strtoull(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0' || errno == ERANGE) {
      std::cout << "food_year: seed argument is not a number: " << argv[1] << '\n';
      return 2;
    }
  }
  g_bands_bind = seed == kCanonSeed;
  std::cout << "food_year: seed " << seed
            << (g_bands_bind ? "\n" : " — swept: bands print, model claims still bind\n");

  // A DIRECTORY OF ITS OWN FOR EVERY INVOCATION. Shared by name, two runs at
  // once wrote each other's tables half-way: a nine-seed sweep on 2026-09-24
  // read 80.11 for seed 1929, which alone reads 46.01, and a band was
  // re-recorded on it before the collision was found. The seed and the
  // clock tell two invocations apart on every platform the runs build on.
  const std::string tag =
      std::to_string(seed) + "_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const fs::path good_root = fs::temp_directory_path() / ("run_food_year_good_" + tag);
  const fs::path bad_root = fs::temp_directory_path() / ("run_food_year_bad_" + tag);
  CopyTables(good_root, "");
  CopyTables(bad_root, "issue_kg_per_trudoden");

  const Outcome good = RunYears(good_root, kYears, 0.0F, seed);
  Report("shipped tables", good);
  const Outcome bad = RunYears(bad_root, kYears, 0.0F, seed);
  Report("nothing issued", bad);

  // THE EXPERIMENT THE OTHER TWO CANNOT RUN: the same shipped tables, with a
  // chairman who opens the sealed funds when the village is hungry. It is
  // printed and not asserted, and that is the point — its job is to say which
  // KIND of red the measures above are, not to add a threshold of its own.
  //
  //   hunger stays with both funds open  -> the red is the model's, and the
  //                                         thresholds are right to complain
  //   hunger goes                        -> the measures were reading a
  //                                         village with no chairman in it
  const Outcome chaired = RunYears(good_root, kYears, 1.0F, seed);
  Report("chairman, door off its hinges", chaired);
  // AND THE SAME CHAIRMAN REACHING BY TENTHS. The pair answers the question
  // the ceiling arm cannot: a fund opened wide tells you what is possible,
  // and only a fund opened by portions tells you what was NEEDED.
  const Outcome portioned = RunYears(good_root, kYears, kChairmanPortion, seed);
  Report("chairman, a tenth at a time", portioned);

  // THE DOOR'S ACCOUNTING, and the two are NOT the same kind of statement —
  // an earlier draft of this comment said they were.
  //
  // The first is a model claim and binds on every seed: a village with
  // nobody to open a fund must show an untouched fund, and if it does not,
  // every release figure below is measuring something else.
  //
  // The second goes through ExpectBand and therefore prints on a swept seed
  // rather than judging. It reads like a direction — reaching by tenths
  // cannot cost more than taking the door off its hinges — but it is not
  // structural: a chairman kept hungry longer by his own thrift could reach
  // on more days and authorise more in the end.
  // AND THE OTHER HALF OF THE ANCHOR, without which the pair above is
  // satisfied by a tally that has stopped recording: zero equals zero and
  // zero is no more than zero, so a broken counter would read as a village
  // whose chairman behaved perfectly. This says the wide-open arm reached.
  //
  // A KNOWN GAP SINCE 0.34.42, NOT AN ASSERTION. The door is entered only by
  // a HUNGRY chairman, and 0.34.42 (the plan's reserve instead of the whole
  // planned crop, the ration from 40) fed this village: on the canonical
  // seed no arm authorises a gram, and of nine seeds only 1937 reaches (52.5 t
  // of the reserve wide open, 5.25 t by tenths) — the shipped arm's leanest
  // day is 66.0-73.9 on all nine. The door itself is tested where it lives
  // (the ladder's unsealing, core_common and core_production units). RETURN
  // WHEN: an arm here reaches a fund again on the canonical seed — printed as
  // KNOWN GAP CLOSED — or a hungrier fixture is given to this experiment.
  const auto reached = [](const Outcome& outcome, core::FundKind fund) {
    return outcome.released[static_cast<std::size_t>(fund)];
  };
  constexpr const char* kFedSince = "0.34.42 fed the village: no hungry chairman enters the door";
  failures += run::KnownGap(reached(chaired, core::FundKind::kPlanReserve) > 0,
                            "the wide-open chairman did reach the reserve (anchor)",
                            std::to_string(reached(chaired, core::FundKind::kPlanReserve)) + " g",
                            kFedSince);
  failures += run::KnownGap(reached(chaired, core::FundKind::kSeed) > 0,
                            "and the seed fund too (anchor)",
                            std::to_string(reached(chaired, core::FundKind::kSeed)) + " g",
                            kFedSince);
  // AND THE PORTIONED ARM REACHED AS WELL — but not for the reason the first
  // draft of this comment gave. It said a chairman whose share had been lost
  // would authorise nothing; the one-gram clamp in RunDay, added by this same
  // change, makes that impossible, so this anchor cannot catch a lost share
  // at all. What catches that is the strict `<` band further down. This one
  // catches the portioned arm falling silent for any other reason — a hunger
  // trigger that stops firing, an order shape the verb refuses.
  failures += run::KnownGap(reached(portioned, core::FundKind::kPlanReserve) > 0,
                            "the portioned chairman reached the reserve too (anchor)",
                            std::to_string(reached(portioned, core::FundKind::kPlanReserve)) + " g",
                            kFedSince);
  // THE RESET RULE'S OWN WITNESS. The totals are the sum of what the rule
  // collected, so a rule that stopped collecting reports a smaller number
  // and nothing distinguishes that from a thriftier chairman. Both
  // directions are structural: a three-year run whose chairman reaches must
  // cross a year's turn with a fund open, and a village with no chairman
  // has nothing for the turn to wipe.
  failures += run::Expect(
      good.tally_reads ==
          kYears * core::kTicksPerYear * static_cast<std::uint32_t>(core::FundKind::kFundKindCount),
      "the fund tally was read every tick, not once a day (anchor)");
  failures += run::KnownGap(chaired.fund_wipes_seen > 0,
                            "the year's turn wiped an open fund at least once (anchor)",
                            std::to_string(chaired.fund_wipes_seen) + " wipes",
                            kFedSince);
  failures += run::Expect(good.fund_wipes_seen == 0,
                          "and a village with no chairman had nothing to wipe (anchor)");
  for (std::size_t fund = 0; fund < good.released.size(); ++fund) {
    failures += run::Expect(good.released[fund] == 0,
                            "a village with no chairman leaves the funds sealed (anchor)");
    // A BAND AND NOT A MODEL CLAIM, though it reads like one. It held on
    // every seed sampled and the ratio sat near the tenth it was given —
    // but it is not STRUCTURAL: a chairman kept hungry longer by his own
    // thrift could reach on more days and authorise more in the end. A
    // direction that happens to hold is a measurement, and measurements
    // print on a seed they were not read on.
    failures += ExpectBand(portioned.released[fund] <= chaired.released[fund],
                           "and reaching by tenths authorised no more than opening wide");
  }

  // AND STRICTLY LESS AT THE RESERVE. The per-fund `<=` above is green under
  // the one regression it exists to catch: lose the share and the two
  // deterministic arms become the same run, and equality satisfies it. This
  // says the two arms really are two. A band and not a model claim — the
  // MARGIN between them is a measurement, even though its direction is not.
  // Nought against nought since 0.34.42 (above): a gap, not a band.
  failures += run::KnownGap(
      reached(portioned, core::FundKind::kPlanReserve) <
          reached(chaired, core::FundKind::kPlanReserve),
      "and a tenth at a time really is less than the door off its hinges",
      std::to_string(reached(portioned, core::FundKind::kPlanReserve)) + " g against " +
          std::to_string(reached(chaired, core::FundKind::kPlanReserve)) + " g",
      kFedSince);

  // WHAT THIS CRITERION IS MEASURED ON, and it changed on 2026-08-31 after
  // the arithmetic of the whole balance was added up for the first time.
  //
  // Not the yearly mean. A peasant year is not level: after the harvest the
  // plenty is real, by spring the bins are empty, and the average between
  // them describes neither. The design's claim is about the LEAN SEASON —
  // how deep it goes and how many households it reaches (metrics design §8).
  // The mean is printed and not asserted on.
  //
  // A LEVEL satiety curve would be a symptom rather than a success: it would
  // mean food arrives faster than it is eaten and is piling up somewhere.
  // That is not a hypothetical — a level curve is exactly what 241 tonnes of
  // hay in the larders looked like before anyone added the numbers up.
  failures += run::Expect(good.health_line > 0.0F,
                          "the run found its health line in food.csv (fixture anchor)");
  // TWO MORE ANCHORS, and they are about the fixture and not the weather, so
  // they bind on every seed. The first says the lean-day measurement really
  // walked the days it claims; the second says there is a village to measure
  // at all, because a settlement of nobody scores perfectly under a ceiling
  // on the share of it that goes hungry — the one catastrophe the guard
  // exists to notice would have read as the best run ever made.
  // Days 1..kYears*48 are observed and day 0 never is, because the first
  // hour-zero tick of the loop lands on day 1; of those the lean-day measure
  // takes the ones in year 2 and later, which is days 48..144 inclusive. The
  // count is printed as well as asserted: an anchor that says only "wrong"
  // leaves the next reader to rediscover the off-by-one I just did.
  constexpr std::uint32_t kLeanDaysExpected = (kYears - 1U) * core::kDaysPerYear + 1U;
  std::cout << "food_year: the lean-day measure walked " << good.leanest_days_measured
            << " days, expected " << kLeanDaysExpected << '\n';
  failures += run::Expect(good.leanest_days_measured == kLeanDaysExpected,
                          "the lean-day measure walked every day it claims (fixture anchor)");
  failures += run::Expect(good.people > 0U && bad.people > 0U,
                          "there is a village on both arms to measure (fixture anchor)");
  // THE FOURTH BARE NUMBER OF THIS RUN, withdrawn 2026-09-12 on boss's
  // decision and by the same argument as the three before it — 45 on the
  // yearly mean, 25 on the leanest day, four fifths on the share hungry at
  // once. 65 is not derived, not measured against anything, and asserts
  // nothing about the world; it asserts that somebody once wrote it down.
  //
  // AND IT WENT RED BY A DECISION, not by a breakage. Sowing stopped being
  // horse-pulled the same day, the three field phases got about a ninth
  // cheaper at an empty fodder fund (223.6 to 203.8 game man-days), and
  // the settlement's yearly mean fell from 67.54 to 64.84 — through a
  // threshold that had no ground to stand on either side of.
  //
  // AND IT ROSE AGAIN ON 12 SEPTEMBER, 64.8391647 to 68.361969, when the
  // work queue stopped putting a closed window ahead of an open one: the
  // ratchet follows the model up as well as down, or it stops catching the
  // return. The same change cost a point on the leanest day below — the
  // trade is named there.
  //
  // AND AGAIN ON 13 SEPTEMBER, 68.361969 to 69.9886627, when the district's
  // norm stopped being priced off the crop standing in each field's slot and
  // started coming off last year's worked arable at the positions' own
  // shares. The village owes a different figure, and on the shipped rotation
  // it is a lighter one at the lean end of the year. The ratchet follows the
  // model up as well as down, or it stops catching the return.
  //
  // AND IT CAME DOWN ON 13 SEPTEMBER, 69.9886627 to 69.7490158, with the
  // ripening rule and the price of a late sowing. Two tenths, and they are
  // paid for a model that no longer reaps a crop sown the day before its
  // window: a field put in late now gives less, and one that cannot ripen at
  // all is not sown. THE RATCHET FOLLOWS THE MODEL DOWN TOO, or it is not a
  // ratchet but a wish — and the leanest day below went the other way in the
  // same change, which is the trade and is named there.
  //
  // AND TWO HUNDREDTHS DOWN THE SAME DAY, 69.7490158 to 69.7271729, when the
  // cattle yard was moved 19.7 m off the bed of the village spur, where it had
  // stood (boss's map export). Measured apart from every other change: the
  // same number with and without the core's own fixes of that afternoon.
  // A first export put the yard 103 m away on the other side and cost 3.3
  // points; that was withdrawn as a misreading, and this is the real price of
  // a yard that no longer stands on the road.
  //
  // AND 69.7065887 LATER THE SAME DAY, when the herds stopped eating the plan
  // reserve (resources design §6: fodder is the rung below the plan). The
  // horses go thinner in the months the reaped grain is set aside for the
  // district, and the village with them — two hundredths, the price of the
  // ladder's own sentence "лошадь худеет раньше, чем срывается сдача".
  //
  // AND 66.9979935 ON 14 SEPTEMBER, with the fallow before winter rye getting
  // a place in the queue (labor_system.cpp, PreparesWinterCrop). Until then
  // that fallow was never ploughed and its rye never sown; now it is, the
  // work yields to every job with a window, and this ONE seed's three years
  // lost 3.5 points of mean — the rye seed out of the stores and a different
  // harvest calendar after it. The same change on thirty_years' nine seeds
  // moved the mean satiety of years 1-3 from 58.1 to 58.7 and the potato of
  // those years by -8 t against a spread of 31 t: no loss the village carries,
  // a trajectory this seed follows. The four records below moved together.
  //
  // AND 66.8810043 THE SAME DAY, with the meadow cut given its June-July
  // window and a tier of its own (assignment.cpp; boss, parcel 262). The cut
  // had no window and the fallow took its hay; on this one seed the three
  // years lost a tenth of a point. thirty_years' nine seeds with orders,
  // against 312f5fe: satiety of years 1-3 58.7 -> 58.7, the leanest day
  // 27.6 -> 28.1, potatoes +4 t (sd 31 t), rye unchanged, the same granaries,
  // population at year 30 +29.7 (2se 102.6). The four records moved together.
  //
  // AND 61.1317711 ON 14 SEPTEMBER, when the core stopped raising a house from
  // nothing (boss, parcels 257, 306). A stranger comes only to a free house
  // (district design §2), so a world with no chairman who builds had no
  // migrant in three years — 86 people in 21 households against 119 in 48 —
  // and the issue, paid by trudodni, came to 0.63 t a head against 0.98. With
  // the building chairman this run now plays (building_chairman.h) it is a
  // different world: 99 people, the mean 5.7 points under the old record, and
  // the WORST year 48.8 against 37.1 — better, not worse. The four records
  // below moved together.
  //
  // AND 61.4696999 THE SAME NIGHT, with the felling brigade riding and the
  // first sawmill built of logs (boss, parcel 308): one seed, three years, a
  // different timber calendar — the mean up a third of a point, the leanest
  // day down half of one, the hungry share 83 of 99 -> 85 of 98, the issue's
  // gap 8.09 -> 7.64. A trajectory this seed follows, not a loss the village
  // carries; the four records moved together.
  //
  // AND UP TO 76.5670853 ON 15 SEPTEMBER, when a numbered store began taking
  // only what it is the home of (boss, parcels 408, 415) — with the leanest
  // day DOWN from 22.2465935 to 17.6482697 and the hungry share UP from
  // 0.867346942 to 0.886792481 in the same change. Measured by switching the
  // parts off one at a time: with the home check off all three records held
  // (23.38, 0.858); the settled-snow rule and the fixture's food store after
  // the stable took a part each. Nothing of a tonne or more was written off in
  // the first year, so the food was there and lay in the wrong place or came
  // late. THE LOWER RECORDS ARE A DEBT, NOT A NORM: where the food lies at the
  // lean season and how long it travels to the issue is the first line of
  // boss's task 4 (b).
  //
  // AND UP TO 78.6153107 THE SAME DAY, when the debt was measured and the
  // run's chairman began putting up a CLAMP by the fields before the first
  // reaping (boss, parcels 418, 419; fixture_policy.h). The food had not lain
  // in the wrong place: the snow had taken it off the fields for want of any
  // home — 493, 734 and 652 t of potatoes and vegetables in four years on
  // seeds 1929, 1933 and 1936, the church full of the start's grain and no
  // food store standing by the first harvest. The three records moved up
  // together: leanest day 17.6482697 -> 20.4674702, hungry share 0.886792481
  // -> 0.854166687. The leanest day is still under the 22.2465935 it stood at
  // before the stores' homes, and what is left is carrying off the fields.
  //
  // AND DOWN TO 78.5717392 THE SAME DAY, deliberately and with the cause, when
  // the seed fund stopped holding this year's seed for a field already reaped
  // (boss, parcel 421; fund_ladder.cpp): the leanest day rose in the same
  // change, 20.4674702 -> 20.7970276, and the hungry share held. What the fund
  // freed in the autumn is handed out in the autumn, and the year's mean gave
  // four hundredths of a point for a third of a point at its lean end.
  //
  // AND UP TO 79.4115295, the three records together, when a harnessed job
  // stopped being skipped whole once the day's horses were taken
  // (assignment.cpp; 2026-09-15): the autumn plough took the last horse and
  // the carts behind it were not offered at all. Leanest day 20.7970276 ->
  // 22.1130543, hungry share 0.854166687 -> 0.817307711.
  //
  // AND UP TO 97.309639 when the issue went DAILY (boss, parcels 428-434;
  // labor-payment §3: "Семья может прийти за ресурсами в любой момент"): milk
  // keeps two days, and an issue every four let the stores rot most of it.
  // The arm "every four days" on the same code gives the old records to the
  // digit, so the whole shift is the rhythm's. Leanest day 22.1130543 ->
  // 32.5325203, hungry share 0.817307711 -> 0.63809526. The plan reserve
  // holding its rot until the delivery (family_exchange.cpp) rides in the
  // same change and moves none of the three.
  //
  // AND UP TO 97.659462 when bread became rye and wheat instead of oats
  // (boss, parcel 424) under "first the plan, then the issue" (labor-payment
  // §7, parcel 438): a planned crop goes out on trudodni only after the
  // delivery, the rye ration to the hungry is not held. Leanest day
  // 32.5325203 -> 58.2252274, hungry share 0.63809526 -> 0.252427191.
  //
  // AND DOWN TO 97.6370621 THE SAME DAY, deliberately and with the cause:
  // before the spring names a plan the district's positions are held BY THE
  // LIST, not by what last year delivered of them (boss, parcel 440) — a
  // position failed outright used to open the issue and prepare the next
  // failure. Leanest day 58.2252274 -> 45.7935219, hungry share 0.252427191 ->
  // 0.409523815; the canon holds more rye through the winter, and the day is
  // still above every record before the bread norm.
  //
  // AND DOWN TO 89.4596481 THE SAME DAY, deliberately and with the cause: the
  // state holidays became days of rest (host door request no. 2; time design
  // §4) — May Day in the sowing, 7 November in the late reaping. On THIS seed
  // alone the year turned on those two days: leanest day 45.7935219 ->
  // 30.1344814, hungry share 0.409523815 -> 0.879999995. The other eight seeds
  // of 1929-1937 kept a leanest day of 52-64 and a hungry share of 0.17-0.36;
  // what exactly the two days cost seed 1931 is not traced yet.
  failures += ExpectNoLower(
      good.mean_satiety, 89.4596481F, "the settlement's mean over the year (recorded, not a band)");
  // A YEAR's mean sits well below the year's end, and that is the model
  // telling the truth rather than failing: a subsistence village is at its
  // fullest after the harvest and at its thinnest in spring, when the garden
  // is months away and the trudodni that buy the issue have not been earned
  // yet. What the criterion asks is that the lean season stays a lean season
  // and never becomes a collapse.
  // 45 NO LONGER ASSERTS ANYTHING, and it stopped on 2026-09-12 rather than
  // when its replacement is found. The number has no home: not a row in the
  // tables, not a sentence in the design, arrived with the stage-6 criterion
  // on 2026-08-30 under a comment that says what the criterion asks and not
  // where the figure came from — while the design's own satiety section says
  // the seasonal-minimum MEASURE was adopted "по замеру прогона". A threshold
  // with no ground is false today and not from the day that is proved, and a
  // guard that reddens every run becomes background (architecture §8бм).
  //
  // It prints instead, beside the chairman's run and for the same reason:
  // its job is to say what KIND of number this is, not to add a verdict.
  //
  // WHAT IT MEASURES, said here because the next reader will need it before
  // any replacement: the worst of the last three FINISHED years in
  // VitalsState::satiety_year_means — a mean over days of the mean over
  // residents. A settlement double mean, not a resident's figure and not a
  // seasonal minimum. Boss's candidate ground is
  // health_loss_satiety_threshold (40): "the village does not spend a WHOLE
  // YEAR with its average resident at the line where health falls". That is
  // a real argument about harm — but 40 is a threshold on ONE resident, and
  // carrying it onto an aggregate changes the claim without changing the
  // number (architecture §8бо).
  std::cout << "food_year: worst year " << good.worst_year_satiety << " of " << good.years_measured
            << " finished — printed, not asserted: the old floor of 45 has no ground, and its "
               "replacement waits on what the aggregate should be measured against\n";
  // A PROBE THAT GOT NOTHING SAYS SO: the year figures are over finished
  // years only, and a run that finished none has no year to compare.
  failures +=
      run::Expect(good.years_measured > 0U, "the year figures are over at least one finished year");
  failures += ExpectBand(good.last_year_satiety > good.worst_year_satiety - 5.0F,
                         "and the settlement is not sliding year on year");
  // BAND WITHDRAWN, REGRESSION KEPT. The stage-6 criterion asked for 25 on
  // the leanest day; the model gives 19.4991512 since the district's norm
  // moved off the crop in the slot (2026-09-13) — it had been 18.5974369
  // since the queue's three tiers, and 19.8146267 before that. Whether 25 is the right thing to
  // want is the model's question, and it is boss's to schedule — so what is watched here is only
  // that the leanest day does not sink below what it already was.
  //
  // MOVED DOWN FROM 19.8146267 ON 2026-09-12, deliberately and with the
  // cause: the day's work queue stopped ranking work whose window had CLOSED
  // above work whose window was still open (assignment.h, the three tiers).
  // The year's mean satiety rose by three and a half points in the same
  // change — 64.8391647 to 68.361969 — and the leanest day fell by one and
  // two tenths: the village now does the work that still pays and the peak
  // of its hunger sits a little deeper. The ratchet moves only as a
  // deliberate act with a stated cause, and both numbers stand here beside
  // each other so the next reader can see the trade rather than the loss.
  //
  // AND UP AGAIN ON 13 SEPTEMBER, 19.4991512 to 21.1246548, with the ripening
  // rule and the late-sowing price — the OTHER half of the same trade: the
  // year's mean lost two tenths above and the leanest day gained a point and
  // six. The village sows less and sows it in time, so what it does reap is
  // reaped whole, and the deepest point of the hunger is shallower. This is
  // the half that matters more to us of the two.
  // And 21.0634956 the same day with the cattle yard moved off the road (see
  // the mean above: the same export, measured apart). And 20.9036217 when the
  // herds stopped eating the plan reserve (the mean above says why). And
  // 19.379631 with the meadow cut's window (the mean above: this seed only;
  // the nine seeds' leanest day of years 1-3 rose by half a point). And up to
  // 23.2599049 with the building chairman, and 22.8028908 with the felling
  // brigade riding (the mean above says why for both).
  // AND DOWN TO 22.2465935 ON 15 SEPTEMBER with boss's export of the count's
  // lodge and the reserves — the north groves 11.5 ha smaller — measured apart:
  // the same code on the tables just before it gave 23.6760788 (the runs'
  // insulation in October and November; begun in August, its crews came off
  // the reaping and the day fell to 22.29 — so the window moved, not this).
  // AND DOWN TO 17.6482697 THE SAME DAY with the stores' homes — a debt, not
  // a norm (the mean above says why). And up to 20.4674702 with the clamp,
  // and to 20.7970276 with the seed fund's reaped fields (the mean above), and
  // to 22.1130543 with the carts that no longer wait for a horse, and to
  // 32.5325203 with the daily issue (the mean above), and to 58.2252274 with
  // the bread norm under the plan first (the mean above), and down to
  // 45.7935219 with the positions held by the list (the mean above), and to
  // 30.1344814 with the holidays (the mean above), and to 30.0426960 when the
  // horses' summer discount stopped being free.
  //
  // THE LAST MOVE IS NINE HUNDREDTHS AND IT IS THE RIGHT SIGN. The team's
  // half-ration in the pasture months had been applied unconditionally since
  // day zero — no yard, no chairman's order, no children — and it is the
  // night pasture's whole gain (livestock design, «Ночное»). Behind its three
  // conditions now, the horses eat more hay in the summers before the yard
  // stands, and the village's leanest day feels it at one remove: hay and
  // bread compete for the same hands in the same weeks. A fall this small
  // from a change this large is itself the measurement — the fodder bill
  // moved by 116 tonnes and the PEOPLE barely noticed.
  failures += ExpectNoLower(
      good.leanest_day_satiety, 30.0426960F, "the leanest day of the year (recorded, not a band)");
  // BAND WITHDRAWN, REGRESSION KEPT — and the claim SPLIT, because it was
  // two things in one sentence. "Hunger never takes the WHOLE village" is a
  // direction and stands above, binding on every seed. "Four fifths and no
  // more" was a level: it went from seven tenths to four fifths when task A4
  // made food go bad where it lies (transport design §10), so the autumn's
  // abundance stopped waiting in the store until March. The model now stands
  // at 107 of 124 — 86 %, well past four fifths now that hand sowing has
  // freed labour into more mouths — and where the edge belongs is not
  // this run's to say — so the share is watched against itself.
  const float hungry_share = good.people > 0U ? static_cast<float>(good.most_hungry_at_once) /
                                                    static_cast<float>(good.people)
                                              : 0.0F;
  failures += run::Expect(good.most_hungry_at_once < good.people,
                          "and hunger never takes the whole village at once");
  // 0.862903237 until 2026-09-12, moved for the same reason as the leanest
  // day above and in the same change: 107 of 122 instead of 107 of 124 — the
  // count of the hungry did not move at all, the village did, by two people.
  // And down again on 2026-09-13 with the ripening rule: 0.842519701. A
  // smaller sowing reaped whole feeds more mouths at the lean end than a
  // bigger one reaped late, which is the same trade the two records above
  // carry. And 0.86178863 with the meadow cut's window, 106 of 123 (the mean
  // above says why). And 0.838383853 with the building chairman, 83 of 99, and
  // 0.867346942 with the felling brigade riding, 85 of 98. And 0.886792481,
  // 94 of 106, with the stores' homes (the mean above says why). And
  // 0.854166687, 82 of 96, with the clamp. And 0.817307711 with the carts that
  // no longer wait for a horse. And 0.63809526 with the daily issue, and
  // 0.252427191 with the bread norm under the plan first, and 0.409523815 with
  // the positions held by the list, and 0.879999995 with the holidays.
  failures += ExpectNoHigher(
      hungry_share, 0.879999995F, "the share of the village hungry at once (recorded, not a band)");
  // A KNOWN GAP of the building chain (boss, parcel 306): 24 of 99 under the
  // line at the end against a sixth, where the stub's 119 had 14.
  // RESTORED ON 2026-09-15 with the clamp: the gap closed on all nine seeds
  // 1929-1937 (0 to 11 under the line, 11 of 116 the most), where the day
  // before seed 1934 still had 51 of 126.
  std::cout << "food_year: at the year's end " << good.hungry << " of " << good.people
            << " are under the line\n";
  failures += ExpectBand(good.hungry * 6U <= good.people,
                         "the year ends with hardly anyone under the threshold");

  // And it does go hungry when the kolkhoz hands out nothing.
  //
  // The head count at the end of the run is a poor witness: it lands
  // wherever the season left it, and after a good harvest even a badly run
  // village looks fed. The YEAR is what the claim is about.
  //
  // Note what this run will NOT show, and why that is the design and not a
  // weakness: the private plot is the main source of food in the early years
  // (household design §1), and no chairman can take it away. Two goats, eight
  // hens and ten sotkas of potatoes cover about three fifths of the table by
  // themselves. So striking out the issue is a marked, visible dip — many
  // times as many hungry people, health and life expectancy both down — and
  // not a famine. A model in which the kolkhoz could starve the village by
  // handing out nothing would be a model that had forgotten the yards.
  // MEASURED AT THE LEAN SEASON, not at the year's mean. With spoilage the
  // mean is dominated by the months when everything is fresh, and the two
  // villages differ there by barely a point — while at the hungry end of the
  // year they differ by ten, in health by four, and in life expectancy by
  // nearly two. Averaging over a year that has a lean season in it is how a
  // model hides the very thing the run exists to see.
  failures +=
      ExpectBand(bad.mean_health < good.mean_health - 2.0F, "striking out the issue norms is felt");
  // THIS ONE SPLITS IN TWO, by this file's own rule. The DIRECTION is a
  // claim about the model — striking out the issue makes the lean season
  // worse — and it binds on every seed. The five points of margin were a
  // measured number wearing a claim's clothes: the model gives a gap of
  // 0.78 since hand sowing, and it is the gap that is watched, against
  // itself.
  failures += run::Expect(bad.leanest_day_satiety < good.leanest_day_satiety,
                          "and the lean season is worse without the issue");
  // THE SIZE OF THE GAP IS A BAND OVER NINE SEEDS, NOT A RECORD (boss, parcel
  // 352). It was a ratchet from 2026-09-12 — 0.78, 0.79, 3.66, 3.60, 3.44,
  // 1.84, 8.09, 7.64, each move named with the change that made it — and the
  // night trades showed what it was ratcheting: with the catch set to zero the
  // gap was 7.49 again, with no trade handed out at all 9.81. The lots the
  // trades draw moved every later draw, and the gap swings by points on the
  // stream alone. A record of it catches a moved stream, not a village fed or
  // starved. The direction above is the model's claim and binds on every
  // seed; the size is measured here, on seeds 1929-1937 after the start's
  // night trades (2026-09-15): 3.99, 4.13, 4.68, 5.43, 6.54 (median), 7.31,
  // 9.66, 13.36, 18.00.
  //
  // RE-MEASURED THE SAME DAY when a numbered store began taking only what it
  // is the home of (boss, parcels 408, 415), on the same nine seeds: 0.54,
  // 0.61, 0.72, 0.80, 0.82 (median), 2.50, 2.65, 2.91, 3.56. The whole band
  // fell eightfold at the median: the issue now barely moves the lean day,
  // and that is the debt the leanest day above carries, not a new truth
  // about the issue.
  //
  // AND AGAIN WITH THE CLAMP (boss, parcel 419), the same nine seeds: 4.64,
  // 5.18, 5.60, 7.49, 8.81 (median), 9.72, 17.94, 22.32, 22.40. The debt paid:
  // with the potatoes kept, the issue has something to hand out again.
  //
  // AND AGAIN WITH THE SEED FUND'S REAPED FIELDS (boss, parcel 421): 5.93,
  // 6.77, 7.49, 10.03, 10.57 (median), 11.01, 18.35, 22.95, 25.17. The fund
  // freed potatoes the issue could hand out, so the issue weighs more.
  //
  // AND WITH THE CARTS THAT NO LONGER WAIT FOR A HORSE (assignment.cpp): 6.77,
  // 7.24, 7.25, 9.60, 10.67 (median), 12.20, 18.89, 23.08, 25.68.
  //
  // AND WITH THE DAILY ISSUE (boss, parcel 428): 8.59, 12.16, 12.57, 14.49,
  // 29.03 (median), 29.04, 29.34, 34.22, 36.73 — the milk reaches the table.
  //
  // AND WITH RYE AND WHEAT ON THE NORMS UNDER THE PLAN FIRST (boss, parcels
  // 424, 438): 27.50, 31.88, 34.31, 35.45, 38.25 (median), 38.78, 41.17,
  // 41.58, 42.74.
  //
  // AND WITH THE POSITIONS HELD BY THE LIST (boss, parcel 440): 25.82, 27.50,
  // 31.88, 34.32, 35.46 (median), 38.94, 41.11, 41.68, 42.75.
  //
  // AND WITH THE HOLIDAYS (host door request no. 2): 9.22 (seed 1931), 31.17,
  // 32.83, 38.04, 39.05 (median), 39.82, 40.33, 40.41, 42.71.
  //
  // AND WITH THE SIRE COUNT CARRIED RATHER THAN RE-DERIVED (2026-09-16, boss
  // parcel 20): 7.11 (seed 1931 again), 30.66, 33.11, 35.60, 38.27 (median),
  // 39.30, 39.70, 41.47, 43.08.
  //
  // THE BAND BARELY MOVED AND THE MEDIAN FELL BY THREE QUARTERS OF A POINT,
  // which is the whole of what this measurement has to say. The herds keep
  // their share of sires now instead of "at least one", so a herd of six
  // stands at two males and four females where it used to stand at three and
  // three: more milk, in BOTH arms. The gap is a difference, so more milk
  // everywhere shows up as slightly less difference — not as a village fed
  // worse. The shipped arm's leanest day is 30.78 against a recorded 30.13,
  // unmoved; it is the arm with NOTHING ISSUED that rose, which is the arm
  // that leans hardest on what the herds give.
  //
  // Seed 1931 is the low outlier here as it was before it, at 7.11 against
  // 9.22 — the same seed, the same shape, three quarters of the fall.
  //
  // AND WITH RAIN STOPPING THE SOWING AND THE REAPING (boss-core-epoch1-
  // resume seq 3): 34.88, 40.13, 41.35, 41.38, 41.70 (median), 42.85, 43.87,
  // 44.21, 47.23 (seed 1931, now the TOP of the band).
  //
  // THE BAND HAD GONE STALE BEFORE THE RAIN, and the rain's red is what
  // showed it. The same nine seeds on the tree just before it (2a63bf9, the
  // rain's contract, which stops nothing): 35.28, 36.11, 37.53 (seed 1931),
  // 39.41, 40.81 (median), 42.42, 43.41, 47.63, 47.64 — three of nine outside
  // 7.11-43.09 already, and the canonical seed, the only one this run binds,
  // inside by luck at 37.53 and no longer the low outlier the paragraph above
  // describes. The rain itself moved the median by less than a point; it moved
  // seed 1931 from the middle of the band to its top.
  //
  // AND WITH THE PLAN'S RESERVE INSTEAD OF THE WHOLE PLANNED CROP, AND THE
  // RATION FROM 40 (0.34.42; boss, boss-core-epoch1-4 seq 9, 10 and 13), the
  // same nine seeds, one at a time: 46.10, 47.02, 47.90, 49.03 (seed 1931),
  // 49.98 (median), 51.37, 51.58, 52.32, 54.84. The issue closes more of the
  // lean season than it did: grain above the plan's debt goes out on
  // trudodni. A first sweep read 79.99 and 80.11 for 1929 and 1930 — two runs
  // at once writing each other's copied tables (the shared directory, fixed
  // above) — and the band was re-recorded on it before that was found.
  const float issue_gap = good.leanest_day_satiety - bad.leanest_day_satiety;
  std::cout << "food_year: the gap the issue makes at the lean season — " << issue_gap
            << " (nine seeds: 46.10-54.84, median 49.98)\n";
  // The edges are the measured 46.1034 and 54.8420 rounded OUTWARD, so that
  // the seeds that set them stay inside it.
  // A KNOWN GAP FROM 0.34.51 TO 0.35.10, RESTORED: with the horses counted
  // once the gap fell from 46.5 (0.34.50) to 40.8, out of the band. The
  // spring's repairs of 0.35.1-0.35.8 and the young start team of 0.35.10
  // brought it back to 48.5, and the gap printed CLOSED.
  failures +=
      ExpectBand(issue_gap >= 46.10F && issue_gap <= 54.85F,
                 "the gap the issue makes at the lean season stays in the nine seeds' band");
  // NOT "more people go hungry" — that was the claim here, and it is false
  // for a reason worth keeping. THE ISSUE SPREADS SCARCITY: hand the village
  // a thin ration and many are slightly short; hand it nothing and the
  // families fall back on their own plots, so FEWER are counted hungry and
  // those who are are far worse off. The head count went the wrong way while
  // the lean day fell eight points and life expectancy two — the count was
  // never the witness, the depth was. Same trap as the yearly mean: a number
  // that averages or tallies across a village hides what happens inside it.
  failures += ExpectBand(bad.life_expectancy < good.life_expectancy - 1.0F,
                         "and it is paid for in years of life, not in the head count");
  failures += run::Expect(bad.mean_health < good.mean_health,
                          "hunger reaches health, which is the only way it reaches anyone");
  failures += run::Expect(bad.life_expectancy < good.life_expectancy,
                          "and lean years cost the settlement years of life");

  // The red line: hunger never kills. It works through health and nothing
  // else (food model §2), so a starved village is smaller only by the
  // ordinary deaths of its ordinary mortality ladder.
  failures += run::Expect(bad.people > 0, "a starved village is still a village");
  failures += run::Expect(bad.people * 2U > good.people,
                          "hunger costs no lives of its own: no famine deaths exist to model");

  fs::remove_all(good_root);
  fs::remove_all(bad_root);
  if (failures == 0) {
    std::cout << "food_year: all checks passed\n";
  }
  return failures;
}
