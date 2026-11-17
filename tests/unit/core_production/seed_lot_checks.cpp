// The checks of the district's seed lot (seed_lot_checks.h).
//
// THE KNOWN ANSWERS, by hand (a barley field inside its window, the lot 30 points for 5 t):
// - The barn empty: the shortfall is the field's whole need with its rot. A 40 ha field
//   needs more than 5 t: the lot delivers 5 t for 30 points. A 10 ha field needs N < 5 t:
//   the lot delivers N for ceil(30 x N / 5 t) points.
// - Bought: the points fall by exactly the price, one cart carries N of barley, the year book
//   holds N. Ordered again the same day (the cart not yet in): the cart's N is coming, nothing
//   is short - kRuleForbids, no crop, 0 g. The cart in and the seed lost again: short by N, and
//   bought N + N > the need N - kRuleForbids naming barley and need - bought = 0 g.
// - 0.1 t short: the lot delivers its least, 0.5 t, for ceil(30 x 0.5 / 5) = 3 points.
// - Twice the need in the barn: nothing short, kRuleForbids, 0 g, no crop named.
// - 2 points left against a price of 3: kLimitShort.
// - The year's turn empties the book.

#include "seed_lot_checks.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "core_catalog/limit_catalog.h"
#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "district_limit.h"
#include "production_alarms.h"
#include "production_config.h"
#include "seed_lot.h"
#include "stock_ops.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: seed lot: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr std::int32_t kLotPoints = 30;
constexpr core::Grams kMost = 5'000'000;
constexpr core::Grams kLeast = 500'000;

core::CropId SpringCropOf(const core::ProductionConfig& config, core::ResourceId resource) {
  for (std::size_t index = 0; index < config.crops.size(); ++index) {
    if (config.crops[index].resource == resource && config.crops[index].is_winter == 0 &&
        config.crops[index].sowing_norm_kg_per_ha > 0.0F) {
      return core::DefIdFromIndex<core::CropIdTag>(index);
    }
  }
  return core::CropId{};
}

/// A standing store of `resource` holding `grams` of it, the only unit.
void PutInTheBarn(const core::ProductionConfig& config,
                  core::WorldState& world,
                  core::ResourceId resource,
                  core::Grams grams) {
  core::UnitRow barn;
  barn.level = 1;
  for (std::uint32_t type = 0; type < config.unit_types.size(); ++type) {
    barn.type = core::UnitTypeId{static_cast<std::uint16_t>(type)};
    if (core::StoresGoods(barn, config) && core::StorageCapacityGrams(barn, config) > 0 &&
        core::TypeIsHomeOf(barn, config, resource)) {
      break;
    }
  }
  barn.stock.assign(static_cast<std::size_t>(resource.value) + 1U, 0);
  barn.stock[resource.value] = grams;
  world.units.rows.clear();
  world.units.row_ids.clear();
  core::AppendRow(world.units, barn);
}

/// A world on day 14 of the year (barley's window open), one barley field of
/// `hectares` under the harrow, the barn holding `held` of barley.
core::WorldState BarleyWorld(const core::ProductionConfig& config,
                             core::CropId crop,
                             float hectares,
                             core::Grams held) {
  core::WorldState world;
  world.epoch = core::Epoch::kOne;
  world.calendar.tick = static_cast<core::Tick>(14) * core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.area_ga = hectares;
  field.phase = core::FieldPhase::kHarrowing;
  field.crop = crop;
  field.rotation_year0 = crop;
  core::AppendRow(world.fields, field);
  PutInTheBarn(config, world, config.crops[crop.value].resource, held);
  world.limit.points = 100;
  return world;
}

core::OrderRow SeedLotOrder(core::LimitLotId lot) {
  core::OrderRow order;
  order.kind = core::OrderKind::kOrderLimitLot;
  order.lot = lot;
  order.issued_tick = 14U * core::kTicksPerDay;
  return order;
}

