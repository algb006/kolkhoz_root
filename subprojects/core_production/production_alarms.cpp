// The alarm walks of production_alarms.h. Nothing here writes: the world
// arrives const and leaves untouched, and the only output is the vector the
// caller passes in.

#include "production_alarms.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "core_common/away_in_district.h"
#include "core_common/calendar.h"
#include "core_common/day_off.h"
#include "core_common/land_state.h"
#include "core_common/quantities.h"
#include "core_common/rain_stops_work.h"
#include "core_common/reaping_pace.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"
#include "district_plan.h"
#include "field_work.h"
#include "herd_life.h"
#include "herd_system.h"
#include "stock_ops.h"

namespace core {
namespace {

/// Free room of every numbered store together, in grams — what a harvest
/// has to fit into. Outline-bounded stores are unbounded and are left out
/// of the sum: counting them would make the answer meaningless.
///
/// The `continue` that drops them was UNREACHABLE until 2026-09-07 —
/// StoresGoods read a blank ladder cell as a zero and dropped the outlines
/// a line earlier — so this function has always been right, and until that
/// day it was right for the wrong reason. Nothing here changed; the reason
/// did.
Grams FreeRoomOfStores(const ProductionConfig& config, const WorldState& world) {
  Grams room = 0;
  for (const UnitRow& unit : world.units.rows) {
    if (!StoresGoods(unit, config)) {
      continue;
    }
    const Grams free_here = FreeRoomGrams(unit, config);
    if (free_here == std::numeric_limits<Grams>::max()) {
      continue;
    }
    room += free_here;
  }
  return room;
}

/// The crop the field's rotation sows next: the slot of the coming year,
/// which is what the player has just assigned and what the alarm is about
/// ("an alarm at assignment, not in spring" — farming design §7).
CropId NextSownCrop(const ProductionConfig& config,
                    const WorldState& world,
                    const FieldRow& field) {
  const std::uint32_t year = world.calendar.date.year;
  const CropId slots[3] = {field.rotation_year0, field.rotation_year1, field.rotation_year2};
  const CropId next = slots[(year + 1) % 3];
  return next.value < config.crops.size() ? next : CropId{};
}

/// Grams of seed the next sowing is short of, 0 when it is covered or
/// when there is nothing to sow. Sowing takes ORDINARY produce of the
/// crop out of the stores (§7), so the question is what the stores hold.
Grams SeedShortfall(const ProductionConfig& config,
                    const WorldState& world,
                    const FieldRow& field) {
  if (field.kind != LandKind::kArable) {
    return 0;
  }
  const CropId next = NextSownCrop(config, world, field);
  if (next.value >= config.crops.size()) {
    return 0;
  }
  const CropDef& crop = config.crops[next.value];
  if (crop.sowing_norm_kg_per_ha <= 0.0F) {
    return 0;
  }
  const auto need = GramsFromKilograms(crop.sowing_norm_kg_per_ha * field.area_ga);
  const Grams have = HeldEverywhere(world, crop.resource);
  return have >= need ? 0 : need - have;
}

/// @brief What this field will still put into a store this season, in
///        grams — its claim on the shared room.
///
/// THE RULE, AND IT IS WORTH HAVING A NAME BECAUSE IT HAS NOW BEEN GOT
/// WRONG THREE TIMES IN A WEEK:
///
///   A FIELD'S CLAIM ON THE ROOM IS EVERYTHING THAT WILL ARRIVE FROM IT,
///   NOT THE THING THE FIELD IS NAMED AFTER.
///
/// The three were: a field being reaped, which fell out of the walk
/// altogether; a load already cut and lying on the ground, which is not in
/// a store but is going to be; and the STRAW that arrives in the same
/// delivery as the grain and was never counted at all. Each was found
/// singly, by measurement, after it had already cost a harvest. Named here
/// so the fourth is found by reading instead.
///
/// WHAT WILL LAND, NOT WHAT LIES. Four parts, and only the first was ever
/// counted:
///
/// 1. A crop still GROWING claims its whole expected yield.
/// 2. A crop being REAPED claims the part still standing. It used to claim
///    nothing at all — the alarm's loop skipped any field that was not
///    growing — and that is the defect host measured on 0.17.28: the room
///    promised to two other fields was already spoken for by twenty-seven
///    tonnes of timothy that landed two days later. The alarm may keep
///    silent about a field being reaped; THE ARITHMETIC MAY NOT.
/// 3. A load already CUT and lying on the field claims its own weight. It
///    is not in a store, so it has not reduced today's free room, and it
///    goes in the moment there is anywhere to put it.
/// 4. THE STRAW ARRIVES WITH THE GRAIN, through the same door, in the same
///    tick — `yield x straw_ratio`, and the ratio runs from 0.8 for
///    buckwheat to 1.5 for rye. A rye field therefore delivers two and a
///    half times the tonnage the forecast was crediting it with. host
///    traced one: an oat field's warning stood from day 20 to day 26 at
///    11.6 t, WENT OUT on day 27 because the grain by then fitted, and on
///    day 30 the load landed with 26.6 t of straw beside it.
///
///    And the straw is the sharper half: it HAS NO BUFFER. Grain that does
///    not fit waits on the field; straw that does not fit is written off
///    the same tick (Harvest, lost_no_room). Only the standing part of the
///    crop brings straw — what is already cut has already had its straw
///    placed or lost.
///
/// The standing part is measured by the labour left against the labour the
/// phase started with (harvest_days_per_ha x hectares), because that is
/// the only measure of "how much of this field is still uncut" the state
/// carries. It is a share of the estimate, not a second estimate.
Grams RoomClaimOf(const ProductionConfig& config, const FieldRow& field) {
  // A CLAIM IS ROOM FOR WHAT HAS NOT BEEN DELIVERED. That one sentence
  // settles a fork this code spent a day inside (boss, 2026-09-06): "what
  // is still STANDING" is wrong under a one-shot harvest and "the whole
  // crop" is wrong under a gradual one, while "the whole crop minus what
  // has already reached a store" is right under both. Delivered grain
  // occupies its room itself and needs no reservation; everything else
  // does, on the stalk or in a heap alike.
  //
  // The heap on the field is UNDELIVERED, so it adds rather than
  // subtracts. While this year's crop is being reaped the only heap a
  // field can hold is LAST year's (the half-cut heap is unreachable by
  // design — a state that changes no decision is not modelled), and last
  // year's load has not touched today's free room either.
  Grams claim = field.reaped_grams > 0 ? field.reaped_grams : 0;
  if (field.kind != LandKind::kArable || field.crop.value >= config.crops.size()) {
    return claim;
  }
  const bool growing = field.phase == FieldPhase::kGrowing;
  const bool reaping = field.phase == FieldPhase::kHarvest;
  if (!growing && !reaping) {
    return claim;
  }
  const CropDef& crop = config.crops[field.crop.value];
  const float soil = field.fertility / config.farming.fertility_neutral;
  const Grams expected = GramsFromKilograms(crop.yield_kg_per_ha * field.area_ga * soil);
  // Grain and straw travel together, so the standing crop claims both.
  const float with_straw = 1.0F + (crop.straw_ratio > 0.0F ? crop.straw_ratio : 0.0F);
  // A FIELD BEING REAPED CLAIMS ALL OF IT, exactly as a standing one does,
  // and the share of labour left has no part in the answer.
  //
  // It used to claim only the STANDING share, on the stated ground that
  // "the cut part is already accounted as reaped_grams". THAT PREMISE IS
  // FALSE IN THIS CODE: nothing is placed while a field is being reaped —
  // Harvest() runs once, when the phase FINISHES, and until that moment
  // reaped_grams is zero and no straw has been delivered. So the cut part
  // was accounted in neither place: gone from the standing share, not yet
  // a heap. The hole is widest on the last day of reaping, when almost
  // nothing is standing and the whole yield lands tomorrow.
  //
  // host measured it before it was explained (0.17.58, seed 53, oat f7,
  // 10.5 ha): the warning stood at 22.63 t through d29, went dark for d30
  // alone, and 11.46 t landed on d31. One day of silence, in the one day
  // that mattered — and a signal that goes out just before the trouble
  // does not read as silence, it reads as "it turned out fine".
  //
  // Two diagnoses were offered for it first and both were wrong: the
  // forecast counting grain without straw (fixed in 0.17.36) and the claim
  // HALVING at the cut (there is no halving — there is a drop to nothing
  // and back). The measurement outlived both explanations, which is the
  // argument for keeping it.
  // THE SUBTRACTION STANDS HERE EXPLICITLY, and its term is zero by the
  // model rather than by omission: Harvest() runs ONCE, when the phase
  // finishes, so not a gram of THIS year's crop reaches a store while the
  // field is growing or being reaped. The day the harvest becomes gradual,
  // this is the one place that has to learn what has gone — and it will
  // read a number instead of an assumption, because the assumption is
  // written down here as a number.
  const Grams delivered_this_year = 0;
  const auto standing_and_cut = static_cast<Grams>(static_cast<float>(expected) * with_straw);
  return claim + (standing_and_cut - delivered_this_year);
}

/// @brief Field rows ordered by when their crop is reaped, then by row.
///
/// Only the order matters, so the key is the whole months from today to
/// the crop's first harvest month, wrapped: a field whose crop is reaped
/// next month comes before one reaped in eleven, and the answer does not
/// change when the year rolls over. A field with no crop of the roster
/// sorts last on a key of twelve — it is not growing anything the alarm
/// can be about, and the loop skips it anyway.
std::vector<std::uint32_t> FieldsInHarvestOrder(const ProductionConfig& config,
                                                const WorldState& world) {
  const auto today = static_cast<std::uint32_t>(world.calendar.date.month);
  std::vector<std::uint32_t> order(world.fields.rows.size());
  for (std::uint32_t row = 0; row < order.size(); ++row) {
    order[row] = row;
  }
  const auto months_away = [&config, &world, today](std::uint32_t row) {
    const FieldRow& field = world.fields.rows[row];
    // A field being reaped, or one already holding a cut load, lands NOW —
    // whatever month its crop is nominally reaped in. Reading the month
    // alone put a field whose harvest month has just PASSED eleven months
    // into the future and let three others spend the room in front of it.
    if (field.phase == FieldPhase::kHarvest || field.reaped_grams > 0) {
      return 0U;
    }
    if (field.crop.value >= config.crops.size()) {
      return kMonthsPerYear;
    }
    const auto month =
        static_cast<std::uint32_t>(config.crops[field.crop.value].harvest_from_month);
    return (month + kMonthsPerYear - today) % kMonthsPerYear;
  };
  std::stable_sort(order.begin(), order.end(), [&months_away](std::uint32_t a, std::uint32_t b) {
    return months_away(a) < months_away(b);
  });
  return order;
}

/// kHerdWithoutStable: the farm owns a horse team and the yard has not
/// reached its second step, so the team ages and cannot renew itself
/// (alarm_state.h).
///
/// LIT ON THE FOUNDING MORNING, and that is a measurement and not a
/// convenience. The order said "light it with the death of the first
/// horse", for the sake of an early date with a natural link. The date
/// is earlier than that and the link is the same one: the sixteen start
/// horses are SIXTEEN HERDS OF ONE HEAD, aged 1.8 to 7.8 game years
/// against a lifespan band of 6 to 8, so four of them stand inside the
/// death band on day zero and the first head goes on day 33 (seed 1930,
/// core 2026-09-05). There is no morning on which this team is not
/// dying; waiting for the first death would only spend a fifth of the
/// 144-day deadline saying nothing.
///
/// AND IT COSTS NO STATE. "A horse has died" is a transition and the
/// world keeps no per-kind tally of one, so that predicate would need a
/// new field in HerdRow — a save-format change, and the save version is
/// the human's to raise. "The farm has horses and no stable" is a
/// property of the completed state, which is what an alarm is allowed to
/// be (alarm_state.h).
///
/// ONE ALARM FOR THE TEAM, not one per row. The team is sixteen rows at
/// the start and one after the horses are stabled, and sixteen identical
/// lines on the founding morning would bury the very line they are. The
/// core has no id for "the team", so the subject is the first of its
/// rows in row order — deterministic, and the amount is the whole team's.
///
/// THREE THINGS IT DOES NOT LOOK AT, and each was deliberate:
///   * whether a yard exists at all — a yard at step one is a pen and
///     breeds nobody, so building one must not silence the ask;
///   * whether a groom is appointed — kYardWithoutGroom goes out on the
///     appointment and the team goes on dying behind that silence, which
///     is the defect this kind was written for;
///   * how many head are left — the outcome has no warning form: 26 head
///     on day 160 and none on day 168.
void CollectStableAlarms(const ProductionConfig& config,
                         const WorldState& world,
                         std::vector<Alarm>& alarms) {
  if (config.horse_kind.value == kInvalidDefIdValue || StableBuilt(world, config)) {
    return;
  }
  std::uint32_t first = kNoRow;
  std::int64_t heads = 0;
  for (std::uint32_t row = 0; row < world.herds.rows.size(); ++row) {
    const HerdRow& herd = world.herds.rows[row];
    // The farm's own team, wherever it stands: the start keeps it at
    // private yards and it is kolkhoz property there (herd_state.h), so
    // the place says nothing and the ownership says everything. A
    // family's own mare is not the chairman's business.
    if (herd.household_owned != 0 || herd.kind.value != config.horse_kind.value) {
      continue;
    }
    const std::int64_t mine = static_cast<std::int64_t>(herd.newborn_count) +
                              static_cast<std::int64_t>(herd.juvenile_count) +
                              static_cast<std::int64_t>(herd.adult_count);
    if (mine <= 0) {
      continue;  // an emptied row is not a team
    }
    first = first == kNoRow ? row : first;
    heads += mine;
  }
  if (first == kNoRow) {
    return;  // no horses: nothing to lose, and no stable to ask for
  }
  Alarm alarm;
  alarm.kind = AlarmKind::kHerdWithoutStable;
  alarm.herd = world.herds.row_ids[first];
  alarm.amount = heads;
  alarms.push_back(alarm);
}
}  // namespace

/// kStoreFull: a numbered store holding at least its capacity. A store
/// bounded by the outline the player drew has no number to be full
/// against and never raises it (stock_ops.h, StorageCapacityGrams).
void CollectStoreAlarms(const ProductionConfig& config,
                        const WorldState& world,
                        std::vector<Alarm>& alarms) {
  for (const UnitRow& unit : world.units.rows) {
    if (!StoresGoods(unit, config)) {
      continue;
    }
    const Grams capacity = StorageCapacityGrams(unit, config);
    if (capacity <= 0 || RoomUsed(unit.stock, config) < capacity) {
      continue;
    }
    Alarm alarm;
    alarm.kind = AlarmKind::kStoreFull;
    alarm.unit = world.units.row_ids[static_cast<std::size_t>(&unit - world.units.rows.data())];
    alarm.amount = capacity;
    alarms.push_back(alarm);
  }
}

/// kHarvestWaitingOnField, kHarvestWillNotFit and kSeedShort — the three
/// conditions of a field, in kind order so that the caller's sort has
/// less to do (it still sorts: row order is not id order).
void CollectFieldAlarms(const ProductionConfig& config,
                        const WorldState& world,
                        std::vector<Alarm>& alarms) {
  // THE ROOM IS ONE AND THE FIELDS SHARE IT, so it is SPENT as the loop
  // walks them and not re-offered whole to each. Comparing every field
  // against the whole free room is the defect host measured on 0.17.24:
  // three fields of fifty tonnes facing sixty tonnes of room each "fit",
  // nobody is warned, and a hundred and fifty arrive. The warning then
  // became true only once the room had already shrunk below one field —
  // which happens because the harvest has started — so a forecast was
  // being compared against TODAY's room and fired at the same tick as the
  // loss it exists to precede (window measured: 0 days on two seeds of
  // three, against a granary that takes 15 days to raise).
  //
  // AND IT IS SPENT IN THE ORDER THE FIELDS WILL BE REAPED, because the
  // alarm points at ONE field and the player walks to it. Row order would
  // do the arithmetic just as well — the sum is the same whoever is
  // named — but it would name an arbitrary field as the one that will not
  // fit, and an arbitrary answer shown as a definite one is a lie. What
  // comes in last is what finds the room gone; that is causally true and
  // not merely consistent (boss, 2026-09-05).
  //
  // The order is read off the crop's harvest month, counted forward from
  // today so that a crop reaped in two months precedes one reaped in
  // eleven whatever the numbers happen to be. Fields whose crops are
  // reaped in the same month keep row order between them: that much IS
  // arbitrary, and there is nothing in the model that says otherwise.
  // EVERY FIELD SPENDS THE ROOM ITS PRODUCE WILL TAKE; only some of them
  // are told about it. The two are separate questions and used to be one:
  // the loop skipped a field that was not growing, so a field being reaped
  // was neither warned about nor SUBTRACTED, and the room promised to the
  // fields behind it had already been spoken for by the load that landed
  // two days later (host, seed 1930: twenty-seven tonnes of timothy).
  //
  // The alarm may keep silent about a field. The arithmetic may not.
  Grams room_left = FreeRoomOfStores(config, world);
  for (const std::uint32_t row : FieldsInHarvestOrder(config, world)) {
    const FieldRow& field = world.fields.rows[row];
    const Grams claim = RoomClaimOf(config, field);
    const Grams over = claim > room_left ? claim - room_left : 0;
    room_left = claim >= room_left ? 0 : room_left - claim;
    // THE ALARM BURNS UNTIL THE HARVEST IS RESOLVED, and "resolved" means
    // stored or lost — not "the field changed phase".
    //
    // It used to go out the moment the field left kGrowing, and host
    // measured what that looks like from the outside: a median of FOUR
    // DAYS of silence between the warning going dark and the load hitting
    // the ground, every single time. The field enters its harvest a few
    // days before the grain lands, the alarm stops, and the last thing the
    // player sees before losing the crop is the warning going away.
    //
    // A SIGNAL THAT SWITCHES OFF JUST BEFORE THE TROUBLE DOES NOT READ AS
    // SILENCE. IT READS AS "IT TURNED OUT FINE" (host, 2026-09-05), and
    // acting on the last state you were shown is the whole of what a live
    // signal is for. This one told the player he was safe.
    //
    // So `phase == kGrowing` is gone from here. It was answering two
    // questions at once — "can this be estimated" and "should this go on
    // warning" — which is the same shape as the two it was untangled from
    // this week: the room's arithmetic against the alarm's silence, and a
    // month counted forward against one counted back. THE ESTIMATE still
    // comes from a growing field alone, and it does: RoomClaimOf gives a
    // reaped field the part still standing and a heap its own weight, both
    // of which are known BETTER than a forecast, not worse.
    //
    // It goes out when the claim goes to zero, which happens when the load
    // is carried into a store or written off. That is the trouble ending,
    // one way or the other, and either is a thing the player can see.
    if (over > 0 && field.kind == LandKind::kArable) {
      const bool standing =
          field.phase == FieldPhase::kGrowing || field.phase == FieldPhase::kHarvest;
      Alarm alarm;
      alarm.kind = AlarmKind::kHarvestWillNotFit;
      alarm.field = world.fields.row_ids[row];
      // What the produce IS: the crop while any of it is still on the
      // stalk, the load's own resource once the field is only a heap. A
      // field lying under last year's rye may already have this year's
      // crop written in its rotation slot.
      alarm.resource = standing && field.crop.value < config.crops.size()
                           ? config.crops[field.crop.value].resource
                           : field.reaped_resource;
      alarm.amount = over;
      alarms.push_back(alarm);
    }
    if (field.reaped_grams > 0) {
      Alarm alarm;
      alarm.kind = AlarmKind::kHarvestWaitingOnField;
      alarm.field = world.fields.row_ids[row];
      alarm.resource = field.reaped_resource;
      alarm.amount = field.reaped_grams;
      alarms.push_back(alarm);
    }
    const Grams short_of = SeedShortfall(config, world, field);
    if (short_of > 0) {
      Alarm alarm;
      alarm.kind = AlarmKind::kSeedShort;
      alarm.field = world.fields.row_ids[row];
      alarm.resource = config.crops[NextSownCrop(config, world, field).value].resource;
      alarm.amount = short_of;
      alarms.push_back(alarm);
    }
  }
}

/// kHerdStarving: a kolkhoz herd that went underfed and has not been fed
/// since. A household herd is the family's business, not the farm's.
void CollectHerdAlarms(const ProductionConfig& config,
                       const WorldState& world,
                       std::vector<Alarm>& alarms) {
  for (std::uint32_t row = 0; row < world.herds.rows.size(); ++row) {
    const HerdRow& herd = world.herds.rows[row];
    if (herd.unfed_days <= 0.0F || herd.household.value != kInvalidEntityIdValue) {
      continue;
    }
    Alarm alarm;
    alarm.kind = AlarmKind::kHerdStarving;
    alarm.herd = world.herds.row_ids[row];
    alarm.amount = static_cast<std::int64_t>(herd.newborn_count) +
                   static_cast<std::int64_t>(herd.juvenile_count) +
                   static_cast<std::int64_t>(herd.adult_count);
    alarms.push_back(alarm);
  }
  // The pig slaughter that waits for room (herd_life.h): the herd, and the
  // meat the stores lack room for.
  for (std::uint32_t row = 0; row < world.herds.rows.size(); ++row) {
    const Grams short_of = AutumnSlaughterMeatShort(config, world, world.herds.rows[row]);
    if (short_of <= 0) {
      continue;
    }
    Alarm alarm;
    alarm.kind = AlarmKind::kSlaughterWaitsForRoom;
    alarm.herd = world.herds.row_ids[row];
    alarm.resource = config.meat_resource;
    alarm.amount = short_of;
    alarms.push_back(alarm);
  }
  CollectStableAlarms(config, world, alarms);
}

namespace {

/// The three seasons a chain lays out: year0, year1, year2.
constexpr std::uint32_t kChainYears = 3;

CropId ChainSlot(const FieldRow& field, std::uint32_t year) {
  if (year == 0) {
    return field.rotation_year0;
  }
  return year == 1 ? field.rotation_year1 : field.rotation_year2;
}

/// Hectares of arable whose chain grows `produce` in `year` of its three. BY
/// PRODUCE AND NOT BY CROP KEY: the district's figure is owed in resource, and
/// any crop that yields it pays it. Also returns the worked arable, the area
/// the district prices next year's norm from.
float ChainHectares(const ProductionConfig& config,
                    const WorldState& world,
                    ResourceId produce,
                    std::uint32_t year,
                    float& worked_ha) {
  float grown_ha = 0.0F;
  worked_ha = 0.0F;
  for (const FieldRow& field : world.fields.rows) {
    if (field.kind != LandKind::kArable || !HasRotation(field)) {
      continue;
    }
    worked_ha += field.area_ga;
    const CropId slot = ChainSlot(field, year);
    if (slot.value < config.crops.size() && config.crops[slot.value].resource == produce) {
      grown_ha += field.area_ga;
    }
  }
  return grown_ha;
}

/// kPlanPositionShort (boss seq 89): on the year's last day, every position
/// the turn's delivery cannot bring to the met share. The turn takes what is
/// owed from the stores (DeliverPlan), so the forecast is delivered plus the
/// least of owed and takeable — the same walk the take makes (IsTakenFrom).
void CollectPlanShortAlarms(const ProductionConfig& config,
                            const WorldState& world,
                            std::vector<Alarm>& alarms) {
  // «ЗА СУТКИ ДО ПОВОРОТА ГОДА», the boss's word: the day the turn follows.
  if (world.plan.announced == 0 || world.calendar.day % kDaysPerYear != kDaysPerYear - 1) {
    return;
  }
  for (std::uint32_t index = 0; index < world.plan.due.size(); ++index) {
    const Grams due = world.plan.due[index];
    if (due <= 0) {
      continue;
    }
    const Grams delivered = index < world.plan.delivered.size() ? world.plan.delivered[index] : 0;
    const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
    const Grams owed = due > delivered ? due - delivered : 0;
    const Grams takeable = TakeableGrams(world, config, resource);
    const Grams shipped_at_turn = delivered + (takeable < owed ? takeable : owed);
    if (PositionDelivered(config, due, shipped_at_turn)) {
      continue;
    }
    const auto met_grams = static_cast<Grams>(
        std::llround(static_cast<double>(due) * static_cast<double>(config.plan_met_share)));
    Alarm alarm;
    alarm.kind = AlarmKind::kPlanPositionShort;
    alarm.resource = resource;
    // At least a gram: the share compares in float and the grams in double,
    // and a position PositionDelivered calls short is never short by nought.
    alarm.amount = std::max<Grams>(met_grams - shipped_at_turn, 1);
    alarms.push_back(alarm);
  }
}

}  // namespace

void CollectPlanAlarms(const ProductionConfig& config,
                       const WorldState& world,
                       std::vector<Alarm>& alarms) {
  // Positions in table order, years in chain order: the emission order is a
  // function of the tables and the chains, and the session sorts it anyway.
  for (const ProductionConfig::PlanPosition& position : config.plan_positions) {
    if (position.crop.value >= config.crops.size() || !(position.area_share > 0.0F)) {
      continue;
    }
    const ResourceId produce = config.crops[position.crop.value].resource;
    for (std::uint32_t year = 0; year < kChainYears; ++year) {
      float worked_ha = 0.0F;
      const float grown_ha = ChainHectares(config, world, produce, year, worked_ha);
      // THE DISTRICT'S OWN RATE, READ BACKWARDS, and not a forecast (boss,
      // 2026-09-13; district design §9: the norm is off the worked arable and
      // a normal yield per hectare). The position is yield × area × share, so
      // the hectares that pay it at a normal yield are area × share. This year
      // is priced off last year's worked land, the two after off today's.
      //
      // IT WAS PRESENCE UNTIL THE SAME DAY, and presence lied: on seed 1933 the
      // chairman closed a missing potato with a 3.5 ha field against 4.63 ha
      // owed — the alarm went out on a correct-looking action that did not
      // help, and the plan failed anyway. What this one still does not say is
      // how much THAT field's fertility will fall short; fertility is visible
      // on the ground, and that is the player's call.
      const float priced_ha = year == 0 ? world.plan.worked_ha_last_year : worked_ha;
      const float owed_ha = priced_ha * position.area_share * config.plan_grain_share;
      const bool covered = owed_ha > 0.0F ? grown_ha >= owed_ha : grown_ha > 0.0F;
      if (covered) {
        continue;
      }
      Alarm alarm;
      alarm.kind = AlarmKind::kPlanPositionUncovered;
      alarm.resource = produce;
      alarm.amount = year;
      alarms.push_back(alarm);
    }
  }
  CollectPlanShortAlarms(config, world, alarms);
}

namespace {

/// Square metres in a hectare: the unit kSowingWillNotFit's amount is in.
constexpr double kSquareMetresPerHectare = 10000.0;

/// One spring field in the plough, as the sowing alarm wants it.
struct SowingClaim {
  std::uint32_t row = 0;
  std::int32_t last_sowing_day = 0;  ///< day of the year; later, and it will not ripen
  float team_days = 0.0F;            ///< ploughing and harrowing still owed
  float area_ga = 0.0F;
};

/// The adult horses of every herd of the horse kind: the same pool labor caps
/// its harnessed crews with (labor_system.cpp, DraughtHorses), one horse to
/// one man.
std::uint32_t DraughtTeam(const ProductionConfig& config, const WorldState& world) {
  std::uint32_t horses = 0;
  for (const HerdRow& herd : world.herds.rows) {
    if (config.horse_kind.value != kInvalidDefIdValue &&
        herd.kind.value == config.horse_kind.value) {
      horses += herd.adult_count;
    }
  }
  return horses;
}

/// The spring fields that have entered the plough and still owe harnessed
/// work, with the last day on which each can be sown and still ripen before
/// the snow — the same test FinishSowing's gate asks (field_work.cpp).
std::vector<SowingClaim> SpringFieldsInThePlough(const ProductionConfig& config,
                                                 const WorldState& world,
                                                 std::uint32_t day_of_year) {
  std::vector<SowingClaim> claims;
  for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
    const FieldRow& field = world.fields.rows[row];
    float team_days = 0.0F;
    if (field.phase == FieldPhase::kPlowing) {
      team_days = field.work_days_remaining + (field.area_ga * config.farming.harrow_days_per_ha);
    } else if (field.phase == FieldPhase::kHarrowing) {
      team_days = field.work_days_remaining;
    }
    const std::int32_t ripen = RipenDays(config, field.crop);
    if (field.kind != LandKind::kArable || !(team_days > 0.0F) || ripen == 0) {
      continue;  // not harnessed work, or a crop the snow does not gate
    }
    const std::int32_t last_day = static_cast<std::int32_t>(config.growing_season_last_day) - ripen;
    // A DAY ALREADY GONE IS NOT A FORECAST: that field is lost, and its loss
    // is the unsown field the player sees, not this warning.
    if (last_day < static_cast<std::int32_t>(day_of_year)) {
      continue;
    }
    claims.push_back(SowingClaim{
        .row = row, .last_sowing_day = last_day, .team_days = team_days, .area_ga = field.area_ga});
  }
  return claims;
}

}  // namespace

