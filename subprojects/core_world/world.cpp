// Implementation of the core_world boundary (include/core_world/world.h):
// world genesis (task O0) and the wired standard simulation (task O2) — the
// subsystems, the two composite sequential slots and the step engine.

#include "core_world/world.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "campaign_tables.h"
#include "core_catalog/district_trip_catalog.h"
#include "core_catalog/district_visit_catalog.h"
#include "core_catalog/extraction_catalog.h"
#include "core_catalog/limit_catalog.h"
#include "core_catalog/processing_catalog.h"
#include "core_catalog/road_cost_catalog.h"
#include "core_catalog/road_rules_catalog.h"
#include "core_catalog/table_value.h"
#include "core_catalog/timber_catalog.h"
#include "core_catalog/world_conventions.h"
#include "core_catalog/world_junctions.h"
#include "core_common/calendar.h"
#include "core_common/chairman_away.h"
#include "core_common/emit_event.h"
#include "core_common/ids.h"
#include "core_common/ledger_state.h"
#include "core_common/order_state.h"
#include "core_common/random.h"
#include "core_common/road_view.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_construction/construction_system.h"
#include "core_labor/labor_system.h"
#include "core_log/log.h"
#include "core_logistics/logistics_system.h"
#include "core_production/production_system.h"
#include "core_residents/residents_system.h"
#include "core_tables/required_tables.h"
#include "core_tables/tables.h"
#include "core_time/month_ice.h"
#include "core_time/time_system.h"
#include "core_world/era_readiness.h"
#include "core_world/road_tools.h"
#include "core_world/watchdog.h"
#include "start_elder.h"
#include "start_literacy.h"
#include "wait_rules.h"

namespace core {

namespace {

/// The composite decisions slot (phase 3): the subsystems' sequential
/// sub-steps in the fixed order of manual/54-modules.md §3 — assignments,
/// then demography, then production decisions. The order is part of the
/// design contract, not a wiring accident.
class DecisionsSlot final : public ISequentialPhase {
 public:
  DecisionsSlot(ILaborSystem& labor,
                ILogisticsSystem& logistics,
                IResidentsSystem& residents,
                IProductionSystem& production,
                IConstructionSystem& construction)
      : labor_(&labor),
        logistics_(&logistics),
        residents_(&residents),
        production_(&production),
        construction_(&construction) {}

  void RunSequential(const WorldState& previous, WorldState& current) override {
    // The chairman in the district (core_common/chairman_away.h): the
    // village's orders are answered before any consumer can take one.
    RefuseVillageOrdersWhileAway(current);
    // The chairman's doors to the groom's tasks (B7), ahead of the placement:
    // a task paused this hour is not offered to it, a task raised to level 0
    // is placed ahead of every window (labor_system.cpp).
    logistics_->ReadTaskOrders(current);
    labor_->RunAssignmentDecisions(previous, current);
    // The groom's tasks (routing stage B, B2), once a day after the morning's
    // placement: the carters placed today mark their loads served.
    logistics_->RunTasks(previous, current);
    // The groom's plan (B3), at hour 1: after the labour sub-step's top-up and
    // the goods carts' passengers, before the hour's work reads it (B4).
    if (HourFromTick(current.calendar.tick) == 1) {
      logistics_->BuildPlan(current);
    } else {
      // THE RE-PLAN BY EVENT, every later hour (B5; logistics_system.h).
      logistics_->Replan(current);
    }
    // A load of level 0 an hour unserved stops the fast-forward (B8).
    logistics_->SayLateLoads(current);
    residents_->RunDemographyDecisions(previous, current);
    production_->RunProductionDecisions(previous, current);
    // Construction last (task A2, manual/71-construction.md §6): a unit
    // finished here is seen by everybody from the next tick, and a site
    // started here gets its crew at tomorrow's placement — which is the
    // design's own rule that a change of assignment takes effect the next
    // day.
    construction_->RunConstructionDecisions(previous, current);
  }

 private:
  ILaborSystem* labor_;

  ILogisticsSystem* logistics_;

  IResidentsSystem* residents_;

  IProductionSystem* production_;

  IConstructionSystem* construction_;
};

/// The most calorie-dense thing a person eats is fat, at about 9 kcal per
/// gram. Ten is the ceiling: generous enough that no real food approaches
/// it, tight enough that a decimal point in the wrong place is caught. The
/// shipped table runs 0.25 to 3.5.
///
/// THE BOUND EXISTS BECAUSE THE PRODUCT IS CAST TO AN INTEGER (UB-005). The
/// year's harvest_kcal is `sum(grams * density)` accumulated in a double and
/// then `static_cast<std::int64_t>`, and a conversion whose value will not
/// fit the target is undefined behaviour, not a large number
/// ([conv.fpint]/1). The cell was checked for being POSITIVE and nothing
/// else, so one absurd figure in resources.csv reached that cast unopposed.
///
/// The door is here, where the vector is filled, and not beside the cast.
/// Today the vector has exactly ONE reader, so the two places would be
/// equally effective — and that is the reason to prefer this one rather than
/// an argument against it: a guard beside the single reader is a guard the
/// second reader will not inherit. Same rule as the plot radii and the level
/// ladder.
///
/// It bounds this vector and not the column: food_config.cpp reads
/// kcal_per_gram out of the same table by its own path and is not covered
/// here. That is a separate reader with a separate door to write, and saying
/// so is the difference between a bound and a belief.
constexpr float kMaxKcalPerGram = 10.0F;

/// @brief Caloric density per resource, dense by ResourceId.
/// A resource without the column, or with a blank cell, reads zero — which
/// is the right answer for hay and straw and the honest one for anything
/// the design has not priced yet.
/// @note A cell outside 0..kMaxKcalPerGram reads zero as well, but SAYS SO:
///       a blank cell is the design not having priced a thing, and a figure
///       of ten thousand is a typo, and the two must not arrive at the same
///       silence.
std::vector<float> FoodValuePerResource(const ITableSet& tables) {
  std::vector<float> density;
  const ITable* resources = tables.FindTable("resources");
  if (resources == nullptr) {
    return density;
  }
  const std::uint32_t column = resources->FindColumn("kcal_per_gram");
  density.assign(resources->RowCount(), 0.0F);
  if (column == kNoTableColumn) {
    return density;
  }
  for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
    const std::optional<float> cell = resources->CellReal(row, column);
    if (!cell) {
      continue;  // blank: not priced yet, and the zero already there says so
    }
    // Written positively, so that a NaN fails it. CellReal refuses nan and
    // inf already; this does not lean on that, because a test written as
    // `> kMax` would pass one the day the reader beneath it changes.
    if (*cell > 0.0F && *cell <= kMaxKcalPerGram) {
      density[row] = *cell;
      continue;
    }
    if (*cell != 0.0F) {
      LogWarning("resources: kcal_per_gram in row " + std::to_string(row) +
                 " is outside 0..10 and was read as zero — the densest food there is comes to "
                 "about 9 kcal per gram");
    }
  }
  return density;
}

/// The events slot (phase 6). The event system itself is still a STUB — it
/// is a phase-3 project feature — but the slot is no longer empty: since
/// stage 7 it keeps the run ledger's two entries that cannot be kept where
/// they happen (manual/68-run-ledger.md §3), and it turns the year's book.
///
/// WHY THE FOLD LIVES HERE. The garden and the family meal are PARALLEL
/// phases, and a parallel phase may keep no cross-row accumulator
/// (buffer-law rule 5) — so neither can add to a settlement-wide counter as
/// it works. What they leave behind is a pantry that changed, and the hours
/// separate them: at hour 22 the only writer of any pantry is the plot, at
/// hour 23 the only writer is the meal (family_state.h, the stage-6 write
/// map). So the pantry difference between `previous` and `current` at those
/// two hours IS those two flows, taken sequentially, in row order.
///
/// If that separation is ever broken, this fold starts lying — and it lies
/// visibly, because the year stops balancing as "what came in minus what
/// was eaten equals what is left".
/// The subsystems that raise lamps, and the red's threshold: ONE HOME for the
/// world's collection, read by the boundary between steps (CollectAlarms) and
/// by the daily red check inside the step (EventsSlot).
struct AlarmSources {
  const ILaborSystem* labor = nullptr;
  const IResidentsSystem* residents = nullptr;
  const IProductionSystem* production = nullptr;
  const IConstructionSystem* construction = nullptr;
  const ILogisticsSystem* logistics = nullptr;

