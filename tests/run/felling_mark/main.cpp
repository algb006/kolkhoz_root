// Focused check: which stand the run's chairman marks (run::PickMarkStand,
// tests/run/common/felling_policy.h; #1205 (a), boss-core-1205-birth-prints
// [23]).
//
// THE NEAREST CRUMB HELD THE VILLAGE: on 0.37.347, nine villages x 33 years,
// the nearest manned stand won however little it held, and from year 14 the
// marked stand held 7-10 m3 while 8 470 m3 stood over 97 stands. The known
// direction (boss [23]): a 9 m3 crumb at 2.5 km against a full stand at 3 km
// marks the full one.
//
// Usage: felling_mark (no arguments).

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

#include "../common/felling_policy.h"

namespace {

int Expect(bool ok, const char* what) {
  if (!ok) {
    std::cerr << "FAIL: " << what << '\n';
    return 1;
  }
  return 0;
}

int CheckTheFullStandBeatsTheNearerCrumb() {
  // Wanted 150 logs; the crumb's 9 m3 yields 5, the full stand's mark 150.
  const std::vector<run::MarkOffer> offers = {
      {.row = 7, .distance_m = 2500.0F, .volume_m3 = 9.0F, .logs = 5.0F},
      {.row = 3, .distance_m = 3000.0F, .volume_m3 = 270.0F, .logs = 150.0F},
  };
  const std::size_t pick = run::PickMarkStand(offers, 150.0F);
  return Expect(pick == 1, "a 9 m3 crumb at 2.5 km marked over a full stand at 3 km");
}

int CheckTheNearerFullStandWins() {
  // Two stands hold the wanted mark (one only its share, kFullMarkShare): the nearer.
  const std::vector<run::MarkOffer> offers = {
      {.row = 1, .distance_m = 4000.0F, .volume_m3 = 400.0F, .logs = 150.0F},
      {.row = 2, .distance_m = 2800.0F, .volume_m3 = 140.0F, .logs = 80.0F},
      {.row = 5, .distance_m = 1200.0F, .volume_m3 = 9.0F, .logs = 5.0F},
  };
  const std::size_t pick = run::PickMarkStand(offers, 150.0F);
  return Expect(pick == 1, "of two full stands the nearer is not marked");
}

int CheckTheFullestWhenNoneIsFull() {
  // Wanted 200; nothing holds 100: the fullest, not the nearest crumb.
  const std::vector<run::MarkOffer> offers = {
      {.row = 1, .distance_m = 1000.0F, .volume_m3 = 9.0F, .logs = 5.0F},
      {.row = 2, .distance_m = 4000.0F, .volume_m3 = 70.0F, .logs = 40.0F},
      {.row = 3, .distance_m = 3000.0F, .volume_m3 = 70.0F, .logs = 40.0F},
  };
  const std::size_t pick = run::PickMarkStand(offers, 200.0F);
  return Expect(pick == 2, "with no full stand the fullest (the nearer of a tie) is not marked");
}

int CheckALoneCrumbIsStillMarked() {
  const std::vector<run::MarkOffer> offers = {
      {.row = 4, .distance_m = 2500.0F, .volume_m3 = 9.0F, .logs = 5.0F},
  };
  return Expect(run::PickMarkStand(offers, 150.0F) == 0,
                "the only stand in reach, a crumb, is not marked");
}

int CheckNoOfferMarksNothing() {
  const std::vector<run::MarkOffer> offers;
  return Expect(run::PickMarkStand(offers, 150.0F) == offers.size(),
                "no stand in reach and still a pick");
}

}  // namespace

int main() {
  int failures = 0;
  failures += CheckTheFullStandBeatsTheNearerCrumb();
  failures += CheckTheNearerFullStandWins();
  failures += CheckTheFullestWhenNoneIsFull();
  failures += CheckALoneCrumbIsStillMarked();
  failures += CheckNoOfferMarksNothing();
  std::cout << "felling_mark: " << (5 - failures) << " of 5 checks pass\n";
  return failures == 0 ? 0 : 1;
}