namespace {

/// One annual still to be reaped, as the gathering alarm wants it.
struct GatherClaim {
  std::uint32_t row = 0;
  std::int32_t open_day = 0;  ///< day of the year its reaping may open
  float owed_days = 0.0F;     ///< reaping still owed, norm-days
};

/// The village's hands before the season has shown its pace: residents of
/// working age with a home to leave from — labor's own rule (Employable),
/// read off the same life.csv row, one norm-day each.
std::uint32_t HandsOfTheVillage(const ProductionConfig& config, const WorldState& world) {
  std::uint32_t hands = 0;
  Vec2 home;
  for (const ResidentRow& person : world.residents.rows) {
    const float age =
        BiologicalAgeYears(config.farming.life_speedup, person.birth_day, world.calendar.day);
    // Away in the district is nobody's hand — labor's Employable says so,
    // and this copy of that rule has to say it too.
    if (age >= config.farming.adult_age_years && HomePositionOf(world, person.family, home) &&
        !OffWork(person, world.calendar.tick)) {
      ++hands;
    }
  }
  return hands;
}

/// The annuals standing or being reaped that the snow gates, each with the
/// first day its reaping may open — the reaping gate's own answer, asked day
/// by day — and the reaping it still owes. A crop that cannot open before
/// the snow is not here: that is the sowing's loss, not the reaping's.
std::vector<GatherClaim> AnnualsToGather(const ProductionConfig& config,
                                         const WorldState& world,
                                         std::uint32_t day_of_year) {
  std::vector<GatherClaim> claims;
  const auto snow = static_cast<std::int32_t>(config.growing_season_last_day);
  const SimDay year_start = world.calendar.day - day_of_year;
  for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
    const FieldRow& field = world.fields.rows[row];
    const bool standing =
        field.phase == FieldPhase::kGrowing || field.phase == FieldPhase::kHarvest;
    if (field.kind != LandKind::kArable || !standing || field.crop.value >= config.crops.size() ||
        RipenDays(config, field.crop) == 0) {
      continue;
    }
    const CropDef& crop = config.crops[field.crop.value];
    GatherClaim claim{.row = row};
    if (field.phase == FieldPhase::kHarvest) {
      claim.open_day = static_cast<std::int32_t>(day_of_year);
      claim.owed_days = field.work_days_remaining;
    } else {
      claim.open_day = -1;
      for (auto day = static_cast<std::int32_t>(day_of_year); day <= snow; ++day) {
        const auto month =
            static_cast<std::uint8_t>(static_cast<std::uint32_t>(day) / kDaysPerMonth);
        if (ReapingMayOpen(config, field, month, year_start + static_cast<SimDay>(day))) {
          claim.open_day = day;
          break;
        }
      }
      claim.owed_days = crop.harvest_days_per_ha * field.area_ga;
    }
    // THE HORIZON (boss seq 161 А): a field whose reaping opens further off
    // than the chairman needs to act is not judged yet — its pace would be
    // today's, and today's hands are not the autumn's.
    const auto horizon = static_cast<std::int32_t>(config.farming.gather_alarm_horizon_days);
    if (claim.open_day >= 0 && claim.owed_days > 0.0F &&
        claim.open_day <= static_cast<std::int32_t>(day_of_year) + horizon) {
      claims.push_back(claim);
    }
  }
  return claims;
}

}  // namespace

