// Unit test of core_world: the wiring config contract and the O0 world
// genesis STUB. CreateStandardSimulation coverage arrives with task O2.

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../common/fake_tables.h"
#include "core_catalog/extraction_catalog.h"
#include "core_common/calendar.h"
#include "core_common/herd_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_construction/construction_system.h"
#include "core_log/log.h"
#include "core_tables/tables.h"
#include "core_world/era_readiness.h"
#include "core_world/world.h"

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

/// @brief One table held in memory, so a test can hand genesis a cell no

/// @brief Rewrites ONE key's value in a key/value CSV, leaving the rest of
/// the file alone. Returns false when the key is not there, so a renamed row
/// fails the test instead of silently changing nothing.
bool SetKeyValue(const std::filesystem::path& path, std::string_view key, std::string_view value) {
  std::ifstream input(path);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }
  input.close();
  const std::string prefix = std::string(key) + ",";
  bool found = false;
  for (std::string& row : lines) {
    if (row.rfind(prefix, 0) != 0) {
      continue;
    }
    const std::size_t after_key = prefix.size();
    const std::size_t next_comma = row.find(',', after_key);
    const std::string tail =
        next_comma == std::string::npos ? std::string() : row.substr(next_comma);
    row = prefix + std::string(value) + tail;
    found = true;
  }
  if (found) {
    std::ofstream out(path, std::ios::trunc);
    for (const std::string& row : lines) {
      out << row << '\n';
    }
  }
  return found;
}

/// @brief Replaces every value of `column` in a CSV with `value`, keeping the
/// file otherwise as it is. Named for the column and not for the table it
/// was written against: it spoils a cell of any of them, and the livestock
/// name it carried until 2026-09-06 read as a restriction that was never
/// there. Returns false when the column is not there, so a
/// renamed column fails the test instead of silently emptying it.
bool SpoilColumn(const std::filesystem::path& path,
                 std::string_view column,
                 std::string_view value) {
  std::ifstream input(path);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }
  input.close();

  std::size_t header_index = lines.size();
  std::size_t target = 0;
  for (std::size_t index = 0; index < lines.size(); ++index) {
    if (lines[index].empty() || lines[index][0] == '#') {
      continue;
    }
    header_index = index;
    std::size_t position = 0;
    std::size_t field = 0;
    while (position <= lines[index].size()) {
      const std::size_t comma = lines[index].find(',', position);
      const std::size_t end = comma == std::string::npos ? lines[index].size() : comma;
      if (lines[index].substr(position, end - position) == column) {
        target = field;
        break;
      }
      if (comma == std::string::npos) {
        return false;
      }
      position = comma + 1;
      ++field;
    }
    break;
  }
  if (header_index == lines.size()) {
    return false;
  }

  std::ofstream output(path, std::ios::trunc);
  for (std::size_t index = 0; index < lines.size(); ++index) {
    if (index <= header_index || lines[index].empty() || lines[index][0] == '#') {
      output << lines[index] << '\n';
      continue;
    }
    std::string rebuilt;
    std::size_t position = 0;
    std::size_t field = 0;
    while (position <= lines[index].size()) {
      const std::size_t comma = lines[index].find(',', position);
      const std::size_t end = comma == std::string::npos ? lines[index].size() : comma;
      rebuilt +=
          field == target ? std::string(value) : lines[index].substr(position, end - position);
      if (comma == std::string::npos) {
        break;
      }
      rebuilt += ',';
      position = comma + 1;
      ++field;
    }
    output << rebuilt << '\n';
  }
  return true;
}

}  // namespace

/// The era-readiness score, checked on its STRUCTURE and not on its numbers
/// (boss, parcels 130 and 132): the weights and thresholds are balance and
/// will be tuned by runs, while what may never change is the shape — a
/// component the era does not have is NULL rather than nought, the index
/// divides by the weights it actually has, a component is capped at 100
/// before it is weighed, and a divisor of nought is an unanswered question
/// rather than a score of nought.
int CheckReadinessShape() {
  int failures = 0;
  const core::ReadinessCatalog empty;

  // A WORLD THAT LIVED NOTHING SCORES NOTHING, and says so by its bytes
  // rather than by zeroes. This is the case every one of these components
  // has to survive, because it is the case a young campaign IS: no plan
  // spoken of, no December, nobody able-bodied, no building standing.
  core::WorldState blank;
  core::ScoreReadiness(empty, 4.0F, 4.0F, blank);
  failures += Expect(blank.readiness.economy.plan.measured == 0,
                     "readiness: a year the district never spoke of was not MEASURED on its "
                     "plan — which is not the same as nought per cent");
  failures += Expect(blank.readiness.economy.winter_stocks.measured == 0,
                     "readiness: a year with no December was not measured on its wintering");
  failures += Expect(blank.readiness.economy.mechanisation.measured == 0,
                     "readiness: no assignment days is a divisor of nought, not a score of it");
  failures += Expect(blank.readiness.economy.funds.measured == 0,
                     "readiness: no building standing means the funds could not be asked");
  failures += Expect(blank.readiness.society.satisfaction.measured == 0,
                     "readiness: and no family means no satisfaction to average");
  // AND EVERY ONE OF THEM STAYS IN THE DIVISOR, which is the repair of
  // 2026-09-17 and the assertion that tells the two versions apart: with the
  // unmeasured dropped, this world scored nought only for want of anything to
  // divide, and a world with ONE good component scored that component. Now an
  // unmeasured component costs its weight.
  failures += Expect(blank.readiness.economy.plan.available == 1,
                     "readiness: Era I HAS a plan component even in a year it could not be "
                     "scored on — the era not having it and the year not showing it are two "
                     "different facts");
  failures += Expect(blank.readiness.economic_index == 0.0F && blank.readiness.social_index == 0.0F,
                     "readiness: a year that could show nothing scores nothing");
  failures += Expect(blank.readiness.both_above_run == 0 && blank.readiness.wintering_run == 0,
                     "readiness: and no run is begun by a year that scored nothing");

  // ONE COMPONENT AVAILABLE AND THE INDEX IS THAT COMPONENT, which is the
  // whole of "divided by what the era can have": it is NOT the component
  // divided by a hundred, and the difference is the weight of every absent
  // cell.
  core::WorldState one;
  one.ledger.closed.plan_percent = 60.0F;
  one.ledger.closed.plan_percent_known = 1;
  core::ScoreReadiness(empty, 4.0F, 4.0F, one);
  failures +=
      Expect(one.readiness.economy.plan.measured == 1 && one.readiness.economy.plan.score > 59.9F &&
                 one.readiness.economy.plan.score < 60.1F,
             "readiness: the plan of a single known year is that year");
  // THE PLAN IS 25 OF THE ERA'S 80, so one perfect-ish plan and three
  // unmeasured components is 60 x 25 / 80 — not 60. This is the assertion the
  // whole repair turns on: before it, an index over one measurable component
  // WAS that component, and a village with nothing to show scored like a
  // village that had shown everything.
  const float expected = 60.0F * 25.0F / 80.0F;
  failures += Expect(one.readiness.economic_index > expected - 0.2F &&
                         one.readiness.economic_index < expected + 0.2F,
                     "readiness: an unmeasured component keeps its weight in the divisor and "
                     "scores nought there — only a component the ERA lacks leaves it");

  // THE CEILING. Overshooting one component may not buy another, so a
  // delivery of two hundred per cent weighs as a hundred.
  core::WorldState over;
  over.ledger.closed.total_assignment_days = 10.0F;
  over.ledger.closed.horse_backed_assignment_days = 30.0F;
  core::ScoreReadiness(empty, 4.0F, 4.0F, over);
  failures += Expect(over.readiness.economy.mechanisation.score <= 100.0F,
                     "readiness: a component is capped at 100 before it is weighed");

  // THE RUNS ARE RUNS: they count consecutive years and a year that misses
  // puts them back to nought. Scored twice over a world whose wintering
  // closed, then once over one whose did not.
  core::WorldState run;
  run.ledger.closed.winter_cover_taken = 1;
  run.ledger.closed.winter_days_dec1 = 100;
  run.ledger.closed.food_days_dec1 = 120.0F;
  run.ledger.closed.feed_days_dec1 = 110.0F;
  core::ScoreReadiness(empty, 4.0F, 4.0F, run);
  core::ScoreReadiness(empty, 4.0F, 4.0F, run);
  failures += Expect(run.readiness.wintering_run == 2 && run.readiness.blocks.wintering_two_years,
                     "readiness: two closed winterings in a row open the wintering block");
  // AND ONE SHORT SIDE BREAKS IT, both halves having to reach the grass: a
  // barn full of hay does not feed the village.
  run.ledger.closed.food_days_dec1 = 90.0F;
  core::ScoreReadiness(empty, 4.0F, 4.0F, run);
  failures += Expect(run.readiness.wintering_run == 0 && !run.readiness.blocks.wintering_two_years,
                     "readiness: and a year short of food on either side puts the run back to "
                     "nought, however full the other side");

  // THE VARIETY BLOCK NEEDS ALL FOUR SEASONS LIVED. A year short of a season
  // has not failed it, it has not been asked — and the block must not open on
  // an unasked question.
  core::WorldState seasons;
  seasons.ledger.closed.worst_season_variety = 6.0F;
  seasons.ledger.closed.variety_seasons_seen = 3;
  core::ScoreReadiness(empty, 4.0F, 4.0F, seasons);
  failures += Expect(!seasons.readiness.blocks.food_variety,
                     "readiness: three rich seasons are not four, and the variety block does "
                     "not open on a year that was never asked the fourth");
  seasons.ledger.closed.variety_seasons_seen = 4;
  core::ScoreReadiness(empty, 4.0F, 4.0F, seasons);
  failures += Expect(seasons.readiness.blocks.food_variety,
                     "readiness: four seasons all above the threshold open it");
  seasons.ledger.closed.worst_season_variety = 2.0F;
  core::ScoreReadiness(empty, 4.0F, 4.0F, seasons);
  failures += Expect(!seasons.readiness.blocks.food_variety,
                     "readiness: and the WORST season decides — a rich summer does not answer "
                     "for a bare winter");
  return failures;
}