  /// world_params `alarm_red_within_days` (PaintAlarms).
  std::uint16_t red_within_days = 0;
};

/// Every subsystem's lamps over `state`, coloured. The order is the
/// boundary's (it sorts after); the colour last — every subsystem has raised
/// its alarms with their days, and production's full store has taken the
/// days of the refusal that lights it.
void CollectWorldAlarms(const AlarmSources& sources,
                        const WorldState& state,
                        std::vector<Alarm>& alarms) {
  sources.labor->CollectAlarms(state, alarms);
  sources.residents->CollectAlarms(state, alarms);
  sources.production->CollectAlarms(state, alarms);
  sources.construction->CollectAlarms(state, alarms);
  // The groom's lamp (B8; kLogisticsLate). Not asked until B8: its
  // CollectAlarms was a STUB and nobody called it.
  sources.logistics->CollectAlarms(state, alarms);
  PaintAlarms(alarms, sources.red_within_days);
}

class EventsSlot final : public ISequentialPhase {
 public:
  EventsSlot(std::vector<float> kcal_per_gram,
             ReadinessCatalog readiness,
             float food_variety_categories,
             float life_speedup,
             AlarmSources alarm_sources)
      : kcal_per_gram_(std::move(kcal_per_gram)),
        readiness_(std::move(readiness)),
        food_variety_categories_(food_variety_categories),
        life_speedup_(life_speedup),
        alarm_sources_(alarm_sources),
        watchdog_(CreateWatchdog(CreateWaitRules())) {}

  /// The era's catalog the transition is judged by — read by the office's
  /// «куда я иду» (EraReadiness), so the view and the order share one.
  const ReadinessCatalog& Readiness() const { return readiness_; }

  void RunSequential(const WorldState& previous, WorldState& current) override {
    FoldPantryFlows(previous, current);
    // THE WATCHDOG, every game hour, here and nowhere else: the one
    // single-threaded phase of the step (architecture §7ж³; core_world/
    // watchdog.h; routing stage B, B6). Before the ledger turns, so a firing
    // in the year's last hour is counted in that year.
    if (watchdog_ != nullptr) {
      const WatchdogTally tally = watchdog_->WalkHour(current);
      for (std::size_t kind = 0; kind < kWaitKindCount; ++kind) {
        current.ledger.current.watchdog_fired[kind] += tally.fired[kind];
      }
    }
    // Before the sweep, so the transition is answered in the step it was
    // read in and the sweep finds it terminal rather than unconsumed. On the
    // year's first tick it reads the readiness of the turn BEFORE, because
    // the rotation below scores the new one: an order and a turn in the same
    // step meet the older verdict, and the next step the newer.
    ConsumeTransitionOrders(readiness_, current);
    // The day's red check (hour 0), over the step's finished world.
    SayLampsTurnedRed(current);
    SweepOrderBook(current);
    RotateLedger(current);
  }

  /// The events slot's half of the order book's life (order_state.h): every
  /// row that reached a terminal status this step gets its event and is
  /// removed, and a row still kPending after every consumer has looked at it
  /// is refused with kNoConsumer — an order kind whose mechanic has not
  /// arrived answers with a refusal the presentation can show, never with
  /// silence.
  ///
  /// Removal is by id and after the sweep, because removing rows moves the
  /// ones behind them (state_table.h: swap-with-last).
  static void SweepOrderBook(WorldState& current) {
    std::vector<OrderId> done;
    for (std::uint32_t row = 0; row < current.orders.rows.size(); ++row) {
      OrderRow& order = current.orders.rows[row];
      if (order.status == OrderStatus::kPending) {
        order.status = OrderStatus::kRefused;
        order.refusal = OrderRefusal::kNoConsumer;
      }
      EventKind kind = EventKind::kNone;
      switch (order.status) {
        case OrderStatus::kDone:
          kind = EventKind::kOrderDone;
          break;
        case OrderStatus::kRefused:
          kind = EventKind::kOrderRefused;
          break;
        case OrderStatus::kCancelled:
          kind = EventKind::kOrderCancelled;
          break;
        default:
          continue;  // accepted or active: still the consumer's
      }
      SimEvent& event = EmitEvent(current, kind, EventSeverity::kNotable);
      event.order = current.orders.row_ids[row];
      event.unit = order.unit;
      event.resident = order.resident;
      event.field = order.field;
      event.herd = order.herd;
      event.amount = static_cast<std::int64_t>(order.refusal);
      // The lot the order named (host door request no. 4): the row is gone
      // at the end of this sweep, so the event is where it has to travel.
      event.lot = order.lot;
      // AND THE RESOURCE IT NAMED — for kMaterialsShort the first line of the
      // recipe the village lacks (boss seq 167, «отказ называет что»). The
      // construction wrote it into the row, and the row is swept here: until
      // 0.34.29 the what died with it, and no reader of the refusal ever saw
      // which material held a site (the Epoch II diagnosis).
      event.resource = order.resource;
      // And the road an upgrade or a demolition named (delivery 7a), for the
      // same reason as the lot.
      event.road = order.road;
      done.push_back(current.orders.row_ids[row]);
    }
    for (const OrderId id : done) {
      RemoveRow(current.orders, id);
    }
  }

 private:
  // The kolkhoz yard used to be RAISED here, by a stub that stood in for the
  // PLAYER: phase 1 had no construction, so nobody could build the first
  // building of the campaign, and without it the team died of old age and
  // the farm stopped ploughing for ever. Task A7 removed it whole. The yard
  // is built by an order now, the groom is appointed by an order, and the
  // horses come in off the private yards when he is (production's herd day,
  // herd_system.cpp — StableHorses). What stood in for the player is played
  // by whoever plays him: the run policy in tests/run, never a rule of the
  // core (manual/74-posts.md §8).

