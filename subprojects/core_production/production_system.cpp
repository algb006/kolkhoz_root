// Implementation of the core_production boundary
// (include/core_production/production_system.h). Stage 4: the field life
// cycle (sowing by windows and temperature, growth under weather stress,
// harvest into storage, fertility bookkeeping, snow loss), herd feeding and
// the manure flow. Herd sizes stay static — offspring belongs to the
// feeding stage; horse offspring is additionally capacity-blocked until a
// stable exists (start rework parcel).
//
// Stage 5 wired the labor seam into it: a working phase is OPENED here with
// its demand (area x the phase's norm in game man-days) and closed here when
// the crew has drained FieldRow::work_days_remaining to zero. Production
// never calls labor and labor never calls production — the field row carries
// the whole contract (manual/65-labor-model.md §2).
//
// DELIVERY IS NO LONGER INSTANT (task A4, manual/75-logistics.md). What is
// reaped stays on the field until somebody carries it: this module sizes the
// carrying in man-days, labor drains that seam with real people, and
// SettleHauling turns what was drained back into grain in a store. Hay still
// goes to the stock yard and manure to the compost heap in the same tick —
// those two paths are the remaining instant stubs, and they are named in
// OPEN_ITEMS rather than left to be discovered.

#include "core_production/production_system.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/haul.h"
#include "core_common/ledger_state.h"
#include "core_common/order_state.h"
#include "core_common/spoilage.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_tables/tables.h"
#include "field_haul.h"
#include "herd_system.h"
#include "production_config.h"
#include "stock_lights.h"
#include "stock_ops.h"

namespace core {
namespace {

/// Wipe the weather a field has been through: both accumulators, both
/// run counters and the judgement made off them. Kept as one function
/// because there are four places that end a growing spell (a perennial
/// plan change, a field lost to snow, a fresh sowing, a finished harvest)
/// and five fields to clear — four copies of five lines is how one of them
/// eventually keeps a stale "kSoaking" on a field that is bare.
void ClearFieldWeather(FieldRow& field) {
  field.drought_stress = 0.0F;
  field.wet_stress = 0.0F;
  field.drought_run_days = 0;
  field.wet_run_days = 0;
  field.weather_state = FieldWeatherState::kNone;
}

/// @brief The id of a field row, from the row itself.
///
/// The field loops of this file hand a REFERENCE around — that is how they
/// were written, and threading an id through a dozen helpers to say one
/// sentence in the journal would be a larger change than the sentence. The
/// rows live in a vector, so the reference names its own index.
///
/// PRECONDITION, and the only one: `field` is a row of `current.fields`, not
/// a copy of one. Every caller here is inside a loop over those rows; the
/// assert catches the day somebody passes a temporary.
FieldId FieldIdOf(const WorldState& current, const FieldRow& field) {
  const auto index = static_cast<std::size_t>(&field - current.fields.rows.data());
  assert(index < current.fields.row_ids.size());
  return index < current.fields.row_ids.size() ? current.fields.row_ids[index] : FieldId{};
}

/// @brief Moves a field into a phase AND says so.
///
/// One function because there are ten places that move a phase, and ten
/// copies of "set it, then announce it" is exactly how eighteen event kinds
/// came to have no emitter at all (boss, 2026-09-05). The announcement
/// carries the NEW phase in `amount`, as the kind's contract says.
void MoveFieldPhase(WorldState& current, FieldRow& field, FieldPhase phase) {
  if (field.phase == phase) {
    return;  // a phase that did not change is not news
  }
  field.phase = phase;
  SimEvent& event = EmitEvent(current, EventKind::kFieldPhaseChanged);
  event.field = FieldIdOf(current, field);
  event.amount = static_cast<std::int64_t>(phase);
}

/// The production slot (phase 4), parallel by field: accumulates the
/// growth-season weather stress. Reads the calendar and the day's weather
/// from `current` — both blocks are written by phase 1 alone and frozen for
/// the rest of the step (buffer-law rule 4) — and writes only the owned
/// field rows.
class FieldGrowthPhase final : public IParallelPhase {
 public:
  explicit FieldGrowthPhase(const ProductionConfig& config) : config_(&config) {}

  std::uint32_t ParallelItemCount(const WorldState& current) const override {
    return static_cast<std::uint32_t>(current.fields.rows.size());
  }