/// The level the transition requires (boss, 2026-09-18; epochs §6
/// «доведены до требуемого уровня»): the highest rung the CURRENT era has
/// opened, counted up without stepping over a gap — and the block reads it.
int CheckRequiredUnitLevel() {
  int failures = 0;
  core::ReadinessCatalog catalog;
  // 0: rung 2 in Epoch II · 1: rungs 1, 2 in Epoch I, 3 in II · 2: no ladder
  // row at all · 3: rung 2 missing from the table, rung 3 in Epoch I.
  catalog.rung_eras = {{1, 2}, {1, 1, 2}, {}, {1, 0, 1}};
  const auto required = [&catalog](std::uint16_t type, core::Epoch era) {
    return core::RequiredUnitLevel(catalog, core::UnitTypeId{type}, era);
  };
  failures += Expect(required(0, core::Epoch::kOne) == 1 && required(0, core::Epoch::kTwo) == 2,
                     "required level: a second rung of Epoch II is not asked in Epoch I, and is "
                     "in Epoch II");
  failures += Expect(required(1, core::Epoch::kOne) == 2,
                     "required level: the highest rung the era has opened, not the first");
  failures += Expect(required(2, core::Epoch::kOne) == 0 && required(9, core::Epoch::kOne) == 0,
                     "required level: no ladder, and a type past the catalogue, ask nothing");
  failures += Expect(required(3, core::Epoch::kOne) == 1,
                     "required level: a rung the table skipped is not stepped over");

  // THE BLOCK READS IT. A kolkhoz building of type 0 at level 1 is at its
  // level in Epoch I; type 1 at level 1 is not.
  catalog.kolkhoz_types = {core::UnitTypeId{0}, core::UnitTypeId{1}};
  core::WorldState world;
  core::UnitRow school;
  school.type = core::UnitTypeId{0};
  school.level = 1;
  AppendRow(world.units, school);
  core::ScoreReadiness(catalog, 4.0F, 4.0F, world);
  failures += Expect(world.readiness.blocks.units_at_level == 1,
                     "units block: a building whose second rung is the NEXT era's stands at "
                     "its level at level one");
  core::UnitRow granary;
  granary.type = core::UnitTypeId{1};
  granary.level = 1;
  AppendRow(world.units, granary);
  core::ScoreReadiness(catalog, 4.0F, 4.0F, world);
  failures += Expect(world.readiness.blocks.units_at_level == 0,
                     "units block: and one whose second rung this era opens holds it shut");
  return failures;
}

/// The chairman's order into Epoch II (order_state.h, kAdvanceEra; epochs
/// §6 and §8). Its refusal names the FIRST unmet condition: the indices,
/// then the six blocks in the order of §6's list.
int CheckTransitionOrder() {
  int failures = 0;
  core::ReadinessState open;
  open.both_above_run = 3;
  open.blocks = {.food_variety = 1,
                 .social_objects = 1,
                 .own_traction = 1,
                 .wintering_two_years = 1,
                 .units_at_level = 1,
                 .office_repaired = 1};
  // The standing three are passed apart from the year's bytes; where a test
  // is about the ORDER of the answers both carry the same, so shutting one
  // shuts it for whichever the function reads.
  const auto refusal = [](const core::ReadinessState& readiness, core::Epoch era) {
    return core::TransitionRefusal(readiness, readiness.blocks, era);
  };
  failures += Expect(refusal(open, core::Epoch::kOne) == core::OrderRefusal::kNone,
                     "transition: three years of both indices and six open blocks let it through");
  failures += Expect(refusal(open, core::Epoch::kTwo) == core::OrderRefusal::kNotEligible,
                     "transition: Epoch II has no transition in this build");

  // THE STANDING THREE ARE READ FROM THE WORLD NOW, NOT FROM THE YEAR. An
  // office repaired after the turn — the year's byte says worn, the world
  // says repaired — lets the order through; and one that wore past 1 per
  // cent since the turn refuses it, whatever the year's byte says.
  core::ReadinessState turned_worn = open;
  turned_worn.blocks.office_repaired = 0;
  failures += Expect(core::TransitionRefusal(turned_worn, open.blocks, core::Epoch::kOne) ==
                         core::OrderRefusal::kNone,
                     "transition: an office repaired since the turn opens the door now");
  core::TransitionBlocks worn_now = open.blocks;
  worn_now.office_repaired = 0;
  failures += Expect(core::TransitionRefusal(open, worn_now, core::Epoch::kOne) ==
                         core::OrderRefusal::kOfficeNotRepaired,
                     "transition: an office worn since the turn shuts it, the year's byte aside");
  core::TransitionBlocks built_now = open.blocks;
  built_now.social_objects = 0;
  built_now.units_at_level = 0;
  failures += Expect(core::TransitionRefusal(open, built_now, core::Epoch::kOne) ==
                         core::OrderRefusal::kSocialObjectsShort,
                     "transition: the social objects and the units are read now, too");
  core::ReadinessState starved = open;
  starved.blocks.food_variety = 0;
  failures += Expect(core::TransitionRefusal(starved, open.blocks, core::Epoch::kOne) ==
                         core::OrderRefusal::kFoodVarietyShort,
                     "transition: the food of the year stays the year's, the standing aside");

  // THE ORDER OF THE ANSWERS. Each step shuts one more condition at the
  // FRONT of the list, so every later one is shut too and the answer must
  // still be the first — a refusal that reported the last would pass a test
  // that shut one at a time.
  struct Step {
    void (*shut)(core::ReadinessState&);
    core::OrderRefusal expected;
    const char* label;
  };

  const std::array<Step, 7> steps = {{
      {[](core::ReadinessState& state) { state.blocks.units_at_level = 0; },
       core::OrderRefusal::kUnitsBelowLevel,
       "transition: units below their level refuse it"},
      {[](core::ReadinessState& state) { state.blocks.social_objects = 0; },
       core::OrderRefusal::kSocialObjectsShort,
       "transition: the social objects are named before the units"},
      {[](core::ReadinessState& state) { state.blocks.food_variety = 0; },
       core::OrderRefusal::kFoodVarietyShort,
       "transition: the food variety before the social objects"},
      {[](core::ReadinessState& state) { state.blocks.office_repaired = 0; },
       core::OrderRefusal::kOfficeNotRepaired,
       "transition: the office before the food"},
      {[](core::ReadinessState& state) { state.blocks.wintering_two_years = 0; },
       core::OrderRefusal::kWinteringNotClosed,
       "transition: the wintering before the office"},
      {[](core::ReadinessState& state) { state.blocks.own_traction = 0; },
       core::OrderRefusal::kNoOwnTraction,
       "transition: the traction before the wintering"},
      {[](core::ReadinessState& state) { state.both_above_run = 2; },
       core::OrderRefusal::kIndicesNotHeld,
       "transition: two years of the indices are not three, and they come first"},
  }};
  core::ReadinessState shutting = open;
  for (const Step& step : steps) {
    step.shut(shutting);
    failures += Expect(refusal(shutting, core::Epoch::kOne) == step.expected, step.label);
  }
  failures += Expect(
      refusal(core::ReadinessState{}, core::Epoch::kOne) == core::OrderRefusal::kIndicesNotHeld,
      "transition: a readiness never scored has held nothing");

  // STANDING BLOCKS OFF A WORLD. Type 0 the office, types 1..4 the era's
  // social objects, no kolkhoz ladders — so every unit stands at its level.
  core::ReadinessCatalog catalog;
  catalog.office = core::UnitTypeId{0};
  catalog.social_objects = {
      core::UnitTypeId{1}, core::UnitTypeId{2}, core::UnitTypeId{3}, core::UnitTypeId{4}};
  const auto ready_world = [&catalog] {
    core::WorldState world;
    world.readiness.both_above_run = 3;
    world.readiness.blocks.own_traction = 1;
    world.readiness.blocks.wintering_two_years = 1;
    world.readiness.blocks.food_variety = 1;
    for (std::uint16_t type = 0; type <= catalog.social_objects.size(); ++type) {
      core::UnitRow unit;
      unit.type = core::UnitTypeId{type};
      unit.level = 1;
      unit.wear = 0.5F;  // per cent: just repaired
      AppendRow(world.units, unit);
    }
    return world;
  };
  const core::WorldState fresh = ready_world();
  const core::TransitionBlocks fresh_blocks = core::StandingBlocks(catalog, fresh);
  failures += Expect(fresh_blocks.office_repaired == 1 && fresh_blocks.social_objects == 1 &&
                         fresh_blocks.units_at_level == 1,
                     "standing blocks: an office at 0.5 % and four social objects stand open");
  core::WorldState worn = ready_world();
  worn.units.rows[0].wear = 2.0F;
  worn.units.rows[4].dead = 1;
  const core::TransitionBlocks worn_blocks = core::StandingBlocks(catalog, worn);
  failures += Expect(worn_blocks.office_repaired == 0 && worn_blocks.social_objects == 0,
                     "standing blocks: an office at 2 % and a dead fourth object shut them");

  // THE CONSUMER. Two orders in one step: the first takes the village, the
  // second finds it gone and is not eligible. A cancelled one and another
  // kind are left for their own doors.
  core::WorldState world = ready_world();
  const auto order_of = [](core::OrderKind kind, core::OrderStatus status) {
    core::OrderRow row;
    row.kind = kind;
    row.status = status;
    return row;
  };
  const core::OrderId first =
      AppendRow(world.orders, order_of(core::OrderKind::kAdvanceEra, core::OrderStatus::kPending));
  const core::OrderId second =
      AppendRow(world.orders, order_of(core::OrderKind::kAdvanceEra, core::OrderStatus::kPending));
  const core::OrderId cancelled = AppendRow(
      world.orders, order_of(core::OrderKind::kAdvanceEra, core::OrderStatus::kCancelled));
  const core::OrderId other = AppendRow(
      world.orders, order_of(core::OrderKind::kGrazeAtNight, core::OrderStatus::kPending));
  core::ConsumeTransitionOrders(catalog, world);
  const auto row = [&world](core::OrderId id) -> const core::OrderRow& {
    return world.orders.rows[FindRow(world.orders, id)];
  };
  failures +=
      Expect(world.epoch == core::Epoch::kTwo && row(first).status == core::OrderStatus::kDone,
             "transition order: done, and the village is in Epoch II");
  failures += Expect(row(second).status == core::OrderStatus::kRefused &&
                         row(second).refusal == core::OrderRefusal::kNotEligible,
                     "transition order: a second in the same step finds the era already moved");
  failures += Expect(row(cancelled).status == core::OrderStatus::kCancelled &&
                         row(other).status == core::OrderStatus::kPending,
                     "transition order: a cancelled one and another kind are not touched");

  // The consumer reads the office in the WORLD: the year's byte says open,
  // the office stands at 2 per cent — refused.
  core::WorldState shut = ready_world();
  shut.readiness.blocks.office_repaired = 1;
  shut.units.rows[0].wear = 2.0F;
  const core::OrderId refused =
      AppendRow(shut.orders, order_of(core::OrderKind::kAdvanceEra, core::OrderStatus::kPending));
  core::ConsumeTransitionOrders(catalog, shut);
  const core::OrderRow& refused_row = shut.orders.rows[FindRow(shut.orders, refused)];
  failures +=
      Expect(shut.epoch == core::Epoch::kOne && refused_row.status == core::OrderStatus::kRefused &&
                 refused_row.refusal == core::OrderRefusal::kOfficeNotRepaired,
             "transition order: refused with the block's name, and the era stays");
  return failures;
}