  static Grams AmountAt(const ResourceAmounts& amounts, std::size_t index) {
    return index < amounts.size() ? amounts[index] : 0;
  }

  static void FoldPantryFlows(const WorldState& previous, WorldState& current) {
    const std::uint32_t hour = HourFromTick(current.calendar.tick);
    const bool plot_hour = hour == kTicksPerDay - 2U;
    const bool meal_hour = hour == kTicksPerDay - 1U;
    if (!plot_hour && !meal_hour) {
      return;
    }
    YearLedger& book = current.ledger.current;
    for (std::uint32_t row = 0; row < current.families.rows.size(); ++row) {
      const std::uint32_t before = FindRow(previous.families, current.families.row_ids[row]);
      if (before == kNoRow) {
        continue;  // a household younger than the previous step; nothing to diff
      }
      const ResourceAmounts& now = current.families.rows[row].pantry;
      const ResourceAmounts& was = previous.families.rows[before].pantry;
      const std::size_t width = now.size() > was.size() ? now.size() : was.size();
      // The one walk that already knew, and the loop bound stays: it stops
      // the WALK, not just the id, and AmountAt past the sentinel would be
      // counting columns no ledger can name.
      for (std::size_t index = 0; index < width && index < kInvalidDefIdValue; ++index) {
        const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
        const Grams delta = AmountAt(now, index) - AmountAt(was, index);
        AddLedgerAmount(
            plot_hour ? book.plot_harvest : book.eaten, resource, plot_hour ? delta : -delta);
      }
    }
  }

  /// @brief Writes the closed year onto the office wall (ledger_state.h,
  /// ChronicleYear).
  ///
  /// Called from the rotation and only from there: the row is a statement
  /// about a year that is over, and a row appended anywhere else would be
  /// about a year still being lived. One row per rotation, so a thirty-year
  /// run leaves thirty of them — and the first year leaves ONE point, which
  /// is the honest picture of a farm that has only just started rather than
  /// a line drawn from zero.
  void AppendChronicleYear(WorldState& current) const {
    ChronicleYear row;
    row.year = current.ledger.closed.year;
    row.residents = static_cast<std::uint32_t>(current.residents.rows.size());
    // Area-weighted over the WORKED arable — meadows excluded, and ground
    // nobody has given a rotation excluded with them. The reasoning is on
    // the field itself, and it is a decision rather than an average.
    //
    // THE SECOND HALF OF THAT TEST WAS A LAND KIND until 2026-09-12: the
    // start's ninety-three unworked hectares carried LandKind::kDerelict and
    // fell out here without anyone saying so. The kind went, and with it the
    // silence — ninety-three of a hundred and sixty-three hectares, frozen
    // at the start's sixty-five for ever, would have entered both numerator
    // and denominator of the figure the player reads off the office wall.
    // Nothing asserted it either way; the analysis walked the readers of the
    // removed kind and found this one.
    float weighted = 0.0F;
    float area = 0.0F;
    for (const FieldRow& field : current.fields.rows) {
      if (field.kind != LandKind::kArable || !HasRotation(field)) {
        continue;
      }
      weighted += field.fertility * field.area_ga;
      area += field.area_ga;
    }
    row.fertility = area > 0.0F ? weighted / area : 0.0F;
    // FOOD OFF THE ARABLE, AND NOT MASS OFF THE FARM. Mass was the first
    // answer and it was wrong in a way worth keeping written down: potatoes
    // yield 9000 kg/ha against rye's 850 and feed a quarter as much per
    // kilogram, so a year that swapped rye for potatoes would have read as a
    // bumper year. A sum of unmixable quantities is a number somebody
    // decides by and gets it wrong (boss's own rule, applied to his own
    // order, 2026-09-05).
    //
    // Caloric density does two jobs at once here. It makes the crops
    // comparable, and it drops the meadow without a special case: hay and
    // straw feed animals, not people, so their density is zero and they fall
    // out. That is what pairs this sheet with the fertility one — both are
    // then about the same land.
    //
    // THE LIMIT, SAID RATHER THAN DISCOVERED: a crop grown for fibre is
    // invisible here. Flax exhausts the soil and adds nothing to this curve,
    // so the pair reads "the land is being eaten" for a year that was in
    // fact spent on linen. The sheet measures FOOD off the arable, and that
    // is the whole of what it measures.
    double kcal = 0.0;
    for (std::size_t index = 0; index < current.ledger.closed.harvest.size(); ++index) {
      const Grams grams = current.ledger.closed.harvest[index];
      const float density = index < kcal_per_gram_.size() ? kcal_per_gram_[index] : 0.0F;
      if (grams > 0 && density > 0.0F) {
        kcal += static_cast<double>(grams) * static_cast<double>(density);
      }
    }
    // AND THE SUM IS BOUNDED TOO, BECAUSE THE CELL BOUND IS NOT ENOUGH.
    // Ten kcal per gram makes each FACTOR safe and does nothing for their
    // sum: this cap is the only thing that bounds the value at all.
    //
    // THE FIRST DRAFT OF THIS COMMENT GOT THE ARITHMETIC WRONG, AND WRONG IN
    // THE DIRECTION THAT MADE THE CAP LOOK OPTIONAL. It said "102 resources
    // times the 9.0e15 g ceiling of Grams times 10 leaves half a percent of
    // room". kMaxGrams bounds ONE CONVERSION — GramsFromFloat refuses more
    // than that in a single call — and bounds nothing that is stored: a
    // harvest column is accumulated by AddLedgerAmount over every field and
    // every day of the year, so one cell of it is bounded by int64 and by
    // nothing else. The table cap is 65535 rows, not 102. The honest worst
    // case is around 9e21, three orders past int64, and there never was a
    // margin to lose.
    //
    // This is not a second home for the cell bound. "What may a density be"
    // is answered where the vector is filled; "can this accumulation reach
    // the target type" is a different question, and it needed a different
    // answer rather than a comforting subtraction.
    //
    // Saturating rather than refusing: a year of 9e18 kcal is four billion
    // years of one person's food, so the number is nonsense long before it
    // gets here, and the chronicle's job is to carry the year rather than to
    // judge it. It says so out loud, which is the part that matters.
    //
    // WRITTEN POSITIVELY, like every range test in this core: `kcal > kMax`
    // would let a NaN through, because NaN compares false against everything.
    // No NaN can reach here today — CellReal refuses non-finite cells, the
    // density filter is positive, and a sum of finite terms stays finite —
    // but the rule exists so that the day one of those doors moves, this line
    // does not have to be found again.
    constexpr double kMaxHarvestKcal = 9.0e18;
    if (!(kcal >= 0.0 && kcal <= kMaxHarvestKcal)) {
      LogWarning(
          "chronicle: the year's harvest_kcal fell outside 0..9e18 and was capped — a "
          "harvest column accumulates all year, so this is a quantity that ran away, not "
          "a table that grew");
      kcal = kcal > 0.0 ? kMaxHarvestKcal : 0.0;
    }
    row.harvest_kcal = static_cast<std::int64_t>(kcal);
    current.ledger.chronicle.push_back(row);
  }