void CollectGatherAlarms(const ProductionConfig& config,
                         const WorldState& world,
                         std::vector<Alarm>& alarms) {
  const std::uint32_t day_of_year = world.calendar.day % kDaysPerYear;
  std::vector<GatherClaim> claims = AnnualsToGather(config, world, day_of_year);
  if (claims.empty()) {
    return;
  }
  // IN THE ORDER THEY RIPEN, the reaping's days spent as the sowing alarm
  // spends the team's: the field that opens last finds what is left.
  std::ranges::stable_sort(claims, [](const GatherClaim& left, const GatherClaim& right) {
    return left.open_day < right.open_day;
  });
  // THE PACE (boss seq 91, option В): once the season has reaped, the best
  // day it has been seen to manage; before that, every hand at one norm-day
  // — optimistic on purpose, so that it does not cry before there is a
  // season to read.
  //
  // THE BEST DAY UNDER TODAY'S SUN (boss seq 95): a man reaps from sunrise to
  // sunset less the road, so the best day's norm-days are scaled by today's
  // daylight over the best day's. Taken of TODAY and not of each day to come:
  // the light keeps falling to the snow, so this still errs on the side of
  // silence, only by less than 15.2 h against 8.4 h did. One home with
  // labor's last days (core_common/reaping_pace.h).
  const double pace = ReapingPacePerDay(
      world.ledger.current, world.weather.daylight_hours, HandsOfTheVillage(config, world));
  //
  // A DAY OF PACE IS A DRY DAY (core_common/rain_stops_work.h): rain stops
  // the reaping, so the days to the snow are counted by the climate's dry
  // share, and the clock walks forward over the rain as well as the work.
  // TODAY IS NOT A FORECAST: its sky is written, and its share is 0 or 1.
  RainDayShares ahead = config.rain_day_shares;
  ahead[day_of_year] = RainStopsWork(world.weather.precipitation, WorkKind::kHarvest) ? 1.0F : 0.0F;
  // A DAY OFF IS NO WORKING DAY EITHER (the static loop of 23 September):
  // labor's queue before the snow never counted the Sundays and holidays,
  // this alarm did, and was a seventh more hopeful than the village that
  // does the reaping. Written as "no dry share" so that the one walk
  // (DryDaysBetween, CalendarPointAfterDryDays) skips it. The rest of the
  // year is not asked: nothing past the snow counts.
  const SimDay year_start = world.calendar.day - day_of_year;
  for (std::uint32_t day = day_of_year; day < kDaysPerYear; ++day) {
    if (IsDayOffIn(world, year_start + static_cast<SimDay>(day))) {
      ahead[day] = 1.0F;
    }
  }
  // TO THE EARLY SNOW (production_config.h, gather_alarm_snow_day): whichever
  // comes first of the probe's P10 and the climate's mean edge. Which fields
  // are judged at all still asks the mean edge (AnnualsToGather): a crop that
  // cannot open before it is the sowing's loss, and one that opens between
  // the two edges is exactly the one to warn about.
  //
  // THE EARLY SNOW IS THE FIRST DAY THE SNOW LIES, and that day counts for
  // nothing: RunFields takes a standing field on its morning. econ's answer
  // (econ-boss-snow-edge-reading): the key is the P10 of the first kSnow
  // day, so the last day that still counts is the one before. 0.34.16
  // counted the snow's own day and was a day late. The mean edge is a last
  // SAFE day and is counted as it is.
  const auto snow = std::min(static_cast<double>(config.growing_season_last_day),
                             static_cast<double>(config.farming.gather_alarm_snow_day) - 1.0);
  double clock = static_cast<double>(day_of_year);
  for (const GatherClaim& claim : claims) {
    const double start = std::max(clock, static_cast<double>(claim.open_day));
    const double available = DryDaysBetween(ahead, start, snow + 1.0);
    const double needed = pace > 0.0 ? static_cast<double>(claim.owed_days) / pace
                                     : std::numeric_limits<double>::infinity();
    clock = CalendarPointAfterDryDays(ahead, start, needed);
    if (needed <= available) {
      continue;
    }
    const FieldRow& field = world.fields.rows[claim.row];
    const CropDef& crop = config.crops[field.crop.value];
    Alarm alarm;
    alarm.kind = AlarmKind::kHarvestWillNotBeGathered;
    alarm.field = world.fields.row_ids[claim.row];
    alarm.resource = crop.resource;
    // THE WHOLE FIELD, and not the share left unreaped (boss seq 176): the
    // crop is gathered only when its reaping finishes, and the snow on a field
    // still being reaped takes all of it (LoseFieldToSnow). Until 2026-09-19
    // this named the missing share — less than the snow takes.
    alarm.amount = static_cast<std::int64_t>(FieldYieldGrams(config, field, crop));
    alarms.push_back(alarm);
  }
}

