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
    for (const float year_mean : day.vitals.satiety_year_means) {
      outcome.worst_year_satiety =
          year_mean < outcome.worst_year_satiety ? year_mean : outcome.worst_year_satiety;
    }
    outcome.last_year_satiety = day.vitals.satiety_year_means.back();
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
            << outcome.mean_satiety << ", mean health " << outcome.mean_health << ", worst year "
            << outcome.worst_year_satiety << ", last year " << outcome.last_year_satiety
            << ", worst year with " << outcome.worst_year_below_health_line
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

  const fs::path good_root = fs::temp_directory_path() / "run_food_year_good";
  const fs::path bad_root = fs::temp_directory_path() / "run_food_year_bad";
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
  failures +=
      run::Expect(chaired.released[static_cast<std::size_t>(core::FundKind::kPlanReserve)] > 0,
                  "the wide-open chairman did reach the reserve (anchor)");
  failures += run::Expect(chaired.released[static_cast<std::size_t>(core::FundKind::kSeed)] > 0,
                          "and the seed fund too (anchor)");
  // AND THE PORTIONED ARM REACHED AS WELL — but not for the reason the first
  // draft of this comment gave. It said a chairman whose share had been lost
  // would authorise nothing; the one-gram clamp in RunDay, added by this same
  // change, makes that impossible, so this anchor cannot catch a lost share
  // at all. What catches that is the strict `<` band further down. This one
  // catches the portioned arm falling silent for any other reason — a hunger
  // trigger that stops firing, an order shape the verb refuses.
  failures +=
      run::Expect(portioned.released[static_cast<std::size_t>(core::FundKind::kPlanReserve)] > 0,
                  "the portioned chairman reached the reserve too (anchor)");
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
  failures += run::Expect(chaired.fund_wipes_seen > 0,
                          "the year's turn wiped an open fund at least once (anchor)");
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
  failures +=
      ExpectBand(portioned.released[static_cast<std::size_t>(core::FundKind::kPlanReserve)] <
                     chaired.released[static_cast<std::size_t>(core::FundKind::kPlanReserve)],
                 "and a tenth at a time really is less than the door off its hinges");

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
  failures += ExpectNoLower(
      good.mean_satiety, 64.8391647F, "the settlement's mean over the year (recorded, not a band)");
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
  std::cout << "food_year: worst year " << good.worst_year_satiety
            << " — printed, not asserted: the old floor of 45 has no ground, and its "
               "replacement waits on what the aggregate should be measured against\n";
  failures += ExpectBand(good.last_year_satiety > good.worst_year_satiety - 5.0F,
                         "and the settlement is not sliding year on year");
  // BAND WITHDRAWN, REGRESSION KEPT. The stage-6 criterion asked for 25 on
  // the leanest day; the model gives 19.8146267 and has done since spoilage
  // arrived. Whether 25 is the right thing to want is the model's question,
  // and it is boss's to schedule — so what is watched here is only that the
  // leanest day does not sink below what it already was.
  failures += ExpectNoLower(
      good.leanest_day_satiety, 19.8146267F, "the leanest day of the year (recorded, not a band)");
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
  failures += ExpectNoHigher(
      hungry_share, 0.862903237F, "the share of the village hungry at once (recorded, not a band)");
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
  failures += ExpectNoLower(good.leanest_day_satiety - bad.leanest_day_satiety,
                            0.778614044F,
                            "the gap the issue makes at the lean season (recorded, not a band)");
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