  /// Caloric density by ResourceId, from tables/resources.csv. Read once at
  /// wiring: the chronicle needs it every year and a table cannot change
  /// under a running campaign.
  std::vector<float> kcal_per_gram_;

  /// The unit types the readiness score needs, read once at wiring for the
  /// same reason as the densities above: a table cannot change under a
  /// running campaign.
  ReadinessCatalog readiness_;

  /// The era's food-variety threshold and the biology factor, both belonging
  /// to rules elsewhere and passed in rather than restated here.
  float food_variety_categories_ = 0.0F;

  float life_speedup_ = 1.0F;

  /// The lamps' sources for the daily red check (SayLampsTurnedRed).
  AlarmSources alarm_sources_;

  /// @brief THE LAMPS THAT TURNED RED (the lamp colour's interrupt; boss, the
  /// logistics thread [123], [127]; the human, 4 October 2026: «Такие срочные
  /// сигналы должны прерывать режим пропуска времени в игре»). Once a day, at
  /// hour 0: the world's lit lamps (Alarm::lamp) are collected and painted;
  /// each red one not in WorldState::red_lamps is said — kLampTurnedRed,
  /// kInterrupting, but kNotable for a kind RedLampInterrupts refuses — and
  /// the memory becomes today's reds.
  /// ONCE A DAY, NOT EVERY STEP: a collection costs 1.5-2.1 steps (Debug,
  /// 0.37.192's measure), so every hour would double the run; a lamp red and
  /// out between two checks says nothing here, and its own event, where it
  /// has one, still does (the census of 0.37.193, boss [129]).
  void SayLampsTurnedRed(WorldState& current) const {
    if (HourFromTick(current.calendar.tick) != 0 || alarm_sources_.labor == nullptr) {
      return;
    }
    std::vector<Alarm> alarms;
    CollectWorldAlarms(alarm_sources_, current, alarms);
    std::vector<RedLamp> red;
    std::vector<const Alarm*> said;
    for (const Alarm& alarm : alarms) {
      if (alarm.lamp == 0 || alarm.colour != AlarmColour::kRed) {
        continue;
      }
      const RedLamp lamp{.kind = alarm.kind, .subject = AlarmSubjectValue(alarm)};
      red.push_back(lamp);
      if (!std::ranges::binary_search(current.red_lamps, lamp)) {
        said.push_back(&alarm);
      }
    }
    std::ranges::sort(red);
    const auto [first, last] = std::ranges::unique(red);
    red.erase(first, last);
    for (const Alarm* alarm : said) {
      // A STARVING KOLKHOZ HERD'S LAMP IS NOTABLE (alarm_state.h,
      // RedLampInterrupts): its episode was said by kHerdWentHungry already.
      SimEvent& event = EmitEvent(
          current,
          EventKind::kLampTurnedRed,
          RedLampInterrupts(alarm->kind) ? EventSeverity::kInterrupting : EventSeverity::kNotable);
      event.amount =
          static_cast<std::int64_t>((static_cast<std::uint64_t>(alarm->kind) << kLampKindShift) |
                                    static_cast<std::uint64_t>(AlarmSubjectValue(*alarm)));
      event.unit = alarm->unit;
      event.field = alarm->field;
      event.herd = alarm->herd;
      event.family = alarm->family;
      event.resource = alarm->resource;
      event.stand = alarm->stand;
    }
    current.red_lamps = std::move(red);
  }

  /// kLampTurnedRed's amount: the lamp's kind above the subject's 32 bits.
  static constexpr std::uint32_t kLampKindShift = 32;

  /// The watchdog over every wait kind's rules (wait_rules.h). Built with the
  /// slot; CreateWatchdog refuses a kind without rules, and then nothing
  /// walks — which the unit test of the assembly reddens on.
  std::unique_ptr<IWatchdog> watchdog_;

  /// @brief Closes the year's book and opens the next.
  ///
  /// It closes on the FIRST tick of the new calendar year, after its phases
  /// 1-6 have run — so the turn's own bookkeeping (the trudodni burn, the
  /// plan delivery, the life-expectancy recompute), all of which happens at
  /// hour 0, lands in the year it settles rather than in the one that just
  /// began. The price, stated in ledger_state.h so nobody hunts for it:
  /// day 0's demography is booked to the year before.
  void RotateLedger(WorldState& current) const {
    if (current.calendar.tick == 0 || current.calendar.tick % kTicksPerYear != 0) {
      return;
    }
    const std::uint16_t ended = current.calendar.date.year > 1
                                    ? static_cast<std::uint16_t>(current.calendar.date.year - 1)
                                    : 0;
    current.ledger.closed = std::move(current.ledger.current);
    current.ledger.closed.year = ended;
    current.ledger.current = YearLedger{};
    // THE NEW YEAR'S LIMIT GRANT, booked in the book it belongs to. Production
    // made it earlier in this very tick, at its year start, while `current`
    // was still the closing year's book (core_production/district_limit.h).
    current.ledger.current.limit_points_granted = current.limit.points;
    AppendChronicleYear(current);
    // THE READINESS IS SCORED AFTER THE BOOKS HAVE ROTATED, so `closed` is
    // the year being judged. Scored anywhere earlier it would weigh the year
    // that has not happened yet — and the run it folds into would be a run of
    // empty books (era_readiness.h).
    ScoreReadiness(readiness_, food_variety_categories_, life_speedup_, current);
    // The books rotated, and the year that closed rides in `amount` as the
    // kind's contract says. Interrupting on purpose: a fast-forward that
    // runs past a year's end has run past the one moment the player is
    // certain to want to look at.
    SimEvent& event = EmitEvent(current, EventKind::kYearClosed, EventSeverity::kInterrupting);
    event.amount = static_cast<std::int64_t>(ended);
  }
};

/// The assembled simulation: owns the subsystems, the composite slots and
/// the engine, exposes only ISimulation. Subsystems hold configuration only
/// (subsystem law), so ResetWorld needs no notification fan-out — the
/// engine's own reset is the whole story.
class StandardSimulation final : public ISimulation {
 public:
  StandardSimulation(const StandardSimulationConfig& config,
                     WorldState start,
                     std::unique_ptr<ITimeSystem> time,
                     std::unique_ptr<IResidentsSystem> residents,
                     std::unique_ptr<IProductionSystem> production,
                     std::unique_ptr<ILaborSystem> labor,
                     std::unique_ptr<IConstructionSystem> construction,
                     std::unique_ptr<ILogisticsSystem> logistics,
                     std::shared_ptr<const RoadTools> road_tools,
                     std::vector<JunctionView> junctions,
                     std::uint16_t alarm_red_within_days)
      : road_tools_(std::move(road_tools)),
        junctions_(std::move(junctions)),
        alarm_red_within_days_(alarm_red_within_days),
        time_(std::move(time)),
        residents_(std::move(residents)),
        production_(std::move(production)),
        labor_(std::move(labor)),
        construction_(std::move(construction)),
        logistics_(std::move(logistics)),
        decisions_slot_(*labor_, *logistics_, *residents_, *production_, *construction_),
        events_slot_(FoodValuePerResource(*config.tables),
                     ReadReadinessCatalog(*config.tables, Epoch::kOne),
                     ReadFoodVarietyThreshold(*config.tables, Epoch::kOne),
                     ReadLifeSpeedup(*config.tables),
                     AlarmSources{.labor = labor_.get(),
                                  .residents = residents_.get(),
                                  .production = production_.get(),
                                  .construction = construction_.get(),
                                  .logistics = logistics_.get(),
                                  .red_within_days = alarm_red_within_days}) {
    const StepPhaseSet phases{
        .time_and_weather = &time_->TimeAndWeatherPhase(),
        .needs = &residents_->NeedsPhase(),
        .decisions = &decisions_slot_,
        .production = &production_->ProductionPhase(),
        .metrics = &residents_->MetricsPhase(),
        .events = &events_slot_,
    };
    // The world arrives BUILT. Genesis moved out to the factory below on
    // 2026-09-06 so that its one refusal — a start layout whose row or
    // column cannot be read — has somewhere to go: a constructor's only
    // ways out are an exception and a half-built object, and both are worse
    // than the nullptr this module already returns for a refusing factory.
    engine_ = CreateStepEngine(std::move(start), phases, config.worker_count);
  }

