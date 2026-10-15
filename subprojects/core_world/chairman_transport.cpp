#include "chairman_transport.h"

#include "core_common/herd_state.h"
#include "core_common/world_state.h"

namespace core {

ChairmanTransportView ChairmanTransportOf(const WorldState& world, LivestockKindId horse_kind) {
  ChairmanTransportView view;
  bool any_at_stable = false;
  for (const HerdRow& herd : world.herds.rows) {
    // A KOLKHOZ herd AT A UNIT: the private yards' billeted start horses are
    // the farm's too and are fed from the stores, but the chairman's horse
    // eats from the common stable (the human, 15 October 2026), and there is
    // none until a horse herd stands at one.
    const bool heads = herd.newborn_count + herd.juvenile_count + herd.adult_count > 0;
    const bool at_unit = herd.unit.value != kInvalidEntityIdValue;
    if (herd.kind != horse_kind || !at_unit || herd.household_owned != 0 || !heads) {
      continue;
    }
    any_at_stable = true;
    if (herd.fed_share > 0.0F) {
      view.horse_at_post = true;
      view.horse_ground = HorsePostGround::kFed;
      return view;
    }
  }
  view.horse_at_post = !any_at_stable;
  view.horse_ground = any_at_stable ? HorsePostGround::kNoFeedToday : HorsePostGround::kNoStableYet;
  return view;
}

}  // namespace core
