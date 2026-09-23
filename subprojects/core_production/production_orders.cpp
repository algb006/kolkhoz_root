// The production half of the order book (core_production/production_orders.h).
// Moved whole out of production_system.cpp (boss, parcel 332).

#include "production_orders.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "district_limit.h"
#include "district_plan.h"
#include "district_trip.h"
#include "extraction_digging.h"
#include "field_removal.h"
#include "herd_system.h"
#include "night_pasture.h"
#include "stock_ops.h"
#include "timber_felling.h"

namespace core {
namespace {

/// @brief The chairman opens a sealed fund (resources design §6).
///
/// IT SUBTRACTS AND NOTHING ELSE. The funds are notional — the grain is
/// one heap and the ladder is a computation over it — so an unsealing is
/// recorded as a release the fund's computation then asks for less by.
/// The consequences the design names are already there and need no code:
/// less in the store come the delivery is a plan fallen short, less come
/// the sowing is a spring undersown.
///
/// REFUSED WHEN THE FUND DOES NOT HOLD IT, because a door that opens on to
/// nothing has not been opened. The chairman is told so and the figure he
/// named stands — the alternative, quietly giving him whatever is there,
/// is the "незаметная утечка" the design refuses by name.
OrderRefusal UnsealFund(const ProductionConfig& config,
                        WorldState& current,
                        const OrderRow& order) {
  // THE FUND IS NAMED EXPLICITLY AND NOT BY A TERNARY'S "else". A ternary
  // on kSeed makes every other value of the enum mean the PLAN reserve,
  // including kNone — and kNone reaches here, because the codecs accept it
  // (kMaxFundKind includes zero, rightly: it is the value every other kind
  // of order carries) while only the boundary refuses it. An order replayed
  // out of a journal therefore skips ShapeIsValid, and the quiet answer
  // would have been to open the plan reserve nobody named.
  // BOUNDED BY THE ARRAY, NOT BY A LIST OF BAD VALUES. The guard named
  // kNone and the count sentinel for one afternoon, and that was a
  // blacklist where the code needed a range: `FundKind` has a fixed
  // underlying type, so an object of it may hold 0..255, and everything
  // from 5 up walked past both names into `operator[]` on a four-slot
  // array — not for a stray read, because the next lines call `resize()`
  // and assign. The boundary could not be relied on to have screened it
  // either: it carried the identical two-value test, and an order built by
  // the graphics layer off a stale copy of this enum is a drift this very
  // file already records as having happened once.
  //
  // The if/else chain this replaced was a WHITELIST and was immune. The
  // hole arrived with the array, which is worth saying out loud: a reshape
  // that makes data cheaper to extend can quietly move a guard from naming
  // what is allowed to naming what is not.
  const auto slot = static_cast<std::size_t>(order.fund);
  if (order.fund == FundKind::kNone || slot >= current.unsealed.by_fund.size()) {
    return OrderRefusal::kNoSuchSubject;
  }
  ResourceAmounts* const released = &current.unsealed.by_fund[slot];
  // THE RESOURCE IS CHECKED AGAINST THE ROSTER and not merely against the
  // invalid marker. These vectors are dense by ResourceId, and resizing one
  // to an id that no table row backs would take it past that contract — up
  // to 65535 cells for a 16-bit id — and the save refuses to write a vector
  // that long, which turns a mistyped order into a campaign that can never
  // be saved again. The boundary cannot make this check: a table-less world
  // is legal there by design, and the roster is the factory's knowledge.
  const std::uint32_t index = order.resource.value;
  if (index == kInvalidDefIdValue || index >= config.spoil_days.size()) {
    return OrderRefusal::kNoSuchSubject;
  }
  const Grams opened = index < released->size() ? (*released)[index] : 0;
  // NEITHER NUMBER MAY BE NEGATIVE, and both can arrive so. The releases
  // come back from a save through ReadAmounts with no range check, and the
  // order's own amount through a raw ReadU64 cast — and a row replayed out
  // of a save never passes the boundary's ShapeIsValid, which is where the
  // "amount > 0" rule lives. A negative `opened` turns each of the three
  // ceilings below into `held + |opened|`, signed overflow inside the very
  // comparison the subtraction was written to keep safe: the same defect
  // arriving from the other side. Guarded once for all three rather than
  // patched at the one the analysis happened to name.
  if (opened < 0 || order.amount <= 0) {
    return OrderRefusal::kRuleForbids;
  }
  // What the fund is holding RIGHT NOW is the fund's own business and is
  // recomputed by its owner every day, so the only ceiling this verb can
  // honestly enforce is the one it can see: the plan reserve may not be
  // opened past what the district asked for.
  //
  // WRITTEN AS A SUBTRACTION, because the sum it replaces overflowed inside
  // the very check meant to catch it: `opened + amount > owed` on a signed
  // 64-bit pair is undefined before it is false.
  // THE FODDER FUND OPENS FODDER GRAIN AND NOTHING ELSE. Its ceiling is a
  // ceiling on WHAT, not on how much: the fund is the working stock's oats
  // and barley (resources design §6), and a door that let hay out of it
  // would put four hundred tonnes nobody but the animals can eat into the
  // kolkhoz fund — a release with no cost, which is the shape this whole
  // verb was built to avoid.
  //
  // Told apart by the feed roster's own flag rather than by a list of keys
  // here — `fodder_fund` since 0.34.17, `work_only` before it (see below):
  // that flag IS the statement "this feed is the fund's", it comes from the
  // design db, and
  // a second list of fodder grains in this file would be its second home.
  if (order.fund == FundKind::kFodder) {
    // AND TOLD APART BY `fodder_fund`, NOT BY `work_only`, since 0.34.17:
    // the fund holds the feeds the design names for it (oats and barley),
    // and FodderClaimGrams sizes it by the same flag. Asked by work_only,
    // this door said "a fodder grain" of the horse's rye and wheat and left
    // the refusal to a ceiling of nought — two doors, one fund.
    bool fund_feed = false;
    for (const FeedLinkDef& link : config.feed_links) {
      if (link.fodder_fund != 0 && link.resource.value == index) {
        fund_feed = true;
        break;
      }
    }
    if (!fund_feed) {
      return OrderRefusal::kRuleForbids;
    }
    // AND NOT WIDER THAN THE RUNG TODAY. It was the year's work ration of
    // the working stock until 0.34.17 — a size the ladder held nowhere, so
    // the order could open a year of oats that no rung was keeping. The rung
    // is now held (fund_ladder.h, FodderRungLeft), and its size today is the
    // team's ration until the next reaping, capped by the last one (boss,
    // boss-core-epoch1-resume seq 14).
    const Grams held = FodderClaimGrams(config, current, order.resource);
    if (opened > held || order.amount > held - opened) {
      return OrderRefusal::kRuleForbids;
    }
  }
  if (order.fund == FundKind::kPlanReserve) {
    const Grams owed = index < current.plan.due.size() ? current.plan.due[index] : 0;
    // "THE PLAN HAS NOT BEEN NAMED YET" IS ITS OWN ANSWER, and it was
    // kRuleForbids until 2026-09-12 — which sent a chairman looking for a
    // rule that does not exist. JudgePlan clears plan.due at the year's
    // turn and AnnouncePlan fills it again on the first day of spring, so
    // for eight days of forty-eight the share this door is measured by has
    // no number behind it. The fund is not empty and the rule does not
    // forbid; what is missing is the figure, and waiting for the spring
    // announcement is a move the player can actually make.
    //
    // Told apart from a fund already drawn to its ceiling by looking at
    // the WHOLE vector and not this one resource: a plan that names no rye
    // is a plan, and refusing rye with "no plan yet" would be a lie about
    // a district that simply asked for something else.
    bool announced = false;
    for (const Grams due : current.plan.due) {
      announced = announced || due > 0;
    }
    if (!announced) {
      return OrderRefusal::kNoPlanYet;
    }
    if (opened > owed || order.amount > owed - opened) {
      return OrderRefusal::kRuleForbids;
    }
  }
  // THE SEED FUND HAS NO CEILING HERE, and inventing one would be worse
  // than having none: its size comes from the sowing norms over the fields
  // still to be sown, which core_residents computes and this module does
  // not know. What CAN be checked from here is that the running total
  // stays a number — an unbounded `+=` of a signed 64-bit amount is
  // undefined the moment it wraps, and the seed side has nothing else
  // stopping it from being ordered twice.
  if (order.amount > std::numeric_limits<Grams>::max() - opened) {
    return OrderRefusal::kRuleForbids;
  }
  // AND THE VECTOR GROWS ONLY AFTER THE REFUSALS. It grew before them for
  // one afternoon, so an order that was turned down still enlarged the
  // state it was refused by.
  if (released->size() <= index) {
    released->resize(static_cast<std::size_t>(index) + 1U, 0);
  }
  (*released)[index] += order.amount;
  return OrderRefusal::kNone;
}

/// @brief The player tells a field what to grow for three years.
///
/// THE ONE DECISION THE CORE COULD NOT TAKE UNTIL NOW, and its absence is
/// why ninety-three of the start's hundred and sixty-three hectares lay
/// unworked through every thirty-year run this project has measured. Work
/// is opened off the rotation; a field with no chain has none opened; and
/// nothing anywhere could give a field a chain after genesis. The order
/// existed from the first day and had no consumer (order_state.h said so),
/// so the lever was drawn on the boundary and connected to nothing.
///
/// A CHAIN, NOT A CROP. It sets the three seasons and never touches
/// `field.crop` — what is in the ground this minute is this year's sowing
/// and was settled when it went in.
///
/// AND THE CHAIN IS LIVE THE SAME STEP IT SETTLES, not at the year's turn:
/// this runs in ConsumeOrders at the top of RunProductionDecisions and
/// RunFields runs at the bottom of the same call, so an idle arable field
/// sows from the new year0 the moment the month enters that crop's window,
/// and the autumn sowing reads the new year1 in the same August-September.
/// AnnouncePlan says the same thing about the norm and says it correctly.
/// A first draft of this comment claimed the opposite (analysis, 0.17.96);
/// deferring the chain to the year's turn would be a staged slot set
/// applied in RunYearStart — a code change and a boss question, not a
/// sentence.
///
/// AND THE FIRST NAMED CROP IS THE ONE THE NEXT SOWING PUTS IN THE GROUND,
/// whatever month the order arrives (boss, 2026-09-12). RunYearStart
/// rotates the three slots on 1 January, so a chain written as named in
/// November — which is exactly when a chairman with a finished harvest
/// would lay one out — would have its first crop shifted into the third
/// slot before any window opened: THREE YEARS LATE FOR GIVING THE ORDER ON
/// TIME, and nothing would have told him. That is "do not punish the
/// unforeseeable" broken by a calendar detail.
///
/// THE CHAIN IS WRITTEN AS NAMED AND THE TURN IS HELD UNTIL IT IS USED,
/// which is the third answer to this and the first that holds.
///
/// The first answer rotated the slots on the way in — write (c, a, b) and
/// let January bring a to the top — and had three reachable holes: a winter
/// crop in the LAST named slot landed in year0 and was sown that same
/// autumn ahead of the crop named first; "is spring over" asked the whole
/// crop table instead of the named crop; and an order settled on the year's
/// first day was rotated by the turn a few lines below it. Worse than any
/// of them, the field then held a chain the chairman's own order sheet did
/// not match.
///
/// The second asked the CALENDAR — hold the turn if the first named crop's
/// window is past — and the analysis pass found that the month is only a
/// proxy: a field carrying a standing crop, an order settling after the
/// day's field walk, and a cold spring all leave the ground unsown while
/// the month says there is time.
///
/// So the slots go in exactly as ordered, FieldRow::rotation_skips_turn is
/// set on every chain written here, and the FIELD spends it when work opens
/// from the chain (OpenPlowing). The turn holds while it stands. A chain
/// whose first season is used the same year — a spring order, or a winter
/// crop named first in August — turns with everything else.
///
/// AN EMPTY SLOT IS A FALLOW YEAR, AND THREE OF THEM ARE THE CHAIRMAN
/// TAKING HIS WORD BACK (boss, 2026-09-12). One or two empty slots in a
/// chain are fallow years and the field is worked; all three empty and the
/// field goes back to "nobody has told it anything" — `rotation_assigned`
/// is cleared. The alternative was a field told once and worked for ever,
/// whose nearest release was an all-fallow chain that is still ploughed,
/// recovered and manured every year: a layout mistake costing work for the
/// rest of the campaign, against "a planning mistake must not cost the
/// game". No new order kind for it — the door is this one.
///
/// WHAT THE RELEASE DOES NOT TOUCH, and it is worth saying because the
/// word "release" sounds wider than the act: work already opened on the
/// field runs to its end. The phase machine keys off `field.phase` and
/// `field.crop`, both settled when the ploughing opened, so a field
/// released mid-season is still harrowed, sown, grown and reaped, and the
/// manure already spread stays in the ground. Only the NEXT year finds no
/// chain to open. Taking the standing work back would be a second decision
/// — abandoning a sown field — and this order does not carry it.
///
/// @return kNoSuchSubject for a field that is not there; kNoSuchCrop for a
///         slot naming a crop this build's table does not carry — two
///         cases with two repairs, which is why they are two words since
///         2026-09-12; kWrongLand for a meadow, which is mown where it
///         grew and is never sown at all.
OrderRefusal SetRotation(const ProductionConfig& config,
                         WorldState& current,
                         const OrderRow& order) {
  const std::uint32_t row = FindRow(current.fields, order.field);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  FieldRow& field = current.fields.rows[row];
  if (field.kind != LandKind::kArable) {
    return OrderRefusal::kWrongLand;
  }
  // EVERY SLOT IS EITHER A CROP THIS BUILD KNOWS OR NOTHING AT ALL. An id
  // that names no row is not a fallow year — it is a layer and a core
  // disagreeing about the crop table, and writing it into the field would
  // put a subject into the rotation that every reader of crop norms would
  // then ask questions of. The boundary checks the SHAPE of an order and
  // says so; the roster is this module's knowledge.
  std::array<CropId, 3> slots = {order.rotation_year0, order.rotation_year1, order.rotation_year2};
  std::uint32_t named = 0;
  for (const CropId slot : slots) {
    if (slot.value == kInvalidDefIdValue) {
      continue;
    }
    if (slot.value >= config.crops.size()) {
      return OrderRefusal::kNoSuchCrop;
    }
    ++named;
  }
  if (named == 0) {
    // THE WORD TAKEN BACK, and the slots are cleared with the byte: a
    // released field must not keep the crops of the chain it no longer
    // has, or TrySow would sow from a rotation nobody owns.
    field.rotation_year0 = CropId{};
    field.rotation_year1 = CropId{};
    field.rotation_year2 = CropId{};
    field.rotation_assigned = 0;
    // And the standing turn goes with them: a field with no chain has no
    // phase to hold still, and a bit left set would spend itself on
    // whatever chain the next order writes.
    field.rotation_skips_turn = 0;
    return OrderRefusal::kNone;
  }
  field.rotation_year0 = slots[0];
  field.rotation_year1 = slots[1];
  field.rotation_year2 = slots[2];
  field.rotation_assigned = 1;
  // A FRESH CHAIN HAS NOT USED ITS FIRST SEASON YET, and that — not the
  // month — is what the mark says (land_state.h, rotation_skips_turn). It
  // is cleared by the field itself, the moment work opens from the chain.
  field.rotation_skips_turn = 1;
  return OrderRefusal::kNone;
}

/// @brief «Освободить склад» (kEmptyStore; start §5; registers 214, 233).
/// @return kNoSuchSubject for no such unit; kNotEligible for anything but the
///         church store or a clamp; kRuleForbids to empty with no other built
///         store that takes a food, or to cancel an order that does not stand.
OrderRefusal EmptyStore(const ProductionConfig& config,
                        WorldState& current,
                        const OrderRow& order) {
  const std::uint32_t row = FindRow(current.units, order.unit);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  const UnitTypeId type = current.units.rows[row].type;
  if (type.value != config.church_store_type.value && type.value != config.clamp_type.value) {
    return OrderRefusal::kNotEligible;
  }
  if (order.enable == 0) {
    UnitRow& unit = current.units.rows[row];
    if (unit.emptying == 0) {
      return OrderRefusal::kRuleForbids;  // nothing to cancel
    }
    unit.emptying = 0;
    unit.haul_days_remaining = 0.0F;
    unit.haul_days_written = 0.0F;
    return OrderRefusal::kNone;
  }
  // «КОГДА ПОСТРОЕН ХОТЬ ОДИН СКЛАД, ПРИНИМАЮЩИЙ ЕДУ» (start §5): a built
  // store, not this one and not being emptied, that takes some food.
  bool somewhere = false;
  for (std::uint32_t other = 0; other < current.units.rows.size() && !somewhere; ++other) {
    const UnitRow& store = current.units.rows[other];
    if (other == row || !StoresGoods(store, config)) {
      continue;
    }
    for (std::uint32_t index = 0; index < config.food_kcal_per_gram.size() && !somewhere; ++index) {
      somewhere = config.food_kcal_per_gram[index] > 0.0F &&
                  NumberedStoreTakes(store, config, DefIdFromIndex<ResourceIdTag>(index));
    }
  }
  if (!somewhere) {
    return OrderRefusal::kRuleForbids;
  }
  current.units.rows[row].emptying = 1;
  return OrderRefusal::kNone;
}

void Settle(OrderRow& order, OrderRefusal refusal) {
  order.status = refusal == OrderRefusal::kNone ? OrderStatus::kDone : OrderStatus::kRefused;
  order.refusal = refusal;
}

/// @brief Stops a unit or starts it again.
/// @return kNoSuchSubject when there is no such unit, kRuleForbids for a
///         site (level 0 is pegs and string — there is no production to
///         stop) and for an order that asks for the state the unit is
///         already in: "pause the paused" is not a no-op to be swallowed,
///         it means the chairman is looking at something stale.
OrderRefusal SetPaused(const ProductionConfig& config,
                       WorldState& current,
                       UnitId unit,
                       std::uint8_t paused) {
  const std::uint32_t row = FindRow(current.units, unit);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  // A LEVEL-0 ROW IS PAUSED ONLY WHILE WORK GOES ON AT IT: a started
  // building (materials on the way or labour going in) or a demolition —
  // construction design §6, the human's word of 2026-09-14, "стройку и снос
  // можно ставить на паузу". A marked plot is a plan, not work. Until then
  // every level-0 row was refused. A standing unit under an upgrade has one
  // flag for both: pausing it stops its production and its upgrade together.
  const UnitRow& target = current.units.rows[row];
  const bool work_at_site = target.construction.phase == ConstructionPhase::kDelivering ||
                            target.construction.phase == ConstructionPhase::kBuilding ||
                            target.construction.phase == ConstructionPhase::kDemolishing;
  if (target.level == 0 && !work_at_site) {
    return OrderRefusal::kRuleForbids;
  }
  // ONLY A UNIT THAT PRODUCES IS STOPPED BY A PAUSE (units rules §5; boss's
  // ruling on econ's audit, П5 / R1, 2026-09-18 — a defect of the core, not
  // a revision). A school or a house has no production to stand still: a
  // pause on it would only stop its wear, which is the ESCAPE the ruling
  // closed. Work at a site stays pausable whatever the class — the building
  // and the demolition are the work, and the human's word of 2026-09-14
  // stands. RESUME IS NEVER REFUSED on this ground, so a unit paused before
  // the rule can always be let go.
  const bool pausable =
      target.type.value < config.unit_types.size() && config.unit_types[target.type.value].pausable;
  // A STORE BEING EMPTIED PAUSES ITS CARRYING, NOT ITSELF (kEmptyStore; boss
  // seq 117 Б and 119). The pause is written into `emptying` — 2 holds the
  // perevalka, 1 lets it go — and `paused` is not touched, so the church
  // keeps wearing as it did. For one version it set `paused`, and that
  // stopped the wear too: the escape the ruling above closed, reopened by a
  // new door (boss seq 119).
  if (target.emptying != 0 && !work_at_site) {
    const std::uint8_t wanted = paused != 0 ? 2U : 1U;
    if (target.emptying == wanted) {
      return OrderRefusal::kRuleForbids;
    }
    current.units.rows[row].emptying = wanted;
    SimEvent& held = EmitEvent(current,
                               paused != 0 ? EventKind::kUnitPaused : EventKind::kUnitResumed,
                               EventSeverity::kNotable);
    held.unit = unit;
    return OrderRefusal::kNone;
  }
  if (paused != 0 && !pausable && !work_at_site) {
    return OrderRefusal::kNotEligible;
  }
  if (current.units.rows[row].paused == paused) {
    return OrderRefusal::kRuleForbids;
  }
  current.units.rows[row].paused = paused;
  // The two verbs announce themselves here, where the flag turns, and not
  // in the order book beside kOrderDone: the order is that the chairman
  // asked, the event is that the unit stopped. They are the same tick and
  // different facts, and only the second one is what the player sees in
  // the world.
  SimEvent& event = EmitEvent(current,
                              paused != 0 ? EventKind::kUnitPaused : EventKind::kUnitResumed,
                              EventSeverity::kNotable);
  event.unit = unit;
  return OrderRefusal::kNone;
}

}  // namespace

/// @brief The production half of the order book (task A8): the two verbs
/// that stop a unit and start it again.
///
/// Settled IN THE STEP THEY ARE READ, like the construction kinds and
/// unlike an appointment: there is nothing to wait for. The design's
/// "stops when the running cycle ends" (unit rules §5) is a promise about
/// CYCLES, and the core has none — when it does, this is where the waiting
/// will be, and the order is what will wait.
void ConsumeProductionOrders(const ProductionConfig& config, WorldState& current) {
  for (OrderRow& order : current.orders.rows) {
    if (order.status != OrderStatus::kPending) {
      continue;
    }
    switch (order.kind) {
      case OrderKind::kPauseUnit:
        Settle(order, SetPaused(config, current, order.unit, 1));
        break;
      case OrderKind::kResumeUnit:
        Settle(order, SetPaused(config, current, order.unit, 0));
        break;
      case OrderKind::kUnsealFund:
        Settle(order, UnsealFund(config, current, order));
        break;
      case OrderKind::kSetRotation:
        Settle(order, SetRotation(config, current, order));
        break;
      case OrderKind::kMarkFelling:
        Settle(order, MarkFelling(config, current, order));
        break;
      case OrderKind::kMarkExtraction:
        Settle(order, MarkExtraction(config, current, order));
        break;
      case OrderKind::kOrderLimitLot:
        // A refusal for goods nowhere to store names them in the row's
        // resource (boss seq 159); a lot order carries none otherwise.
        Settle(order, OrderLimitLot(config, current, order, &order.resource));
        break;
      case OrderKind::kRemoveField:
        Settle(order, RemoveField(current, order));
        break;
      case OrderKind::kGrazeAtNight:
        Settle(order, OrderNightPasture(config, current));
        break;
      case OrderKind::kHandStock:
        Settle(order, OrderHandStock(config, current, order));
        break;
      case OrderKind::kDeliverPlan:
        Settle(order, DeliverPlanNow(config, current, order.resource, order.amount));
        break;
      case OrderKind::kEmptyStore:
        Settle(order, EmptyStore(config, current, order));
        break;
      case OrderKind::kTripToDistrict:
        Settle(order, OrderTripToDistrict(config, current));
        break;
      case OrderKind::kTradePlan:
        Settle(order, OrderTradePlan(config, current, order));
        break;
      default:
        break;  // not ours: another consumer's, or the events slot's refusal
    }
  }
}

}  // namespace core
