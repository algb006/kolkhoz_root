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

#include "../common/building_chairman.h"
#include "../common/orders_policy.h"
#include "../common/run_harness.h"
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
///
/// AND SIX SINCE 2026-09-12, for the second time and by the same reasoning.
/// The day's work queue stopped ranking work whose window had closed above
/// work whose window was still open (assignment.h, the three tiers), the
/// hands spread differently again, and the first walk-offs moved from the
/// fifth year to the sixth: measured on tests/run/idle_curve, 0 man-hours of
/// truancy through year five and 1198 in year six. The waiver was not
/// touched — the state is reachable, the window was short.
///
/// AND TEN SINCE 2026-09-14, the third time, by boss's rule for it (parcel
/// 306): lengthen the window before touching the waiver. The core stopped
/// raising houses from nothing and the census now plays the building chairman
/// (building_chairman.h); in six years nobody walked off.
constexpr std::uint32_t kYears = 10;

/// THE CENSUS'S WORLDS: NINE SEEDS, 1929-1937, SINCE 2026-09-14 (boss, parcel
/// 314). A state counts as dead only if it happens on NONE of them. The history
/// below is why: with one seed, the seed had to move every time the world did.
/// An argument runs one seed alone, for looking.
constexpr std::uint64_t kFirstSeed = 1929;
constexpr std::uint64_t kSeedCount = 9;

/// The history of the single seed, kept for the reason above.
///
/// 1931 SINCE 2026-09-14, and 1930 before it, by boss's rule (parcel 306):
/// ten years on 1930 still showed no walk-off, so the census takes the first
/// of the nine seeds on which one happens. In ten years with the building
/// chairman truancy came on 1931 (509 man-hours), 1932 (1370) and 1936 (4563),
/// and on none of the other six. The state is reachable; it is rarer than it
/// was, and the six seeds without it are a number for the building chain.
///
/// AND 1932 THE SAME NIGHT, by the same rule, once the meadow cut rode in its
/// working hour and took one horse for the brigade (parcel 312): on 1931 the
/// walk-offs went, and of the nine seeds truancy came on 1932 (14 man-hours),
/// 1933 (3799) and 1937 (3298). A census whose seed moves with every change of
/// the world is a finding in itself, and it is put to boss — who answered with
/// the nine seeds.

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
///
/// AND TREATED CAME OFF IT ON 2026-09-15 THE SAME WAY. It stood as "no source"
/// while its source was there all along — health below `treated_health` —
/// and simply had not been reached on these nine seeds. The night trades'
/// lots moved the stream, a resident on one seed fell under the line, and the
/// census refused the waiver. The state is reachable; the waiver was luck —
/// and so is its absence, which is why it is RARE below and not demanded.
constexpr std::array<std::string_view, 2> kWaived = {"away", "resting"};

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

/// RARE: reachable, and reached or not by the stream alone. Neither half of
/// the check fits it — waived, it failed the day a seed reached it; demanded,
/// it failed the day the start's night trades moved the stream and no seed
/// did (2026-09-15, both within one afternoon). It is printed and asserted
/// neither way; its source is named in resident_activity.h.
constexpr std::array<std::string_view, 1> kRare = {"treated"};

bool Rare(std::string_view name) {
  return std::ranges::find(kRare, name) != kRare.end();
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
  // The two road rates, from the same table the labour model reads them
  // from. A run that made one up would be measuring its own invention: the
  // half-hour that used to stand here counted a man four hours from his
  // field as working.
  const core::ITable* const transport = tables.FindTable("transport");
  const std::uint32_t speed_column =
      transport == nullptr ? core::kNoTableColumn : transport->FindColumn("speed_kmh");
  const auto rate = [&](std::string_view key, float fallback) {
    const std::uint32_t row =
        transport == nullptr ? core::kNoTableRow : transport->FindRowByKey(key);
    if (row == core::kNoTableRow || speed_column == core::kNoTableColumn) {
      return fallback;
    }
    const float kmh =
        std::strtof(std::string(transport->CellText(row, speed_column)).c_str(), nullptr);
    // Real km/h against game hours: the clock runs four times faster.
    return kmh > 0.0F ? core::kClockScale / kmh : fallback;
  };
  rules.walk_hours_per_km = rate("pedestrian", 2.4F);
  rules.harness_hours_per_km = rate("horse_trot", 1.0F);
  const core::ITable* const life = tables.FindTable("life");
  const std::uint32_t row =
      life == nullptr ? core::kNoTableRow : life->FindRowByKey("life_speedup");
  const std::uint32_t column = life == nullptr ? core::kNoTableColumn : life->FindColumn("value");
  if (row != core::kNoTableRow && column != core::kNoTableColumn) {
    rules.life_speedup = std::strtof(std::string(life->CellText(row, column)).c_str(), nullptr);
  }
  return rules;
}

