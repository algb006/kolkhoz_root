// The supply of a works: its seam, a leg at a time, and its price before a
// start (field_haul.h). Split out of field_haul.cpp when the direct leg took
// that file past the thousand-line limit: a load carted INTO the stores and a
// works' materials carted out of them to a site are different journeys.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "cart_trips.h"
#include "core_common/day_off.h"
#include "core_common/haul.h"
#include "core_common/hauled_book.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/road_route.h"
#include "core_common/site_supply_floor.h"
#include "core_common/supply_estimate.h"
#include "core_common/unit_state.h"
#include "core_common/work_seam.h"
#include "field_haul.h"
#include "haul_ways.h"
#include "trip_terms_writer.h"

namespace core {

namespace {

/// The grams of a dense amounts vector.
Grams SumOf(const ResourceAmounts& amounts) {
  Grams sum = 0;
  for (const Grams grams : amounts) {
    sum += grams;
  }
  return sum;
}

/// The rate of one leg of a works' supply: from its sender to the site, the
/// log cart's way for logs straight off a stand (StandHaulRate's mode).
/// @param carried What a carrier on foot would take (RateToward): read only
///        when no draught horse is free.
HaulRate LegRate(const ProductionConfig& config,
                 const WorldState& world,
                 const SupplyLeg& leg,
                 Vec2 site,
                 ResourceId carried = ResourceId{}) {
  return RateToward(config,
                    world,
                    leg.from,
                    site,
                    leg.log_way != 0 ? TravelMode::kLogCart : TravelMode::kCart,
                    carried);
}

/// The seam-days of bringing `leg`'s grams to `site` today. A CART MIXES: the
/// sum at the cart's rate. A CARRIER ON FOOT TAKES ONE RESOURCE A TRIP
/// (transport design §2, «Смешанное снабжение стройки»; #671): each resource
/// by its own form's trip - the straw's bundle, the boards' armful - summed.
/// `by_resource`, when given, receives the days of each (on foot only).
float LegDays(const ProductionConfig& config,
              const WorldState& world,
              const SupplyLeg& leg,
              Vec2 site,
              std::vector<float>* by_resource = nullptr) {
  if (SettlementHasCarts(world, config.horse_kind)) {
    return CartDaysFor(config, SumOf(leg.grams), LegRate(config, world, leg, site));  // В4
  }
  float days = 0.0F;
  if (by_resource != nullptr) {
    by_resource->assign(leg.grams.size(), 0.0F);
  }
  // One way for every resource (the walker's), asked of the roads once; the
  // trip's grams differ by the form.
  const HaulRate walked = LegRate(config, world, leg, site);
  for (std::size_t index = 0; index < leg.grams.size(); ++index) {
    if (leg.grams[index] <= 0) {
      continue;
    }
    // A LOG GOES BY TWO OR NOT AT ALL (haul.h, LogPairRate): its days are a
    // pair's heads', whether today's gate lets a pair carry or not - the
    // seam of a log no pair may carry is simply not drained (labour's gate).
    HaulRate rate = walked;
    if (index == config.porter.log.value) {
      rate = LogPairRate(config.porter, walked);
    } else {
      rate.load = WalkerLoadGrams(config.porter, DefIdFromIndex<ResourceIdTag>(index));
    }
    const float own = HaulDaysFor(leg.grams[index], rate, config.standard_day_hours);
    days += own;
    if (by_resource != nullptr) {
      (*by_resource)[index] = own;
    }
  }
  return days;
}

/// A MEADOW'S CUT OPEN IN ITS WINDOW TODAY: a meadow in kHarvest with its cut
/// not done, in a month up to meadow_cut_month_end - the cut's window by the
/// same month the meadow's opening reads (field_work.cpp, RunMeadow) and the
/// labour's FieldWindow gives the cut its tier by.
bool MowingHoldsTheCarters(const ProductionConfig& config, const WorldState& current) {
  const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
  if (month > config.farming.meadow_cut_month_end) {
    return false;
  }
  for (const FieldRow& field : current.fields.rows) {
    const bool meadow =
        field.kind == LandKind::kMeadow || field.kind == LandKind::kFloodplainMeadow;
    if (meadow && field.phase == FieldPhase::kHarvest && field.work_days_remaining > 0.0F) {
      return true;
    }
  }
  return false;
}

/// THE FLOOR'S MEMORY, SETTLED IN THE EVENING (#734; ConstructionState::
/// supply_idle_days, core_common/site_supply_floor.h): a works with something
/// awaiting counts the working day on which not one gram landed, and starts
/// again from nought on a day anything did. A day off and a day the works
/// stood paused neither count nor reset - nobody carts on either by rule, and
/// neither is the works standing unserved (the crewless days' own reading,
/// construction_system.cpp). Here and not in labour: production is the module
/// that knows a delivery from a carter merely sent.
///
/// A WORKS ON THE FLOOR STAYS ON IT TILL ITS RECIPE IS ON THE SITE (the
/// contract's own word; the first pair of 0.37.243, nine villages, had it
/// wrong): a delivery does not take the count below the floor's days. With
/// the count starting again at every delivery, the floor's one cart-day
/// reset it and the works waited four working days more for the next - a
/// barrack of 28.8 t got a cart a week and stood 8-42 days delivering.
///
/// AND THE LAMP'S SHARE OF IT (save 165; stage В6 part 3, boss, the В6 thread
/// [10]-[12]): an idle evening on which a meadow's cut was open in its window
/// is counted in supply_mowing_days too. The floor reads the idle days whole
/// - the carters it gives are 0.37.276's - and kSiteSupplyWaits reads the
/// difference: «дни, когда покос забрал возчиков, не идут в срок стройки —
/// иначе каждая стройка июня–июля желтела бы по правилу». Moved into the
/// count itself (0.37.277, never released), the rule reshuffled the
/// villages' calendars both ways (the E3 trace, the thread [9]).
void SettleSupplyIdleDays(const SupplyFloorRules& rules,
                          bool mowing_holds_carters,
                          WorldState& current) {
  const bool day_off = IsDayOffIn(current, current.calendar.day);
  for (UnitRow& unit : current.units.rows) {
    ConstructionState& works = unit.construction;
    const bool moved = works.supply_moved_today != 0;
    works.supply_moved_today = 0;
    if (!AnythingAwaits(works)) {
      works.supply_idle_days = 0;
      works.supply_mowing_days = 0;
      continue;
    }
    if (moved) {
      if (!OnSupplyFloor(rules, works)) {
        works.supply_idle_days = 0;
        works.supply_mowing_days = 0;
      }
      continue;
    }
    if (day_off || unit.paused != 0) {
      continue;
    }
    if (works.supply_idle_days < std::numeric_limits<std::uint16_t>::max()) {
      ++works.supply_idle_days;
      if (mowing_holds_carters) {
        ++works.supply_mowing_days;
      }
    }
  }
}

}  // namespace

SupplyEstimate PriceSiteSupply(const ProductionConfig& config,
                               const WorldState& world,
                               SupplyEstimate plan,
                               Vec2 site) {
  if (plan.grams <= 0) {
    return plan;
  }
  // The rate's own question (work_seam.h): no adult kolkhoz horse, no cart.
  plan.on_foot = !SettlementHasCarts(world, config.horse_kind);
  // The seam's own arithmetic (SettleSiteSupply, LegDays): the rate of
  // today's road, bed and carrier from the sender to the site - ON FOOT EACH
  // RESOURCE BY ITS OWN TRIP when the plan names them (#671): until 0.37.240
  // the estimate was the sum by the sack, and with the forms in the seam it
  // would have told the chairman one price while the works paid another. A
  // plan with grams alone is still priced by the sack.
  if (plan.on_foot && !plan.by_resource.empty()) {
    SupplyLeg as_leg;
    as_leg.from = plan.sender;
    as_leg.grams = plan.by_resource;
    plan.cart_days = LegDays(config, world, as_leg, site);
    return plan;
  }
  const HaulRate rate = RateToward(config, world, plan.sender, site);
  plan.cart_days = CartDaysFor(config, plan.grams, rate);  // В4's door
  return plan;
}

void SettleSiteSupply(const ProductionConfig& config, WorldState& current, bool evening) {
  // THE TOURS OF THE EVENING (routing stage В4, WR; cart_trips.h): each works'
  // seam is priced at its share of the tours its front leg rides in, planned
  // once from the legs as they stand this evening. Empty under F and W.
  std::vector<float> tour_days;
  if (evening) {
    const std::vector<CartTour> tours = PlanSupplyTours(config, current);
    if (!tours.empty()) {
      tour_days.assign(current.units.rows.size(), -1.0F);
      for (const CartTour& tour : tours) {
        for (std::size_t stop = 0; stop < tour.stops.size(); ++stop) {
          float& days = tour_days[tour.stops[stop].unit_row];
          days = (days < 0.0F ? 0.0F : days) + StopShareOfTour(config, tour, stop);
        }
      }
    }
  }
  for (std::size_t row = 0; row < current.units.rows.size(); ++row) {
    UnitRow& unit = current.units.rows[row];
    ConstructionState& works = unit.construction;
    // Legs carted out are dropped from the front; the sender follows.
    const auto drop_empty_front = [&works]() {
      bool dropped = false;
      while (!works.legs.empty() && SumOf(works.legs.front().grams) <= 0) {
        works.legs.erase(works.legs.begin());
        dropped = true;
      }
      if (!works.legs.empty()) {
        works.supply_from = works.legs.front().from;
        works.supply_log_way = works.legs.front().log_way;
      }
      return dropped;
    };
    if (drop_empty_front()) {
      works.supply_days_remaining = 0.0F;
      works.supply_days_written = 0.0F;
    }
    if (works.legs.empty()) {
      works.supply_days_remaining = 0.0F;
      works.supply_days_written = 0.0F;
      works.supply_days_first = 0.0F;
      works.supply_days_all = 0.0F;
      continue;
    }
    // THE WORKS' WHOLE LOAD AS FIRST PRICED (ConstructionState::
    // supply_days_first): what the fixing added since the last hour, each leg
    // by its own way and today's bed.
    for (SupplyLeg& leg : works.legs) {
      if (leg.unpriced > 0) {
        // At the leg's own mix: `unpriced` is a sum of grams, and on foot the
        // price of a gram depends on its resource (LegDays).
        const Grams all = SumOf(leg.grams);
        works.supply_days_first +=
            all > 0 && !SettlementHasCarts(current, config.horse_kind)
                ? LegDays(config, current, leg, unit.position) *
                      (static_cast<float>(leg.unpriced) / static_cast<float>(all))
                : CartDaysAdded(config,  // В4's door: the new grams' share of whole trips
                                all - leg.unpriced,
                                leg.unpriced,
                                LegRate(config, current, leg, unit.position));
        leg.unpriced = 0;
      }
    }
    SupplyLeg& front = works.legs.front();
    Grams awaiting = SumOf(front.grams);
    // The drained share of what awaits ON THE FRONT LEG is on the site this
    // hour (stage V1's rule, DeliverDrained). A site has no door and no room
    // to refuse it, so all the drained work converts; a seam drained to
    // nought brings the leg's last gram, whatever the share's rounding left.
    const float done = works.supply_days_written > works.supply_days_remaining
                           ? works.supply_days_written - works.supply_days_remaining
                           : 0.0F;
    if (done > 0.0F && works.supply_days_written > 0.0F) {
      const float share = done / works.supply_days_written;
      Grams budget = works.supply_days_remaining <= 0.0F || share >= 1.0F
                         ? awaiting
                         : GramsFromFloat(static_cast<float>(awaiting) * share);
      // ON FOOT ONE RESOURCE A TRIP (#671; transport design §2): the hour's
      // work is spent on the front leg's resources in id order, each at its
      // own form's price - the first to the last gram before the next is
      // touched. Until 0.37.240 a walker's hour brought a share of the leg's
      // SUM, several resources at once. The work done is taken at today's
      // price of the leg (the seam's share of it), as the carts' is.
      std::vector<Grams> quota;
      const bool has_carts = SettlementHasCarts(current, config.horse_kind);
      if (!has_carts && budget < awaiting) {
        std::vector<float> days;
        float left = LegDays(config, current, front, unit.position, &days) * share;
        quota.assign(front.grams.size(), 0);
        budget = 0;
        for (std::size_t index = 0; index < front.grams.size() && left > 0.0F; ++index) {
          if (!(days[index] > 0.0F)) {
            continue;
          }
          quota[index] =
              left >= days[index]
                  ? front.grams[index]
                  : GramsFromFloat(static_cast<float>(front.grams[index]) * (left / days[index]));
          left -= days[index];
          budget += quota[index];
        }
      }
      // The way the carts took, for the year's book (CartLoadSource::kSupply),
      // measured before anything moved, as the day's other loads are.
      BookCartRun(config,
                  current,
                  CartLoadSource::kSupply,
                  LegRate(config, current, front, unit.position),
                  budget);
      // Who carried it (#671's instrument; hauled_book.h). A site takes all
      // the drained work, so the notes are spent whole.
      WorkAssignment hauled;
      hauled.kind = WorkKind::kHauling;
      hauled.unit = current.units.row_ids[row];
      const float foot = FootShareOf(current, hauled, has_carts);
      for (std::size_t index = 0; index < front.grams.size() && budget > 0; ++index) {
        // Never more than the works' own total holds of it: the two homes
        // are bound gram for gram (ConstructionState::legs).
        const Grams total = index < works.awaiting.size() ? works.awaiting[index] : 0;
        Grams moved = front.grams[index] < budget ? front.grams[index] : budget;
        moved = moved < total ? moved : total;
        if (!quota.empty() && moved > quota[index]) {
          moved = quota[index];
        }
        if (moved <= 0) {
          continue;
        }
        front.grams[index] -= moved;
        works.awaiting[index] -= moved;
        if (unit.stock.size() <= index) {
          unit.stock.resize(index + 1, 0);
        }
        unit.stock[index] += moved;
        BookHauled(current.ledger.current, DefIdFromIndex<ResourceIdTag>(index), moved, foot);
        // LOGS BY TWO, FOR THE YEAR'S BOOK (#671): a log that came on foot
        // came under a pair - one log a pair's trip.
        if (index == config.porter.log.value && foot > 0.0F && config.porter.log_piece_grams > 0) {
          current.ledger.current.log_pair_trips +=
              static_cast<float>(moved) * foot / static_cast<float>(config.porter.log_piece_grams);
        }
        budget -= moved;
        awaiting -= moved;
        works.supply_moved_today = 1;  // the floor's memory (#734): a gram landed
      }
      SpendHaulNotes(current, hauled, 1.0F);
      works.supply_days_written = works.supply_days_remaining;
    }
    // A LEG CARTED OUT GIVES THE SEAM TO THE NEXT: the seam is closed here and
    // written below in this same hour from the next sender («unwritten»), so
    // the carts of this hour's top-up go on to it.
    if (awaiting <= 0 && drop_empty_front()) {
      works.supply_days_remaining = 0.0F;
      works.supply_days_written = 0.0F;
      if (works.legs.empty()) {
        works.supply_days_first = 0.0F;
        works.supply_days_all = 0.0F;
        continue;
      }
      awaiting = SumOf(works.legs.front().grams);
    }
    // NOTHING IS CARTED TO A PAUSED WORKS (construction design §6, «Подвоз на
    // стоящую стройку прекращается»): its seam is closed, what awaits waits.
    if (unit.paused != 0) {
      works.supply_days_remaining = 0.0F;
      works.supply_days_written = 0.0F;
      continue;
    }
    // The seam is written in the evening, as every load's is — and in the
    // hour a load first appears with none (a site started this morning is
    // offered to the day's top-up, not to tomorrow's placement).
    const bool unwritten = works.supply_days_remaining <= 0.0F && works.supply_days_written <= 0.0F;
    if (!evening && !unwritten) {
      continue;
    }
    const bool toured = row < tour_days.size() && tour_days[row] >= 0.0F;
    works.supply_days_remaining = awaiting <= 0 ? 0.0F
                                  : toured
                                      ? tour_days[row]  // its share of the evening's tours (WR)
                                      : LegDays(config, current, works.legs.front(), unit.position);
    works.supply_days_written = works.supply_days_remaining;
    // And the front leg's trip facts, from LegDays's own rate (#1056 C3;
    // trip_terms_writer.h): the seam is the front leg's, so are its trips.
    // (The front leg read afresh: the one above may have been carted out and dropped.)
    if (!works.legs.empty()) {
      const SupplyLeg& leg_now = works.legs.front();
      WorkAssignment supplied;
      supplied.kind = WorkKind::kHauling;
      supplied.unit = current.units.row_ids[row];
      WriteTripTermsFor(config,
                        current,
                        LogisticsLoadKind::kSiteSupply,
                        supplied,
                        leg_now.from,
                        LegRate(config, current, leg_now, unit.position),
                        awaiting > 0 ? awaiting : 0);
    }
    // And what is still to cart on every leg, for the crew's size
    // (ConstructionState::supply_days_all).
    works.supply_days_all = 0.0F;
    for (const SupplyLeg& leg : works.legs) {
      works.supply_days_all += LegDays(config, current, leg, unit.position);
    }
  }
  if (evening) {
    SettleSupplyIdleDays(config.supply_floor, MowingHoldsTheCarters(config, current), current);
  }
}

}  // namespace core
