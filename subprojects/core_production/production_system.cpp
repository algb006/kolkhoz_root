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
#include "district_limit.h"
#include "district_plan.h"
#include "district_visit.h"
#include "extraction_digging.h"
#include "field_haul.h"
#include "field_removal.h"
#include "field_work.h"
#include "herd_system.h"
#include "night_pasture.h"
#include "production_alarms.h"
#include "production_config.h"
#include "production_orders.h"
#include "stock_lights.h"
#include "stock_ops.h"
#include "timber_felling.h"
#include "unit_production.h"

namespace core {
namespace {

/// Days a snow cover has lain before it is settled snow: the melt rule never
/// lets a dusting reach a second day (world_state.h, snow_cover_days).
constexpr std::uint16_t kSettledSnowCoverDays = 2;

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
    ConsumeProductionOrders(config_, current);
    // A felling the crew finished this hour is lying on the ground this hour
    // (timber_felling.h) — the same reasoning as the field phases below.
    FellFinishedStands(config_, current);
    // And a digging finished this hour lies on its site this hour
    // (extraction_digging.h).
    DigFinishedSites(current);
    if (config_.crops.empty()) {
      return;  // a table-less world idles (STUB)
    }
    // Finished work is picked up the same hour the crew finishes it: labor
    // runs earlier in this very slot, so a field ploughed by noon opens its
    // harrowing at noon instead of losing the afternoon.
    AdvanceFinishedPhases(current);
    // The MTS column after the phases: a phase the crew opened this hour is
    // held to the column's share before any crew is sent to it.
    RunMtsColumn(config_, current);
    if (current.calendar.tick == 1) {
      // The first winter's plan: genesis hands over a heap and a January, and
      // day zero is no year's turn for the daily bookkeeping below. Without
      // this the inherited 250 t lay untouched through the whole first year.
      PlanManure(current);
      // And the first year's limit, for the same reason: genesis hands over a
      // world, and the district's plan stands from the first day.
      GrantFirstLimitYear(config_, current);
    }
    // The day's hauling is settled at its LAST tick, and the hour matters.
    // Labor runs earlier in this same slot, so by now the carriers have
    // finished walking; and settling here rather than at tomorrow's dawn
    // means the seam labor reads next morning already says what is really
    // left to carry. Settle at dawn instead and every second day would find
    // an empty demand and send nobody (task A4).
    if (HourFromTick(current.calendar.tick) + 1U >= kTicksPerDay) {
      SettleHauling(config_, current);
      SettleStandHauling(config_, current);
      SettleSiteHauling(config_, current);
      // The sawmill after the carting, so tonight's logs off the stands are
      // in tomorrow's demand (unit_production.h).
      SettleUnitProduction(config_, current);
      // The district's carts: what came today goes through the same door.
      ArriveLimitDeliveries(config_, current);
      // And the stock bought on the limit, which comes through no door at
      // all: it stands under a roof, or at a yard, on its day. Before the
      // herd day below, so a head that arrives this morning is fed tonight
      // and billeted by the same walk as every other animal.
      ArriveLivestock(config_, current);
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
      AnnouncePlan(config_, current);
    }
    // THE WINTERING AS IT STANDS ON ITS DATE (epochs design §6): the fodder
    // the farm holds and the days to the first spring grass, booked raw so
    // the reader divides once over numbers it can check. Taken off the same
    // forecast the office's feed light is drawn from — the index and the
    // light the player watched all autumn must not be able to disagree — and
    // core_residents books the food half of the same day beside it.
    if (current.calendar.day % kDaysPerYear ==
        static_cast<std::uint32_t>(Month::kDecember) * kDaysPerMonth) {
      const StockForecast fodder = FeedLight(config_, current);
      current.ledger.current.feed_days_dec1 = static_cast<float>(fodder.days_of_stock);
      current.ledger.current.winter_days_dec1 =
          static_cast<std::uint16_t>(fodder.days_to_date < 0 ? 0 : fodder.days_to_date);
    }
    // The district's people: announced first, so a notice of zero days
    // announces a visit before it arrives on the same tick (district_visit.h).
    AnnounceRegularVisits(config_, current);
    ArriveDistrictVisits(config_, current);
    // THE YEAR'S HIGH-WATER MARK OF WORKED LAND, raised once a day. The
    // district's next norm comes off it (world_state.h), and it is a MAXIMUM
    // so that no single day's order can decide a year's figure.
    const float worked_today = WorkedArableHa(current);
    current.plan.worked_ha_this_year = worked_today > current.plan.worked_ha_this_year
                                           ? worked_today
                                           : current.plan.worked_ha_this_year;
    RunFields(current);
    // BEFORE THE HERD DAY, because the herd day is what reads it: the feed
    // need asks whether the team is out tonight, and the first night has to
    // be said on the day it happens and not the day after.
    RunNightPasture(config_, current);
    // The era events, last of the day's district business: the accumulated
    // grant they weigh is booked at the year's turn, and an office raised
    // this morning is standing by now.
    RunEraEvents(config_, current);
    RunHerdDay(config_, current);
  }

