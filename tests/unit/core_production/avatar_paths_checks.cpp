// The checks of the avatar paths on the limit's door (avatar_paths_checks.h).
//
// THE KNOWN ANSWERS, by hand:
// - The ex-chairman's carry (share 0.333): 100 unspent points keep 33 and burn 67. A neutral
//   chairman burns all 100, and the year's grant is the same for both.
// - The promoted's lot (factor 0.5): the same order arrives in round(d / 2) days, never under one,
//   where the neutral's arrives in d.
// - The dealer's grain (6 points a tonne): a lot of 20 points costs 3.333 t, taken rye first,
//   then barley. 2 t of rye and 2 t of barley pay it as 2 t + 1.333 t, and the points do not move.
//   1 t of each is short: kGrainShort, and nothing is taken. A neutral chairman's pay_in_grain is
//   ignored: he pays points. A lot not grain_payable is paid in points by the dealer too.
#include "avatar_paths_checks.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "core_catalog/avatar_catalog.h"
#include "core_catalog/limit_catalog.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "district_limit.h"
#include "production_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: avatar paths on the limit: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr core::Grams kTonne = core::kGramsPerTonne;
constexpr std::size_t kRye = 0;
constexpr std::size_t kBarley = 1;  // the tools are resource 2

enum Avatar : std::uint16_t { kNeutral = 0, kDealer, kExChairman, kPromoted };

core::ProductionConfig PathsConfig() {
  core::ProductionConfig config;
  config.unit_types.resize(1);
  config.unit_types[0].level_storage_capacity_kg.assign(4, 100000.0F);  // a 100 t store
  config.feed_values.resize(3);
  core::LimitLotDef tools;
  tools.points = 20;
  tools.era = 1;
  tools.kind = core::LimitLotKind::kGoods;
  tools.goods = {0, 0, 10 * core::kGramsPerKilogram};
  tools.grain_payable = true;
  core::LimitLotDef glass = tools;
  glass.grain_payable = false;
  config.limit.lots = {tools, glass};
  config.limit.grain_points_per_tonne = 6.0F;
  config.limit.grain_payment_order = {core::ResourceId{kRye}, core::ResourceId{kBarley}};
  core::AvatarDef neutral;
  neutral.key = "neutral";
  core::AvatarDef dealer;
  dealer.key = "dealer";
  dealer.lot_pays_grain = true;
  core::AvatarDef ex_chairman;
  ex_chairman.key = "ex_chairman";
  ex_chairman.limit_points_carry_share = 0.333F;
  core::AvatarDef promoted;
  promoted.key = "promoted";
  promoted.lot_arrival_factor = 0.5F;
  config.avatars = {neutral, dealer, ex_chairman, promoted};
  return config;
}

core::WorldState PathsWorld(Avatar avatar, core::Grams rye, core::Grams barley) {
  core::WorldState world;
  world.epoch = core::Epoch::kOne;
  world.calendar.day = 100;
  world.chairman.avatar = core::AvatarId{avatar};
  world.chairman.raikom_reputation = 50.0F;
  core::UnitRow store;
  store.type = core::UnitTypeId{0};
  store.level = 1;
  store.stock = {rye, barley, 0};
  core::AppendRow(world.units, store);
  world.limit.points = 10;  // short of the 20-point lot
  return world;
}

core::OrderRow LotOrder(std::uint16_t lot, bool in_grain) {
  core::OrderRow order;
  order.kind = core::OrderKind::kOrderLimitLot;
  order.lot = core::LimitLotId{lot};
  order.pay_in_grain = in_grain ? 1U : 0U;
  order.issued_tick = 2400;
  return order;
}