  void AdvanceStep() override { engine_->AdvanceStep(); }

  void StageOrders(std::span<const OrderRow> issued, std::span<const OrderId> cancelled) override {
    engine_->StageOrders(issued, cancelled);
  }

  const WorldState& CompletedState() const override { return engine_->CompletedState(); }

  void ResetWorld(const WorldState& initial) override { engine_->ResetWorld(initial); }

  /// The fan-out of task A3: every subsystem that owns alarms answers over
  /// the completed state with its own configuration, in the FIXED order of
  /// the decisions slot (manual/54-modules.md §3) — the same order for the
  /// same reason, so that the list a caller gets is a function of the state
  /// and nothing else. Labor owns none yet; when assignments arrive (task
  /// A7, the yard without a stableman) it takes its place first, here.
  bool CanBeOrdered(ResidentId resident) const override {
    return labor_->CanBeOrdered(engine_->CompletedState(), resident);
  }

  WorkforceCount Workforce() const override {
    return labor_->CountWorkforce(engine_->CompletedState());
  }

  std::optional<ResidentActivityState> ActivityOf(ResidentId resident) const override {
    return labor_->ActivityOf(engine_->CompletedState(), resident);
  }

  /// ONE CROSSING, AND THE ASSEMBLY CARRIES IT. The food light needs the
  /// harvest date, which core_production owns, and core_residents may not
  /// reach into another module for it (CLAUDE.md §7). So the assembly — the
  /// one place allowed to see everybody — reads the date from its owner and
  /// hands it to the other.
  ///
  /// THERE WERE TWO. The seed light used to want the eating rate from
  /// core_residents, because it forecast "days until the seed fund is eaten
  /// away" — and that number does not exist: seed is spent all at once, on
  /// the sowing (boss, 2026-09-04). With the right unit — coverage of the
  /// campaign — the second crossing was not needed at all. A seam that a
  /// wrong unit made necessary is not a seam; it is the wrong unit.
  ///
  /// The ORDER is the fan-out order of the decisions slot, like alarms; the
  /// session sorts what comes back, so nothing here depends on it.
  void CollectStockForecast(std::vector<StockForecast>& lights) const override {
    const WorldState& completed = engine_->CompletedState();
    residents_->CollectStockForecast(completed, production_->DaysToNextHarvest(completed), lights);
    production_->CollectStockForecast(completed, lights);
  }

  Deadline WearDeadline(UnitId unit) const override {
    return construction_->WearDeadline(engine_->CompletedState(), unit);
  }

  DeliveryTerm LimitDeliveryTerm() const override {
    return production_->LimitDeliveryTerm(engine_->CompletedState());
  }

  std::vector<WorkbookLine> OfficeWorkbook() const override {
    return labor_->OfficeWorkbook(engine_->CompletedState());
  }

  bool FellingCanBeManned(Vec2 place) const override {
    return labor_->FellingCanBeManned(engine_->CompletedState(), place);
  }

  PlanBook OfficePlan() const override {
    return production_->OfficePlan(engine_->CompletedState());
  }

  LimitBook OfficeLimit() const override {
    return production_->OfficeLimit(engine_->CompletedState());
  }

  std::vector<IssueNormLine> IssueNorms() const override {
    return residents_->IssueNorms(engine_->CompletedState());
  }

  std::optional<HarvestNeed> NeedUntilHarvest(ResourceId resource) const override {
    return residents_->NeedUntilHarvest(engine_->CompletedState(), resource);
  }

  /// The era's catalog the transition order reads, and its rule
  /// (era_readiness.h, BuildEraReadinessView).
  EraReadinessView EraReadiness() const override {
    return BuildEraReadinessView(events_slot_.Readiness(), engine_->CompletedState());
  }

  ElderView Elder() const override { return ElderViewOf(engine_->CompletedState()); }

  std::vector<BarterYardLine> BarterDryLines() const override {
    return residents_->BarterDryLines(engine_->CompletedState());
  }

  std::vector<MaterialShortfall> MaterialsShortFor(UnitId unit) const override {
    return construction_->MaterialsShortFor(engine_->CompletedState(), unit);
  }

  StinkStrength StinkFullAt(Vec2 point) const override {
    return construction_->StinkFullAt(engine_->CompletedState(), point);
  }

  StinkStrength StinkNowAt(Vec2 point) const override {
    return construction_->StinkNowAt(engine_->CompletedState(), point);
  }

  float ResidentHeightMeters(ResidentId resident) const override {
    return residents_->HeightMeters(engine_->CompletedState(), resident);
  }

  /// Tomorrow first. The weather is a pure function of (seed, day), so the
  /// days ahead are evaluated exactly as the days behind would be — nothing
  /// is remembered and nothing is cached.
  void CollectWeatherForecast(std::span<DayForecast> into) const override {
    const WorldState& completed = engine_->CompletedState();
    for (std::size_t ahead = 0; ahead < into.size(); ++ahead) {
      into[ahead] = time_->WeatherOn(completed.world_seed,
                                     completed.calendar.day + static_cast<SimDay>(ahead) + 1U);
    }
  }

