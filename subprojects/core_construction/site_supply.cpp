// How a site is supplied (core_construction/site_supply.h).

#include "site_supply.h"

#include <cstddef>

#include "core_common/unit_state.h"

namespace core {

void AddTo(ResourceAmounts& amounts, ResourceId resource, Grams delta) {
  if (delta == 0 || resource.value == kInvalidDefIdValue) {
    return;
  }
  if (resource.value >= amounts.size()) {
    amounts.resize(static_cast<std::size_t>(resource.value) + 1, 0);
  }
  amounts[resource.value] += delta;
  if (amounts[resource.value] < 0) {
    amounts[resource.value] = 0;
  }
}

Grams TakeFromStores(WorldState& current,
                     std::uint32_t site_row,
                     ResourceId resource,
                     Grams wanted) {
  Grams taken = 0;
  for (std::uint32_t row = 0; row < current.units.rows.size() && taken < wanted; ++row) {
    if (row == site_row || current.units.rows[row].level == 0) {
      continue;
    }
    // Not another upgrade's recipe: it was checked and carried in for that
    // works, and a second start must not undo the first one's check.
    const Grams have = UnreservedOf(current.units.rows[row], resource);
    if (have <= 0) {
      continue;
    }
    const Grams give = have < wanted - taken ? have : wanted - taken;
    AddTo(current.units.rows[row].stock, resource, -give);
    taken += give;
  }
  return taken;
}

}  // namespace core
