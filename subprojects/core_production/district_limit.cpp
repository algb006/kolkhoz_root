// The district's limit in the simulation (district_limit.h).

#include "district_limit.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "herd_life.h"
#include "stock_ops.h"

namespace core {
namespace {

/// The random stream the district's delays are drawn from. Its own stream id,
/// so a delay never shifts the draws of any other system of the world.
constexpr std::uint64_t kDeliveryDelayStream = 0x4C494D4954ULL;  // "LIMIT"

bool CarriesAnything(const ResourceAmounts& goods) {
  return std::any_of(goods.begin(), goods.end(), [](Grams grams) { return grams > 0; });
}

/// Head of KOLKHOZ stock standing in the village, of every kind and age. A
/// family's own animals are not counted: they are the family's, they live in
/// the family's yard by right, and the farm's ceiling is not about them.
std::uint32_t KolkhozHeads(const WorldState& world) {
  std::uint32_t heads = 0;
  for (const HerdRow& herd : world.herds.rows) {
    if (herd.household_owned != 0) {
      continue;
    }
    heads +=
        static_cast<std::uint32_t>(herd.newborn_count) + herd.juvenile_count + herd.adult_count;
  }
  return heads;
}

/// Places the village has for kolkhoz stock: the roofs plus the yards.
///
/// THE YARDS ARE PART OF IT AND NOT A FALLBACK. Billeting is the ordinary
/// state of a farm that has not built its byres yet — the start canon puts
/// all sixteen horses in private yards on the first morning — so "how many
/// head can this village hold" has always been roof room PLUS yard room. What
/// was missing until 2026-09-16 is that the yard half had no limit whatever,
/// which is how twenty-one households could hold five hundred horses.
float PlacesForStock(const ProductionConfig& config, const WorldState& world) {
  float roofs = 0.0F;
  for (const UnitRow& unit : world.units.rows) {
    if (unit.type.value >= config.unit_types.size()) {
      continue;
    }
    roofs += config.unit_types[unit.type.value].LivestockCapacityHeadAt(unit.level);
  }
  const auto yards = static_cast<float>(world.families.rows.size());
  return roofs + (yards * config.farming.billet_heads_per_yard);
}

/// The MTS column bought (limit_state.h, MtsColumnState): one at a time, and
/// only while it can still reach its season's window this year. Refused
/// before any point is spent — "not cancellable" is about the accepted one.
OrderRefusal OrderMtsColumn(const ProductionConfig& config,
                            WorldState& current,
                            LimitLotId lot,
                            std::int32_t points) {
  const MtsColumnPhase phase = current.mts_column.phase;
  if (phase == MtsColumnPhase::kOnTheRoad || phase == MtsColumnPhase::kWorking) {
    return OrderRefusal::kRuleForbids;  // one column a season, and this one is out
  }
  const bool spring = lot.value == config.limit.mts_spring_lot.value;
  const std::uint8_t window_end =
      spring ? config.limit.mts_spring_to_month : config.limit.mts_autumn_to_month;
  const std::uint32_t arrive_day =
      static_cast<std::uint32_t>(current.calendar.day) + config.limit.delivery_days;
  const std::uint32_t arrive_month = (arrive_day % kDaysPerYear) / kDaysPerMonth;
  if (arrive_month > window_end) {
    return OrderRefusal::kRuleForbids;  // it would come after its season
  }
  if (current.limit.points < points) {
    return OrderRefusal::kLimitShort;
  }
  current.limit.points -= points;
  current.ledger.current.limit_points_spent += points;
  current.mts_column = MtsColumnState{};
  current.mts_column.phase = MtsColumnPhase::kOnTheRoad;
  current.mts_column.lot = lot;
  current.mts_column.arrive_day = arrive_day;
  return OrderRefusal::kNone;
}

}  // namespace

OrderRefusal LotOrderable(const LimitCatalog& catalog, LimitLotId lot, Epoch epoch) {
  if (lot.value == kInvalidDefIdValue || lot.value >= catalog.lots.size()) {
    return OrderRefusal::kNoSuchSubject;
  }
  const LimitLotDef& def = catalog.lots[lot.value];
  if (def.era > static_cast<std::uint8_t>(epoch)) {
    return OrderRefusal::kGateClosed;
  }
  // A service is bought here when it is the MTS column of spring or autumn
  // (boss, parcel 449); any other service has no body yet.
  if (def.kind == LimitLotKind::kService) {
    const bool column =
        lot.value == catalog.mts_spring_lot.value || lot.value == catalog.mts_autumn_lot.value;
    return column && def.points >= 0 ? OrderRefusal::kNone : OrderRefusal::kRuleForbids;
  }
  // STOCK, since the livestock window (2026-09-16). It needs a price and a
  // head count; the batches whose size the tables leave empty are refused by
  // the same "nothing written yet" rule that refuses a goods lot with no
  // amount.
  //
  // THIS BRANCH USED TO SAY STUB AND WAS READ AS A RULE OF THE WORLD — «район
  // живое не покупает» got as far as a named design rejection before anyone
  // re-read the catalogue, which has priced a horse at 70 points from epoch I
  // all along. The design calls this lot «страховка от тупика», and it is the
  // only way out of losing the last draught horse.
  if (def.kind == LimitLotKind::kLivestock) {
    const bool named = def.livestock.value != kInvalidDefIdValue;
    return named && def.points >= 0 && def.head_count > 0 ? OrderRefusal::kNone
                                                          : OrderRefusal::kRuleForbids;
  }
  // Only goods are bought here besides those (machines, people and "choice"
  // have their own windows — STUB), and only a lot with a price and at least
  // one written amount (boss, parcel 211).
  if (def.kind != LimitLotKind::kGoods || def.points < 0 || !CarriesAnything(def.goods)) {
    return OrderRefusal::kRuleForbids;
  }
  return OrderRefusal::kNone;
}

float LimitReputationMultiplier(float reputation) {
  // District design §5, band for band. The bands are the design's table and
  // not balance knobs: no row carries them, and boss named them from the
  // document (parcel 211).
  if (!(reputation > 20.0F)) {
    return 0.7F;
  }
  if (reputation <= 40.0F) {
    return 0.85F;
  }
  if (reputation <= 60.0F) {
    return 1.0F;
  }
  if (reputation <= 80.0F) {
    return 1.2F;
  }
  return 1.4F;
}

std::int32_t YearLimitPoints(const LimitCatalog& catalog,
                             FarmStatusTier tier,
                             bool plan_fully_met,
                             float overfulfil_percent,
                             float reputation) {
  const auto tier_index = static_cast<std::size_t>(tier);
  double points = tier_index < catalog.base_points.size()
                      ? static_cast<double>(catalog.base_points[tier_index])
                      : 0.0;
  if (plan_fully_met) {
    points += static_cast<double>(catalog.plan_met_points);
  }
  if (overfulfil_percent > 0.0F) {
    const double over = std::floor(static_cast<double>(overfulfil_percent)) *
                        static_cast<double>(catalog.overfulfil_points_per_percent);
    points += std::min(over, static_cast<double>(catalog.overfulfil_points_max));
  }
  points *= static_cast<double>(LimitReputationMultiplier(reputation));
  const double rounded = std::round(points);
  if (!(rounded > 0.0)) {
    return 0;
  }
  constexpr double kMostPoints = 1.0e6;
  return static_cast<std::int32_t>(std::min(rounded, kMostPoints));
}

OrderRefusal OrderLimitLot(const ProductionConfig& config,
                           WorldState& current,
                           const OrderRow& order) {
  const OrderRefusal refusal = LotOrderable(config.limit, order.lot, current.epoch);
  if (refusal != OrderRefusal::kNone) {
    return refusal;
  }
  const LimitLotDef& def = config.limit.lots[order.lot.value];
  if (def.kind == LimitLotKind::kService) {
    return OrderMtsColumn(config, current, order.lot, def.points);
  }
  // THE ROOM BEFORE THE POINTS, for the reason the column checks its season
  // first: a refusal must not cost anything. «Некуда поставить — нельзя
  // заказать» (district design §1).
  if (def.kind == LimitLotKind::kLivestock &&
      static_cast<float>(KolkhozHeads(current) + def.head_count) >
          PlacesForStock(config, current)) {
    return OrderRefusal::kNoRoomForStock;
  }
  if (current.limit.points < def.points) {
    return OrderRefusal::kLimitShort;
  }
  current.limit.points -= def.points;
  current.ledger.current.limit_points_spent += def.points;

  // THE DELAY IS DETERMINED BY THE WORLD, NOT RANDOM: the world's generator
  // state at this moment and the order's own tick pick it, so the same
  // campaign delivers the same lot on the same day on one worker and on many
  // (determinism, CLAUDE.md §10). It is the CURRENT state, not the campaign's
  // seed: any system that draws more or fewer numbers earlier in the tick
  // moves the day a later order's cart arrives. That is deterministic and is
  // not a promise that an unrelated change keeps the day (delivery analysis,
  // RACE-001 / UB-002, 2026-09-14).
  const std::uint32_t spread = config.limit.delivery_delay_days_max + 1U;
  RngState rng = SeedRngState(current.rng.state ^ order.issued_tick, kDeliveryDelayStream);
  const std::uint32_t delay = NextRandomBelow(rng, spread);

  const std::uint32_t arrive_day =
      static_cast<std::uint32_t>(current.calendar.day) + config.limit.delivery_days + delay;

  // STOCK TRAVELS ON NOTHING, and the same days it would have taken on a
  // cart: «голова появляется в закрытом помещении через несколько суток
  // после заказа». The delay knobs are the district's own, one pair for one
  // sentence — a second pair would be the same fact with two homes.
  if (def.kind == LimitLotKind::kLivestock) {
    LivestockArrivalRow bought;
    bought.lot = order.lot;
    bought.kind = def.livestock;
    bought.head_count = def.head_count;
    bought.arrive_day = arrive_day;
    bought.stage = def.arrives_stage;
    // The sex is the chairman's where the lot asks for it and nothing where
    // it does not: a batch comes mixed, and a `male` set on an order that was
    // never meant to carry one is dropped rather than refused.
    bought.male = def.sex_choice ? order.male : std::uint8_t{0};
    AppendRow(current.livestock_arrivals, bought);
    return OrderRefusal::kNone;
  }

  LimitDeliveryRow cart;
  cart.lot = order.lot;
  cart.arrive_day = arrive_day;
  cart.goods = def.goods;
  AppendRow(current.limit_deliveries, cart);
  return OrderRefusal::kNone;
}

void ArriveLimitDeliveries(const ProductionConfig& config, WorldState& current) {
  std::vector<LimitDeliveryId> emptied;
  for (std::uint32_t row = 0; row < current.limit_deliveries.rows.size(); ++row) {
    if (current.limit_deliveries.rows[row].arrive_day > current.calendar.day) {
      continue;
    }
    // By index and re-read: the door writes unit rows, not cart rows, but the
    // cart's own goods are what it subtracts from, so they are read afresh.
    for (std::size_t resource = 0; resource < current.limit_deliveries.rows[row].goods.size();
         ++resource) {
      const Grams left = current.limit_deliveries.rows[row].goods[resource];
      if (left <= 0) {
        continue;
      }
      const Grams placed =
          DeliverToStores(current, config, DefIdFromIndex<ResourceIdTag>(resource), left);
      current.limit_deliveries.rows[row].goods[resource] = left - placed;
    }
    // WHAT DID NOT FIT WAITS AT THE GATE (boss, parcel 211) and is offered
    // again at tomorrow's last tick; an empty cart leaves.
    if (!CarriesAnything(current.limit_deliveries.rows[row].goods)) {
      emptied.push_back(current.limit_deliveries.row_ids[row]);
    }
  }
  for (const LimitDeliveryId cart : emptied) {
    RemoveRow(current.limit_deliveries, cart);
  }
}

namespace {

/// The lot the district SELLS this species by, single head only. The price of
/// a hand-over is a share of it, so a kind the catalogue offers only as a
/// batch — piglets, chicks — has no per-head price to take a share of, and
/// the hand-over refuses rather than inventing one.
const LimitLotDef* SingleHeadLot(const LimitCatalog& limit, LivestockKindId kind) {
  for (const LimitLotDef& lot : limit.lots) {
    if (lot.kind == LimitLotKind::kLivestock && lot.livestock.value == kind.value &&
        lot.head_count == 1 && lot.points > 0) {
      return &lot;
    }
  }
  return nullptr;
}

/// What the district pays for ONE head of the given age band, in points.
///
/// THE BANDS ARE THE ONES `livestock.csv` ALREADY DRAWS and no others: a
/// newborn, a juvenile, an adult, and an adult past `life_game_years_min`.
/// A second ladder of ages here would be the same fact with two homes, and
/// the herd carries no per-head age to hang a finer one on anyway.
std::int32_t HandoverPoints(std::int32_t buy_points, float share) {
  const auto paid = static_cast<std::int32_t>(std::floor(static_cast<float>(buy_points) * share));
  return paid < 0 ? 0 : paid;
}

/// Takes up to `wanted` off a cohort counter and says how many went.
std::uint16_t TakeFromCohort(std::uint16_t& count, std::uint16_t wanted) {
  const std::uint16_t gone = std::min(count, wanted);
  count = static_cast<std::uint16_t>(count - gone);
  return gone;
}

}  // namespace

OrderRefusal OrderHandStock(const ProductionConfig& config,
                            WorldState& current,
                            const OrderRow& order) {
  const std::uint32_t row = FindRow(current.herds, order.herd);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  HerdRow& herd = current.herds.rows[row];
  // A FAMILY'S OWN ANIMAL IS NOT THE CHAIRMAN'S TO SELL. The yards' stock is
  // the families', it lives there by right, and the farm's ceiling does not
  // count it either (KolkhozHeads above) — the two rules read the same flag
  // for the same reason.
  if (herd.household_owned != 0 || herd.kind.value >= config.livestock.size()) {
    return OrderRefusal::kNotEligible;
  }
  const LivestockDef& kind = config.livestock[herd.kind.value];
  const LimitLotDef* const lot = SingleHeadLot(config.limit, herd.kind);
  if (lot == nullptr) {
    return OrderRefusal::kNotEligible;  // the district takes this kind only by the batch
  }
  const auto wanted = static_cast<std::uint16_t>(
      std::min<std::int64_t>(order.amount, std::numeric_limits<std::uint16_t>::max()));
  if (wanted == 0 || TotalHeads(herd) == 0) {
    return OrderRefusal::kNoSuchSubject;
  }
  // THE LAST SIRE STAYS. The purchase asks which sex precisely so the farm
  // cannot be left without a producer and no way to fix it; a way out of one
  // dead end that opens the way into another is not a way out at all.
  // Counted against the heads that would go, not against the herd: handing
  // over every adult of a herd with one sire is the case this catches.
  if (kind.sexed != 0 && kind.males_share > 0.0F && herd.adult_male_count > 0 &&
      wanted >= herd.adult_count && herd.adult_male_count <= 1) {
    return OrderRefusal::kLastSire;
  }
  // THE OLDEST FIRST, cohort by cohort: adults from the old end, then the
  // juveniles, then the newborns. An old head is dearer to keep and cheaper
  // to hand over, so a chairman shedding stock sheds these — and the order
  // names no head because a head is not an entity in this model.
  std::int32_t points = 0;
  const bool old_herd = MeanAdultAgeYears(herd) >= kind.life_game_years_min;
  const std::uint16_t adults_gone = TakeOldestAdults(kind, herd, wanted);
  points += static_cast<std::int32_t>(adults_gone) *
            HandoverPoints(
                lot->points,
                old_herd ? config.limit.handover_share_old : config.limit.handover_share_adult);
  auto left = static_cast<std::uint16_t>(wanted - adults_gone);
  const std::uint16_t juveniles_gone = TakeFromCohort(herd.juvenile_count, left);
  points += static_cast<std::int32_t>(juveniles_gone) *
            HandoverPoints(lot->points, config.limit.handover_share_young);
  left = static_cast<std::uint16_t>(left - juveniles_gone);
  const std::uint16_t newborns_gone = TakeFromCohort(herd.newborn_count, left);
  points += static_cast<std::int32_t>(newborns_gone) *
            HandoverPoints(lot->points, config.limit.handover_share_newborn);

  const auto gone = static_cast<std::uint32_t>(adults_gone + juveniles_gone + newborns_gone);
  current.limit.points += points;
  current.ledger.current.limit_points_granted += points;
  SimEvent& handed = EmitEvent(current, EventKind::kStockHandedOver, EventSeverity::kNotable);
  handed.herd = order.herd;
  handed.amount = static_cast<std::int64_t>(gone);
  return OrderRefusal::kNone;
}

void ArriveLivestock(const ProductionConfig& config, WorldState& current) {
  std::vector<LivestockArrivalId> landed;
  for (std::uint32_t row = 0; row < current.livestock_arrivals.rows.size(); ++row) {
    const LivestockArrivalRow arrival = current.livestock_arrivals.rows[row];
    if (arrival.arrive_day > current.calendar.day) {
      continue;
    }
    // THE HERD IT JOINS: a kolkhoz herd of the same kind, the one standing at
    // a unit first. A head put into a household's own herd would change whose
    // animal it is, and ownership is not what a purchase decides.
    std::uint32_t home = kNoRow;
    for (std::uint32_t index = 0; index < current.herds.rows.size(); ++index) {
      const HerdRow& herd = current.herds.rows[index];
      if (herd.household_owned != 0 || herd.kind.value != arrival.kind.value) {
        continue;
      }
      if (home == kNoRow || (herd.unit.value != kInvalidEntityIdValue &&
                             current.herds.rows[home].unit.value == kInvalidEntityIdValue)) {
        home = index;
      }
    }
    // NO HERD OF THAT KIND AT ALL — a village whose team died to the last
    // head has exactly that, and it is the case this whole window exists
    // for. The row is made, standing nowhere in particular; the billeting
    // walk of the herd day puts it where there is room.
    if (home == kNoRow) {
      HerdRow founded;
      founded.kind = arrival.kind;
      AppendRow(current.herds, founded);
      home = static_cast<std::uint32_t>(current.herds.rows.size()) - 1U;
    }
    HerdRow& herd = current.herds.rows[home];
    if (arrival.stage == LivestockArrivalStage::kYoung) {
      herd.newborn_count = static_cast<std::uint16_t>(herd.newborn_count + arrival.head_count);
    } else {
      herd.adult_count = static_cast<std::uint16_t>(herd.adult_count + arrival.head_count);
      herd.adult_male_count = static_cast<std::uint16_t>(
          herd.adult_male_count + (arrival.male != 0 ? arrival.head_count : 0));
      // AND THE AGE, WHICH IS THE WHOLE OF "в начале взрослого возраста".
      // A head entered at age nil would be a free extra lifetime bought for
      // the same seventy points, and the age total is what the death draw
      // reads (herd_state.h, adult_age_game_years_total).
      const float entry = arrival.kind.value < config.livestock.size()
                              ? config.livestock[arrival.kind.value].adult_from_game_months /
                                    static_cast<float>(kMonthsPerYear)
                              : 0.0F;
      herd.adult_age_game_years_total += entry * static_cast<float>(arrival.head_count);
    }
    landed.push_back(current.livestock_arrivals.row_ids[row]);
  }
  for (const LivestockArrivalId head : landed) {
    RemoveRow(current.livestock_arrivals, head);
  }
}

void TurnLimitYear(const ProductionConfig& config, WorldState& current, bool plan_fully_met) {
  // The closing year's book is still `current` here: the ledger turns in the
  // events slot, later in this same tick (core_world/world.cpp, RotateLedger),
  // which is also where the new year's grant is booked.
  current.ledger.current.limit_points_burned += current.limit.points;
  // STUB: the farm's status tier is kLagging until the economic readiness
  // index exists, and the overfulfilment term is zero until the core can
  // deliver above the plan (boss, parcel 211).
  current.limit.points = YearLimitPoints(config.limit,
                                         FarmStatusTier::kLagging,
                                         plan_fully_met,
                                         0.0F,
                                         current.chairman.raikom_reputation);
}

void GrantFirstLimitYear(const ProductionConfig& config, WorldState& current) {
  current.limit.points = YearLimitPoints(
      config.limit, FarmStatusTier::kLagging, false, 0.0F, current.chairman.raikom_reputation);
  current.ledger.current.limit_points_granted = current.limit.points;
}

}  // namespace core