  std::int32_t DaysToNextHarvest(const WorldState& completed) const override {
    return DaysToHarvest(config_, completed);
  }

  Grams StandingCropGrams(const WorldState& /*world*/, const FieldRow& field) const override {
    if (field.crop.value >= config_.crops.size()) {
      return 0;
    }
    return FieldYieldGrams(config_, field, config_.crops[field.crop.value]);
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
    CollectSowingAlarms(config_, completed, alarms);
    CollectGatherAlarms(config_, completed, alarms);
    CollectHerdAlarms(config_, completed, alarms);
    CollectPlanAlarms(config_, completed, alarms);
    CollectTimberAlarms(config_, completed, alarms);
  }

 private:
  /// The labor seam, seen from the production side (land_state.h): a field
  /// stands in a working phase until its crew has drained
  /// work_days_remaining, and only then moves on. No workers, no progress —
  /// deliberately.
  void AdvanceFinishedPhases(WorldState& current) {
    for (FieldRow& field : current.fields.rows) {
      AdvanceFinishedField(config_, current, field);
    }
  }

  /// January 1: the rotation plan advances one year, and fallow that stood
  /// the whole year pays out its recovery.
  void RunYearStart(WorldState& current) const {
    DeliverPlan(config_, current);
    // Read before JudgePlan hands the next year's plan down over this one.
    const bool plan_fully_met = PlanFullyDelivered(config_, current);
    const float overfulfil_tonnes = PlanOverfulfilGrainTonnes(config_, current);
    JudgePlan(config_, current);
    TurnLimitYear(config_, current, plan_fully_met, overfulfil_tonnes);
    GrowOldForest(config_, current);
    // THE CLOSING YEAR'S LARGEST WORKED AREA becomes next spring's figure,
    // and the running maximum is what makes it un-gameable: a single tick's
    // reading could be emptied by an order settled that same tick — the order
    // book is consumed at the top of this very call — and re-filled the next
    // morning at no cost at all. Against the year's maximum the same escape
    // costs the whole harvest (world_state.h).
    //
    // AND THE BASE ONLY GROWS (boss, register 222; econ's reading): against
    // the year's maximum alone the escape cost ONE failed year, and then the
    // plan was off for good — withdraw every chain, fail once, owe nothing
    // ever after, which made «three failed years to the trial» unreachable
    // (host measured it: no plan at all from day 144). District §9: «Недосев
    // — способ провалить план, а не уменьшить его». A removed field does not
    // lower it either; only the district writes arable off, and Epoch I has
    // no such verb.
    const float worked_today = WorkedArableHa(current);
    const float year_max = std::max(worked_today, current.plan.worked_ha_this_year);
    current.plan.worked_ha_last_year = std::max(current.plan.worked_ha_last_year, year_max);
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
      // A field still waiting to sow last year's crop lets it go before the
      // chain moves on, or it sows that crop in the next slot's place.
      ReleaseUnsownPreparation(current, field);
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

  void RunFields(WorldState& current) {
    const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
    const float temperature = current.weather.air_temperature_celsius;
    const bool snowing = current.weather.precipitation == Precipitation::kSnow;
    // THE SETTLED SNOW TAKES WHAT LIES ON THE FIELD (farming design §6: "Лёг
    // снег — всё, что осталось на этом поле… в кучах на краю — пропадает
    // целиком"). A cover on its second day is settled: the melt rule never lets
    // a dusting reach it (world_state.h, snow_cover_days).
    const bool cover_settled = current.weather.snow_cover_days >= kSettledSnowCoverDays;
    for (FieldRow& field : current.fields.rows) {
      // Until 2026-09-15 a reaped load that no cart had taken lay out through
      // the winter and into the next year, booked as lost only when a second
      // harvest came to take its place — and a loaded field stood as the
      // shortage signal all that time. The snow takes it the winter it lies
      // out, and the field is empty by spring (boss, parcel 408).
      if (cover_settled && field.reaped_grams > 0) {
        AddLedgerAmount(
            current.ledger.current.lost_no_room, field.reaped_resource, field.reaped_grams);
        field.reaped_grams = 0;
        field.reaped_resource = ResourceId{};
        field.haul_days_remaining = 0.0F;
        field.haul_days_written = 0.0F;
      }
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
      //
      // AND IT IS A LOSS OF THE HARVEST SEASON, not of any snowy day: "снег
      // остаётся бедой УБОРКИ" (farming design). Until 2026-09-13 a spring
      // flurry took a field sown three days earlier — oats sown on day 11 of
      // the second year in oat_balance vanished on day 14 at −0.05 °C, and
      // together with the stale-crop turn that kept the run from sowing oats
      // for fifteen years. Snow before the crop's own reaping season finds
      // seedlings, not a standing harvest.
      const bool reaping_season = month >= crop.harvest_from_month;
      if (snowing && reaping_season && !crop.is_winter && !crop.is_perennial) {
        LoseFieldToSnow(config_, current, field, crop);
        // No LogWarning: phase code does not log (core_log contract,
        // DEADLOCK-001), and a condition worth telling the player is an
        // alarm, not a line in a file nobody opens.
        continue;
      }
      if (field.phase != FieldPhase::kGrowing) {
        continue;  // already being reaped
      }
      // THE CALENDAR AND THE RIPENING, and until 2026-09-13 it was the
      // calendar alone — so a crop sown the day before its window opened gave
      // a full yield, and `growth_min_temp_c` sat in crops.csv with no reader
      // at all. Ripening is a DURATION now (field_work.h, RipenDays), measured
      // from the day the seed went in, and this is the half of boss's chain
      // that makes the other half bite: without it a late sowing still ripens
      // instantly and nothing is ever lost to snow.
      // One home for the condition; its back edge is open defect UB-001.
      const bool in_window = ReapingMayOpen(config_, field, month, current.calendar.day);
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
      // THE QUESTION IS "WILL THIS GROUND BE WORKED", NOT "DOES IT HAVE A
      // CHAIN", and those were the same question only until a field could skip
      // a year (boss's decision of 2026-09-13).
      //
      // `HasRotation` asks whether the chairman has EVER told this field what
      // to grow. That answered the right question while the only fields
      // without a chain were the derelict ninety hectares nobody had claimed.
      // The moment a chairman could withdraw a chain from ground he means to
      // go on working — which is what he does when the spring will not fit —
      // the two came apart: such a field fell out of the manure plan, grew
      // poorer, and so was chosen to be dropped again the next year. A circle
      // the gate made itself.
      //
      // Agronomy says the same thing: manure goes under the sowing to come,
      // and a field that has rested a year is the FIRST candidate, not the
      // last.
      //
      // Land never cropped stays out, and the test tells it apart on its own:
      // the derelict ground has never been sown, so it has no sowing day.
      const bool will_be_worked = HasRotation(field) || field.sown_day != kNeverSownDay;
      if (field.kind == LandKind::kArable && field.phase == FieldPhase::kIdle &&
          field.manure_applied == 0 && will_be_worked) {
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
                                                          StubTables stubs,
                                                          std::uint32_t growing_season_last_day) {
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
                     {"crops",           "livestock",     "farming",         "resources",
                      "unit_types",      "unit_levels",   "feed_links",      "meadow_kinds",
                      "field_phases",    "campaign",      "transport",       "labor",
                      "professions",     "world_params",  "timber_stands",   "extraction_sites",
                      "resource_stores", "limit_catalog", "limit_lot_goods", "limit_lot_livestock"},
                     nullptr)) {
    return nullptr;
  }

  ProductionConfig config;
  std::string error;
  if (!ParseProductionConfig(tables, config, error)) {
    LogError(error);
    return nullptr;
  }
  config.growing_season_last_day = growing_season_last_day;
  return std::make_unique<ProductionSystem>(config);
}

}  // namespace core
