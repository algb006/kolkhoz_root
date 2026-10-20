/// @file
/// @brief THE CART AND THE SLEDGE (SL; transport design §16 «Телега и сани —
/// одно и то же имущество, меняющее обличье по наличию снега»; §1 «Сани»: 8
/// km/h, load x1.5 of the cart on snow; econ logistics-all-on-2026-10-19 SL;
/// boss, the #1056 thread [101]): ONE DOOR for the pace and the load every
/// cart reader takes, so that a sledge rule on one reader cannot be bypassed
/// through another.
/// @threading PARALLEL_READONLY
/// Pure functions of their arguments; any phase, any thread.
///
/// A cart leaves the stable as a sledge on a day snow lies (WeatherState::
/// snow_cover_days > 0 - the same input road_rules.cpp turns into the snow
/// bed). The cover is set at the day's start and the groom's plan is built
/// for the day, so a cart is a sledge for the whole day or not at all: «a cart
/// caught by the snow finishes its trip as a cart» holds by construction.

#ifndef CORE_COMMON_CART_GEAR_H_
#define CORE_COMMON_CART_GEAR_H_

namespace core {

struct WorldState;

/// @brief The sledge's own numbers, the one thing a module's config adds to
///        the cart's it already holds (transport.csv `sledge`: its pace both
///        ways, its load_tonnes x load_scale). Zero: no sledge row, a snow day
///        is a cart's.
struct SledgeSpecs {
  float kmh = 0.0F;
  float load_kg = 0.0F;
};

/// @brief The two vehicles' numbers for one call, put together from the
///        module's config: the cart's from its own fields (`horse_trot`
///        empty, `cart_loaded` loaded and its load), the sledge's from its
///        SledgeSpecs - one home each, no copy of the cart's numbers.
struct CartSpecs {
  float cart_empty_kmh = 12.0F;
  float cart_loaded_kmh = 9.0F;
  float cart_load_kg = 1800.0F;
  /// The sledge's pace, empty and loaded alike (the registry's 8 km/h).
  float sledge_kmh = 8.0F;
  float sledge_load_kg = 2700.0F;
};

/// @brief What a cart is today: its paces and its load.
struct CartGear {
  float empty_kmh = 12.0F;
  float loaded_kmh = 9.0F;
  float load_kg = 1800.0F;
  /// True on a sledge day: the layer draws runners (CartLeg::sledge), and
  /// the bed's factor is the runners' (road_rules.h RoadBedFactor).
  bool sledge = false;
};

/// @brief True on a day snow lies: the carts leave the stable as sledges.
bool SledgeDay(const WorldState& world);

/// @brief The gear of a day: the sledge's on a sledge day, the cart's else.
CartGear CartGearOf(const CartSpecs& specs, bool sledge_day);

/// @brief CartGearOf for today's world.
CartGear CartGearToday(const WorldState& world, const CartSpecs& specs);

}  // namespace core

#endif  // CORE_COMMON_CART_GEAR_H_