int CheckTheCarry(const core::ProductionConfig& config) {
  int failures = 0;
  core::WorldState neutral = PathsWorld(kNeutral, 0, 0);
  core::WorldState ex = PathsWorld(kExChairman, 0, 0);
  neutral.limit.points = 100;
  ex.limit.points = 100;
  core::TurnLimitYear(config, neutral, true, 0.0F);
  core::TurnLimitYear(config, ex, true, 0.0F);
  failures += Expect(neutral.ledger.current.limit_points_burned == 100 &&
                         neutral.ledger.current.limit_points_carried == 0,
                     "the neutral chairman burns all 100 unspent points");
  failures += Expect(
      ex.ledger.current.limit_points_burned == 67 && ex.ledger.current.limit_points_carried == 33,
      "the ex-chairman keeps floor(0.333 x 100) = 33 and burns 67");
  failures += Expect(ex.limit.points - neutral.limit.points == 33,
                     "and starts the year with the same grant plus his 33");
  failures += Expect(ex.limit.points_granted_total == neutral.limit.points_granted_total,
                     "the carry is not granted: the running total moves by the grant alone");
  return failures;
}

int CheckTheArrival(const core::ProductionConfig& config) {
  int failures = 0;
  const core::WorldState neutral = PathsWorld(kNeutral, 0, 0);
  const core::WorldState promoted = PathsWorld(kPromoted, 0, 0);
  const core::OrderRow order = LotOrder(1, false);
  const std::uint32_t neutral_days = core::LimitCartArriveDay(config, neutral, order) - 100U;
  const std::uint32_t promoted_days = core::LimitCartArriveDay(config, promoted, order) - 100U;
  std::cout << "  avatar paths: a lot's days, neutral " << neutral_days << ", promoted "
            << promoted_days << '\n';
  const std::uint32_t half = (neutral_days + 1U) / 2U;  // lround of d / 2
  failures += Expect(promoted_days == (half > 0 ? half : 1U),
                     "the promoted's lot arrives in round(d / 2) days, never under one");
  return failures;
}

int CheckTheGrain(const core::ProductionConfig& config) {
  int failures = 0;
  constexpr core::Grams kPrice = 3333333;  // 20 points / 6 a tonne, in grams (rounded)
  core::WorldState paid = PathsWorld(kDealer, 2 * kTonne, 2 * kTonne);
  failures +=
      Expect(core::OrderLimitLot(config, paid, LotOrder(0, true)) == core::OrderRefusal::kNone,
             "the dealer orders the tool lot in grain with 10 points of the 20");
  const core::ResourceAmounts& stock = paid.units.rows[0].stock;
  failures += Expect(stock[kRye] == 0 && stock[kBarley] == 4 * kTonne - kPrice,
                     "and pays 3.333 t: the 2 t of rye first, the rest in barley");
  failures += Expect(paid.limit.points == 10 && paid.ledger.current.limit_points_spent == 0,
                     "the points are not touched");
  failures += Expect(paid.ledger.current.limit_lots_paid_in_grain == 1 &&
                         paid.ledger.current.limit_grain_paid == kPrice &&
                         paid.limit_deliveries.rows.size() == 1,
                     "the book counts one lot and its grain, and the cart leaves");
  core::WorldState short_of = PathsWorld(kDealer, kTonne, kTonne);
  failures += Expect(
      core::OrderLimitLot(config, short_of, LotOrder(0, true)) == core::OrderRefusal::kGrainShort &&
          short_of.units.rows[0].stock[kRye] == kTonne &&
          short_of.units.rows[0].stock[kBarley] == kTonne && short_of.limit_deliveries.rows.empty(),
      "with 2 t of grain against 3.333 t: kGrainShort, nothing taken, no cart");
  core::WorldState neutral = PathsWorld(kNeutral, 2 * kTonne, 2 * kTonne);
  failures += Expect(
      core::OrderLimitLot(config, neutral, LotOrder(0, true)) == core::OrderRefusal::kLimitShort &&
          neutral.units.rows[0].stock[kRye] == 2 * kTonne,
      "a neutral chairman's pay_in_grain is ignored: points, and 10 are short");
  core::WorldState glass = PathsWorld(kDealer, 2 * kTonne, 2 * kTonne);
  failures += Expect(
      core::OrderLimitLot(config, glass, LotOrder(1, true)) == core::OrderRefusal::kLimitShort &&
          glass.units.rows[0].stock[kRye] == 2 * kTonne,
      "a lot not grain_payable is points only, for the dealer too");
  return failures;
}

}  // namespace

int CheckTheAvatarPathsOnTheLimit() {
  const core::ProductionConfig config = PathsConfig();
  return CheckTheCarry(config) + CheckTheArrival(config) + CheckTheGrain(config);
}
