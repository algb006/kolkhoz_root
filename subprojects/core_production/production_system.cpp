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
#include "field_work.h"
#include "herd_system.h"
#include "production_alarms.h"
#include "production_config.h"
#include "stock_lights.h"
#include "stock_ops.h"

namespace core {
namespace {

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

  /// THE WALKS THEMSELVES LIVE ELSEWHERE (production_alarms.h). They read a
  /// finished world and move nothing, and this file moves the world — the
  /// two are different jobs, and the seam between them is checked by the
  /// compiler rather than by anyone remembering it.
  void CollectAlarms(const WorldState& completed, std::vector<Alarm>& alarms) const override {
    CollectStoreAlarms(config_, completed, alarms);
    CollectFieldAlarms(config_, completed, alarms);
    CollectHerdAlarms(config_, completed, alarms);
  }

 private:
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
          OpenPhase(config_, current, field, FieldPhase::kHarrowing);
          break;
        case FieldPhase::kHarrowing:
          if (field.crop.value == kInvalidDefIdValue) {
            FinishSowing(config_, current, field);  // bare fallow: nothing to sow
          } else {
            OpenPhase(config_, current, field, FieldPhase::kSowing);
          }
          break;
        case FieldPhase::kSowing:
          FinishSowing(config_, current, field);
          break;
        case FieldPhase::kHarvest:
          FinishHarvest(config_, current, field);
          break;
        default:
          break;
      }
    }
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

  /// January 1: the rotation plan advances one year, and fallow that stood
  /// the whole year pays out its recovery.
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
          field.fertility += ManureBonus(config_, field);
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
        RunMeadow(config_, current, field, month);
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
        TrySow(config_, current, field, month, temperature);
        continue;
      }
      const bool standing =
          field.phase == FieldPhase::kGrowing || field.phase == FieldPhase::kHarvest;
      if (standing && field.crop.value == kInvalidDefIdValue) {
        // Black fallow: ploughed this spring and standing bare (D11). It is
        // sown only from the NEXT slot, and only with a winter crop — the
        // canon's "fallow, then winter rye" — never re-ploughed as fallow.
        TrySowWinter(config_, current, field, month, temperature);
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
        OpenPhase(config_, current, field, FieldPhase::kHarvest);
      }
    }
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
