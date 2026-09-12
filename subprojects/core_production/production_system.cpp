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
#include <limits>
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
#include "core_tables/required_tables.h"
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
    // THE NORM IS ANNOUNCED IN THE SPRING, on the day the season turns
    // (boss, 2026-09-12). Not at the year's turn: by spring the worked land
    // and its rotation are settled, and the figure never moves again.
    if (current.calendar.season == Season::kSpring && previous.calendar.season != Season::kSpring) {
      AnnouncePlan(current);
    }
    // THE YEAR'S HIGH-WATER MARK OF WORKED LAND, raised once a day. The
    // district's next norm comes off it (world_state.h), and it is a MAXIMUM
    // so that no single day's order can decide a year's figure.
    const float worked_today = WorkedArableHa(current);
    current.plan.worked_ha_this_year = worked_today > current.plan.worked_ha_this_year
                                           ? worked_today
                                           : current.plan.worked_ha_this_year;
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
  /// recorded as delivered. A shortfall is a shortfall now — it used to be
  /// "simply a smaller delivery" because the district had no mechanics, and
  /// JudgePlan below is those mechanics arriving.
  void DeliverPlan(WorldState& current) const {
    current.plan.delivered.assign(current.plan.due.size(), 0);
    for (std::uint32_t index = 0; index < current.plan.due.size(); ++index) {
      const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
      const Grams taken = TakeFromStorage(current, config_, resource, current.plan.due[index]);
      current.plan.delivered[index] = taken;
      AddLedgerAmount(current.ledger.current.delivered, resource, taken);
    }
  }

  /// @brief Was every position delivered to the share that counts as met?
  ///
  /// EVERY POSITION AND NOT THE TOTAL. Grain is not potato: a settlement
  /// that shipped double the oat and no wheat at all has not met a plan
  /// that asked for both, and a tonnage summed across resources would say
  /// it had. The design's word is "сорванный план", one plan, and a plan is
  /// its positions.
  ///
  /// A position the district asked nothing of is met by anything, including
  /// nothing — which is why the zero case is tested for rather than divided
  /// through.
  bool PlanWasMet(const WorldState& current) const {
    for (std::uint32_t index = 0; index < current.plan.due.size(); ++index) {
      const Grams due = current.plan.due[index];
      if (due == 0) {
        continue;
      }
      const Grams delivered =
          index < current.plan.delivered.size() ? current.plan.delivered[index] : 0;
      const float share = static_cast<float>(delivered) / static_cast<float>(due);
      if (share < config_.plan_met_share) {
        return false;
      }
    }
    return true;
  }

  /// @brief The district's verdict on the year that has just been shipped,
  /// and the next year's plan handed down in its place.
  ///
  /// THE CORE'S HALF OF "ПОД СУД" IS THE CONDITION, not the court (epochs
  /// design §8; boss, 2026-09-12: "твоё — условие и событие"). The counter
  /// reaching its threshold raises kPlanTrialDue once, on the day it
  /// reaches it, and the commission, the case and the courtroom belong to
  /// the presentation.
  ///
  /// A PLAN OF NOTHING IS NOT A MET PLAN. A world whose tables carry no
  /// plan.csv — every unit test's world — is a world with no district, and
  /// it must not accumulate a record of triumphs it was never asked for.
  /// The verdict is simply not taken there, which is what kNone is for.
  /// @brief The district names the year's norm in the spring: a share of
  /// what the arable WORKED LAST YEAR should give at a normal yield
  /// (boss's decision of 2026-09-12; district design §9).
  ///
  /// OFF THE LAND AND NOT OFF THE REAPING, which is the whole repair. A
  /// share of the reaping was owed only by a settlement that had already
  /// cut it, so the plan could not be missed and a verdict on it could not
  /// fail. A norm off the land stands whatever the weather does.
  ///
  /// OFF WORKED LAND AND NOT OFF SOWN LAND, so that sowing less does not owe
  /// less: undersowing is a way to FAIL a plan, not a way to shrink one.
  ///
  /// AND OFF LAST YEAR'S WORKING SINCE 2026-09-13, which this block said for
  /// a day was NOT what the code did — it is now. The figure comes off
  /// PlanState::worked_ha_last_year, the largest worked area the closing year
  /// held, so raising ground enters the plan the year AFTER it is broken
  /// (district §9) and no order settled today can move today's norm.
  ///
  /// THE POSITIONS ARE THE DISTRICT'S, not the chairman's crops: campaign.csv
  /// names them and the share of the worked arable counted under each. A norm
  /// priced off what he planted is a norm he sets.
  void AnnouncePlan(WorldState& current) const {
    // THE RELEASES ARE NOT CLEARED HERE, and they were for one afternoon:
    // the edit that put the clearing into JudgePlan matched this line too,
    // because both functions open by assigning the plan away. The spring
    // clearing wiped a SEED fund opened in the hungry end of winter — on the
    // very day the sowing year begins, which is the day that release was
    // taken for. A scripted edit that finds a second anchor is silently
    // successful; this one was found by the delivery cycle's RACE pass.
    current.plan.due.assign(current.plan.due.size(), 0);
    current.plan.announced = 0;
    if (!(config_.plan_grain_share > 0.0F) || config_.plan_positions.empty()) {
      return;  // no district in these tables: nothing is asked and nothing is judged
    }
    // THE AREA IS LAST YEAR'S WORKED ARABLE, AND THE POSITIONS ARE THE
    // DISTRICT'S. Both halves are repairs of the same defect, and the defect
    // was a button: until 2026-09-13 the norm was priced off the crop
    // standing in each field's year0 slot, so a fallow year — an empty slot —
    // cost nothing, and a chairman who laid every field to fallow, or who
    // withdrew every chain (the order book allows it on purpose, as the move
    // that forgives a layout mistake), owed the district NOTHING AT ALL: no
    // norm, no verdict, no failed year, no trial. The epoch's main pressure
    // switched off by a decision not to sow.
    //
    // District design §9 says the opposite in the paragraph the share comes
    // from, and these are its own words: "Норма идёт с обработанной земли, а
    // не с посеянной. Недосев — способ провалить план, а не уменьшить его"
    //
    // A norm computed from what the chairman planted is a norm the chairman
    // SETS. So neither half of the figure is his any more: the area is what
    // was worked in the year that closed (PlanState::worked_ha_last_year,
    // written at the turn) and the shares are the district's, computed off
    // the canon layout's 210 hectare-years and written in campaign.csv.
    // Taking LAST year's area also delivers §9's other line for free: raised
    // ground enters the plan the year AFTER it is broken.
    //
    // A FIRST DRAFT SPLIT THE AREA EVENLY between the positions, for want of
    // the shares, and the analysis measured what that costs: the district
    // asked for oats on a third of the arable every year while the canon
    // rotation sows them on 10.5 ha every third year, and the reference run
    // failed the plan in nineteen years of thirty and reached the trial
    // condition in its FOURTH. The instrument had gone from "cannot be
    // failed" straight through the middle to "cannot be met".
    for (const ProductionConfig::PlanPosition& position : config_.plan_positions) {
      if (position.crop.value >= config_.crops.size()) {
        continue;
      }
      const CropDef& crop = config_.crops[position.crop.value];
      if (crop.yield_kg_per_ha <= 0.0F || crop.resource.value == kInvalidDefIdValue) {
        continue;
      }
      const float area = current.plan.worked_ha_last_year * position.area_share;
      AddToStock(current.plan.due,
                 crop.resource,
                 GramsFromKilograms(crop.yield_kg_per_ha * area * config_.plan_grain_share));
    }
    // ANNOUNCED EVEN WHEN THE FIGURE IS ZERO, and that is the whole point of
    // the byte: a settlement that worked no land last year is one the
    // district HAS spoken to and asked nothing of, which is not the same
    // state as a world with no district in its tables (world_state.h).
    current.plan.announced = 1;
  }

  /// @brief The arable the village actually worked this year, in hectares —
  /// written at the year's turn for next spring's norm to be computed from.
  ///
  /// WORKED MEANS A CHAIN WAS GIVEN, which is the same test every other
  /// reader of worked land uses (HasRotation, land_state.h): the fallow
  /// ploughing, the fertility recovery, the manure queue and both mean
  /// fertility walks. Read at the TURN rather than in spring, so that what
  /// the chairman does between January and the announcement cannot move the
  /// figure — which is exactly what the old reading allowed.
  static float WorkedArableHa(const WorldState& current) {
    float worked_ha = 0.0F;
    for (const FieldRow& field : current.fields.rows) {
      if (field.kind == LandKind::kArable && HasRotation(field)) {
        worked_ha += field.area_ga;
      }
    }
    return worked_ha;
  }

  void JudgePlan(WorldState& current) const {
    bool asked = false;
    for (const Grams due : current.plan.due) {
      asked = asked || due > 0;
    }
    if (asked) {
      const bool met = PlanWasMet(current);
      current.plan.last_verdict = met ? PlanVerdict::kMet : PlanVerdict::kFailed;
      current.plan.failed_years_in_a_row =
          met ? 0U : static_cast<std::uint8_t>(current.plan.failed_years_in_a_row + 1U);
      current.plan.met_years_in_a_row =
          met ? static_cast<std::uint8_t>(current.plan.met_years_in_a_row + 1U) : 0U;
      const float step = met ? config_.plan_met_reputation : config_.plan_failed_reputation;
      // Clamped to the metric's own scale, both ends. A reputation that
      // walked past 100 on a run of good years would make the fall back
      // through the bands take years of nothing happening — the band table
      // of district design §5 is read off this number, so the number has
      // to mean what the table says it means.
      const float moved = current.chairman.raikom_reputation + step;
      current.chairman.raikom_reputation =
          moved < kMetricMin ? kMetricMin : (moved > kMetricMax ? kMetricMax : moved);
      SimEvent& judged = EmitEvent(
          current, met ? EventKind::kPlanMet : EventKind::kPlanFailed, EventSeverity::kNotable);
      judged.amount = met ? current.plan.met_years_in_a_row : current.plan.failed_years_in_a_row;
      // ON THE DAY IT REACHES THE THRESHOLD AND NOT AFTERWARDS: the
      // equality rather than >= is what keeps a fourth failed year from
      // announcing the same news again. A condition that re-announces
      // itself every year is an alarm, and this is an event.
      if (current.plan.failed_years_in_a_row == config_.plan_failed_years_to_trial) {
        // kInterrupting, and it is the only one of the three: a met year and
        // a failed year are news the player reads in his own time, while
        // the district deciding to take him to court is the thing a
        // fast-forward must not run past (time design §1).
        SimEvent& trial =
            EmitEvent(current, EventKind::kPlanTrialDue, EventSeverity::kInterrupting);
        trial.amount = current.plan.failed_years_in_a_row;
      }
    }
    // THE NEXT NORM IS NOT ANNOUNCED HERE, and what it is computed from has
    // ONE home: PlanState::worked_ha_last_year, written a few lines below.
    // Clearing the old figure is what the year's turn does — an undelivered
    // remainder is a failed year, not a debt carried forward, and the
    // district keeps no tab (district §9).
    //
    // AND THE "THE DISTRICT HAS SPOKEN" BYTE GOES WITH THE FIGURE IT
    // DESCRIBES. It was cleared only in AnnouncePlan for one round, which
    // left the first eight days of every year saying "a figure was named and
    // it asked for nothing" — the one state the byte exists to tell apart
    // from "no figure yet", wrong for a sixth of every year, and a save
    // taken in that window carried the lie across a load.
    current.plan.due.assign(current.plan.due.size(), 0);
    current.plan.announced = 0;
    // The unsealings go with the year they were an emergency of. Carried
    // over, they would quietly become a lower fund instead of a decision
    // somebody took on a particular hungry winter.
    for (ResourceAmounts& opened : current.unsealed.by_fund) {
      opened.assign(opened.size(), 0);
    }
  }

  /// January 1: the rotation plan advances one year, and fallow that stood
  /// the whole year pays out its recovery.
  void RunYearStart(WorldState& current) const {
    DeliverPlan(current);
    JudgePlan(current);
    // THE CLOSING YEAR'S LARGEST WORKED AREA becomes next spring's figure,
    // and the running maximum is what makes it un-gameable: a single tick's
    // reading could be emptied by an order settled that same tick — the order
    // book is consumed at the top of this very call — and re-filled the next
    // morning at no cost at all. Against the year's maximum the same escape
    // costs the whole harvest (world_state.h).
    const float worked_today = WorkedArableHa(current);
    current.plan.worked_ha_last_year = worked_today > current.plan.worked_ha_this_year
                                           ? worked_today
                                           : current.plan.worked_ha_this_year;
    current.plan.worked_ha_this_year = worked_today;
    for (FieldRow& field : current.fields.rows) {
      if (field.kind != LandKind::kArable) {
        continue;  // a meadow has no rotation to shift and no fallow to pay out
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
      // RESTING FALLOW RECOVERS; UNWORKED GROUND MERELY KEEPS WHAT IT HAS.
      // The two were told apart by the land kind until 2026-09-12, and when
      // LandKind::kDerelict went this branch started paying the fallow rate
      // to ninety-three hectares nobody has ever ploughed. Measured on the
      // delivery's own field sheet before it was believed: 71, 77, 83, 89,
      // 95, 100 — six a year, which is fallow_recovery — and 100 from the
      // sixth year to the thirtieth. That is defect D5 of the reconciliation
      // returning under a new name, and it would have handed a player who
      // raised the land a hundred-point field instead of the canon's sixty-
      // five.
      if ((field.phase == FieldPhase::kIdle) && field.rotation_year0.value == kInvalidDefIdValue &&
          HasRotation(field)) {
        field.fertility += config_.farming.fallow_recovery;
        field.fertility = field.fertility > 100.0F ? 100.0F : field.fertility;
        field.last_crop = CropId{};
        field.repeat_years = 0;
      }
      if (field.rotation_skips_turn != 0) {
        // A CHAIN WHOSE FIRST SEASON HAS NOT BEEN USED STANDS STILL
        // (land_state.h, rotation_skips_turn). The mark is NOT spent here: it
        // is spent by the field, on the day work opens from the chain, and
        // until then the turn may not carry year0 away. A chairman who laid
        // his three years out in November finds the crop he named first in
        // the season that is sown first — and so does one whose field was
        // still carrying last year's rye when he gave the order.
      } else {
        const CropId shifted = field.rotation_year0;
        field.rotation_year0 = field.rotation_year1;
        field.rotation_year1 = field.rotation_year2;
        field.rotation_year2 = shifted;
      }
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
        case OrderKind::kUnsealFund:
          Settle(order, UnsealFund(current, order));
          break;
        case OrderKind::kSetRotation:
          Settle(order, SetRotation(current, order));
          break;
        default:
          break;  // not ours: another consumer's, or the events slot's refusal
      }
    }
  }

  /// @brief What the fodder fund holds of one resource, in grams: the
  /// working stock's WORK RATION FOR THE YEAR.
  ///
  /// MEASURED OFF THE HARNESS AND NOT OFF THE HARVEST, which is the whole
  /// difference between this rung and the plan reserve above it. The seed
  /// fund is counted from the sowing to come; this one from the animals that
  /// will pull the plough, in feed units, and a share of the year's need is
  /// what a working animal may take as grain at all.
  ///
  /// THE SHARE IS THE RESOURCE'S OWN CAP — `max_share` of its `work_only`
  /// row in feed_links.csv — and NOT traction_full_ration_share, which this
  /// comment named until 2026-09-12. The two are different numbers that
  /// happen to agree on oats, 0.5 in both places; barley and compound feed
  /// are 0.4. They answer different questions: the fund is opened in ONE
  /// grain and is capped by what that grain may be, while the ration is one
  /// figure for the whole working stock and is measured against the balance
  /// knob (livestock design §11 — "больше половины нормы им не закроешь").
  /// Move the knob and this ceiling does not move; the comment said it
  /// would.
  ///
  /// The daily need is taken at the CURRENT month and multiplied by the
  /// year: the pasture months discount it, so a ceiling read in July would
  /// be smaller than one read in January for the same herd. That is a
  /// simplification and it is named rather than hidden — the alternative is
  /// a twelve-month walk for a number the chairman uses once.
  Grams FodderFundGrams(const WorldState& current, ResourceId resource) const {
    const float value =
        resource.value < config_.feed_values.size() ? config_.feed_values[resource.value] : 0.0F;
    if (!(value > 0.0F)) {
      return 0;
    }
    float share = 0.0F;
    for (const FeedLinkDef& link : config_.feed_links) {
      if (link.work_only != 0 && link.resource.value == resource.value) {
        share = link.max_share;
        break;
      }
    }
    const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
    float units = 0.0F;
    for (const HerdRow& herd : current.herds.rows) {
      if (herd.household_owned != 0 || herd.kind.value >= config_.livestock.size()) {
        continue;  // the fund is the kolkhoz's; a yard's animals feed themselves
      }
      bool works = false;
      for (const FeedLinkDef& link : config_.feed_links) {
        works = works || (link.kind.value == herd.kind.value && link.work_only != 0);
      }
      if (!works) {
        continue;  // a cow has no work ration, so it holds nothing in this fund
      }
      units += FeedNeedUnits(config_, config_.livestock[herd.kind.value], herd, month);
    }
    const float year_units = units * static_cast<float>(kDaysPerYear) * share;
    return GramsFromKilograms(year_units / value);
  }

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
  OrderRefusal UnsealFund(WorldState& current, const OrderRow& order) const {
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
    if (index == kInvalidDefIdValue || index >= config_.spoil_days.size()) {
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
    // Told apart by the feed roster's own `work_only` flag rather than by a
    // list of keys here: that flag IS the statement "this is the working
    // ration and not the maintenance one", it comes from the design db, and
    // a second list of fodder grains in this file would be its second home.
    if (order.fund == FundKind::kFodder) {
      bool work_feed = false;
      for (const FeedLinkDef& link : config_.feed_links) {
        if (link.work_only != 0 && link.resource.value == index) {
          work_feed = true;
          break;
        }
      }
      if (!work_feed) {
        return OrderRefusal::kRuleForbids;
      }
      // AND NOT WIDER THAN THE FUND ITSELF. Its size is the year's work
      // ration of the working stock in feed units (resources design §6,
      // boss's decision of 2026-09-12) — the seed fund is measured off the
      // sowing to come, and this one off the harness that will plough.
      const Grams held = FodderFundGrams(current, order.resource);
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
  OrderRefusal SetRotation(WorldState& current, const OrderRow& order) const {
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
    std::array<CropId, 3> slots = {
        order.rotation_year0, order.rotation_year1, order.rotation_year2};
    std::uint32_t named = 0;
    for (const CropId slot : slots) {
      if (slot.value == kInvalidDefIdValue) {
        continue;
      }
      if (slot.value >= config_.crops.size()) {
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
      // Ploughed this year: idle arable with a rotation, whether a crop is in
      // the slot or it is fallow. A winter crop already standing was manured
      // when IT was ploughed, last autumn.
      //
      // THE ROTATION TEST IS HALF OF ONE FIX AND NOT A SECOND ONE. Ground
      // nobody has told anything is never ploughed, so manure spread on it
      // is never turned in — it would simply be gone. It was safe here only
      // by accident until 2026-09-12: the same missing guard in the year's
      // fertility recovery had run the weeds up to a hundred points, which
      // put them at the BACK of the poorest-first queue below. Repair that
      // and they become the poorest land on the farm, and the first two
      // years' manure goes into the burdock. The analysis said so before it
      // happened; the two lines are one repair.
      if (field.kind == LandKind::kArable && field.phase == FieldPhase::kIdle &&
          field.manure_applied == 0 && HasRotation(field)) {
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
  //
  // THE LIST IS THE WHOLE READ SET (core_tables/required_tables.h), and it
  // named four of these until 2026-09-08. The rest are read by
  // ParseProductionConfig below and fell back to their defaults in silence,
  // which is the state this very check exists to refuse.
  if (!RequireTables(tables,
                     stubs,
                     "production",
                     {"crops",
                      "livestock",
                      "farming",
                      "resources",
                      "unit_types",
                      "unit_levels",
                      "feed_links",
                      "meadow_kinds",
                      "field_phases",
                      "campaign",
                      "transport",
                      "labor",
                      "professions"},
                     nullptr)) {
    return nullptr;
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