int CensusOfSeed(std::uint64_t seed, bool last, std::array<std::uint64_t, kNames.size()>& seen);

}  // namespace

int main(int argc, char** argv) {
  int failures = 0;
  const std::uint64_t first_seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : kFirstSeed;
  const std::uint64_t seed_count = argc > 1 ? 1U : kSeedCount;
  std::array<std::uint64_t, kNames.size()> seen{};
  for (std::uint64_t seed = first_seed; seed < first_seed + seed_count; ++seed) {
    const bool last = seed + 1 == first_seed + seed_count;
    failures += CensusOfSeed(seed, last, seen);
  }

  std::cout << "activity_census: " << kYears << " years on " << seed_count << " seeds from "
            << first_seed << "\n";
  for (std::size_t index = 0; index < kNames.size(); ++index) {
    const bool dead = seen[index] == 0;
    const char* mark = "        ";
    if (dead) {
      mark = Waived(kNames[index]) ? "молчит  " : "МЁРТВОЕ ";
    }
    std::cout << "activity_census:   " << mark << kNames[index] << ' ' << seen[index]
              << " человеко-часов\n";
    if (Rare(kNames[index])) {
      continue;  // printed above, asserted neither way
    }
    if (dead && !Waived(kNames[index])) {
      failures += run::Expect(false, "an activity nothing waived never happened on any seed");
    }
    if (!dead && Waived(kNames[index])) {
      // A waived state that DID fire is the other half of the same check:
      // either it gained a source and the waiver is stale, or it is firing
      // on a guess. Both are worth stopping for.
      failures += run::Expect(false, "a waived activity happened after all — the waiver is stale");
    }
  }
  if (failures == 0) {
    std::cout << "activity_census: all checks passed\n";
  }
  return failures;
}

namespace {

/// One seed's census, added to `seen`; on the `last` seed the save probe too.
/// @return The failures of the save probe.
int CensusOfSeed(std::uint64_t seed, bool last, std::array<std::uint64_t, kNames.size()>& seen) {
  int failures = 0;
  const run::Simulation world = run::Start(seed);
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
  // And since the core raises no house from nothing, the rest of the building
  // chairman (building_chairman.h): without it the village leaves in its
  // first winters and half the roll-call has nobody to answer it.
  run::BuildingChairman builder(*world.tables);
  if (last) {
    run::BuildingChairman::Declare("activity_census");  // once, not nine times
  }
  run::OrdersPolicy orders;

  for (std::uint32_t day = 0; day < kYears * core::kDaysPerYear; ++day) {
    builder.RunDay(*world.simulation);
    orders.RunDay(*world.simulation);
    for (std::uint32_t tick = 0; tick < core::kTicksPerDay; ++tick) {
      world->AdvanceStep();
      const core::WorldState& state = world.State();
      for (std::uint32_t row = 0; row < state.residents.rows.size(); ++row) {
        const core::ResidentActivityState answer = core::ActivityOfResident(state, row, rules);
        ++seen[static_cast<std::size_t>(answer.activity)];
      }
    }
  }

  if (!last) {
    return failures;
  }

  // -- the save probe, on the last seed's world -----------------------------
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
  return failures;
}

}  // namespace
