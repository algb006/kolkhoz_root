// The checks of the winter horse for the fellers (winter_horse_checks.h).

#include "winter_horse_checks.h"

#include <cstdint>
#include <iostream>
#include <vector>

#include "assignment.h"
#include "core_catalog/timber_catalog.h"
#include "core_common/calendar.h"
#include "core_common/state_table_ops.h"
#include "core_common/timber_state.h"
#include "core_common/world_state.h"
#include "winter_felling_horse.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// A December day of the people's cart checks' kind: 9.4 hours of light, a walker at 2.4 game
/// hours a kilometre, the cart at 1.0, six hours of road at most, a cart of two seats.
core::AssignmentParams DecemberDay(std::uint32_t horses, std::uint32_t reserve) {
  core::AssignmentParams params;
  params.window_hours = 9.4F;
  params.walk_hours_per_km = 2.4F;
  params.harness_hours_per_km = 1.0F;
  params.people_cart_hours_per_km = params.harness_hours_per_km;
  params.travel_limit_hours = 6.0F;
  params.min_usable_hours = 1.0F;
  params.standard_day_hours = 10.0F;
  params.placement_level = 0;
  params.draught_horses = horses;
  params.people_cart_seats = 2;
  params.people_cart_min_walk_hours = 2.0F;
  params.winter_horse_reserve = reserve;
  return params;
}

std::vector<core::AssignmentCandidate> Hands(std::uint32_t count) {
  std::vector<core::AssignmentCandidate> hands;
  for (std::uint32_t row = 0; row < count; ++row) {
    core::AssignmentCandidate hand;
    hand.resident_row = row;
    hand.home = core::Vec2{0.0F, 0.0F};
    hand.efficiency = 1.0F;
    hands.push_back(hand);
  }
  return hands;
}

/// Job 0: a district lot's fetch 500 m out, which stops without a horse and yields the winter
/// horse; job 1: a felling 3 km out - 7.2 hours on foot, past the road limit, so only the
/// people's cart reaches it.
std::vector<core::AssignmentJob> LotAndFelling() {
  core::AssignmentJob lot;
  lot.kind = core::WorkKind::kHauling;
  lot.limit_delivery = core::LimitDeliveryId{3};
  lot.harnessed = true;
  lot.position = core::Vec2{500.0F, 0.0F};
  lot.work_days_remaining = 1.0F;
  lot.yields_winter_horse = true;
  core::AssignmentJob felling;
  felling.kind = core::WorkKind::kFelling;
  felling.position = core::Vec2{3000.0F, 0.0F};
  felling.work_days_remaining = 2.0F;
  return {lot, felling};
}

struct Plan {
  std::uint32_t on_lot = 0;
  std::uint32_t on_felling = 0;
  std::uint32_t lot_holders = 0;
};

Plan Run(const core::AssignmentParams& params) {
  std::vector<std::uint8_t> rides;
  std::vector<std::uint32_t> with;
  const std::vector<std::uint32_t> placed =
      core::PlanDayAssignments(LotAndFelling(), Hands(6), params, &rides, nullptr, nullptr, &with);
  Plan plan;
  for (std::size_t hand = 0; hand < placed.size(); ++hand) {
    plan.on_lot += placed[hand] == 0 ? 1U : 0U;
    plan.on_felling += placed[hand] == 1 ? 1U : 0U;
    plan.lot_holders += placed[hand] == 0 && hand < rides.size() && rides[hand] != 0 ? 1U : 0U;
  }
  return plan;
}

/// A December world of year 2 with one stand marked, `lying` grams of logs on it, and the
/// closed book's logs built in `built` grams (ResourceId 2 the log).
core::WorldState WinterWorld(std::uint32_t month, core::Grams lying, core::Grams built) {
  core::WorldState world;
  world.calendar.day = core::kDaysPerYear + (month * core::kDaysPerMonth);
  core::TimberStandRow stand;
  stand.marked_m3 = 10.0F;
  stand.work_days_remaining = 1.0F;
  stand.load_grams = lying;
  core::AppendRow(world.stands, stand);
  world.ledger.closed.built_in.assign(4, 0);
  world.ledger.closed.processed.assign(4, 0);
  world.ledger.closed.built_in[2] = built;
  return world;
}

}  // namespace

int CheckTheWinterHorse() {
  int failures = 0;
  // THE QUEUE: one horse. With the reserve, the lot's fetch may not take it and the fellers'
  // cart does; without, the lot takes it and the felling, past the walk, gets nobody.
  const Plan reserved = Run(DecemberDay(1, 1));
  failures += Expect(reserved.lot_holders == 0,
                     "winter horse: with the reserve the yielding lot holds no horse");
  failures += Expect(reserved.on_felling > 0,
                     "winter horse: with the reserve the fellers ride to the 3 km stand");
  const Plan plain = Run(DecemberDay(1, 0));
  failures += Expect(plain.lot_holders == 1,
                     "winter horse: with no reserve the lot takes the one horse (as before)");
  failures += Expect(plain.on_felling == 0,
                     "winter horse: with no reserve nobody reaches the 3 km felling");
  // TWO HORSES WITH THE RESERVE: the lot may take the first, the fellers keep the second.
  const Plan two = Run(DecemberDay(2, 1));
  failures += Expect(two.lot_holders == 1 && two.on_felling > 0,
                     "winter horse: two horses - the lot one, the fellers the other");
  // THE DAY'S PREDICATE.
  core::TimberCatalog timber;
  timber.log_resource = core::ResourceId{2};
  constexpr std::uint32_t kDecember = 11;
  constexpr std::uint32_t kJune = 5;
  failures += Expect(core::WinterFellingHorseDay(WinterWorld(kDecember, 100, 1000), timber),
                     "winter horse day: December, a mark, 100 g in hand under 1000 g wanted");
  failures += Expect(!core::WinterFellingHorseDay(WinterWorld(kDecember, 1000, 1000), timber),
                     "winter horse day: not when the logs in hand cover the season");
  failures += Expect(!core::WinterFellingHorseDay(WinterWorld(kJune, 100, 1000), timber),
                     "winter horse day: not in June");
  failures += Expect(!core::WinterFellingHorseDay(WinterWorld(kDecember, 0, 0), timber),
                     "winter horse day: not with an empty closed book (the first year)");
  // WHO YIELDS.
  const core::WorldState world = WinterWorld(kDecember, 0, 1000);
  core::AssignmentJob job;
  job.kind = core::WorkKind::kHauling;
  failures += Expect(core::YieldsTheWinterHorse(world, job), "yields: a plain own-cart load");
  job.herd = core::HerdId{1};
  failures += Expect(!core::YieldsTheWinterHorse(world, job), "yields: never the milk leg");
  job.herd = core::HerdId{};
  job.stand = core::TimberStandId{1};
  failures += Expect(!core::YieldsTheWinterHorse(world, job), "yields: a stand's logs are not");
  job.stand = core::TimberStandId{};
  job.kind = core::WorkKind::kFelling;
  failures += Expect(!core::YieldsTheWinterHorse(world, job), "yields: only hauling yields");
  return failures;
}
