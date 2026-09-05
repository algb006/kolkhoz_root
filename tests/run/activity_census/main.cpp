// Simulation run: the roll-call of resident activities, and the one probe a
// DERIVED value cannot do without.
//
// TWO QUESTIONS, AND THE SECOND IS THE PRICE OF THE FIRST.
//
// 1. WHICH ACTIVITIES NEVER HAPPEN. Five of the fourteen have no source in
//    the model and are stubbed on purpose (resident_activity.h). A stub that
//    silently never fires is indistinguishable from one that is broken, so
//    the roster is walked and the ones no hour of the run ever reached are
//    named. A DEAD ACTIVITY IS NOT AN ERROR while it is deliberately dead —
//    the error is not knowing that it is.
//
//    The same shape as scripts/event_sites.py, and for the same reason: the
//    list comes from the ENUM and not from what the run happened to produce,
//    because a list built from the output can only ever confirm itself.
//
// 2. DOES IT SURVIVE A SAVE. The activity is derived rather than stored,
//    which buys a save format that does not move and costs one requirement:
//    everything it is derived FROM has to be in the save. Boss named the two
//    it could fail on — a truancy decided in the morning and thrown away, an
//    absence imposed from outside — and the probe that catches either is the
//    same one: save in the middle of a day, load it back, derive again, and
//    compare every resident. A derived value has no "before" of its own, so
//    a spoiled one has nothing to be compared against and simply looks
//    right; this run is that missing second half.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
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
#include "core_common/world_state.h"
#include "core_save/save.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {

/// FIVE, AND THE FIFTH IS THE ONE THAT MATTERS. Three years was enough
/// until the day-rotating queue landed: work spread over four times as many
/// hands stopped exhausting anybody, and truancy — a walk-off from fatigue —
/// went to zero. The roll-call refused the run at once, which is what it is
/// for, and the honest answer was not to waive the state but to LOOK LONGER:
/// on the shipped tables the first walk-offs appear in the fifth year, and
/// they run to two thousand man-hours by the ninth (tests/run/idle_curve).
///
/// A window too short to reach a state is the same defect as a list built
/// from the output: both can only confirm what they already contain.
constexpr std::uint32_t kYears = 5;

/// The names, in enum order, for the roll-call to print. Kept beside the
/// enum rather than read from the table on purpose: the roster the check
/// walks must be the CORE's, or a table missing a row would quietly shrink
/// the roll-call instead of failing it.
constexpr std::array<std::string_view,
                     static_cast<std::size_t>(core::ResidentActivity::kResidentActivityCount)>
    kNames = {"treated",
              "away",
              "truant",
              "blocked",
              "idle",
              "working",
              "walking",
              "eating",
              "studying",
              "lph",
              "resting",
              "at_home"};

/// Deliberately silent today, with the reason: no source in the model, and
/// the stub says so in resident_activity.h one by one.
///
/// TRUANCY CAME OFF THIS LIST ON 2026-09-05, and the check is why. It was
/// waived as "an event of the step, not a state of the world" — and that
/// was true of the EVENT and false of the world, which carries the hours a
/// man spent away and the rest he broke off at. The waiver was a reason not
/// to look. The moment truancy started firing the census refused the run,
/// because a state that is both waived and happening means one of the two
/// is out of date; that refusal is what this line records.
constexpr std::array<std::string_view, 3> kWaived = {"treated", "away", "resting"};

/// THE OPEN QUESTION THIS CHECK RAISED IS CLOSED, and the way it closed is
/// the reason to keep the check. It found not_worker and too_young
/// unreachable — at_home outranked them and swallowed everybody not out
/// working — and boss did not reorder the priorities. He took both states
/// OUT: they were answering "why is the idleness signal not charged to him"
/// inside a list that answers "what is he doing", one word for two
/// questions. The condition moved to kIdle, at_home moved to last, and the
/// roster went from fourteen to twelve.
///
/// So the probe asked for as a guard against a dead STUB found a dead
/// REGISTRY instead, on its first run.

bool Waived(std::string_view name) {
  return std::ranges::find(kWaived, name) != kWaived.end();
}

/// The rules, with the speed-up read from the SAME table the world was built
/// from.
///
/// THIS RUN IS WHY THE PARAMETER IS GONE. It used to compute the age itself
/// — calendar years, against thresholds written in biological ones — and
/// with a speed-up of four that read a village of adults as a village of
/// children: every idleness number it published was twenty-five times too
/// small, and it took host's own measurement to see it. The comment above
/// the arithmetic said "the exact speed-up belongs to the configs that own
/// it", which was true, and was the excuse.
core::ActivityRules RulesOfRun(const core::ITableSet& tables) {
  core::ActivityRules rules;
  rules.travel_hours = 0.5F;  // the run's own convention; see the census note
  const core::ITable* const life = tables.FindTable("life");
  const std::uint32_t row =
      life == nullptr ? core::kNoTableRow : life->FindRowByKey("life_speedup");
  const std::uint32_t column = life == nullptr ? core::kNoTableColumn : life->FindColumn("value");
  if (row != core::kNoTableRow && column != core::kNoTableColumn) {
    rules.life_speedup = std::strtof(std::string(life->CellText(row, column)).c_str(), nullptr);
  }
  return rules;
}

}  // namespace