  // THE ROAD TOOLS (delivery 7; road_tools.h): the preview traces since 7b,
  // a path and a dirt road are laid since 7c and their tools open. STUB,
  // named, until their parts land: a selection is no piece (7d), and the
  // paved and demolition tools stand kNotYetBuilt (7d, 7e). Roads() reads
  // the network as it stands, with no work on any road.
  RoadDraftResult PreviewRoad(const RoadDraft& draft) const override {
    return road_tools_->Trace(engine_->CompletedState(), draft);
  }

  RoadPieces SelectRoadPieces(const RoadSelection& selection,
                              RoadOperation operation) const override {
    return road_tools_->Select(engine_->CompletedState(), selection, operation);
  }

  RoadToolStates RoadKindsAvailable() const override {
    return road_tools_->ToolStates(engine_->CompletedState());
  }

  std::vector<RoadView> Roads() const override {
    return RoadViews(engine_->CompletedState().roads);
  }

  /// Read once at assembly (core_catalog/world_junctions.h): the tables'
  /// junctions do not change with the world.
  std::vector<JunctionView> Junctions() const override { return junctions_; }

  void CollectAlarms(std::vector<Alarm>& alarms) const override {
    // The world's one collection, coloured (CollectWorldAlarms): the same the
    // daily red check reads inside the step.
    CollectWorldAlarms(AlarmSources{.labor = labor_.get(),
                                    .residents = residents_.get(),
                                    .production = production_.get(),
                                    .construction = construction_.get(),
                                    .logistics = logistics_.get(),
                                    .red_within_days = alarm_red_within_days_},
                       engine_->CompletedState(),
                       alarms);
  }

 private:
  /// Declared first so the constructor's list can fill it first. Shared
  /// with construction's tracer (the factory below).
  std::shared_ptr<const RoadTools> road_tools_;

  /// The map's junctions with the world beyond it (0.37.27; layers §15а).
  std::vector<JunctionView> junctions_;

  /// world_params `alarm_red_within_days`: a loss this near or nearer is a
  /// red lamp, a farther one a yellow (PaintAlarms).
  std::uint16_t alarm_red_within_days_ = 0;

  std::unique_ptr<ITimeSystem> time_;

  std::unique_ptr<IResidentsSystem> residents_;

  std::unique_ptr<IProductionSystem> production_;

  std::unique_ptr<ILaborSystem> labor_;

  std::unique_ptr<IConstructionSystem> construction_;

  std::unique_ptr<ILogisticsSystem> logistics_;

  DecisionsSlot decisions_slot_;

  EventsSlot events_slot_;

  std::unique_ptr<ISimulation> engine_;
};

}  // namespace

namespace {

/// The seven names a campaign table may use, in Weekday order.
///
/// EVERY CELL HAS A NAME, and that is a different question from "are there
/// as many cells" (host, 2026-09-04). The array takes its length from
/// kDaysPerWeek, so deleting a name from the middle would leave the length
/// intact, the cell holding "", and the build green — and "" is what an
/// empty table cell reads as, so a blank day_zero_weekday would parse as a
/// day. A terminator guards the growth of a set; this guards its holes.
/// world_params' key of the lamp's red (PaintAlarms), read by the assembly.
constexpr std::string_view kAlarmRedWithinDaysKey = "alarm_red_within_days";

constexpr std::array<std::string_view, kDaysPerWeek> kWeekdayNames = {
    "monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"};

static_assert(
    [] {
      for (const std::string_view name : kWeekdayNames) {
        if (name.empty()) {
          return false;
        }
      }
      return true;
    }(),
    "every weekday carries a name: a blank cell must not parse as a day");

/// @brief Weekday by its lowercase English name; `valid` reports success.
Weekday ParseWeekdayNameInternal(std::string_view name, bool& valid) {
  for (std::uint32_t index = 0; index < kWeekdayNames.size(); ++index) {
    if (kWeekdayNames[index] == name) {
      valid = true;
      return static_cast<Weekday>(index);
    }
  }
  valid = false;
  return Weekday::kMonday;
}

}  // namespace

// Campaign-table readers, shared with genesis.cpp (campaign_tables.h).

Weekday CampaignDayZeroWeekday(const ITableSet& tables) {
  // No campaign table is legal while the setup grows (STUB default: Monday);
  // a present but unreadable value is logged and falls back — genesis
  // returns a value and has no error channel.
  const ITable* campaign = tables.FindTable("campaign");
  if (campaign == nullptr) {
    return Weekday::kMonday;
  }
  const std::uint32_t row = campaign->FindRowByKey("day_zero_weekday");
  const std::uint32_t value_column = campaign->FindColumn("value");
  if (row == kNoTableRow || value_column == kNoTableColumn) {
    LogError("campaign: no day_zero_weekday row or value column; using monday");
    return Weekday::kMonday;
  }
  bool valid = false;
  const Weekday weekday = ParseWeekdayNameInternal(campaign->CellText(row, value_column), valid);
  if (!valid) {
    LogError("campaign: day_zero_weekday is not a weekday name; using monday");
    return Weekday::kMonday;
  }
  return weekday;
}

float CampaignValue(const ITableSet& tables, std::string_view key, float fallback) {
  const ITable* campaign = tables.FindTable("campaign");
  if (campaign == nullptr) {
    return fallback;
  }
  const std::uint32_t row = campaign->FindRowByKey(key);
  const std::uint32_t value_column = campaign->FindColumn("value");
  if (row == kNoTableRow || value_column == kNoTableColumn) {
    return fallback;
  }
  const std::optional<float> value = campaign->CellReal(row, value_column);
  if (!value) {
    LogError("campaign: value of '" + std::string(key) + "' is not a number");
    return fallback;
  }
  return *value;
}

