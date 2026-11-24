// A GAME LOADED MID-DAY IS THE CONTINUED GAME (save 229; #2072, boss 24 Nov).
//
// The day's index is built at the morning from the trails and yesterday's walks; the day's
// tread waits in WorldState::walks.pending until the next morning. Both are saved, so the
// loader's index is the morning's: a save at any hour, loaded and stepped on, must leave the
// very world the unsaved game leaves. Before 229 it could not: the tread landed on the trails
// at hour 0 and the loader re-built the index from the trodden trails, and the planner's
// priced walks lived nowhere a save reached.
//
// WHAT IS COMPARED: the whole world through the save codec, byte for byte (determinism's
// rule: a summary agrees on worlds that differ where it does not look).
// WHEN: a year and a half in (trails and walks exist), at hour 7 - after the morning's
// placement, before the evening; then ten more days on both.

#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "../common/run_harness.h"
#include "core_common/calendar.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"
#include "core_save/save.h"

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

std::size_t FirstDifference(const std::vector<std::byte>& left,
                            const std::vector<std::byte>& right) {
  const std::size_t shorter = left.size() < right.size() ? left.size() : right.size();
  for (std::size_t index = 0; index < shorter; ++index) {
    if (left[index] != right[index]) {
      return index;
    }
  }
  return shorter;
}

}  // namespace

int main() {
  constexpr std::uint32_t kDaysBefore = core::kDaysPerYear + (core::kDaysPerYear / 2U);
  constexpr std::uint32_t kSaveHour = 7;
  constexpr std::uint32_t kDaysAfter = 10;
  constexpr std::uint64_t kSeed = 1931;
  int failures = 0;

  run::Simulation continued = run::Start(kSeed, 1);
  run::Simulation loaded = run::Start(kSeed, 1);
  if (!continued || !loaded) {
    return 1;
  }
  run::AdvanceDays(*continued, kDaysBefore);
  // THE FIRST DAY WITH ALL THREE TO LOSE, within a year (the first run saved a July day: its
  // field crews ride the brigade cart, so nobody the accountant placed trod anything).
  std::uint32_t save_day = kDaysBefore;
  // Working heads, and how many of them walk (WorkRidesOut false: the tread's own predicate).
  const auto working = [](const core::WorldState& world) {
    std::size_t count = 0;
    std::size_t walking = 0;
    for (const core::ResidentRow& person : world.residents.rows) {
      if (person.work.kind == core::WorkKind::kNone) {
        continue;
      }
      ++count;
      walking += core::WorkRidesOut(world, person.work) ? 0U : 1U;
    }
    return std::to_string(count) + " (walking " + std::to_string(walking) + ")";
  };
  for (std::uint32_t search = 0; search < core::kDaysPerYear; ++search) {
    for (std::uint32_t tick = 0; tick < kSaveHour; ++tick) {
      continued->AdvanceStep();
      // THE SEARCH'S OWN PRINT (the guard found nothing on two runs): what each early hour
      // holds, the first five days.
      if (search < 5) {
        const core::WorldState& hour = continued.State();
        std::cout << "save_continue: day " << save_day << " after tick " << tick << " working "
                  << working(hour) << " today " << hour.walks.today.size() << " yesterday "
                  << hour.walks.yesterday.size() << " pending " << hour.walks.pending.size()
                  << " trails " << hour.trails.rows.size() << '\n';
      }
    }
    const core::WorldState& now = continued.State();
    if (!now.trails.rows.empty() && !now.walks.pending.empty() && !now.walks.yesterday.empty()) {
      break;
    }
    for (std::uint32_t tick = kSaveHour; tick < core::kTicksPerDay; ++tick) {
      continued->AdvanceStep();
    }
    ++save_day;
  }
  const core::WorldState& at_save = continued.State();
  // NOT A VACUOUS SAVE: the day has trails, a pending tread and priced walks to lose.
  const std::size_t trails = at_save.trails.rows.size();
  const std::size_t pending = at_save.walks.pending.size();
  const std::size_t walks = at_save.walks.yesterday.size();
  failures += Expect(trails > 0 && pending > 0 && walks > 0,
                     "the save carries trails, a pending tread and yesterday's walks - a save "
                     "with none of them would pass for the wrong reason");
  std::cout << "save_continue: at the save - trails " << trails << ", pending passes " << pending
            << ", yesterday's walks " << walks << ", residents at work " << working(at_save)
            << '\n';

  const std::vector<std::byte> bytes = core::EncodeWorld(at_save, *continued.tables);
  core::WorldState decoded;
  std::string error;
  const bool read = core::DecodeWorld(bytes, *loaded.tables, &decoded, &error);
  failures += Expect(read, "the mid-day save decodes");
  if (!read) {
    std::cout << "   " << error << '\n';
    return failures;
  }
  loaded->ResetWorld(decoded);

  run::AdvanceDays(*continued, kDaysAfter);
  run::AdvanceDays(*loaded, kDaysAfter);
  const std::vector<std::byte> left = core::EncodeWorld(continued.State(), *continued.tables);
  const std::vector<std::byte> right = core::EncodeWorld(loaded.State(), *loaded.tables);
  const bool same = left == right;
  failures += Expect(same,
                     "loaded at hour 7 and stepped ten days, the world is the continued "
                     "one byte for byte");
  if (!same) {
    std::cout << "   sizes " << left.size() << " / " << right.size() << ", first difference at "
              << FirstDifference(left, right) << '\n';
  }
  if (failures == 0) {
    std::cout << "save_continue: saved day " << save_day << " hour " << kSaveHour << " (" << trails
              << " trails, " << pending << " pending passes, " << walks << " walks), " << kDaysAfter
              << " days on: " << left.size() << " bytes identical\n";
  }
  return failures;
}