int main() {
  int failures = 0;
  const run::Simulation world = run::Start(1930);
  if (!world) {
    return 1;
  }
  const core::ActivityRules rules = RulesOfRun(*world.tables);

  // THE ROLL-CALL WATCHES A VILLAGE THAT WORKS, and until 2026-09-05 it did
  // not. With no chairman the draught horses die out, every arable field
  // freezes in its ploughing, and the place stops being a farm — a poor
  // world in which to ask whether every activity has a source, because half
  // of them stop having one for reasons that are not about the roster.
  // These are the four the thirty-year run uses.
  run::YardPolicy yard(*world.tables);
  run::FixturePolicy fixture(*world.tables);
  run::OrdersPolicy orders;
  run::RepairPolicy repairs(*world.tables);

  std::array<std::uint64_t, kNames.size()> seen{};
  for (std::uint32_t day = 0; day < kYears * core::kDaysPerYear; ++day) {
    yard.RunDay(*world.simulation);
    fixture.RunDay(*world.simulation);
    orders.RunDay(*world.simulation);
    repairs.RunDay(*world.simulation);
    for (std::uint32_t tick = 0; tick < core::kTicksPerDay; ++tick) {
      world->AdvanceStep();
      const core::WorldState& state = world.State();
      for (std::uint32_t row = 0; row < state.residents.rows.size(); ++row) {
        const core::ResidentActivityState answer = core::ActivityOfResident(state, row, rules);
        ++seen[static_cast<std::size_t>(answer.activity)];
      }
    }
  }

  std::cout << "activity_census: " << kYears << " years, seed 1930\n";
  for (std::size_t index = 0; index < kNames.size(); ++index) {
    const bool dead = seen[index] == 0;
    const char* mark = "        ";
    if (dead) {
      mark = Waived(kNames[index]) ? "молчит  " : "МЁРТВОЕ ";
    }
    std::cout << "activity_census:   " << mark << kNames[index] << ' ' << seen[index]
              << " человеко-часов\n";
    if (dead && !Waived(kNames[index])) {
      failures += run::Expect(false, "an activity nothing waived never happened");
    }
    if (!dead && Waived(kNames[index])) {
      // A waived state that DID fire is the other half of the same check:
      // either it gained a source and the waiver is stale, or it is firing
      // on a guess. Both are worth stopping for.
      failures += run::Expect(false, "a waived activity happened after all — the waiver is stale");
    }
  }

  // -- the save probe -------------------------------------------------------
  const std::filesystem::path file =
      std::filesystem::temp_directory_path() / "activity_census.save";
  std::string error;
  std::vector<core::ResidentActivityState> before;
  const core::WorldState& live = world.State();
  for (std::uint32_t row = 0; row < live.residents.rows.size(); ++row) {
    before.push_back(core::ActivityOfResident(live, row, rules));
  }
  if (!core::SaveWorldToFile(live, *world.tables, file.string(), &error)) {
    std::cout << "FAIL: the world did not save (" << error << ")\n";
    return failures + 1;
  }
  core::WorldState loaded;
  if (!core::LoadWorldFromFile(file.string(), *world.tables, &loaded, &error)) {
    std::cout << "FAIL: the save did not load back (" << error << ")\n";
    return failures + 1;
  }
  std::uint32_t moved = 0;
  for (std::uint32_t row = 0; row < loaded.residents.rows.size() && row < before.size(); ++row) {
    const core::ResidentActivityState after = core::ActivityOfResident(loaded, row, rules);
    moved += after.activity != before[row].activity || after.detail != before[row].detail ? 1U : 0U;
  }
  std::cout << "activity_census: saved mid-day and loaded back — " << moved << " of "
            << before.size() << " residents changed what they were doing\n";
  failures += run::Expect(loaded.residents.rows.size() == before.size(),
                          "the save brings back every resident it took");
  failures += run::Expect(moved == 0,
                          "and every one of them is doing the same thing afterwards: a derived "
                          "value survives a save only if everything under it does");
  std::filesystem::remove(file);

  std::cout << (failures == 0 ? "activity_census: all checks passed\n"
                              : "activity_census: FAILED\n");
  return failures;
}