void CollectSowingAlarms(const ProductionConfig& config,
                         const WorldState& world,
                         std::vector<Alarm>& alarms) {
  const std::uint32_t day_of_year = world.calendar.day % kDaysPerYear;
  std::vector<SowingClaim> claims = SpringFieldsInThePlough(config, world, day_of_year);
  // THE ROOM IS DAYS, SPENT IN THE ORDER THE FIELDS MUST BE SOWN — the echo of
  // the harvest alarm's room spent in reaping order, and measured so
  // (tests/run/sowing_window): summed against the NEAREST deadline, every
  // other field was asked against a day that was not its own. Ties keep row
  // order; nothing in the model says otherwise.
  std::ranges::stable_sort(claims, [](const SowingClaim& left, const SowingClaim& right) {
    return left.last_sowing_day < right.last_sowing_day;
  });
  // THE CAPACITY IS THE TEAM (sowing_window, 2026-09-13): of hands, the
  // settlement's own rate and the horses, only the horses never missed a year
  // the others caught and were ever silent. An optimistic bound — a horse's
  // whole day at one norm-day — so the alarm cannot cry wolf.
  const auto team = static_cast<double>(DraughtTeam(config, world));
  double spent = 0.0;
  for (const SowingClaim& claim : claims) {
    const double days =
        static_cast<double>(claim.last_sowing_day) - static_cast<double>(day_of_year) + 1.0;
    const double available = days - spent > 0.0 ? days - spent : 0.0;
    // No horses and no harnessed work gets done at all: the whole field is short.
    const double needed = team > 0.0 ? static_cast<double>(claim.team_days) / team
                                     : std::numeric_limits<double>::infinity();
    spent += needed;
    if (needed <= available) {
      continue;
    }
    const double short_share = team > 0.0 ? (needed - available) / needed : 1.0;
    Alarm alarm;
    alarm.kind = AlarmKind::kSowingWillNotFit;
    alarm.field = world.fields.row_ids[claim.row];
    const FieldRow& field = world.fields.rows[claim.row];
    alarm.resource = config.crops[field.crop.value].resource;
    alarm.amount = static_cast<std::int64_t>(static_cast<double>(claim.area_ga) * short_share *
                                             kSquareMetresPerHectare);
    alarms.push_back(alarm);
  }
}

}  // namespace core