std::unique_ptr<ISimulation> CreateStandardSimulation(const StandardSimulationConfig& config) {
  assert(config.tables != nullptr);
  // THE ASSEMBLER'S OWN READ SET, refused before any subsystem is built
  // (core_tables/required_tables.h). Genesis and the speedup knob read these
  // after the five factories have each passed their own list, so without
  // this line a set missing start_stock produced a village with empty barns
  // and a run that converged on it.
  if (!RequireTables(*config.tables,
                     config.stub_tables,
                     "world",
                     {"campaign",
                      "construction",
                      "crops",
                      "difficulty",
                      "life",
                      "livestock",
                      "map_areas",
                      "map_lines",
                      "map_places",
                      "resources",
                      "roads",
                      "road_wear",
                      "start_layout",
                      "start_stock",
                      "unit_types",
                      "world_junctions",
                      "world_params"},
                     nullptr)) {
    return nullptr;
  }
  auto time = CreateTimeSystem(*config.tables, config.stub_tables);
  // The growing season's last day travels from the clock to the fields: it is
  // derived from the seasonal curve, which is core_time's, and production
  // refuses a sowing that cannot ripen before it. The same day goes to labor,
  // whose queue ranks a late reaping toward it (2026-09-18): one edge, not one
  // for the fields and another for the queue.
  const std::uint32_t season_last_day =
      time == nullptr ? kDaysPerYear - 1U : time->GrowingSeasonLastDay();
  // The rain days travel the same road and for the same reason: what a rain
  // day is belongs to core_time's generator, and both the gathering alarm and
  // labor's last days before the snow discount the days ahead by it — one
  // count, handed to both (core_common/rain_stops_work.h).
  const RainDayShares rain_days = time == nullptr ? RainDayShares{} : time->ClimateRainDayShares();
  // AND THE FIRST SNOW'S SHARES, for the gathering count's early edge and the
  // queue's (the harvest rule 5; core_common/early_snow.h): each module reads
  // the edge off them by the one key, farming.csv `early_snow_share`.
  const SnowLainShares snow_lain =
      time == nullptr ? SnowLainShares{} : time->ClimateSnowLainShares();
  auto production = CreateProductionSystem(
      *config.tables, config.stub_tables, season_last_day, rain_days, snow_lain);
  // THE GRAMS OF A STANDING CROP ARE PRODUCTION'S to count, and labor's last
  // days before the snow order the reaping by them (boss seq 95): labor is
  // handed the one estimate, not a copy of its formula. The world owns both
  // modules for as long as either runs, so the raw pointer outlives no one
  // WHILE ANYTHING RUNS. At teardown it does, for residents' copy below: the
  // members die in reverse declaration order and residents_ is declared
  // before production_, so its std::function holds a dangling pointer for the
  // span of the destruction. Harmless today — no destructor calls it — and
  // named so that none starts to (the static loop of 23 September).
  const IProductionSystem* const estimate = production.get();
  auto labor = CreateLaborSystem(
      *config.tables,
      config.stub_tables,
      season_last_day,
      [estimate](const WorldState& world, const FieldRow& field) -> Grams {
        return estimate == nullptr ? 0 : estimate->StandingCropGrams(world, field);
      },
      rain_days,
      // And whether the stores hold the hay the herds need ahead (0.37.132):
      // the herds and the feed table are production's, the hay cart's rank
      // labor's.
      [estimate](const WorldState& world, std::uint32_t days) {
        return estimate != nullptr && estimate->StoredHayShortWithin(world, days);
      },
      snow_lain);
  // THE FODDER FUND IS PRODUCTION'S to size, and the people's issue must stay
  // below rung 3 of the ladder, which holds it (core_common/fund_ladder.h;
  // boss seq 14 and 17): residents is handed the one size by the same road
  // labor is handed the grams of a crop, and is therefore built after it.
  // AND THE HOLD FOR NEXT YEAR by the same road (0.37.2; boss-core-epoch1-
  // queue [60], (г)): the herds and the issue stay above one hold.
  auto residents = CreateResidentsSystem(
      *config.tables,
      config.stub_tables,
      [estimate](const WorldState& world) {
        return estimate == nullptr ? ResourceAmounts{} : estimate->FodderFund(world);
      },
      [estimate](const WorldState& world) {
        return estimate == nullptr ? ResourceAmounts{} : estimate->NextYearHold(world);
      },
      // AND THE DAYS TO A POSITION'S HARVEST (0.37.29; labor-payment §7):
      // the default issue norm shares a remainder to that day, and the
      // farming calendar is production's.
      [estimate](const WorldState& world, ResourceId resource) -> std::int32_t {
        return estimate == nullptr ? -1 : estimate->DaysToHarvestOf(world, resource);
      },
      // AND WHAT THE TURN WILL SEAL of next year's plan (0.37.36; labor-
      // payment §7, «Что делится»): a norm whose horizon crosses the turn
      // does not share out what the district is already promised.
      [estimate](const WorldState& world) {
        return estimate == nullptr ? ResourceAmounts{} : estimate->TurnPlanSeal(world);
      },
      // AND THE HARVEST STILL TO COME THIS YEAR (0.37.38; labor-payment §7):
      // before it the plan rung holds of the carry-over only what it will
      // not pay, and the fields' forecast is production's.
      [estimate](const WorldState& world) {
        return estimate == nullptr ? ResourceAmounts{} : estimate->HarvestToComeThisYear(world);
      });
  // THE ROAD TOOLS (delivery 7b, 7c): the map's obstacles, the road levels'
  // prices, the plot radii — one object, and construction is handed its
  // tracer by the same road labor is handed the grams of a crop, so the
  // preview and the order that lays a road trace alike. A malformed table
  // refuses the assembly.
  std::string road_tools_error;
  std::optional<RoadTools> read_tools = RoadTools::Read(*config.tables, road_tools_error);
  if (!read_tools) {
    LogError(road_tools_error);
    return nullptr;
  }
  const auto road_tools = std::make_shared<const RoadTools>(std::move(*read_tools));
  auto construction = CreateConstructionSystem(
      *config.tables,
      config.stub_tables,
      RoadToolDoors{
          .trace = [road_tools](const WorldState& world,
                                const RoadDraft& draft) { return road_tools->Trace(world, draft); },
          .select =
              [road_tools](const WorldState& world,
                           const RoadSelection& selection,
                           RoadOperation operation) {
                return road_tools->Select(world, selection, operation);
              },
      });
  // The groom's logistics (routing stage B, B2): the tasks of carting and
  // their levels, run after the labour sub-step.
  auto logistics = CreateLogisticsSystem(*config.tables, config.stub_tables);
  if (!time || !residents || !production || !labor || !construction || !logistics) {
    // A factory refused its configuration (it already logged why).
    return nullptr;
  }
  // THE TABLE'S DECLARED READERS ARE JUDGED HERE, and here only, because
  // this is the one place that knows every module. The check used to live
  // inside core_time, which reads world_params.csv for the leaf fall — and
  // on 2026-09-06 genesis began reading it for the figure of a person, so a
  // key honestly declared `core` by one module was refused by another that
  // had simply never heard of it. A module cannot judge the core: it is not
  // the core.
  //
  // Each list comes from the module that reads it, out of the same array the
  // reader indexes, so no list can age away from its code.
  std::uint16_t alarm_red_within_days = 0;
  {
    const ITable* const world_params = config.tables->FindTable("world_params");
    if (world_params != nullptr) {
      std::vector<std::string_view> known;
      const std::span<const std::string_view> from_time = TimeWorldParamKeys();
      const std::span<const std::string_view> from_genesis = GenesisWorldParamKeys();
      const std::span<const std::string_view> from_life = LifeWorldParamKeys();
      const std::span<const std::string_view> from_timber = TimberWorldParamKeys();
      known.insert(known.end(), from_timber.begin(), from_timber.end());
      const std::span<const std::string_view> from_processing = ProcessingWorldParamKeys();
      known.insert(known.end(), from_processing.begin(), from_processing.end());
      const std::span<const std::string_view> from_extraction = ExtractionWorldParamKeys();
      known.insert(known.end(), from_extraction.begin(), from_extraction.end());
      const std::span<const std::string_view> from_construction = ConstructionWorldParamKeys();
      known.insert(known.end(), from_construction.begin(), from_construction.end());
      const std::span<const std::string_view> from_limit = LimitWorldParamKeys();
      known.insert(known.end(), from_limit.begin(), from_limit.end());
      const std::span<const std::string_view> from_visits = DistrictVisitWorldParamKeys();
      known.insert(known.end(), from_visits.begin(), from_visits.end());
      const std::span<const std::string_view> from_trip = DistrictTripWorldParamKeys();
      known.insert(known.end(), from_trip.begin(), from_trip.end());
      const std::span<const std::string_view> from_production = ProductionWorldParamKeys();
      known.insert(known.end(), from_production.begin(), from_production.end());
      known.insert(known.end(), from_time.begin(), from_time.end());
      known.insert(known.end(), from_genesis.begin(), from_genesis.end());
      known.insert(known.end(), from_life.begin(), from_life.end());
      const std::span<const std::string_view> from_conventions = ConventionWorldParamKeys();
      known.insert(known.end(), from_conventions.begin(), from_conventions.end());
      const std::span<const std::string_view> from_roads = RoadWorldParamKeys();
      known.insert(known.end(), from_roads.begin(), from_roads.end());
      // The road tools' own (road_tools.h): the clearing's man-days a hectare.
      const std::span<const std::string_view> from_road_tools = RoadToolWorldParamKeys();
      known.insert(known.end(), from_road_tools.begin(), from_road_tools.end());
      // The founders' schooling (start_literacy.h; register 301).
      const std::span<const std::string_view> from_literacy = StartLiteracyWorldParamKeys();
      known.insert(known.end(), from_literacy.begin(), from_literacy.end());
      // The former elder's age band (start_elder.h).
      const std::span<const std::string_view> from_elder = StartElderWorldParamKeys();
      known.insert(known.end(), from_elder.begin(), from_elder.end());
      // The ice's two fulls: read by the month's ice door (month_ice.h), not
      // by the simulation — the door is the core's all the same.
      const std::span<const std::string_view> from_ice = IceWorldParamKeys();
      known.insert(known.end(), from_ice.begin(), from_ice.end());
      // DECLARED FOR THE CORE, READ BY NOBODY YET — each with the door that
      // will read it. Named here so the export that carries the row does not
      // stop the assembly, and so the unread knob is a line somebody sees
      // rather than an absence (boss, parcel 277).
      //   (`road_access_m` stood here from 2026-09-17 until 0.36.9, when the
      //   produce cart's book became its first reader — core_production
      //   declares it now. The road access check of a site, unit rules §12,
      //   will be its second.)
      //   `player_entry_day` (2026-09-24, boss-core-epoch1-4 seq 12 and 21) —
      //   the day of the year the player enters, 12 (1 April); the campaign
      //   still starts on 1 January. ITS DOOR IS THE STANDING WORK ORDERS:
      //   before that day they open no ploughing, sowing or other field work
      //   (start conditions §9). Waits for host's measure of whether the
      //   village ploughs and sows in the days left.
      known.emplace_back("player_entry_day");
      //   `player_entry_hour` (2026-10-01, boss-core-start-no-yards [11];
      //   boss's 07aa3820) — the hour of the entry morning, 8. The layer
      //   fast-forwards the core to TickOfDayHour(player_entry_day, this)
      //   through AdvanceUntil; the core's entry gate will read the day and
      //   this hour together. Declared before its export arrives, so the
      //   export does not stop the assembly.
      known.emplace_back("player_entry_hour");
      //   (`alarm_red_within_days` stood here from 0.37.62 to 0.37.194,
      //   declared ahead of its reader; the assembly reads it below since the
      //   lamp colour's delivery.)
      known.emplace_back(kAlarmRedWithinDaysKey);
      //   (`billet_household_milk_share` stood here from 0.37.65 to 0.37.69,
      //   declared a delivery ahead of the billet milk's reader. The human
      //   cancelled the billet's new rules on 2026-10-01 — «Постой отменяем»
      //   — the reader never came, and the base dropped the key; a key the
      //   tables no longer carry needs no declaration.)
      //   (The cold ladder's five `livestock_cold_*` stood here in 0.37.60,
      //   the contract a delivery ahead of its reader; core_production
      //   declares them since 0.37.62, when the herd day's ladder read them.)
      std::string trouble;
      if (!CheckDeclaredReaders(*world_params, "world_params", known, trouble)) {
        LogError(trouble);
        return nullptr;
      }
      // THE LAMP'S RED (office design §13, «Цвет — по сроку потери»; the
      // human's «Да», 1 October 2026): required — a lamp with no colour rule
      // is not a lamp the layer can draw. A year at most.
      float red_within = 0.0F;
      if (!RequiredValue(*world_params,
                         "world_params",
                         kAlarmRedWithinDaysKey,
                         Range{.low = 0.0F, .high = static_cast<float>(kDaysPerYear)},
                         red_within,
                         trouble)) {
        LogError(trouble);
        return nullptr;
      }
      alarm_red_within_days = static_cast<std::uint16_t>(red_within);
    }
    // The base conventions against the build, and the biology factor's two
    // homes against each other (core_catalog/world_conventions.h) — here,
    // because the readers of the factor that cannot report fall back quietly.
    std::string disagreement;
    if (!CheckWorldConventions(*config.tables, disagreement)) {
      LogError(disagreement);
      return nullptr;
    }
    // The roads' numbers (delivery 3 of the roads work): refused here when a
    // row is out of its range. THEIR READERS ARRIVE WITH THE DELIVERY — the
    // bed's condition, the traffic counter, wear and overgrowth; until then
    // the rows are checked and not yet read.
    RoadRules road_rules;
    if (!ParseRoadRules(*config.tables, road_rules, disagreement)) {
      LogError(disagreement);
      return nullptr;
    }
  }

  // And the start layout is refused on the same terms as a subsystem's
  // table: the parser has already logged the row and the column.
  std::string layout_error;
  WorldState start = CreateStartWorld(
      *config.tables, config.stub_tables, construction.get(), config.world_seed, &layout_error);
  if (!layout_error.empty()) {
    return nullptr;
  }
  // THE START'S DERIVED FAMILY METRICS, before the first tick (0.37.23;
  // boss-core-layers-doors-2026-09-29 [4]): genesis draws the components,
  // and the satisfaction they make read the struct's 55 until the first day.
  residents->SettleStartMetrics(start);
  // AND THE START'S YEAR (0.37.78): the first year's limit points, which the
  // first step used to bring.
  production->SettleStartYear(start);
  // THE MAP'S JUNCTIONS (0.37.27; layers design §15а, register 300): refused
  // on the same terms as the layout — the reader names the row.
  std::vector<JunctionView> junctions;
  std::string junctions_error;
  if (!ReadWorldJunctions(*config.tables, junctions, junctions_error)) {
    LogError(junctions_error);
    return nullptr;
  }
  return std::make_unique<StandardSimulation>(config,
                                              std::move(start),
                                              std::move(time),
                                              std::move(residents),
                                              std::move(production),
                                              std::move(labor),
                                              std::move(construction),
                                              std::move(logistics),
                                              road_tools,
                                              std::move(junctions),
                                              alarm_red_within_days);
}

}  // namespace core
