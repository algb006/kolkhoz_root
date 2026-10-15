#include "chairman_transport_checks.h"

#include <cstdint>
#include <iostream>

#include "chairman_transport.h"
#include "core_common/chairman_transport_view.h"
#include "core_common/herd_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr core::LivestockKindId kHorse{1};
constexpr core::LivestockKindId kCow{0};

/// A herd of `kind` with five adults, at unit 7 or at family 3's yard.
core::HerdRow AHerd(core::LivestockKindId kind, bool at_unit, float fed_share) {
  core::HerdRow herd;
  herd.kind = kind;
  if (at_unit) {
    herd.unit = core::UnitId{7};
  } else {
    herd.household = core::FamilyId{3};
  }
  herd.adult_count = 5;
  herd.fed_share = fed_share;
  return herd;
}

bool Reads(const core::ChairmanTransportView& view, bool present, core::HorsePostGround ground) {
  return view.horse_at_post == present && view.horse_ground == ground &&
         view.vehicle == core::ChairmanVehicle::kNone;
}

}  // namespace

int CheckTheChairmansTransportDoor() {
  int failures = 0;
  using core::HorsePostGround;

  {
    const core::WorldState world;
    failures +=
        Expect(Reads(core::ChairmanTransportOf(world, kHorse), true, HorsePostGround::kNoStableYet),
               "957: a world with no horses keeps the horse at the post, no stable yet");
  }
  {
    // The start: the kolkhoz horses billeted in the private yards and
    // unfed — the chairman's stable does not exist yet.
    core::WorldState world;
    core::AppendRow(world.herds, AHerd(kHorse, false, 0.0F));
    failures +=
        Expect(Reads(core::ChairmanTransportOf(world, kHorse), true, HorsePostGround::kNoStableYet),
               "957: horses in the private yards are not the chairman's stable");
  }
  {
    core::WorldState world;
    core::AppendRow(world.herds, AHerd(kHorse, true, 0.0F));
    failures += Expect(
        Reads(core::ChairmanTransportOf(world, kHorse), false, HorsePostGround::kNoFeedToday),
        "957: the stable's horses got nothing today - the post is empty");
  }
  {
    // A SHORT ration is not «корм кончился» (boss [2] Q1): a third of it
    // keeps the horse at the post, and one fed herd is enough.
    core::WorldState world;
    core::AppendRow(world.herds, AHerd(kHorse, true, 0.0F));
    core::AppendRow(world.herds, AHerd(kHorse, true, 0.3F));
    failures += Expect(Reads(core::ChairmanTransportOf(world, kHorse), true, HorsePostGround::kFed),
                       "957: any stable herd with a share of its ration keeps the horse");
  }
  {
    // A FAMILY's own horse at a unit is not the common stable.
    core::WorldState world;
    core::HerdRow own = AHerd(kHorse, true, 0.0F);
    own.household_owned = 1;
    core::AppendRow(world.herds, own);
    failures +=
        Expect(Reads(core::ChairmanTransportOf(world, kHorse), true, HorsePostGround::kNoStableYet),
               "957: a household-owned horse herd is not the chairman's stable");
  }
  {
    // A hungry COW herd does not take the horse away.
    core::WorldState world;
    core::AppendRow(world.herds, AHerd(kCow, true, 0.0F));
    failures +=
        Expect(Reads(core::ChairmanTransportOf(world, kHorse), true, HorsePostGround::kNoStableYet),
               "957: another kind's hunger is not the stable's");
  }
  {
    // An EMPTY row at a unit (every head gone) is no stable to eat from.
    core::WorldState world;
    core::HerdRow empty = AHerd(kHorse, true, 0.0F);
    empty.adult_count = 0;
    core::AppendRow(world.herds, empty);
    failures +=
        Expect(Reads(core::ChairmanTransportOf(world, kHorse), true, HorsePostGround::kNoStableYet),
               "957: a horse row with no heads is not a stable");
  }
  return failures;
}
