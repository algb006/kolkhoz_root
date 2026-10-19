// The haul arithmetic of haul.h.

#include "core_common/haul.h"

#include <cmath>

namespace core {

HaulRate RateBetween(
    Vec2 from, Vec2 to, float hours_per_km, float loaded_hours_per_km, Grams load) {
  const float dx = to.x - from.x;
  const float dy = to.y - from.y;
  const float metres = std::sqrt((dx * dx) + (dy * dy));
  return RateOverKm(metres / 1000.0F, hours_per_km, loaded_hours_per_km, load);
}

HaulRate RateOverKm(float one_way_km, float hours_per_km, float loaded_hours_per_km, Grams load) {
  // The empty half out and the loaded half back (#570, 2a): until 0.37.229
  // both halves went at the empty pace.
  HaulRate rate;
  rate.load = load;
  // THE TWO PAUSES ON TOP OF THE ROAD (#844): until 0.37.302 a 0.25 h floor
  // stood here instead, so a trip's time was its road alone above ~107 m.
  const float road = one_way_km * (hours_per_km + loaded_hours_per_km);
  rate.round_trip_hours =
      (road > 0.0F ? road : 0.0F) + (2.0F * kLoadingPauseHours);  // nan: no road
  return rate;
}

Grams HaulGrams(float hours, const HaulRate& rate) {
  if (!(hours > 0.0F) || rate.load <= 0 || !(rate.round_trip_hours > 0.0F)) {
    return 0;
  }
  const float trips = hours / rate.round_trip_hours;
  return GramsFromFloat(static_cast<float>(rate.load) * trips);
}

float WalkerShareOfCartDay(Grams carry_grams,
                           Grams cart_grams,
                           float walk_hours_per_km,
                           float harness_hours_per_km,
                           float loaded_hours_per_km) {
  if (carry_grams <= 0 || cart_grams <= 0 || !(walk_hours_per_km > 0.0F) ||
      !(harness_hours_per_km > 0.0F) || !(loaded_hours_per_km > 0.0F)) {
    return 1.0F;
  }
  // Loads by trips: a trip's length is its way by the paces, and the way is
  // one for both (the cart's — haul.h), so the trips go as the round trips:
  // the cart's empty out and loaded back, the walker's twice his walk.
  const float share = (static_cast<float>(carry_grams) / static_cast<float>(cart_grams)) *
                      ((harness_hours_per_km + loaded_hours_per_km) / (2.0F * walk_hours_per_km));
  return share < 1.0F ? share : 1.0F;
}

float HaulDaysFor(Grams waiting, const HaulRate& rate, float standard_day_hours) {
  if (waiting <= 0 || rate.load <= 0 || !(rate.round_trip_hours > 0.0F) ||
      !(standard_day_hours > 0.0F)) {
    return 0.0F;
  }
  const float per_day = static_cast<float>(rate.load) * standard_day_hours / rate.round_trip_hours;
  if (!(per_day > 0.0F)) {
    return 0.0F;
  }
  return static_cast<float>(waiting) / per_day;
}

Grams WalkerTripGrams(CargoForm form,
                      WalkerHold hold,
                      const WalkerCarry& carry,
                      Grams log_piece_grams) {
  switch (form) {
    case CargoForm::kNone:
    case CargoForm::kLive:
    case CargoForm::kPeople:
    case CargoForm::kCargoFormCount:
      return 0;
    case CargoForm::kRick:
      return carry.rick;
    case CargoForm::kManure:
      return hold == WalkerHold::kStretcher ? carry.stretcher_pair / 2 : carry.adult;
    case CargoForm::kLong:
      return hold == WalkerHold::kLogPair ? log_piece_grams / 2 : carry.adult;
    case CargoForm::kBulk:
    case CargoForm::kPacked:
      return carry.adult;
  }
  return carry.adult;
}

CargoForm CargoFormOf(const PorterRules& rules, ResourceId resource) {
  return resource.value < rules.cargo_form.size() ? rules.cargo_form[resource.value]
                                                  : CargoForm::kNone;
}

Grams CartLoadGrams(const PorterRules& rules, float cart_load_kg, ResourceId resource) {
  const float share =
      resource.value < rules.load_share.size() ? rules.load_share[resource.value] : 1.0F;
  // A share of nought would be a cart that carries nothing and a rate that
  // divides by it: the parser refuses it, and a hand-made rule set is read
  // as the whole cart.
  return GramsFromKilograms(cart_load_kg * (share > 0.0F ? share : 1.0F));
}

Grams WalkerLoadGrams(const PorterRules& rules, ResourceId resource) {
  const CargoForm form = CargoFormOf(rules, resource);
  const Grams grams = WalkerTripGrams(form, WalkerHold::kStretcher, rules.carry, 0);
  return grams > 0 ? grams : rules.carry.adult;
}

HaulRate LogPairRate(const PorterRules& rules, HaulRate walked) {
  if (rules.log_piece_grams <= 0 || !(rules.log_pair_loaded_pace > 0.0F)) {
    return walked;
  }
  walked.load = rules.log_piece_grams / 2;
  // The walk's round trip is two equal halves; the loaded one slows.
  walked.round_trip_hours =
      (walked.round_trip_hours / 2.0F) * (1.0F + (1.0F / rules.log_pair_loaded_pace));
  return walked;
}

float LogPairShareOfCartDay(const PorterRules& rules,
                            Grams cart_grams,
                            float walk_hours_per_km,
                            float harness_hours_per_km,
                            float loaded_hours_per_km) {
  if (rules.log_piece_grams <= 0 || !(rules.log_pair_loaded_pace > 0.0F) || cart_grams <= 0 ||
      !(walk_hours_per_km > 0.0F) || !(harness_hours_per_km > 0.0F) ||
      !(loaded_hours_per_km > 0.0F)) {
    return 1.0F;
  }
  const float pair_round = walk_hours_per_km * (1.0F + (1.0F / rules.log_pair_loaded_pace));
  const float share =
      (static_cast<float>(rules.log_piece_grams / 2) / static_cast<float>(cart_grams)) *
      ((harness_hours_per_km + loaded_hours_per_km) / pair_round);
  return share < 1.0F ? share : 1.0F;
}

bool LogPairMayCarry(bool delivering_with_logs_awaiting,
                     bool draught_horse_free,
                     float walk_m,
                     float max_leg_m) {
  return delivering_with_logs_awaiting && !draught_horse_free && walk_m >= 0.0F &&
         walk_m <= max_leg_m;
}

}  // namespace core