  void RunItemRange(const WorldState& /*previous*/,
                    WorldState& current,
                    std::uint32_t begin_item,
                    std::uint32_t end_item) override {
    if (HourFromTick(current.calendar.tick) != 0) {
      return;  // stress is a daily quantity
    }
    const WeatherState& weather = current.weather;
    const FarmingConfig& farming = config_->farming;
    for (std::uint32_t item = begin_item; item < end_item; ++item) {
      FieldRow& field = current.fields.rows[item];
      // The meadow's judgement first, and OUTSIDE the arable gate below: a
      // meadow has no crop row, so the gate would skip it and the flower
      // would never be judged at all.
      field.in_flower = MeadowInFlower(
          field, current.calendar.day, farming.flower_from_month, farming.flower_to_month);
      if (field.phase != FieldPhase::kGrowing || field.crop.value >= config_->crops.size()) {
        continue;
      }
      const CropDef& crop = config_->crops[field.crop.value];
      // Drought is a matter of the AFTERNOON: the mean plus the day's
      // half-swing (camera design §4). On the mean alone the summer never
      // reached +25 and this branch was dead in every run before it. The
      // swing is the DAY's since the cloud parcel — a clear noon is hotter
      // — and it is read off the weather rather than off a copy of the
      // season table kept here.
      const float afternoon = weather.air_temperature_celsius + weather.temperature_swing_celsius;
      // TWO ACCUMULATORS, NOT ONE, and the branches stay exclusive as they
      // were: a day is a rain day or a heat day or neither. The sum is
      // capped where the single number used to be capped — clamping a
      // running total every day and clamping it once at the end give the
      // same value, because the increments are never negative.
      //
      // AND THE SPELL IS COUNTED IN HOT DRY DAYS, which is the design's own
      // definition and not a stricter reading of it: "затяжные +25…+30 в
      // июне–июле" (farming design §6). It was changed to "dry and warm" on
      // 2026-09-05 and changed straight back, because the check below —
      // written against that same design line months earlier — said that a
      // mild dry spell announces nothing, and it was right. The rarity of
      // kDrying is the CLIMATE's: a run of four afternoons past +25 happens
      // about twice in a lifetime here, which is what the design describes.
      // Nothing about the shape was broken; only the number is open, and the
      // number is the run length in tables/farming.csv.
      if (weather.precipitation == Precipitation::kRain) {
        field.wet_stress += farming.stress_per_day * crop.wet_sensitivity;
        field.wet_run_days = field.wet_run_days < 255 ? field.wet_run_days + 1 : 255;
        field.drought_run_days = 0;
      } else if (afternoon >= farming.drought_temp_c) {
        field.drought_stress += farming.stress_per_day * crop.drought_sensitivity;
        field.drought_run_days = field.drought_run_days < 255 ? field.drought_run_days + 1 : 255;
        field.wet_run_days = 0;
      } else {
        // A day that is neither breaks BOTH runs. "Long heat without rain"
        // means without a mild day in the middle of it either: a spell that
        // a fortnight of ordinary weather interrupts is not the spell the
        // design names.
        field.drought_run_days = 0;
        field.wet_run_days = 0;
      }
      // The judgement, made here rather than left to the reader. Whoever
      // draws the field would otherwise infer it from temperature, and a
      // second home for the rule is how a mechanic ends up with two.
      // Inside 0..1e6 by the parse — BOTH bounds, and both matter to the two
      // conversions below: a negative float and a float past UINT32_MAX are
      // equally undefined when cast to unsigned, and the `spell > 0` test
      // comes after the cast and could not help either way. The floor alone
      // was what this note used to claim, back when one knob came this way;
      // production_config.cpp bounds these two against a named limit, and
      // keeps a floor on the row they fall back to.
      // TWO THRESHOLDS AND NOT ONE, since 2026-09-05. They hold the same
      // number today and are two facts all the same: the design wants a dry
      // summer once in five to eight years and a waterlogged one once in two
      // or three, because drowning is the worse of the pair and strikes
      // twice — at the yield and at the calendar. One number set to two
      // frequencies is a number set to neither, and the old rule of choice
      // ("the largest at which both halves still fire") made each half's
      // rarity hostage to the other's.
      const auto drought_spell = static_cast<std::uint32_t>(farming.drought_spell_days);
      const auto wet_spell = static_cast<std::uint32_t>(farming.wet_spell_days);
      if (field.drought_run_days >= drought_spell && drought_spell > 0) {
        field.weather_state = FieldWeatherState::kDrying;
      } else if (field.wet_run_days >= wet_spell && wet_spell > 0) {
        field.weather_state = FieldWeatherState::kSoaking;
      } else {
        field.weather_state = FieldWeatherState::kNone;
      }
    }
  }

 private:
  const ProductionConfig* config_;
};

class ProductionSystem final : public IProductionSystem {
 public:
  explicit ProductionSystem(const ProductionConfig& config) : config_(config), phase_(config_) {}

  IParallelPhase& ProductionPhase() override { return phase_; }

  void RunProductionDecisions(const WorldState& previous, WorldState& current) override {
    // The order book first, and BEFORE the table-less early return below: a
    // world without crops still has units, and a pause is about a unit. An
    // order nobody reads is refused by the events slot with kNoConsumer, and
    // "the tables were thin" is not a reason the chairman should ever see.
    ConsumeOrders(current);
    if (config_.crops.empty()) {
      return;  // a table-less world idles (STUB)
    }
    // Finished work is picked up the same hour the crew finishes it: labor
    // runs earlier in this very slot, so a field ploughed by noon opens its
    // harrowing at noon instead of losing the afternoon.
    AdvanceFinishedPhases(current);
    if (current.calendar.tick == 1) {
      // The first winter's plan: genesis hands over a heap and a January, and
      // day zero is no year's turn for the daily bookkeeping below. Without
      // this the inherited 250 t lay untouched through the whole first year.
      PlanManure(current);
    }
    // The day's hauling is settled at its LAST tick, and the hour matters.
    // Labor runs earlier in this same slot, so by now the carriers have
    // finished walking; and settling here rather than at tomorrow's dawn
    // means the seam labor reads next morning already says what is really
    // left to carry. Settle at dawn instead and every second day would find
    // an empty demand and send nobody (task A4).
    if (HourFromTick(current.calendar.tick) + 1U >= kTicksPerDay) {
      SettleHauling(config_, current);
      // AND ONLY THEN does the day's food go bad. The village has eaten by
      // now — the meal is the needs slot, phase 2, and this is phase 3 of
      // the same tick — and eaten food cannot rot. The other way round and
      // the settlement starves beside a full store, with both halves
      // looking correct (boss, 2026-09-03; transport design §10).
      SpoilStores(config_, current);
    }
    if (current.calendar.day == previous.calendar.day) {
      return;  // everything below is daily work
    }
    if (current.calendar.day % kDaysPerYear == 0) {
      RunYearStart(current);
    }
    RunFields(current);
    RunHerdDay(config_, current);
  }

  std::int32_t DaysToNextHarvest(const WorldState& completed) const override {
    return DaysToHarvest(config_, completed);
  }

  void CollectStockForecast(const WorldState& completed,
                            std::vector<StockForecast>& lights) const override {
    lights.push_back(FeedLight(config_, completed));
    lights.push_back(SeedLight(config_, completed));
  }

  void CollectAlarms(const WorldState& completed, std::vector<Alarm>& alarms) const override {
    CollectStoreAlarms(completed, alarms);
    CollectFieldAlarms(completed, alarms);
    CollectHerdAlarms(completed, alarms);
  }