int CheckTheTerms(const core::ProductionConfig& config,
                  core::LimitLotId lot,
                  core::CropId crop,
                  core::ResourceId barley) {
  int failures = 0;
  // A big field: the shortfall is above the most, the lot is its most.
  {
    const core::WorldState world = BarleyWorld(config, crop, 40.0F, 0);
    const core::Grams need = core::SeedShortByResource(config, world)[barley.value];
    const core::SeedLotView offer = core::SeedLotOfferOf(config, world);
    failures += Expect(need > kMost, "a 40 ha barley field with an empty barn is over 5 t short");
    failures += Expect(offer.lot == lot && offer.resource == barley && offer.grams == kMost &&
                           offer.points == kLotPoints && offer.refusal == core::OrderRefusal::kNone,
                       "over 5 t short: the lot is 5 t of barley for its 30 points");
  }
  // Under the least: 0.1 t short buys the least, 0.5 t, at 3 points.
  {
    const core::WorldState probe = BarleyWorld(config, crop, 10.0F, 0);
    const core::Grams need = core::SeedShortByResource(config, probe)[barley.value];
    const core::WorldState world = BarleyWorld(config, crop, 10.0F, need - 100'000);
    const core::SeedLotView offer = core::SeedLotOfferOf(config, world);
    failures += Expect(
        offer.grams == kLeast && offer.points == 3 && offer.refusal == core::OrderRefusal::kNone,
        "0.1 t short: the lot is its least, 0.5 t, for ceil(30 x 0.5 / 5) = 3");
    core::WorldState poor = world;
    poor.limit.points = 2;
    failures +=
        Expect(core::SeedLotOfferOf(config, poor).refusal == core::OrderRefusal::kLimitShort,
               "2 points against a price of 3: kLimitShort");
  }
  // Nothing short.
  {
    const core::WorldState probe = BarleyWorld(config, crop, 10.0F, 0);
    const core::Grams need = core::SeedShortByResource(config, probe)[barley.value];
    const core::WorldState world = BarleyWorld(config, crop, 10.0F, 2 * need);
    const core::SeedLotView offer = core::SeedLotOfferOf(config, world);
    failures += Expect(offer.refusal == core::OrderRefusal::kRuleForbids && offer.grams == 0 &&
                           offer.resource.value == core::kInvalidDefIdValue,
                       "twice the need in the barn: no seed short, refused, 0 g, no crop");
  }
  return failures;
}

int CheckTheDoor(const core::ProductionConfig& config, core::CropId crop, core::ResourceId barley) {
  int failures = 0;
  core::WorldState world = BarleyWorld(config, crop, 10.0F, 0);
  const core::SeedLotView offer = core::SeedLotOfferOf(config, world);
  const core::Grams need = core::SeedShortByResource(config, world)[barley.value];
  const auto price = static_cast<std::int32_t>((kLotPoints * need + kMost - 1) / kMost);
  failures += Expect(need > kLeast && need < kMost && offer.grams == need && offer.points == price,
                     "a 10 ha field: the lot is the exact shortfall at its pro-rated points");
  core::OrderRow order = SeedLotOrder(offer.lot);
  const core::OrderRefusal bought =
      core::OrderLimitLot(config, world, order, &order.resource, &order.amount);
  failures += Expect(bought == core::OrderRefusal::kNone, "the order is taken");
  failures += Expect(world.limit.points == 100 - price, "the points fall by exactly the price");
  failures += Expect(world.limit_deliveries.rows.size() == 1 &&
                         world.limit_deliveries.rows[0].goods.size() > barley.value &&
                         world.limit_deliveries.rows[0].goods[barley.value] == need,
                     "one district cart carries the shortfall of barley");
  failures += Expect(world.limit.seed_lot_bought.size() > barley.value &&
                         world.limit.seed_lot_bought[barley.value] == need,
                     "the year book holds the grams bought");
  // Again, the cart still on the road: its seed is coming, nothing is short (p1377 bought
  // the same shortfall four days running before this).
  core::OrderRow again = SeedLotOrder(offer.lot);
  const core::OrderRefusal on_road =
      core::OrderLimitLot(config, world, again, &again.resource, &again.amount);
  failures += Expect(on_road == core::OrderRefusal::kRuleForbids &&
                         again.resource.value == core::kInvalidDefIdValue && again.amount == 0 &&
                         world.limit.points == 100 - price,
                     "ordered again before the cart: the seed on the road is not short - refused, "
                     "no crop, 0 g, and costs nothing");
  // The cart in and its seed gone again (eaten, rotted): short once more, and the year's cap
  // refuses, naming barley and the 0 g it still allows.
  world.limit_deliveries.rows.clear();
  world.limit_deliveries.row_ids.clear();
  core::OrderRow capped_order = SeedLotOrder(offer.lot);
  const core::OrderRefusal capped = core::OrderLimitLot(
      config, world, capped_order, &capped_order.resource, &capped_order.amount);
  failures +=
      Expect(capped == core::OrderRefusal::kRuleForbids && capped_order.resource == barley &&
                 capped_order.amount == 0 && world.limit.points == 100 - price,
             "the seed lost after the cart: the year's cap refuses, names barley and 0 g "
             "allowed, and costs nothing");
  core::TurnLimitYear(config, world, false, 0.0F);
  failures += Expect(world.limit.seed_lot_bought.empty(), "the year's turn empties the book");
  return failures;
}

}  // namespace

int CheckTheSeedLot() {
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const core::ITable* const resources = tables->FindTable("resources");
  const core::ITable* const lots = tables->FindTable("limit_catalog");
  if (Expect(resources != nullptr && lots != nullptr, "the tables hold resources and lots") != 0) {
    return 1;
  }
  const std::uint32_t barley_row = resources->FindRowByKey("barley");
  const std::uint32_t lot_row = lots->FindRowByKey("seed_grain_lot");
  if (Expect(barley_row != core::kNoTableRow && lot_row != core::kNoTableRow,
             "the roster names barley and the catalogue seed_grain_lot") != 0) {
    return 1;
  }
  // BOSS'S EXPORT, as the delivery will carry it (core-boss-1377-seed-lot [2] Q1):
  // kind seed, 30 points. Set here so the check does not wait on the tables.
  config.limit.lots[lot_row].kind = core::LimitLotKind::kSeed;
  config.limit.lots[lot_row].points = kLotPoints;
  config.limit.seed_lot_grams_min = kLeast;
  config.limit.seed_lot_grams_max = kMost;
  const core::ResourceId barley = core::DefIdFromRow<core::ResourceIdTag>(barley_row);
  const core::CropId crop = SpringCropOf(config, barley);
  if (Expect(crop.value != core::kInvalidDefIdValue, "the tables name a spring barley") != 0) {
    return 1;
  }
  const core::LimitLotId lot = core::DefIdFromRow<core::LimitLotIdTag>(lot_row);
  int failures = 0;
  failures += CheckTheTerms(config, lot, crop, barley);
  failures += CheckTheDoor(config, crop, barley);
  return failures;
}
