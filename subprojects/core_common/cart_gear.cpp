// The cart and the sledge, one door (cart_gear.h).

#include "core_common/cart_gear.h"

#include "core_common/world_state.h"

namespace core {

bool SledgeDay(const WorldState& world) {
  return world.weather.snow_cover_days > 0;
}

CartGear CartGearOf(const CartSpecs& specs, bool sledge_day) {
  if (sledge_day && specs.sledge_kmh > 0.0F && specs.sledge_load_kg > 0.0F) {
    return CartGear{.empty_kmh = specs.sledge_kmh,
                    .loaded_kmh = specs.sledge_kmh,
                    .load_kg = specs.sledge_load_kg,
                    .sledge = true};
  }
  return CartGear{.empty_kmh = specs.cart_empty_kmh,
                  .loaded_kmh = specs.cart_loaded_kmh,
                  .load_kg = specs.cart_load_kg,
                  .sledge = false};
}

CartGear CartGearToday(const WorldState& world, const CartSpecs& specs) {
  return CartGearOf(specs, SledgeDay(world));
}

}  // namespace core