 private:
  /// kStoreFull: a numbered store holding at least its capacity. A store
  /// bounded by the outline the player drew has no number to be full
  /// against and never raises it (stock_ops.h, StorageCapacityGrams).
  void CollectStoreAlarms(const WorldState& world, std::vector<Alarm>& alarms) const {
    for (const UnitRow& unit : world.units.rows) {
      if (!StoresGoods(unit, config_)) {
        continue;
      }
      const Grams capacity = StorageCapacityGrams(unit, config_);
      if (capacity <= 0 || TotalStock(unit.stock) < capacity) {
        continue;
      }
      Alarm alarm;
      alarm.kind = AlarmKind::kStoreFull;
      alarm.unit = world.units.row_ids[static_cast<std::size_t>(&unit - world.units.rows.data())];
      alarm.amount = capacity;
      alarms.push_back(alarm);
    }
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
  Grams RoomClaimOf(const FieldRow& field) const {
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
    if (field.kind != LandKind::kArable || field.crop.value >= config_.crops.size()) {
      return claim;
    }
    const bool growing = field.phase == FieldPhase::kGrowing;
    const bool reaping = field.phase == FieldPhase::kHarvest;
    if (!growing && !reaping) {
      return claim;
    }
    const CropDef& crop = config_.crops[field.crop.value];
    const float soil = field.fertility / config_.farming.fertility_neutral;
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
  std::vector<std::uint32_t> FieldsInHarvestOrder(const WorldState& world) const {
    const auto today = static_cast<std::uint32_t>(world.calendar.date.month);
    std::vector<std::uint32_t> order(world.fields.rows.size());
    for (std::uint32_t row = 0; row < order.size(); ++row) {
      order[row] = row;
    }
    const auto months_away = [this, &world, today](std::uint32_t row) {
      const FieldRow& field = world.fields.rows[row];
      // A field being reaped, or one already holding a cut load, lands NOW —
      // whatever month its crop is nominally reaped in. Reading the month
      // alone put a field whose harvest month has just PASSED eleven months
      // into the future and let three others spend the room in front of it.
      if (field.phase == FieldPhase::kHarvest || field.reaped_grams > 0) {
        return 0U;
      }
      if (field.crop.value >= config_.crops.size()) {
        return kMonthsPerYear;
      }
      const auto month =
          static_cast<std::uint32_t>(config_.crops[field.crop.value].harvest_from_month);
      return (month + kMonthsPerYear - today) % kMonthsPerYear;
    };
    std::stable_sort(order.begin(), order.end(), [&months_away](std::uint32_t a, std::uint32_t b) {
      return months_away(a) < months_away(b);
    });
    return order;
  }

  /// kHarvestWaitingOnField, kHarvestWillNotFit and kSeedShort — the three
  /// conditions of a field, in kind order so that the caller's sort has
  /// less to do (it still sorts: row order is not id order).
  void CollectFieldAlarms(const WorldState& world, std::vector<Alarm>& alarms) const {
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
    Grams room_left = FreeRoomOfStores(world);
    for (const std::uint32_t row : FieldsInHarvestOrder(world)) {
      const FieldRow& field = world.fields.rows[row];
      const Grams claim = RoomClaimOf(field);
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
        alarm.resource = standing && field.crop.value < config_.crops.size()
                             ? config_.crops[field.crop.value].resource
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
      const Grams short_of = SeedShortfall(world, field);
      if (short_of > 0) {
        Alarm alarm;
        alarm.kind = AlarmKind::kSeedShort;
        alarm.field = world.fields.row_ids[row];
        alarm.resource = config_.crops[NextSownCrop(world, field).value].resource;
        alarm.amount = short_of;
        alarms.push_back(alarm);
      }
    }
  }

  /// kHerdStarving: a kolkhoz herd that went underfed and has not been fed
  /// since. A household herd is the family's business, not the farm's.
  void CollectHerdAlarms(const WorldState& world, std::vector<Alarm>& alarms) const {
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
    CollectStableAlarms(world, alarms);
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
  void CollectStableAlarms(const WorldState& world, std::vector<Alarm>& alarms) const {
    if (config_.horse_kind.value == kInvalidDefIdValue || StableBuilt(world, config_)) {
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
      if (herd.household_owned != 0 || herd.kind.value != config_.horse_kind.value) {
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

  /// Free room of every numbered store together, in grams — what a harvest
  /// has to fit into. Outline-bounded stores are unbounded and are left out
  /// of the sum: counting them would make the answer meaningless.
  Grams FreeRoomOfStores(const WorldState& world) const {
    Grams room = 0;
    for (const UnitRow& unit : world.units.rows) {
      if (!StoresGoods(unit, config_)) {
        continue;
      }
      const Grams free_here = FreeRoomGrams(unit, config_);
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
  CropId NextSownCrop(const WorldState& world, const FieldRow& field) const {
    const std::uint32_t year = world.calendar.date.year;
    const CropId slots[3] = {field.rotation_year0, field.rotation_year1, field.rotation_year2};
    const CropId next = slots[(year + 1) % 3];
    return next.value < config_.crops.size() ? next : CropId{};
  }

  /// Grams of seed the next sowing is short of, 0 when it is covered or
  /// when there is nothing to sow. Sowing takes ORDINARY produce of the
  /// crop out of the stores (§7), so the question is what the stores hold.
  Grams SeedShortfall(const WorldState& world, const FieldRow& field) const {
    if (field.kind != LandKind::kArable) {
      return 0;
    }
    const CropId next = NextSownCrop(world, field);
    if (next.value >= config_.crops.size()) {
      return 0;
    }
    const CropDef& crop = config_.crops[next.value];
    if (crop.sowing_norm_kg_per_ha <= 0.0F) {
      return 0;
    }
    const auto need = GramsFromKilograms(crop.sowing_norm_kg_per_ha * field.area_ga);
    const Grams have = HeldEverywhere(world, crop.resource);
    return have >= need ? 0 : need - have;
  }

  /// What the settlement holds of a resource, anywhere a taker would find
  /// it — the same reach as TakeFromStorage, which is wider than a store.
  static Grams HeldEverywhere(const WorldState& world, ResourceId resource) {
    Grams total = 0;
    for (const UnitRow& unit : world.units.rows) {
      if (unit.level == 0) {
        continue;
      }
      total += StockOf(unit.stock, resource);
    }
    return total;
  }

  /// The labor seam, seen from the production side (land_state.h): a field
  /// stands in a working phase until its crew has drained
  /// work_days_remaining, and only then moves on. No workers, no progress —
  /// deliberately.
  void AdvanceFinishedPhases(WorldState& current) {
    for (FieldRow& field : current.fields.rows) {
      if (KindOfWorkingPhase(field.phase) == FieldPhase::kIdle ||
          field.work_days_remaining > 0.0F) {
        continue;
      }
      field.work_days_remaining = 0.0F;
      switch (field.phase) {
        case FieldPhase::kPlowing:
          OpenPhase(current, field, FieldPhase::kHarrowing);
          break;
        case FieldPhase::kHarrowing:
          if (field.crop.value == kInvalidDefIdValue) {
            FinishSowing(current, field);  // bare fallow: nothing to sow
          } else {
            OpenPhase(current, field, FieldPhase::kSowing);
          }
          break;
        case FieldPhase::kSowing:
          FinishSowing(current, field);
          break;
        case FieldPhase::kHarvest:
          FinishHarvest(current, field);
          break;
        default:
          break;
      }
    }
  }

  /// @brief kIdle for a phase that needs no work, the phase itself for the
  /// four working ones.
  static constexpr FieldPhase KindOfWorkingPhase(FieldPhase phase) {
    switch (phase) {
      case FieldPhase::kPlowing:
      case FieldPhase::kHarrowing:
      case FieldPhase::kSowing:
      case FieldPhase::kHarvest:
        return phase;
      case FieldPhase::kIdle:
      case FieldPhase::kGrowing:
      // Not a phase: handled beside the phases that need no work, so this
      // switch keeps no default and a new phase stays a compile error.
      case FieldPhase::kFieldPhaseCount:
        return FieldPhase::kIdle;
    }
    return FieldPhase::kIdle;
  }

  /// @brief Moves the field into a working phase and sizes its demand:
  /// area x the phase's norm. The crop is the one in the ground or, while
  /// the field is still being prepared, the one this year's rotation plans.
  void OpenPhase(WorldState& current, FieldRow& field, FieldPhase phase) const {
    MoveFieldPhase(current, field, phase);
    if (field.kind != LandKind::kArable) {
      // Grass is mown, never ploughed, harrowed or sown: the meadow has one
      // working phase in the year and one norm to size it.
      field.work_days_remaining = phase == FieldPhase::kHarvest
                                      ? config_.farming.meadow_mow_days_per_ha * field.area_ga
                                      : 0.0F;
      return;
    }
    const CropId crop = field.crop;
    float norm = 0.0F;
    if (phase == FieldPhase::kPlowing) {
      norm = config_.farming.plow_days_per_ha;
    } else if (phase == FieldPhase::kHarrowing) {
      norm = config_.farming.harrow_days_per_ha;
    } else if (crop.value < config_.crops.size()) {
      norm = phase == FieldPhase::kSowing ? config_.crops[crop.value].sow_days_per_ha
                                          : config_.crops[crop.value].harvest_days_per_ha;
    }
    field.work_days_remaining = norm * field.area_ga;
  }

  /// January 1: the rotation plan advances one year, and fallow that stood
  /// the whole year pays out its recovery.
  /// @brief Is this one of the six bread grains the plan counts?
  bool IsPlanGrain(ResourceId resource) const {
    for (const ResourceId grain : config_.plan_grain_resources) {
      if (grain.value == resource.value) {
        return true;
      }
    }
    return false;
  }

  /// The year's delivery: what the plan asked for leaves the stores and is
  /// recorded as delivered. A shortfall is simply a smaller delivery — the
  /// district has no mechanics in phase 1, and inventing consequences for it
  /// would be inventing the district.
  void DeliverPlan(WorldState& current) const {
    current.plan.delivered.assign(current.plan.due.size(), 0);
    for (std::uint32_t index = 0; index < current.plan.due.size(); ++index) {
      const ResourceId resource{static_cast<std::uint16_t>(index)};
      const Grams taken = TakeFromStorage(current, config_, resource, current.plan.due[index]);
      current.plan.delivered[index] = taken;
      AddLedgerAmount(current.ledger.current.delivered, resource, taken);
    }
    current.plan.due.assign(current.plan.due.size(), 0);
  }

  void RunYearStart(WorldState& current) const {
    DeliverPlan(current);
    for (FieldRow& field : current.fields.rows) {
      if (field.kind != LandKind::kArable) {
        continue;  // no rotation to shift, no fallow to pay out; derelict rests as it is
      }
      const bool bare =
          field.phase == FieldPhase::kGrowing && field.crop.value == kInvalidDefIdValue;
      if (bare) {
        MoveFieldPhase(current, field, FieldPhase::kIdle);  // the fallow stood its year
        if (field.manure_applied != 0) {
          // A fallow has no harvest to settle its manure at: it settles here.
          field.fertility += ManureBonus(field);
          field.fertility = field.fertility > 100.0F ? 100.0F : field.fertility;
          field.manure_applied = 0;
        }
      }
      if ((field.phase == FieldPhase::kIdle) && field.rotation_year0.value == kInvalidDefIdValue) {
        field.fertility += config_.farming.fallow_recovery;
        field.fertility = field.fertility > 100.0F ? 100.0F : field.fertility;
        field.last_crop = CropId{};
        field.repeat_years = 0;
      }
      const CropId shifted = field.rotation_year0;
      field.rotation_year0 = field.rotation_year1;
      field.rotation_year1 = field.rotation_year2;
      field.rotation_year2 = shifted;
      // A perennial stand ends when the plan moves on (farming design §5).
      if (field.phase == FieldPhase::kGrowing && field.crop.value < config_.crops.size() &&
          config_.crops[field.crop.value].is_perennial &&
          field.rotation_year0.value != field.crop.value) {
        field.last_crop = field.crop;
        field.crop = CropId{};
        MoveFieldPhase(current, field, FieldPhase::kIdle);
        ClearFieldWeather(field);
      }
    }
    PlanManure(current);
  }

  /// @brief The production half of the order book (task A8): the two verbs
  /// that stop a unit and start it again.
  ///
  /// Settled IN THE STEP THEY ARE READ, like the construction kinds and
  /// unlike an appointment: there is nothing to wait for. The design's
  /// "stops when the running cycle ends" (unit rules §5) is a promise about
  /// CYCLES, and the core has none — when it does, this is where the waiting
  /// will be, and the order is what will wait.
  void ConsumeOrders(WorldState& current) const {
    for (OrderRow& order : current.orders.rows) {
      if (order.status != OrderStatus::kPending) {
        continue;
      }
      switch (order.kind) {
        case OrderKind::kPauseUnit:
          Settle(order, SetPaused(current, order.unit, 1));
          break;
        case OrderKind::kResumeUnit:
          Settle(order, SetPaused(current, order.unit, 0));
          break;
        default:
          break;  // not ours: another consumer's, or the events slot's refusal
      }
    }
  }

  static void Settle(OrderRow& order, OrderRefusal refusal) {
    order.status = refusal == OrderRefusal::kNone ? OrderStatus::kDone : OrderStatus::kRefused;
    order.refusal = refusal;
  }

  /// @brief Stops a unit or starts it again.
  /// @return kNoSuchSubject when there is no such unit, kRuleForbids for a
  ///         site (level 0 is pegs and string — there is no production to
  ///         stop) and for an order that asks for the state the unit is
  ///         already in: "pause the paused" is not a no-op to be swallowed,
  ///         it means the chairman is looking at something stale.
  static OrderRefusal SetPaused(WorldState& current, UnitId unit, std::uint8_t paused) {
    const std::uint32_t row = FindRow(current.units, unit);
    if (row == kNoRow) {
      return OrderRefusal::kNoSuchSubject;
    }
    if (current.units.rows[row].level == 0) {
      return OrderRefusal::kRuleForbids;
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

  void RunFields(WorldState& current) {
    const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
    const float temperature = current.weather.air_temperature_celsius;
    const bool snowing = current.weather.precipitation == Precipitation::kSnow;
    for (FieldRow& field : current.fields.rows) {
      // The buffer is no longer emptied here. Until task A4 this was a
      // daily retry that moved whatever the stores had room for, the moment
      // they had it — the instant-delivery stub. The load now leaves when
      // somebody carries it, and the carrying is settled at the day's last
      // tick (SettleHauling). What A3 wrote about room still holds; what it
      // said about the retry does not.
      if (field.kind == LandKind::kDerelict) {
        continue;  // unraised land: nothing happens here until it is raised
      }
      if (field.kind != LandKind::kArable) {
        RunMeadow(current, field, month);
        continue;
      }
      if (field.phase == FieldPhase::kIdle) {
        // A LOADED FIELD MAY BE WORKED AGAIN, and it may because the two jobs
        // no longer share a number: carrying has a seam of its own
        // (land_state.h). Carting the sheaves off the headland and ploughing
        // the stubble are different crews on the same ground, and the canon's
        // rotation needs them at once — oats come off in the eighth month and
        // winter rye goes in in the eighth.
        //
        // For one measured run this file forbade it, to protect the seam the
        // two jobs were sharing. The forbidding cost the village a quarter of
        // itself, because a field that could not be cleared could not be sown
        // either. The shared seam was the defect; the ban was a splint on it.
        TrySow(current, field, month, temperature);
        continue;
      }
      const bool standing =
          field.phase == FieldPhase::kGrowing || field.phase == FieldPhase::kHarvest;
      if (standing && field.crop.value == kInvalidDefIdValue) {
        // Black fallow: ploughed this spring and standing bare (D11). It is
        // sown only from the NEXT slot, and only with a winter crop — the
        // canon's "fallow, then winter rye" — never re-ploughed as fallow.
        TrySowWinter(current, field, month, temperature);
        continue;
      }
      if (!standing || field.crop.value >= config_.crops.size()) {
        continue;  // being prepared, or a crop this config does not know
      }
      const CropDef& crop = config_.crops[field.crop.value];
      // Snow on an unharvested annual is the one total loss (§6) — and it
      // takes a field the crew is still reaping, which is exactly why the
      // harvest window outranks every other job (assignment.cpp). Winter
      // crops and perennials winter under snow by design.
      if (snowing && !crop.is_winter && !crop.is_perennial) {
        // What was already reaped and still waiting for a cart goes with the
        // standing crop, and it is booked as lost room rather than vanishing
        // (task A3, STUB with a named term: this bounds free storage, it does
        // not model spoilage — manual/72-storage-and-alarms.md §2).
        if (field.reaped_grams > 0) {
          // What LIES there, not what stands there: after a season without a
          // cart the buffer can hold the previous crop, and booking it under
          // this year's resource would put the loss in the wrong column.
          AddLedgerAmount(
              current.ledger.current.lost_no_room, field.reaped_resource, field.reaped_grams);
          field.reaped_grams = 0;
          field.reaped_resource = ResourceId{};
        }
        field.last_crop = field.crop;
        field.repeat_years = 0;
        field.crop = CropId{};
        MoveFieldPhase(current, field, FieldPhase::kIdle);
        field.work_days_remaining = 0.0F;
        ClearFieldWeather(field);
        field.manure_applied = 0;
        current.ledger.current.area_lost_ha += field.area_ga;
        // AND HERE IT IS SAID. The comment that used to stand on these lines
        // claimed the loss "is an event already — kFieldLost, emitted where
        // the events slot folds it". It was not: the kind had no emitter
        // anywhere in the core, and the sentence describing the emission
        // outlived the emission it described (boss, 2026-09-05). Snow on an
        // unreaped field is the only TOTAL loss of a harvest in the game, so
        // it interrupts a fast-forward: the player is entitled to see the
        // day it happened, not the year's total.
        SimEvent& lost = EmitEvent(current, EventKind::kFieldLost, EventSeverity::kInterrupting);
        lost.field = FieldIdOf(current, field);
        // No LogWarning: phase code does not log (core_log contract,
        // DEADLOCK-001), and a condition worth telling the player is an
        // alarm, not a line in a file nobody opens.
        continue;
      }
      if (field.phase != FieldPhase::kGrowing) {
        continue;  // already being reaped
      }
      const bool in_window = month >= crop.harvest_from_month && month <= crop.harvest_to_month;
      // A perennial stand stays growing after its cut, so gate it to one
      // cut a year — the first day of its window; an annual leaves the
      // growing phase at harvest and cannot double-fire.
      const bool cut_today = !crop.is_perennial || (month == crop.harvest_from_month &&
                                                    current.calendar.date.day_in_month == 0);
      if (in_window && cut_today) {
        OpenPhase(current, field, FieldPhase::kHarvest);
      }
    }
  }

  /// The meadow's whole year: it stands, and once a season the scythes go
  /// out. No sowing window, no temperature gate, no snow loss (grass winters
  /// where it grew), no fertility — a meadow is land, not a crop
  /// (land_state.h, LandKind; boss answer Q6).
  void RunMeadow(WorldState& current, FieldRow& field, std::uint8_t month) const {
    if (field.phase != FieldPhase::kGrowing) {
      return;  // already being mown, and one cut a year is all there is
    }
    if (month == config_.farming.meadow_cut_month && current.calendar.date.day_in_month == 0) {
      OpenPhase(current, field, FieldPhase::kHarvest);
    }
  }

  /// @brief Puts a harvested load where it belongs: hay at the manger, the
  /// rest through the store door — none of them above its ceiling (task A3,
  /// manual/72-storage-and-alarms.md §2).
  /// @return What did NOT fit, in grams. The caller decides what that means:
  ///         a field keeps it (FieldRow::reaped_grams), a meadow's hay has
  ///         nowhere else and is booked to the year's lost_no_room.
  Grams DeliverHarvest(WorldState& current, ResourceId resource, Grams amount) const {
    Grams placed = 0;
    if (resource.value == config_.hay_resource.value) {
      // The manger first, and it is not a numbered store: the stock yard's
      // table capacity is in HEADS, so the door does not find it and its
      // fodder buffer has no tonnage to be full against.
      const std::uint32_t manger = FindStockYardRow(current, config_);
      if (manger != kNoRow) {
        AddToStock(current.units.rows[manger].stock, resource, amount);
        return 0;
      }
    }
    placed = DeliverToStores(current, config_, resource, amount);
    return amount - placed;
  }

  /// The season's cut. The yield is the land's own rate for the WHOLE
  /// season, which is why one cut a year is not a simplification: the
  /// second cut is inside the number (farming.csv, meadow_yield_kg_per_ha).
  void MowMeadow(WorldState& current, FieldRow& field) const {
    const float rate = field.kind == LandKind::kFloodplainMeadow
                           ? config_.farming.meadow_floodplain_yield_kg_per_ha
                           : config_.farming.meadow_yield_kg_per_ha;
    const Grams hay = KilogramsToGrams(rate * field.area_ga);
    // A meadow has no reaped buffer of its own: the cut either reaches the
    // manger and the stores or it is lost, and either way it is booked.
    const Grams hay_lost = DeliverHarvest(current, config_.hay_resource, hay);
    AddLedgerAmount(current.ledger.current.harvest, config_.hay_resource, hay);
    AddLedgerAmount(current.ledger.current.lost_no_room, config_.hay_resource, hay_lost);
    current.ledger.current.area_harvested_ha += field.area_ga;
    field.work_days_remaining = 0.0F;
    // THE DAY IT WAS CUT, and it is the only trace the cut leaves. The phase
    // goes straight back to kGrowing below — the grass does stand again —
    // so without this day a mown meadow and an untouched one are the same
    // state, and the layer had nowhere to put a flower or a butterfly.
    field.last_mown_day = current.calendar.day;
    MoveFieldPhase(current, field, FieldPhase::kGrowing);  // the grass stands again next summer
  }

  /// The field year opens here: the sowing window and the temperature say
  /// "go", and the field enters plowing. What follows — harrowing, sowing —
  /// is paced by the crew, so the seed may well go into the ground after
  /// the window has closed. That is the point of the seam: the window is
  /// when the work STARTS, the crew decides when it ends.
  void TrySow(WorldState& current, FieldRow& field, std::uint8_t month, float temperature) {
    if (field.rotation_year0.value >= config_.crops.size()) {
      // A FALLOW YEAR IS PLOUGHED (farming design §7, "fallow is ploughed";
      // defect D11 of the reconciliation): the manure goes in with the
      // plough and the ground stands bare until the year turns, or until the
      // next slot's winter crop goes into it in the autumn (TrySowWinter).
      if (month == config_.farming.fallow_plow_month && temperature >= 0.0F) {
        OpenPlowing(current, field, CropId{});
      }
      return;
    }
    const CropDef& crop = config_.crops[field.rotation_year0.value];
    if (crop.is_winter && field.last_crop.value == field.rotation_year0.value) {
      // This year's winter crop was sown last autumn and is already off:
      // the field is idle because it was HARVESTED, not because the sowing
      // was missed. Sowing it again in August would put the same rye in two
      // years running and eat the next slot with it — the field sheet caught
      // exactly that. Only the next slot's winter crop may go in now.
      TrySowWinter(current, field, month, temperature);
      return;
    }
    // Otherwise a winter crop in THIS year's slot is the fallback path: it
    // was meant to go in last autumn (TrySowWinter) and that autumn was
    // missed, so it is sown in its window a year late. That costs the slot
    // after it, but a winter crop never sown costs the plan its bread.
    if (month < crop.sow_from_month || month > crop.sow_to_month ||
        temperature < crop.sow_min_temp_c) {
      TrySowWinter(current, field, month, temperature);
      return;
    }
    OpenPlowing(current, field, field.rotation_year0);
  }

  /// The autumn sowing (defect D12). A winter crop is harvested the summer
  /// AFTER it is sown, so the slot it belongs to is next year's — "winter rye
  /// goes into the ground in the autumn of the same year, and the ring starts
  /// turning in the second" (start canon §8). Sown from this year's slot it
  /// arrived a year late and ate the following spring as well.
  void TrySowWinter(WorldState& current, FieldRow& field, std::uint8_t month, float temperature) {
    if (field.rotation_year1.value >= config_.crops.size()) {
      return;
    }
    const CropDef& next = config_.crops[field.rotation_year1.value];
    if (!next.is_winter || month < next.sow_from_month || month > next.sow_to_month ||
        temperature < next.sow_min_temp_c) {
      return;
    }
    OpenPlowing(current, field, field.rotation_year1);
  }

  /// @brief Opens the ploughing for `crop` (invalid = bare fallow). The
  /// manure, if the winter's plan gave this field any, is already on the
  /// row (PlanManure) and goes in with the plough (§8).
  void OpenPlowing(WorldState& current, FieldRow& field, CropId crop) {
    field.crop = crop;
    if (field.manure_applied != 0) {
      const float share = static_cast<float>(field.manure_applied) / 100.0F;
      const auto dose =
          GramsFromKilograms(config_.farming.manure_norm_kg_per_ha * field.area_ga * share);
      current.ledger.current.manure_plowed_in += dose;
      current.ledger.current.area_manured_ha += field.area_ga * share;
    }
    OpenPhase(current, field, FieldPhase::kPlowing);
  }

  /// THE WINTER'S MANURE PLAN, made at the year's turn: the heap is dealt out
  /// in full doses to the fields that will be ploughed this year, POOREST
  /// FIELD FIRST, until it runs out. What the herd makes during the year
  /// waits for next winter's plan.
  ///
  /// It used to be first come, first served at the plough: a field took a
  /// full dose if the heap held one that morning, or nothing. The field sheet
  /// of the fifth reconciliation pass showed what that does — the twenty-one
  /// hectare potato field never once qualified in thirty years, because its
  /// dose is 420 t and the heap never holds that, while the ten-hectare field
  /// next to it was manured twenty-seven years out of thirty and stood at a
  /// hundred. The canon's "a hectare gets manure every four or five years"
  /// is a rotation of the manure. NOT a player's decision and not a stub for
  /// one: "the manure norm is a number, not a decision" (farming design §9,
  /// closed), and a yearly allocation of the heap across the fields is the
  /// agronomist's work — the very micromanagement the game removes epoch by
  /// epoch. The player's lever in this loop is how much livestock to keep.
  void PlanManure(WorldState& current) const {
    const std::uint32_t heap = FindUnitRowOfType(current, config_.compost_heap_type);
    if (heap == kNoRow) {
      return;
    }
    Grams held = StockOf(current.units.rows[heap].stock, config_.manure_resource);
    std::vector<std::uint32_t> candidates;
    for (std::uint32_t row = 0; row < current.fields.rows.size(); ++row) {
      const FieldRow& field = current.fields.rows[row];
      // Ploughed this year: idle arable, whether a crop is in the slot or it
      // is fallow. A winter crop already standing was manured when IT was
      // ploughed, last autumn.
      if (field.kind == LandKind::kArable && field.phase == FieldPhase::kIdle &&
          field.manure_applied == 0) {
        candidates.push_back(row);
      }
    }
    // Poorest first; equal fertility keeps row order, so the plan is the
    // same on every machine.
    std::stable_sort(candidates.begin(), candidates.end(), [&](std::uint32_t a, std::uint32_t b) {
      return current.fields.rows[a].fertility < current.fields.rows[b].fertility;
    });
    for (const std::uint32_t row : candidates) {
      if (held <= 0) {
        break;
      }
      FieldRow& field = current.fields.rows[row];
      const auto dose = GramsFromKilograms(config_.farming.manure_norm_kg_per_ha * field.area_ga);
      if (dose <= 0) {
        continue;
      }
      // A PARTIAL DOSE IS A DOSE. The poorest field takes what the heap has,
      // up to its full norm, and its bonus scales with the share it got.
      // Whole doses or nothing meant the biggest field could never be
      // manured at all: 420 t for twenty-one hectares, and a herd of twenty
      // cows makes 250 a year.
      const Grams given = held < dose ? held : dose;
      held -= given;
      AddToStock(current.units.rows[heap].stock, config_.manure_resource, -given);
      const float share = static_cast<float>(given) / static_cast<float>(dose);
      field.manure_applied = static_cast<std::uint8_t>(share * 100.0F + 0.5F);
      if (field.manure_applied == 0) {
        field.manure_applied = 1;  // a dribble still counts as touched
      }
    }
  }

  /// @brief The manure bonus this field has coming, by the share of its dose
  /// it received (FieldRow::manure_applied is that share in percent).
  float ManureBonus(const FieldRow& field) const {
    return config_.farming.manure_fertility_bonus * static_cast<float>(field.manure_applied) /
           100.0F;
  }

  /// The seed goes into the ground when the sowing phase is worked through.
  void FinishSowing(WorldState& current, FieldRow& field) {
    const CropId crop_id = field.crop;
    if (crop_id.value == kInvalidDefIdValue) {
      // Bare fallow: ploughed and harrowed, nothing goes in. It stands as
      // ground with no crop until the year turns or a winter crop takes it.
      MoveFieldPhase(current, field, FieldPhase::kGrowing);
      field.work_days_remaining = 0.0F;
      return;
    }
    if (crop_id.value < config_.crops.size()) {
      const CropDef& crop = config_.crops[crop_id.value];
      // Sowing consumes ordinary produce of the same crop (§7); partial
      // seed sows the whole field anyway — the shortfall alarm is a UI
      // concern.
      if (crop.sowing_norm_kg_per_ha > 0.0F) {
        const auto need = GramsFromKilograms(crop.sowing_norm_kg_per_ha * field.area_ga);
        const Grams got = TakeFromStorage(current, config_, crop.resource, need);
        AddLedgerAmount(current.ledger.current.seed, crop.resource, got);
        // Short seed sows the whole field anyway, and the player is told by
        // kSeedShort — standing from the day the rotation is set, not on the
        // morning of the sowing (task A3; farming design §7). Phase code does
        // not log (DEADLOCK-001).
      }
    }
    current.ledger.current.area_sown_ha += field.area_ga;
    field.crop = crop_id;
    MoveFieldPhase(current, field, FieldPhase::kGrowing);
    field.work_days_remaining = 0.0F;
    ClearFieldWeather(field);
  }

  /// The reaped field pays out and leaves the harvest phase.
  void FinishHarvest(WorldState& current, FieldRow& field) {
    if (field.kind != LandKind::kArable) {
      MowMeadow(current, field);
      return;
    }
    if (field.crop.value >= config_.crops.size()) {
      MoveFieldPhase(current, field, FieldPhase::kIdle);
      return;
    }
    Harvest(current, field, config_.crops[field.crop.value]);
  }

  void Harvest(WorldState& current, FieldRow& field, const CropDef& crop) {
    const float soil_factor = field.fertility / config_.farming.fertility_neutral;
    // The sum of the two, capped exactly where the single number was.
    const float stress_total = field.drought_stress + field.wet_stress;
    const float capped =
        stress_total > config_.farming.stress_cap ? config_.farming.stress_cap : stress_total;
    const float weather_factor = 1.0F - capped;
    const auto yield_grams =
        GramsFromKilograms(crop.yield_kg_per_ha * field.area_ga * soil_factor * weather_factor);
    // THE REAPED CROP STAYS ON THE FIELD. Until task A4 it went into the
    // stores in the same tick it was cut — the instant-delivery stub — and
    // only the remainder that would not fit stayed out. It all stays out
    // now: the field brigade's buffer of the transport design §9, and it
    // empties when somebody comes for it with a back or a cart
    // (SettleHauling). Nothing is lost here and nothing is forced in above
    // a ceiling; what the field gave is booked below either way.
    const Grams unplaced = yield_grams;
    if (unplaced > 0) {
      // A buffer already holding LAST year's produce of another crop cannot
      // hold this one too — one number names one resource. The old load has
      // stood a full season by now, so it is written off, loudly, rather
      // than silently relabelled as this year's.
      if (field.reaped_grams > 0 && field.reaped_resource.value != crop.resource.value) {
        AddLedgerAmount(
            current.ledger.current.lost_no_room, field.reaped_resource, field.reaped_grams);
        field.reaped_grams = 0;
        field.reaped_resource = ResourceId{};  // the invariant: empty means unnamed
      }
      field.reaped_grams += unplaced;
      field.reaped_resource = crop.resource;
    }
    // Booked whether or not a store took it in: what the field gave is what
    // the reconciliation compares against the yield tables, and a settlement
    // with nowhere to put its grain is a different finding entirely.
    AddLedgerAmount(current.ledger.current.harvest, crop.resource, yield_grams);
    current.ledger.current.area_harvested_ha += field.area_ga;
    // What this field gave, and of what: the three fields the kind's
    // contract names (event_state.h). Routine — a harvest is the year
    // working, not news — but the panel and the story layer both read the
    // journal, and a year of harvests that left no trace in it is a year
    // they cannot describe.
    SimEvent& reaped = EmitEvent(current, EventKind::kFieldHarvested);
    reaped.field = FieldIdOf(current, field);
    reaped.resource = crop.resource;
    reaped.amount = static_cast<std::int64_t>(yield_grams);
    // Straw is what the field leaves behind, and it is a feed of its own —
    // own and free, a reserve ration with a lowered effect but plainly there
    // in a winter manger (design db crop.straw_ratio).
    if (crop.straw_ratio > 0.0F) {
      const auto straw = GramsFromFloat(static_cast<float>(yield_grams) * crop.straw_ratio);
      const Grams straw_placed = DeliverToStores(current, config_, config_.straw_resource, straw);
      AddLedgerAmount(current.ledger.current.harvest, config_.straw_resource, straw);
      // Straw has no buffer of its own — it is not why a field waits — so
      // what did not fit is gone, and gone with a line in the book.
      AddLedgerAmount(
          current.ledger.current.lost_no_room, config_.straw_resource, straw - straw_placed);
    }
    // The district's plan accrues as the grain is reaped: it is "just a
    // number" in phase 1 (plan §11), a share of the year's own harvest,
    // handed over at the year's turn with no district mechanics behind it.
    if (config_.plan_grain_share > 0.0F && IsPlanGrain(crop.resource)) {
      AddToStock(current.plan.due,
                 crop.resource,
                 GramsFromFloat(static_cast<float>(yield_grams) * config_.plan_grain_share));
    }
    // Fertility bookkeeping (§2, §7, §8): the crop's delta, the manure
    // bonus, the growing repeat penalty.
    if (crop.is_perennial) {
      // A standing meadow is one sowing cut year after year, not a repeat
      // of that sowing: the rotation penalty must not accrue on it.
      field.repeat_years = 0;
    } else if (field.crop.value == field.last_crop.value) {
      field.repeat_years =
          field.repeat_years < 250 ? static_cast<std::uint8_t>(field.repeat_years + 1) : 250;
    } else {
      field.repeat_years = 0;
    }
    // The repeat penalty has a ceiling (boss answer Q3): the third year in a
    // row is the limit of the punishment, so a rotation mistake costs the
    // year and never the game. Uncapped it took a monocropped field from 65
    // to 2 in six years while the manure heap was still working.
    const auto repeated = static_cast<float>(field.repeat_years);
    const float charged = repeated < config_.farming.repeat_penalty_max_years
                              ? repeated
                              : config_.farming.repeat_penalty_max_years;
    field.fertility += crop.fertility_delta + ManureBonus(field) -
                       charged * config_.farming.repeat_penalty_per_year;
    // And a floor under it: an exhausted field bears little, but it bears.
    const float floor_value = config_.farming.fertility_floor;
    field.fertility = field.fertility < floor_value ? floor_value : field.fertility;
    field.fertility = field.fertility > 100.0F ? 100.0F : field.fertility;
    field.manure_applied = 0;
    field.last_crop = field.crop;
    ClearFieldWeather(field);
    // The reaping is over, so the PHASE seam is cleared — without this the
    // field never leaves kHarvest and the whole rotation stops, which is
    // what happened for one measured run when this line was swallowed by an
    // edit to the lines around it.
    field.work_days_remaining = 0.0F;
    // The load names the price of CARRYING it at once, in its own seam. It
    // has to be named here and not left to the evening: the first settlement
    // works out what was carried from what is missing from this number, and
    // a number nobody set reads as a full day's work — the instant-delivery
    // stub coming back in through the accounting, which is exactly what an
    // instrumented run caught it doing.
    if (field.reaped_grams > 0) {
      const Grams room = ReceivableRoom(config_, current);
      field.haul_days_remaining = HaulDaysFor(room < field.reaped_grams ? room : field.reaped_grams,
                                              FieldHaulRate(config_, current, field),
                                              config_.standard_day_hours);
    } else {
      field.haul_days_remaining = 0.0F;
    }
    // And the settlement's baseline with it. The guard above stopped the
    // FIRST evening from reading an unset number as a day's work; the same
    // reading came back through the other door, because the evening measured
    // today's demand against yesterday's leftover and the room grows every
    // day as the village eats (seventh reconciliation pass).
    field.haul_days_written = field.haul_days_remaining;
    if (crop.is_perennial && field.rotation_year1.value == field.crop.value) {
      MoveFieldPhase(current, field, FieldPhase::kGrowing);  // the stand yields again
      return;
    }
    // A perennial whose next slot is something else ends at this cut, not at
    // the year's turn: the field must be free in the autumn for the winter
    // crop the canon's ring puts after grass ("fallow or grass, then winter
    // rye"). Left standing till January it could only be sown a year late.
    if (crop.is_perennial) {
      field.last_crop = field.crop;
    }
    field.crop = CropId{};
    MoveFieldPhase(current, field, FieldPhase::kIdle);
  }

  ProductionConfig config_;

  FieldGrowthPhase phase_;
};

}  // namespace

std::unique_ptr<IProductionSystem> CreateProductionSystem(const ITableSet& tables,
                                                          StubTables stubs) {
  // THE DEFAULTS ARE LEGITIMATE AND THEIR SILENCE WAS NOT
  // (core_tables/stub_tables.h). A caller that has not said it wants
  // this module's documented defaults is refused by name, so that a
  // table set which is merely INCOMPLETE cannot pass for one that is
  // as its author meant it.
  if (stubs == StubTables::kRefused) {
    for (const std::string_view required : {"crops", "livestock", "farming", "resources"}) {
      if (tables.FindTable(required) == nullptr) {
        LogError(std::string("production: the table set carries no '") + std::string(required) +
                 "' table, and this caller did not allow the defaults");
        return nullptr;
      }
    }
  }

  ProductionConfig config;
  std::string error;
  if (!ParseProductionConfig(tables, config, error)) {
    LogError(error);
    return nullptr;
  }
  return std::make_unique<ProductionSystem>(config);
}

}  // namespace core
