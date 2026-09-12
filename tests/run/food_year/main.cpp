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

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
/// AND IT OPENS THEM WHOLE, not by what is missing. The run cannot compute
/// "what is missing" without a second copy of the food model, and the whole
/// figure makes the experiment STRONGER rather than weaker: it is the most a
/// chairman could possibly do. Hunger that survives it is hunger the model
/// owns.
class MinimalChairman {
 public:
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
      core::OrderRow order;
      order.kind = core::OrderKind::kUnsealFund;
      order.fund = fund;
      order.resource = core::DefIdFromIndex<core::ResourceIdTag>(index);
      order.amount = owed;
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

Outcome RunYears(const std::filesystem::path& tables_root,
                 std::uint32_t years,
                 bool with_chairman,
                 std::uint64_t seed) {
  Outcome outcome;
  const run::Simulation world = run::Start(seed, 1, tables_root.string());
  if (!world) {
    return outcome;
  }
  core::ISimulation* simulation = world.simulation.get();
  MinimalChairman chairman;
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

    if (with_chairman) {
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
            << " hungry at once, " << outcome.hungry << " under " << outcome.health_line
            << " at the end, population never "
            << "below " << outcome.lowest_people << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  namespace fs = std::filesystem;
  int failures = 0;
  constexpr std::uint32_t kYears = 3;
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

  const Outcome good = RunYears(good_root, kYears, false, seed);
  Report("shipped tables", good);
  const Outcome bad = RunYears(bad_root, kYears, false, seed);
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
  const Outcome chaired = RunYears(good_root, kYears, true, seed);
  Report("with a chairman", chaired);

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
  failures += ExpectBand(good.mean_satiety >= 65.0F,
                         "with the shipped tables the village is fed on the year (reference)");
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
  failures +=
      ExpectBand(good.leanest_day_satiety >= 25.0F, "the lean season is a dip and not a collapse");
  // Since task A4 food GOES BAD where it lies (transport design §10), and
  // the lean season bites harder for it: the autumn's abundance no longer
  // waits in the store until March. Four fifths rather than seven tenths,
  // and the claim is unchanged — hunger touches most of the village at the
  // worst moment of the year and never all of it.
  failures += ExpectBand(good.most_hungry_at_once * 10U <= good.people * 8U,
                         "and it never takes the whole village at once");
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
  failures += ExpectBand(bad.leanest_day_satiety < good.leanest_day_satiety - 5.0F,
                         "and the lean season is a different animal without the issue");
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