int main() {
  namespace fs = std::filesystem;
  int failures = 0;
  failures += CheckReadinessShape();
  failures += CheckRequiredUnitLevel();
  failures += CheckTransitionOrder();

  // The wiring config must default to the deterministic verification setup:
  // no tables, seed 0, one worker (world.h).
  const core::StandardSimulationConfig config;
  failures += Expect(config.tables == nullptr, "config defaults to no tables");
  failures += Expect(config.world_seed == 0, "config defaults to seed 0");
  failures += Expect(config.worker_count == 1, "config defaults to the verification mode");

  // Genesis STUB: an empty world at day 0, deterministic from the seed.
  const test::FakeTableSet tables;
  const core::WorldState world =
      core::CreateStartWorld(tables, core::StubTables::kAllowed, nullptr, 12345, nullptr);
  failures += Expect(world.world_seed == 12345, "genesis stores the seed");
  failures += Expect(world.calendar.tick == 0, "genesis starts at tick 0");
  failures +=
      Expect(world.calendar.weekday == core::Weekday::kMonday, "stub campaign starts on Monday");
  failures += Expect(world.calendar.season == core::Season::kWinter, "day 0 is winter");
  failures += Expect(world.epoch == core::Epoch::kOne, "the campaign starts in Epoch I");
  failures += Expect((world.rng.stream & 1U) == 1U, "the world RNG is seeded (odd stream)");

  const core::WorldState same_seed =
      core::CreateStartWorld(tables, core::StubTables::kAllowed, nullptr, 12345, nullptr);
  const core::WorldState other_seed =
      core::CreateStartWorld(tables, core::StubTables::kAllowed, nullptr, 54321, nullptr);
  failures += Expect(same_seed.rng.state == world.rng.state, "same seed — same world RNG");
  failures +=
      Expect(other_seed.rng.state != world.rng.state, "different seed — different world RNG");

  // THE START TEAM IS YOUNG (boss, boss-core-epoch1-5 seq 49; 0.35.10): the
  // sixteen horses are drawn from adulthood (one year) to two years short of
  // old age (6 - 2 = 4). Drawn up to the lifespan's top, eight, one in three
  // died of age in the first year. Sixteen draws over 1..8 would put one past
  // four all but surely, so the band is a real check.
  {
    const auto shipped = core::LoadTableSet(KOLKHOZ_TABLES_DIR, nullptr);
    const core::WorldState start =
        shipped == nullptr
            ? core::WorldState{}
            : core::CreateStartWorld(*shipped, core::StubTables::kRefused, nullptr, 1929, nullptr);
    const std::uint32_t horse = shipped == nullptr
                                    ? core::kNoTableRow
                                    : shipped->FindTable("livestock")->FindRowByKey("horse");
    std::uint32_t horses = 0;
    bool young = true;
    for (const core::HerdRow& herd : start.herds.rows) {
      if (herd.kind.value != horse || herd.household_owned != 0 || herd.adult_count == 0) {
        continue;
      }
      horses += herd.adult_count;
      const float age = herd.adult_age_game_years_total / static_cast<float>(herd.adult_count);
      young = young && age >= 1.0F && age <= 4.0F;
      // AND THE BAND IS THE AGES DRAWN (0.35.16, herd_age_band.h): the age
      // death reads it, not the mean. Inside 1..4, not a point (sixteen
      // draws), and the mean inside it.
      // The start's team stands one head a yard until it is stabled
      // (stable_horses.cpp merges the bands): a one-head band is its age.
      const bool point_only_if_one =
          herd.adult_count == 1 || herd.adult_age_max_game_years > herd.adult_age_min_game_years;
      young = young && herd.adult_age_min_game_years >= 1.0F &&
              herd.adult_age_max_game_years <= 4.0F && point_only_if_one &&
              age >= herd.adult_age_min_game_years - 1.0e-4F &&
              age <= herd.adult_age_max_game_years + 1.0e-4F;
    }
    failures += Expect(horses == 16 && young,
                       "the start's sixteen horses are between one and four years old");
  }

  // THE DRINKING VILLAGE (start §13; register 223; boss seq 121): the men
  // start inside their classes' bands, 15..55; women and children at nought;
  // the village about thirty, and somebody over forty from the first day.
  {
    const auto shipped = core::LoadTableSet(KOLKHOZ_TABLES_DIR, nullptr);
    const core::WorldState start =
        shipped == nullptr
            ? core::WorldState{}
            : core::CreateStartWorld(*shipped, core::StubTables::kRefused, nullptr, 1929, nullptr);
    std::uint32_t drinkers = 0;
    std::uint32_t over_forty = 0;
    float sum = 0.0F;
    bool in_bands = true;
    for (const core::ResidentRow& person : start.residents.rows) {
      if (person.sex != core::Sex::kMale) {
        in_bands = in_bands && person.alcoholism == 0.0F;
        continue;
      }
      if (person.alcoholism == 0.0F) {
        continue;  // a boy
      }
      in_bands = in_bands && person.alcoholism >= 15.0F && person.alcoholism <= 55.0F;
      ++drinkers;
      sum += person.alcoholism;
      over_forty += person.alcoholism > 40.0F ? 1U : 0U;
    }
    const float mean = drinkers > 0 ? sum / static_cast<float>(drinkers) : 0.0F;
    failures += Expect(in_bands && drinkers > 0,
                       "drinking village: men start inside 15..55, women at nought");
    failures += Expect(mean > 25.0F && mean < 35.0F && over_forty >= 1,
                       "drinking village: the village about thirty, somebody over forty");
    // The young start a little sporty (question 225): 10..30, and some do.
    std::uint32_t sporty = 0;
    bool sport_band = true;
    for (const core::ResidentRow& person : start.residents.rows) {
      if (person.sportiness > 0.0F) {
        ++sporty;
        sport_band = sport_band && person.sportiness >= 10.0F && person.sportiness <= 30.0F;
      }
    }
    failures += Expect(sporty > 0 && sport_band,
                       "sportiness: the young founders start in 10..30, and some of them do");
  }

  // THE DIGGING PLOTS OF THE SHIPPED MAP (construction design §3; boss,
  // parcels 270 and 273): genesis makes one site a row, two each of clay,
  // stone and sand, each with its area × its material's density.
  {
    const auto shipped = core::LoadTableSet(KOLKHOZ_TABLES_DIR, nullptr);
    core::ExtractionCatalog catalog;
    std::string catalog_error;
    const bool read =
        shipped != nullptr && core::ParseExtractionCatalog(*shipped, catalog, catalog_error);
    const core::WorldState start =
        shipped == nullptr
            ? core::WorldState{}
            : core::CreateStartWorld(*shipped, core::StubTables::kRefused, nullptr, 1929, nullptr);
    std::array<std::uint32_t, core::kExtractedMaterialCount> by_material{};
    bool stocked = read && start.extraction_sites.rows.size() == catalog.sites.size();
    for (std::size_t row = 0; stocked && row < start.extraction_sites.rows.size(); ++row) {
      const core::ExtractionSiteRow& site = start.extraction_sites.rows[row];
      const core::ExtractionSiteDef& def = catalog.sites[site.table_row];
      ++by_material[static_cast<std::size_t>(def.material)];
      stocked = site.resource.value == def.resource.value &&
                site.stock_grams == core::StartStockGrams(catalog, def) && site.stock_grams > 0 &&
                site.position.x == def.position.x && site.position.y == def.position.y;
    }
    failures += Expect(stocked && start.extraction_sites.rows.size() == 6 && by_material[0] == 2 &&
                           by_material[1] == 2 && by_material[2] == 2,
                       "genesis lays the shipped map's six digging plots, two of each material, "
                       "each stocked at its area times its density");
  }

  // A POST NOBODY CAN EVER REACH IS A QUEST THAT CAN NEVER CLOSE, and one was
  // exactly that until 2026-09-17.
  //
  // `quest_e1_05` «Счетовод» closes on the clerk-accountant being appointed.
  // The appointment is gated on education (posts.cpp, IsEligible), the post
  // asked for SECONDARY, and the whole core has three writers of
  // education_stage between them: genesis and the school both stop at
  // kPrimary, and kVocational belongs to a specialist the district sends —
  // and the district sends teachers and librarians, never a clerk. So the
  // quest was shut, and its own brief said the opposite in as many words:
  // «счетовод — должность, а не диплом, и учить его в Эпохе I негде».
  //
  // The threshold is `primary` now (boss, 2026-09-17), and THIS IS THE GUARD
  // THAT WOULD HAVE SAID SO. It compares the shipped post against the
  // education the shipped START actually produces — not against a number
  // written here, which would be the same mistake one level up.
  //
  // IT IS THE OFFICE'S POST AND NOT ALL SEVENTY, deliberately. Thirty-two
  // posts stand above what a villager can reach, and that is the design
  // («должность без человека нужного уровня не работает» — education §9):
  // scarcity, not a defect. What is a defect is a post an EPOCH I QUEST
  // depends on standing there, and the office's is the one the core can name
  // on its own.
  {
    const auto shipped = core::LoadTableSet(KOLKHOZ_TABLES_DIR, nullptr);
    const core::ITable* const posts =
        shipped == nullptr ? nullptr : shipped->FindTable("professions");
    const std::uint32_t row =
        posts == nullptr ? core::kNoTableRow : posts->FindRowByKey("clerk_accountant");
    const std::uint32_t column =
        posts == nullptr ? core::kNoTableColumn : posts->FindColumn("min_education");
    // THE WORD, NOT A NUMBER. The ladder's order lives in the enum and the
    // spelling in labor_config.cpp; repeating either here would put the fact
    // in a second house. What this test needs is narrower and safe to state:
    // the start produces `primary` at best, so a threshold the start can
    // meet is `none` or `primary` and nothing else.
    const std::string_view word =
        posts == nullptr || row == core::kNoTableRow || column == core::kNoTableColumn
            ? std::string_view()
            : posts->CellText(row, column);
    const bool read = !word.empty();
    core::EducationStage best = core::EducationStage::kNone;
    if (shipped != nullptr) {
      const core::WorldState start =
          core::CreateStartWorld(*shipped, core::StubTables::kRefused, nullptr, 1929, nullptr);
      for (const core::ResidentRow& person : start.residents.rows) {
        best = person.education_stage > best ? person.education_stage : best;
      }
    }
    const bool within =
        best == core::EducationStage::kPrimary && (word == "none" || word == "primary");
    failures += Expect(read && within,
                       "the office's clerk can be appointed out of the village the start ships: a "
                       "post above the education the start produces is a quest that never closes");
  }

  // THE START LAYOUT IS PARSED BEFORE THE WORLD IS BUILT, and the refusal
  // names the row and the column.
  //
  // What stood here until 2026-09-06 measured the WARNING genesis wrote to
  // the log, and said why: "genesis has no way to refuse — it builds a world
  // and returns it". That reason is gone. The parser refuses, CreateStartWorld
  // carries the sentence out, and the check reads the sentence instead of
  // grepping a file.
  //
  // The fixture below also caught the old test in the act: its rows called
  // themselves kind 'arable', a word the shipped table has never used, and
  // the old reader placed them as fields without a murmur. A test that can
  // invent a kind is a test standing on a reader that accepts any.
  {
    const std::vector<std::string> header = {
        "key", "kind", "unit_type", "x_m", "y_m", "area_ha", "is_derelict", "meadow_kind"};
    const std::vector<std::string> good_field = {
        "field_a", "field", "", "100", "200", "7.5", "0", ""};
    // The same header with the two condition columns of the first morning.
    const std::vector<std::string> condition_header = {"key",
                                                       "kind",
                                                       "unit_type",
                                                       "x_m",
                                                       "y_m",
                                                       "area_ha",
                                                       "is_derelict",
                                                       "meadow_kind",
                                                       "start_wear_pct",
                                                       "start_dead"};
    const test::FakeTable empty({"key"}, {});

    // Each case brings its OWN subject, differing from the good row in the
    // one cell its rule is about: a fixture several rules reject cannot say
    // which one did (boss, 2026-09-04).
    struct Case {
      const char* label;
      std::vector<std::string> columns;
      std::vector<std::vector<std::string>> rows;
      const char* names_row;     ///< Substring the message must carry, "" for none.
      const char* names_column;  ///< Column the message must name.
    };

    const std::vector<Case> refused = {
        // A MISSING COLUMN, and deliberately this one. A header without
        // 'kind' or 'key' is refused even with the required-column check
        // taken out — the empty text a missing column reads as fails the
        // per-row rule next door — so a guard built on those two would pass
        // while measuring nothing. A missing 'x_m' has no such second door:
        // an absent column is an absent cell, an absent cell keeps the
        // caller's value, and the whole village is founded at the origin in
        // silence. Found by mutation, 2026-09-06.
        {"a header without 'x_m' is not a layout at all",
         {"key", "kind", "y_m", "area_ha"},
         {{"field_a", "field", "200", "7.5"}},
         "",
         "x_m"},
        {"a kind the layout does not have is refused, not read as a field",
         header,
         {{"field_a", "arable", "", "100", "200", "7.5", "0", ""}},
         "field_a",
         "kind"},
        {"'12kg' in the area is refused, not laid out as nought hectares",
         header,
         {{"field_a", "field", "", "100", "200", "12kg", "0", ""}},
         "field_a",
         "area_ha"},
        {"a negative coordinate is refused: the table's own header says never negative",
         header,
         {{"field_a", "field", "", "-100", "200", "7.5", "0", ""}},
         "field_a",
         "x_m"},
        {"two rows may not share a key: the stock table finds a unit by it",
         header,
         {good_field, {"field_a", "field", "", "300", "400", "3", "0", ""}},
         "field_a",
         "key"},
        {"a unit row must name its type",
         header,
         {{"barn", "unit", "", "100", "200", "", "0", ""}},
         "barn",
         "unit_type"},
        {"a meadow must say which kind it is: the yield differs by two thirds",
         header,
         {{"meadow_a", "meadow", "", "100", "200", "20", "0", ""}},
         "meadow_a",
         "meadow_kind"},
        // THE CONDITION COLUMNS DESCRIBE A BUILDING. A wear on a field is a
        // cell nothing reads, and the first morning's scene would come out
        // exactly as if it had been blank — the silence these two columns
        // were added to end.
        {"a field may not be worn: wear is a property of a building",
         condition_header,
         {{"field_a", "field", "", "100", "200", "7.5", "0", "", "30", "0"}},
         "field_a",
         "start_wear_pct"},
        {"a meadow may not start dead",
         condition_header,
         {{"meadow_a", "meadow", "", "100", "200", "20", "0", "upland", "-1", "1"}},
         "meadow_a",
         "start_dead"},
        // MINUS ONE IS THE ONLY NEGATIVE THE COLUMN ADMITS. Were the rule
        // written as "negative means as built", a mistyped -10 would land
        // as the same "nothing said" and the wrecked mill would come out of
        // the table looking new.
        {"a wear of -10 is a typo, not a second way of saying 'as built'",
         condition_header,
         {{"barn", "unit", "barn", "100", "200", "", "0", "", "-10", "0"}},
         "barn",
         "start_wear_pct"},
    };
    for (const Case& item : refused) {
      const test::FakeTable layout(item.columns, item.rows);
      const test::FakeTableSet set({{"start_layout", &layout},
                                    {"unit_types", &empty},
                                    {"resources", &empty},
                                    {"crops", &empty}});
      std::string error;
      const core::WorldState refused_world =
          core::CreateStartWorld(set, core::StubTables::kAllowed, nullptr, 7, &error);
      failures += Expect(!error.empty(), item.label);
      failures += Expect(error.find(item.names_column) != std::string::npos,
                         "and the refusal names the column it choked on");
      failures += Expect(*item.names_row == '\0' || error.find(item.names_row) != std::string::npos,
                         "and the row, by the key a person can find in the CSV");
      // The refusal is not decoration: nothing of the scene was placed.
      failures += Expect(refused_world.fields.rows.empty() && refused_world.units.rows.empty(),
                         "and a refused layout founds no scene at all");
    }

    // A MISSING COLUMN IS NOT A DEFECTIVE ROW. 'unit_type' and 'meadow_kind'
    // are required by the rows that need them and by no others, so their
    // refusal has to say which of the two happened — the parser was itself
    // reporting an absent column as a bad row, which is the diagnosis it
    // exists to stop (UB-006 of the cycle, in code written that morning).
    {
      const std::vector<std::string> no_type = {"key", "kind", "x_m", "y_m", "area_ha"};
      const test::FakeTable layout(no_type, {{"barn", "unit", "100", "200", ""}});
      const test::FakeTableSet set({{"start_layout", &layout},
                                    {"unit_types", &empty},
                                    {"resources", &empty},
                                    {"crops", &empty}});
      std::string error;
      core::CreateStartWorld(set, core::StubTables::kAllowed, nullptr, 7, &error);
      failures += Expect(error.find("no such column") != std::string::npos,
                         "a unit row with no unit_type COLUMN is told the column is missing");
      failures +=
          Expect(error.find("unit_type") != std::string::npos, "and the missing column is named");
    }

    // A BLANK CELL IS NOT A DEFECT, and this is the half that keeps the
    // refusals above from being a machine that refuses everything: a unit
    // row names no area, an unsown year names no crop.
    {
      const test::FakeTable layout(
          header,
          {good_field,
           {"field_blank", "field", "", "300", "400", "", "0", ""},
           {"meadow_a", "meadow", "", "500", "600", "20", "0", "floodplain"}});
      const test::FakeTableSet set({{"start_layout", &layout},
                                    {"unit_types", &empty},
                                    {"resources", &empty},
                                    {"crops", &empty}});
      std::string error;
      const core::WorldState placed =
          core::CreateStartWorld(set, core::StubTables::kAllowed, nullptr, 7, &error);
      failures += Expect(error.empty(), "a layout whose blanks are blanks is accepted");
      failures += Expect(placed.fields.rows.size() == 3,
                         "and every row of it is placed — two fields and a meadow");
      bool floodplain = false;
      for (const core::FieldRow& field : placed.fields.rows) {
        floodplain = floodplain || field.kind == core::LandKind::kFloodplainMeadow;
      }
      failures += Expect(floodplain, "and the floodplain meadow keeps its kind through the parse");
    }
  }

  // Stage-3 genesis: the designed start, deterministic from the seed.
  failures += Expect(world.residents.rows.size() == 80, "genesis seats 80 residents");
  failures += Expect(world.families.rows.size() == 21, "genesis builds 21 yards");
  failures +=
      Expect(same_seed.residents.rows.size() == world.residents.rows.size() &&
                 same_seed.residents.rows[10].birth_day == world.residents.rows[10].birth_day,
             "genesis is reproducible from the seed");
  std::uint32_t children = 0;
  std::uint32_t old_timers = 0;
  bool links_hold = true;
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const core::ResidentRow& resident = world.residents.rows[row];
    links_hold = links_hold && core::FindRow(world.families, resident.family) != core::kNoRow;
    // Life speedup 4: one biological year is 12 game days.
    const float age_years = static_cast<float>(-resident.birth_day) / 12.0F;
    children += age_years < 16.0F ? 1 : 0;
    old_timers += age_years >= 60.0F ? 1 : 0;
  }
  failures += Expect(links_hold, "every starting resident's family exists");
  failures += Expect(children >= 25 && children <= 35, "about 30 children at the start");
  failures += Expect(old_timers >= 8 && old_timers <= 14, "about 11 old-timers at the start");

  // The stage-1 criterion: the empty world ticks 10 000 steps, and the
  // result is identical with one worker and with many.
  constexpr std::uint32_t kCriterionSteps = 10000;
  core::StandardSimulationConfig config_single;
  config_single.tables = &tables;
  config_single.world_seed = 7;
  config_single.worker_count = 1;
  // THIS TEST IS THE LEGITIMATE STUB CASE AND NOW SAYS SO: its table set is
  // a fake with nothing in it, and the criterion it measures — one worker
  // equals many, bit for bit — is about the engine and not about the
  // balance. Since 2026-09-05 the silence would be a refusal instead
  // (core_tables/stub_tables.h), which is the point: a caller that has not
  // thought about its tables cannot be served another world quietly.
  config_single.stub_tables = core::StubTables::kAllowed;
  const auto single = core::CreateStandardSimulation(config_single);
  failures += Expect(single != nullptr, "a caller that allows the stub tables gets a simulation");
  {
    // And the same config WITHOUT that word is refused — the guard, and the
    // reason this test could segfault when the refusal first appeared.
    core::StandardSimulationConfig unstated = config_single;
    unstated.stub_tables = core::StubTables::kRefused;
    failures += Expect(core::CreateStandardSimulation(unstated) == nullptr,
                       "and one that does not is refused, not served the defaults in silence");
  }
  if (single == nullptr) {
    std::cout << "unit_core_world: FAILED\n";
    return failures;
  }
  for (std::uint32_t step = 0; step < kCriterionSteps; ++step) {
    single->AdvanceStep();
  }
  const core::WorldState& state = single->CompletedState();
  failures += Expect(state.calendar.tick == kCriterionSteps, "10 000 steps advance 10 000 ticks");
  const core::SimDay expected_day = kCriterionSteps / core::kTicksPerDay;
  failures += Expect(state.calendar.day == expected_day, "the day matches the tick count");
  const core::Date expected_date = core::DateFromDay(expected_day);
  failures += Expect(state.calendar.date.year == expected_date.year &&
                         state.calendar.date.month == expected_date.month,
                     "the date matches the calendar arithmetic");
  failures +=
      Expect(state.calendar.weekday == core::WeekdayFromDay(expected_day, core::Weekday::kMonday),
             "the weekday matches the calendar arithmetic");

  core::StandardSimulationConfig config_many = config_single;
  config_many.worker_count = 3;
  const auto many = core::CreateStandardSimulation(config_many);
  for (std::uint32_t step = 0; step < kCriterionSteps; ++step) {
    many->AdvanceStep();
  }
  const core::WorldState& many_state = many->CompletedState();
  failures += Expect(many_state.calendar.tick == state.calendar.tick &&
                         many_state.calendar.day == state.calendar.day &&
                         many_state.rng.state == state.rng.state &&
                         many_state.rng.stream == state.rng.stream &&
                         many_state.world_seed == state.world_seed,
                     "one worker and three workers agree after 10 000 steps");
  failures += Expect(many_state.residents.rows.size() == state.residents.rows.size() &&
                         many_state.residents.next_id_value == state.residents.next_id_value &&
                         many_state.families.rows.size() == state.families.rows.size() &&
                         (state.families.rows.empty() || many_state.families.rows[0].satisfaction ==
                                                             state.families.rows[0].satisfaction),
                     "the population and its metrics agree across worker counts");
  // NOBODY BUILDS IN THIS WORLD, and since 2026-09-14 no house comes from
  // nothing: the start's old houses fall in the fifth and sixth years and the
  // roofless leave in the cold (housing design §20; boss, parcel 257). The
  // comparison above may therefore meet an empty village; the one that
  // cannot be vacuous is the determinism run (tests/run/determinism).

  // The start's old houses begin PART WORN, and the band is the table's —
  // not a constant in genesis (task A5). Checked by moving the band: if the
  // knobs were dead, every house would come out in 25..75 regardless.
  {
    const fs::path banded = fs::temp_directory_path() / "unit_core_world_wear_band";
    fs::remove_all(banded);
    fs::copy(fs::path(KOLKHOZ_TABLES_DIR), banded, fs::copy_options::recursive);
    // REWRITTEN, not appended: the shipped table already names the band, and
    // a second row with the same key is a duplicate the loader refuses —
    // rightly, and it caught this test doing it.
    {
      std::ifstream source(banded / "construction.csv");
      std::string knobs;
      std::string knob_line;
      while (std::getline(source, knob_line)) {
        if (knob_line.rfind("old_house_wear_", 0) == 0) {
          continue;
        }
        knobs += knob_line + "\n";
      }
      source.close();
      std::ofstream(banded / "construction.csv", std::ios::trunc)
          << knobs << "old_house_wear_min,10\nold_house_wear_max,12\n";
    }
    std::string band_error;
    const auto banded_tables = core::LoadTableSet(banded.string(), &band_error);
    failures += Expect(banded_tables != nullptr, "the re-banded table set loads");
    if (banded_tables != nullptr) {
      const core::WorldState banded_world = core::CreateStartWorld(
          *banded_tables, core::StubTables::kAllowed, nullptr, 4242, nullptr);
      const core::UnitTypeId old_house{static_cast<std::uint16_t>(
          banded_tables->FindTable("unit_types")->FindRowByKey("old_house"))};
      std::uint32_t houses = 0;
      bool inside_band = true;
      bool all_equal = true;
      float first = -1.0F;
      for (const core::UnitRow& unit : banded_world.units.rows) {
        if (unit.type.value != old_house.value || unit.level == 0) {
          continue;
        }
        ++houses;
        inside_band = inside_band && unit.wear >= 10.0F && unit.wear <= 12.0F;
        first = first < 0.0F ? unit.wear : first;
        all_equal = all_equal && unit.wear == first;
      }
      failures += Expect(houses > 1, "the start has old houses to wear");
      failures += Expect(inside_band, "and their wear comes from the table's band, not from code");
      failures += Expect(!all_equal,
                         "each is drawn separately: twenty-one roofs must not fall in one night");
    }
    fs::remove_all(banded);
  }

  // THE INHERITED YARD COMES OUT OF THE TABLE AND NOT OUT OF PROSE. Until
  // the column existed the canon's worn church and half-ruined build yard
  // lived only in the design text, and the core founded them as new — which
  // is precisely what the prologue's first morning is a picture of.
  //
  // Checked against the SHIPPED tables, by key, and the numbers are read
  // back from the CSV rather than written here twice: a second copy of a
  // balance figure in a test is the drift this project keeps finding.
  {
    std::string shipped_error;
    const auto shipped = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &shipped_error);
    failures += Expect(shipped != nullptr, "the shipped table set loads");
    if (shipped != nullptr) {
      const core::ITable* const layout = shipped->FindTable("start_layout");
      const core::ITable* const types = shipped->FindTable("unit_types");
      const core::WorldState morning =
          core::CreateStartWorld(*shipped, core::StubTables::kRefused, nullptr, 4242, nullptr);
      // THE START QUEST'S FIELD (2026-09-14): exactly one field carries the
      // reserve's mark, and its area is the layout's reserve_field row.
      float layout_reserve_area = -1.0F;
      for (std::uint32_t row = 0; row < layout->RowCount(); ++row) {
        if (layout->CellText(row, layout->FindColumn("kind")) == "reserve_field") {
          layout_reserve_area =
              std::stof(std::string(layout->CellText(row, layout->FindColumn("area_ha"))));
        }
      }
      std::uint32_t reserves = 0;
      float reserve_area = 0.0F;
      for (const core::FieldRow& field : morning.fields.rows) {
        reserves += field.start_reserve;
        reserve_area += field.start_reserve != 0 ? field.area_ga : 0.0F;
      }
      failures += Expect(reserves == 1 && reserve_area == layout_reserve_area,
                         "genesis marks exactly one field, the layout's reserve, as the start's "
                         "reserve");
      // THE FIRST MORNING'S BILLET (2026-09-14; look, boss parcel 245): the
      // cows take the cattle yard's room and the rest stand on billet, and
      // the horses, with no roof of their own, are billeted whole — before
      // any step, because the prologue's first frame reads this world.
      const core::ITable* const levels = shipped->FindTable("unit_levels");
      float cattle_room = 0.0F;
      for (std::uint32_t row = 0; row < levels->RowCount(); ++row) {
        if (levels->CellText(row, levels->FindColumn("unit")) == "cattle_yard" &&
            levels->CellText(row, levels->FindColumn("level")) == "1") {
          cattle_room =
              levels->CellReal(row, levels->FindColumn("livestock_capacity_head")).value_or(0.0F);
        }
      }
      bool cows_billeted = false;
      bool horses_billeted = true;
      for (const core::HerdRow& herd : morning.herds.rows) {
        if (herd.household_owned != 0) {
          continue;
        }
        const auto heads =
            static_cast<float>(herd.adult_count + herd.juvenile_count + herd.newborn_count);
        if (herd.unit.value != core::kInvalidEntityIdValue) {
          cows_billeted =
              heads > cattle_room && static_cast<float>(herd.billeted_count) == heads - cattle_room;
        } else {
          horses_billeted = horses_billeted && static_cast<float>(herd.billeted_count) == heads;
        }
      }
      failures += Expect(cattle_room > 0.0F && cows_billeted,
                         "genesis billets the cows the cattle yard has no room for");
      failures += Expect(horses_billeted, "and the roofless start horses whole");
      const std::uint32_t wear_col = layout->FindColumn("start_wear_pct");
      failures += Expect(wear_col != core::kNoTableColumn,
                         "the shipped layout carries the first morning's wear");
      std::uint32_t worn = 0;
      bool every_one_arrived = true;
      for (std::uint32_t row = 0; row < layout->RowCount(); ++row) {
        const std::string_view kind = layout->CellText(row, layout->FindColumn("kind"));
        const std::string_view text = layout->CellText(row, wear_col);
        if (kind != "unit" || text.empty() || text == "-1") {
          continue;
        }
        ++worn;
        const float expected = std::stof(std::string(text));
        const core::UnitTypeId type{static_cast<std::uint16_t>(
            types->FindRowByKey(layout->CellText(row, layout->FindColumn("unit_type"))))};
        bool found = false;
        for (const core::UnitRow& unit : morning.units.rows) {
          found = found || (unit.type.value == type.value && unit.wear == expected);
        }
        every_one_arrived = every_one_arrived && found;
      }
      // THE COUNT IS ASSERTED TOO. Without it the loop above passes on an
      // empty table — a check that cannot fail is the shape this project
      // has caught in itself more than once.
      failures += Expect(worn >= 4, "the shipped start names several inherited buildings");
      failures += Expect(every_one_arrived,
                         "and each of them stands at the wear its row gives it, not at nought");

      // AND THE MILL IS DEAD, which no wear can say: the scale ends at "a
      // ruin that still works". Asserted by key rather than by counting
      // dead units, so that the day a second one is added this check still
      // names the one it is about.
      const std::uint32_t dead_col = layout->FindColumn("start_dead");
      failures += Expect(dead_col != core::kNoTableColumn,
                         "the shipped layout says which buildings start dead");
      std::uint32_t dead_rows = 0;
      bool every_dead_one_arrived = true;
      for (std::uint32_t row = 0; row < layout->RowCount(); ++row) {
        if (layout->CellText(row, dead_col) != "1") {
          continue;
        }
        ++dead_rows;
        const core::UnitTypeId type{static_cast<std::uint16_t>(
            types->FindRowByKey(layout->CellText(row, layout->FindColumn("unit_type"))))};
        bool found = false;
        for (const core::UnitRow& unit : morning.units.rows) {
          found = found || (unit.type.value == type.value && unit.dead == 1);
        }
        every_dead_one_arrived = every_dead_one_arrived && found;
      }
      failures += Expect(dead_rows >= 1, "the first morning has something standing dead in it");
      failures += Expect(every_dead_one_arrived,
                         "and it comes out of genesis dead rather than merely worn");
    }
  }

  // The table-value debt (phase-2 task A6), tested where it bites. The
  // loader now refuses inf and nan at the door (tables.h), so what can still
  // reach a reader is a FINITE absurdity — and genesis casts livestock ages
  // to integers. Genesis only reaches the herds with the whole table set
  // present, so the shipped tables are copied and one cell is spoiled.
  const fs::path spoiled = fs::temp_directory_path() / "unit_core_world_tables";
  fs::remove_all(spoiled);
  fs::copy(fs::path(KOLKHOZ_TABLES_DIR), spoiled, fs::copy_options::recursive);
  // NEGATIVE, not huge, and the difference is the whole point of this pass.
  // A 1e30 was refused even by genesis's own private ceiling of 1e9; MINUS
  // FIVE sailed straight through it, because that ceiling was symmetric and
  // was never a band at all — it was one number standing in for the ranges
  // of seven different columns. Since 2026-09-06 every cell genesis reads
  // declares what it may hold, out of core_catalog, and a lifetime is
  // non-negative.
  failures += Expect(SpoilColumn(spoiled / "livestock.csv", "life_game_years_max", "-5"),
                     "the livestock cell to spoil was found");
  std::string spoil_error;
  const auto spoiled_tables = core::LoadTableSet(spoiled.string(), &spoil_error);
  failures += Expect(spoiled_tables != nullptr, "the spoiled table set still loads");
  if (spoiled_tables != nullptr) {
    const core::WorldState wild = core::CreateStartWorld(
        *spoiled_tables, core::StubTables::kAllowed, nullptr, 12345, nullptr);
    failures += Expect(!wild.herds.rows.empty(),
                       "the roster reaches the herds — otherwise the check below is vacuous");
    bool ages_are_sane = true;
    for (const core::HerdRow& herd : wild.herds.rows) {
      const float age_sum = herd.adult_age_game_years_total;
      ages_are_sane = ages_are_sane && age_sum >= 0.0F && age_sum < 1e6F;
    }
    failures += Expect(ages_are_sane, "an absurd livestock cell never reaches the herd ages");
    // The control: the herds must actually be aged from that column, or the
    // check above is satisfied by a world that never read it.
    bool some_age_is_set = false;
    for (const core::HerdRow& herd : wild.herds.rows) {
      some_age_is_set = some_age_is_set || herd.adult_age_game_years_total > 0.0F;
    }
    failures += Expect(some_age_is_set,
                       "and the herds are aged at all — otherwise the check above is vacuous");
    const core::WorldState wild_again = core::CreateStartWorld(
        *spoiled_tables, core::StubTables::kAllowed, nullptr, 12345, nullptr);
    failures += Expect(wild_again.rng.state == wild.rng.state,
                       "and the fallback keeps genesis deterministic");
  }
  fs::remove_all(spoiled);

  // AND THE ASSEMBLY ACTS ON THE REFUSAL — which is a different claim from
  // "the parser refuses", and the one that was missing. Every check above
  // reads CreateStartWorld's error; not one of them would have gone red if
  // CreateStandardSimulation had read that error and built the world anyway.
  // That is the class this whole day has been about: a check that runs and
  // whose result is not used. Found by mutation, 2026-09-06.
  {
    const fs::path bad_scene = fs::temp_directory_path() / "unit_core_world_bad_layout";
    fs::remove_all(bad_scene);
    fs::copy(fs::path(KOLKHOZ_TABLES_DIR), bad_scene, fs::copy_options::recursive);

    // The control first: the SHIPPED tables must assemble, or the refusal
    // below proves nothing — a set that never assembles is refused for
    // whatever reason one likes.
    std::string clean_error;
    const auto clean_tables = core::LoadTableSet(bad_scene.string(), &clean_error);
    failures += Expect(clean_tables != nullptr, "the shipped table set loads");
    if (clean_tables != nullptr) {
      core::StandardSimulationConfig shipped;
      shipped.tables = clean_tables.get();
      shipped.world_seed = 3;
      failures += Expect(core::CreateStandardSimulation(shipped) != nullptr,
                         "and the shipped scene assembles — the control for the refusal below");
    }

    failures += Expect(SpoilColumn(bad_scene / "start_layout.csv", "x_m", "-1"),
                       "the layout column to spoil was found");
    std::string bad_error;
    const auto bad_tables = core::LoadTableSet(bad_scene.string(), &bad_error);
    failures += Expect(bad_tables != nullptr, "a layout of negative metres still LOADS");
    if (bad_tables != nullptr) {
      core::StandardSimulationConfig broken;
      broken.tables = bad_tables.get();
      broken.world_seed = 3;
      failures += Expect(core::CreateStandardSimulation(broken) == nullptr,
                         "and the assembly refuses it instead of founding the village outside "
                         "the world");
    }
    fs::remove_all(bad_scene);
  }

  // THE FIGURE OF THE VILLAGE (core_common/body.h). Eighty people who were
  // all exactly one height until 2026-09-06.
  {
    const fs::path figures = fs::temp_directory_path() / "unit_core_world_figures";
    fs::remove_all(figures);
    fs::copy(fs::path(KOLKHOZ_TABLES_DIR), figures, fs::copy_options::recursive);
    std::string figure_error;
    const auto figure_tables = core::LoadTableSet(figures.string(), &figure_error);
    failures += Expect(figure_tables != nullptr, "the tables for the figure guard load");
    if (figure_tables != nullptr) {
      const core::WorldState village =
          core::CreateStartWorld(*figure_tables, core::StubTables::kAllowed, nullptr, 777, nullptr);
      const core::WorldState same =
          core::CreateStartWorld(*figure_tables, core::StubTables::kAllowed, nullptr, 777, nullptr);
      const core::WorldState other =
          core::CreateStartWorld(*figure_tables, core::StubTables::kAllowed, nullptr, 778, nullptr);

      bool all_alike = true;
      bool inside_the_clamp = true;
      bool same_seed_agrees = true;
      bool some_seed_differs = false;
      const float first = village.residents.rows[0].height_deviation;
      // 2.5 sigma of 3.7 % is 9.25 %; nothing may stand outside it, and the
      // check is written with a hair of slack for the float arithmetic
      // rather than against an exact equality.
      constexpr float kBand = 0.0926F;
      for (std::size_t row = 0; row < village.residents.rows.size(); ++row) {
        const core::ResidentRow& person = village.residents.rows[row];
        all_alike = all_alike && person.height_deviation == first;
        inside_the_clamp = inside_the_clamp && person.height_deviation >= -kBand &&
                           person.height_deviation <= kBand;
        same_seed_agrees = same_seed_agrees &&
                           same.residents.rows[row].height_deviation == person.height_deviation;
        some_seed_differs = some_seed_differs ||
                            other.residents.rows[row].height_deviation != person.height_deviation;
      }
      failures +=
          Expect(!village.residents.rows.empty(), "the figure guard has a village to look at");
      failures += Expect(!all_alike,
                         "the starting village is not eighty people of one height — which is the "
                         "whole reason the field exists");
      // THE CUT'S OWN GUARD IS IN core_common, on two hundred thousand draws.
      // This one is about the VILLAGE, and it is worth having for that: at
      // eighty people the band is never reached, so with the cut removed this
      // line stays green — it says the shipped world is sane, not that the
      // rule works. The distinction is written down because the first version
      // of this check thought it was testing the rule.
      failures += Expect(inside_the_clamp,
                         "and the shipped village stands inside the band the knobs describe");
      failures += Expect(same_seed_agrees,
                         "the same seed gives the same village its same figures — the draw is a "
                         "function of the world, not of the order it was built in");
      failures += Expect(some_seed_differs, "and another seed gives another village");

      // THE FIGURE COSTS THE WORLD'S RNG NOTHING, and this is the assertion
      // the balance runs taught. The first version drew from the stream, and
      // four draws per person rerolled everything downstream: truancy stopped
      // happening at all and the year's labour fell under its reference band.
      // A counter hash keyed by the person consumes nothing, so the state of
      // the stream after genesis must not depend on the figure knobs at all.
      const fs::path widened = fs::temp_directory_path() / "unit_core_world_figures_wide";
      fs::remove_all(widened);
      fs::copy(fs::path(KOLKHOZ_TABLES_DIR), widened, fs::copy_options::recursive);
      // ONE ROW AND NOT THE WHOLE COLUMN. The first draft widened every
      // value in the file, which put `body_height_clamp_sigma` outside its
      // own declared range — so the read was REFUSED, the documented
      // defaults came back, and those are the shipped numbers: the guard
      // compared the village against itself. A blunt instrument gave a
      // green that meant nothing.
      failures +=
          Expect(SetKeyValue(widened / "world_params.csv", "body_height_sigma_frac", "0.02"),
                 "the sigma row to narrow was found");
      std::string wide_error;
      const auto wide_tables = core::LoadTableSet(widened.string(), &wide_error);
      if (wide_tables != nullptr) {
        const core::WorldState wider =
            core::CreateStartWorld(*wide_tables, core::StubTables::kAllowed, nullptr, 777, nullptr);
        failures += Expect(wider.rng.state == village.rng.state,
                           "changing the figure knobs does not move the world's RNG by one step: "
                           "a new fact must not move the facts that were already there");
        bool figures_did_change = false;
        for (std::size_t row = 0; row < wider.residents.rows.size(); ++row) {
          figures_did_change =
              figures_did_change || wider.residents.rows[row].height_deviation !=
                                        village.residents.rows[row].height_deviation;
        }
        failures += Expect(figures_did_change,
                           "while the figures themselves DID change — otherwise the check above "
                           "passes on knobs nobody read");
      }
      fs::remove_all(widened);
    }
    fs::remove_all(figures);
  }

  // THE START STOCK IS MEASURED THROUGH THE CONSTRUCTION DOOR, and this is
  // the check that genesis ASKS it rather than carrying a ladder of its own
  // (boss, 2026-09-06: "an instrument that re-derives a quantity does not
  // check the one it was given").
  //
  // Read from two sides on purpose. The same overfilled table is founded
  // twice: once with the door, where the excess is cut and booked to
  // lost_no_room, and once with nullptr, where there is no capacity to be
  // full against and the stock stands as written. One side alone would pass
  // just as well if genesis measured against something else entirely.
  {
    const fs::path overfilled = fs::temp_directory_path() / "unit_core_world_overfill";
    fs::remove_all(overfilled);
    fs::copy(fs::path(KOLKHOZ_TABLES_DIR), overfilled, fs::copy_options::recursive);
    // The church store holds 60 t on its only rung; the canon puts 54 t in
    // it. 135 t of potatoes instead of 35 puts it 94 t over, and nothing but
    // the ladder says so.
    {
      std::ifstream source(overfilled / "start_stock.csv");
      std::string rows;
      std::string stock_line;
      bool found = false;
      while (std::getline(source, stock_line)) {
        if (stock_line.rfind("church_store,potato,", 0) == 0) {
          rows += "church_store,potato,135,1000\n";
          found = true;
          continue;
        }
        rows += stock_line + "\n";
      }
      source.close();
      failures += Expect(found, "the row to overfill the church store was found");
      std::ofstream(overfilled / "start_stock.csv", std::ios::trunc) << rows;
    }
    std::string overfill_error;
    const auto overfilled_tables = core::LoadTableSet(overfilled.string(), &overfill_error);
    failures += Expect(overfilled_tables != nullptr, "the overfilled table set loads");
    if (overfilled_tables != nullptr) {
      const auto capacities =
          core::CreateConstructionSystem(*overfilled_tables, core::StubTables::kRefused);
      failures += Expect(capacities != nullptr, "and it builds a construction subsystem");
      const core::WorldState measured = core::CreateStartWorld(
          *overfilled_tables, core::StubTables::kAllowed, capacities.get(), 999, nullptr);
      const core::WorldState unmeasured = core::CreateStartWorld(
          *overfilled_tables, core::StubTables::kAllowed, nullptr, 999, nullptr);
      core::Grams cut = 0;
      for (const core::Grams lost : measured.ledger.current.lost_no_room) {
        cut += lost;
      }
      core::Grams cut_without = 0;
      for (const core::Grams lost : unmeasured.ledger.current.lost_no_room) {
        cut_without += lost;
      }
      failures += Expect(cut > 0, "with the door, the excess over the ladder's 60 t is cut");
      failures += Expect(cut_without == 0,
                         "without it nothing is cut — so the cut above came from the ladder and "
                         "not from a number genesis kept for itself");
    }
    fs::remove_all(overfilled);
  }

  // THE LEVEL'S STOCK SCALE (difficulty §4, boss seq 26), read from the
  // normal row genesis builds. Halved rather than raised, so the church
  // store cannot overfill and nothing but the scale moves the numbers. As a
  // PAIR: the potato halves and the hay does not — fodder is sized in weeks
  // against the first cut, not by the multiplier.
  {
    const fs::path halved = fs::temp_directory_path() / "unit_core_world_stock_scale";
    fs::remove_all(halved);
    fs::copy(fs::path(KOLKHOZ_TABLES_DIR), halved, fs::copy_options::recursive);
    {
      std::ifstream source(halved / "difficulty.csv");
      std::string rows;
      std::string level_line;
      bool found = false;
      while (std::getline(source, level_line)) {
        if (level_line.rfind("normal,", 0) == 0) {
          rows += "normal,Нормальная,0.5,0\n";
          found = true;
          continue;
        }
        rows += level_line + "\n";
      }
      source.close();
      failures += Expect(found, "the normal level's row was found");
      std::ofstream(halved / "difficulty.csv", std::ios::trunc) << rows;
    }
    std::string canon_error;
    std::string halved_error;
    const auto canon_tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &canon_error);
    const auto halved_tables = core::LoadTableSet(halved.string(), &halved_error);
    failures += Expect(canon_tables != nullptr && halved_tables != nullptr, "both table sets load");
    if (canon_tables != nullptr && halved_tables != nullptr) {
      const core::WorldState canon =
          core::CreateStartWorld(*canon_tables, core::StubTables::kAllowed, nullptr, 999, nullptr);
      const core::WorldState scaled =
          core::CreateStartWorld(*halved_tables, core::StubTables::kAllowed, nullptr, 999, nullptr);
      const core::ITable* const resources = canon_tables->FindTable("resources");
      const auto held = [&](const core::WorldState& world, std::string_view key) {
        const std::uint32_t index = resources->FindRowByKey(key);
        core::Grams sum = 0;
        for (const core::UnitRow& unit : world.units.rows) {
          sum += index < unit.stock.size() ? unit.stock[index] : 0;
        }
        return sum;
      };
      const core::Grams potato = held(canon, "potato");
      failures += Expect(potato > 0 && held(scaled, "potato") * 2 == potato,
                         "a stock_scale of 0.5 lays half the church's potato");
      const core::Grams hay = held(canon, "hay");
      failures += Expect(hay > 0 && held(scaled, "hay") == hay,
                         "and not half the hay: fodder is not scaled by the level");
    }
    fs::remove_all(halved);
  }

  // EVERY TABLE OF THE SHIPPED SET, TAKEN OUT ONE AT A TIME. The order this
  // answers (boss, 2026-09-07) came from `host`, who removed alarms.csv to
  // test an instrument of his own and found that the run did not refuse —
  // it converged, printed plausible numbers and said nothing.
  //
  // WHY THE LIST HERE IS OF TABLES THE CORE DOES NOT READ, and not of the
  // ones it does. A list of the required would be a third home for a rule
  // that already has two (the module lists and the readers themselves), and
  // it would age towards agreeing with them. This one ages the other way: a
  // NEW file in tables/ is red until somebody says which side it is on, and
  // a table that stops being required is red as well. Neither can happen in
  // silence, which is the whole complaint.
  {
    // Read by the host, the layer, or nobody yet — the core never asks for
    // them, so their absence cannot change a single number it computes.
    // tree_species LEFT this list at save 82: the planting reads it;
    // difficulty at 0.34.45: genesis scales the start stock by it.
    const std::array<std::string_view, 8> not_read_by_the_core = {"alarms",
                                                                  "diseases",
                                                                  "disease_severity",
                                                                  "event_sites",
                                                                  "farm_health_bands",
                                                                  "forest_biome_mix",
                                                                  "forest_biomes",
                                                                  "forest_forage"};
    const std::array<std::string_view, 2> also_not_read = {"resident_activities",
                                                           "resident_activity_details"};
    // AND ONE THAT IS NOT "NEVER READ" BUT "NOT READ YET", which is a
    // different thing and is kept apart on purpose: a table on the list above
    // is one the core has no business with, and a table here is one whose
    // reader is named and not built. Collapsing the two would make the day
    // the reader arrives invisible.
    //
    // `roads` (2026-09-17): 2658 points, thirteen roads and two paths, the
    // drawn line rather than the database's waypoints. Its reader is the road
    // access check of unit rules §12, and that check waits on a channel for
    // placing a LINEAR unit, which the core does not have — an order carries
    // one position, a unit row holds one position, and a plot is a disc.
    const std::array<std::string_view, 1> not_read_yet = {"roads"};
    const fs::path doctored = fs::temp_directory_path() / "unit_core_world_missing_table";
    for (const fs::directory_entry& file : fs::directory_iterator(fs::path(KOLKHOZ_TABLES_DIR))) {
      if (file.path().extension() != ".csv") {
        continue;
      }
      const std::string name = file.path().stem().string();
      const bool ignored =
          std::ranges::find(not_read_by_the_core, name) != not_read_by_the_core.end() ||
          std::ranges::find(also_not_read, name) != also_not_read.end() ||
          std::ranges::find(not_read_yet, name) != not_read_yet.end();
      fs::remove_all(doctored);
      fs::copy(fs::path(KOLKHOZ_TABLES_DIR), doctored, fs::copy_options::recursive);
      fs::remove(doctored / file.path().filename());
      const auto set = core::LoadTableSet(doctored.string(), nullptr);
      if (Expect(set != nullptr, "a set with one file taken out still loads as tables") != 0) {
        continue;
      }
      core::StandardSimulationConfig probe_config;
      probe_config.tables = set.get();
      probe_config.world_seed = 1929;
      probe_config.worker_count = 1;
      // The refusal is the subject, so this is the strict word — the one a
      // game, a run and the ledger tool all use.
      probe_config.stub_tables = core::StubTables::kRefused;
      const bool assembled = core::CreateStandardSimulation(probe_config) != nullptr;
      const std::string label =
          ignored ? "a table the core never reads does not stop the assembly (" + name +
                        ") — if it is read now, it belongs on the other side of that list"
                  : "the assembly refuses a set missing a table the core reads (" + name + ")";
      failures += Expect(ignored == assembled, label.c_str());
    }
    fs::remove_all(doctored);
  }

  if (failures == 0) {
    std::cout << "unit_core_world: all checks passed\n";
  }
  return failures;
}
