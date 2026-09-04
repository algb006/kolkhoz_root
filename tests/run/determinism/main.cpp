// THE LOCK ON HALF THE PHASE GATE.
//
// "One thread and many threads give the same result" is a hard requirement
// (CLAUDE.md §10) and half the machine-checkable criterion of phase two. Until
// this run existed NOTHING CHECKED IT. Nobody had lied: everyone had read the
// line in the plan and taken it for a thing that happens. The seventh
// reconciliation pass measured it by hand, found it whole — and a measurement
// made by hand is one somebody eventually does not make.
//
// A gate written down in a plan is not yet a check.
//
// WHAT IS COMPARED. The whole world, encoded by the save codec, byte for
// byte. Not a summary and not a set of totals: a summary is a hash somebody
// chose, and it agrees on two worlds that differ in what it does not cover.
// The save format carries every row the simulation owns, which is exactly the
// set of things a race could disturb.
//
// HOW LONG. Two game years — long enough for the parallel phases to have real
// work (a hundred residents, twenty fields, herds in two places) and short
// enough to sit in ctest beside everything else.
//
// THE LENGTH IS MEASURED, NOT GUESSED. Determinism was broken on purpose — a
// parallel phase made to depend on which worker got the range — and the check
// caught it: ONE year already fails the byte comparison, two fail the size
// comparison as well. Two is the margin, not the minimum. A lock nobody has
// tried to pick is a sign saying "locked".

#include <cstdint>
#include <iostream>
#include <vector>

#include "../common/run_harness.h"
#include "core_common/calendar.h"
#include "core_save/save.h"

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

/// Where two encodings first differ, for a message that says something.
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
  constexpr std::uint32_t kYears = 2;
  constexpr std::uint32_t kDays = kYears * core::kDaysPerYear;
  constexpr std::uint64_t kSeed = 1929;
  int failures = 0;

  run::Simulation one = run::Start(kSeed, 1);
  run::Simulation many = run::Start(kSeed, 4);
  if (one.simulation == nullptr || many.simulation == nullptr) {
    return 1;
  }
  run::AdvanceDays(*one, kDays);
  run::AdvanceDays(*many, kDays);

  const std::vector<std::byte> single = core::EncodeWorld(one.State(), *one.tables);
  const std::vector<std::byte> parallel = core::EncodeWorld(many.State(), *many.tables);

  failures += Expect(!single.empty(), "the single-threaded run encodes to something");
  failures +=
      Expect(single.size() == parallel.size(), "one worker and four leave worlds of the same size");
  const bool same = single == parallel;
  failures += Expect(same, "and of the same bytes: one thread and four are the same run");
  if (!same) {
    const std::size_t at = FirstDifference(single, parallel);
    std::cout << "   first difference at byte " << at << " of " << single.size() << ", after "
              << kDays << " days\n";
  }

  // The population is printed so that a green run still says what it ran on:
  // a check that compares two empty worlds passes for the wrong reason, and
  // this is the number that would give that away.
  failures += Expect(one.State().residents.rows.size() > 50,
                     "and the world it agreed on has a village in it, not nobody");
  if (failures == 0) {
    std::cout << "determinism: " << kYears << " years, " << one.State().residents.rows.size()
              << " residents, " << single.size() << " bytes identical on 1 and 4 workers\n";
  }
  return failures;
}
