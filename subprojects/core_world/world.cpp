// Implementation of the core_world boundary (include/core_world/world.h):
// world genesis (task O0) and the wired standard simulation (task O2) — the
// subsystems, the two composite sequential slots and the step engine.

#include "core_world/world.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "campaign_tables.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/ids.h"
#include "core_common/ledger_state.h"
#include "core_common/order_state.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_construction/construction_system.h"
#include "core_labor/labor_system.h"
#include "core_log/log.h"
#include "core_production/production_system.h"
#include "core_residents/residents_system.h"
#include "core_tables/tables.h"
#include "core_time/time_system.h"

namespace core {

namespace {

/// The composite decisions slot (phase 3): the subsystems' sequential
/// sub-steps in the fixed order of manual/54-modules.md §3 — assignments,
/// then demography, then production decisions. The order is part of the
/// design contract, not a wiring accident.
class DecisionsSlot final : public ISequentialPhase {
 public:
  DecisionsSlot(ILaborSystem& labor,
                IResidentsSystem& residents,
                IProductionSystem& production,
                IConstructionSystem& construction)
      : labor_(&labor),
        residents_(&residents),
        production_(&production),
        construction_(&construction) {}

  void RunSequential(const WorldState& previous, WorldState& current) override {
    labor_->RunAssignmentDecisions(previous, current);
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

  IResidentsSystem* residents_;

  IProductionSystem* production_;

  IConstructionSystem* construction_;
};

/// @brief Caloric density per resource, dense by ResourceId.
/// A resource without the column, or with a blank cell, reads zero — which
/// is the right answer for hay and straw and the honest one for anything
/// the design has not priced yet.
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
    density[row] = cell && *cell > 0.0F ? *cell : 0.0F;
  }
  return density;
}

/// The events slot (phase 7). The event system itself is still a STUB — it
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
class EventsSlot final : public ISequentialPhase {
 public:
  explicit EventsSlot(std::vector<float> kcal_per_gram)
      : kcal_per_gram_(std::move(kcal_per_gram)) {}

  void RunSequential(const WorldState& previous, WorldState& current) override {
    FoldPantryFlows(previous, current);
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
      for (std::size_t index = 0; index < width && index < kInvalidDefIdValue; ++index) {
        const ResourceId resource{static_cast<std::uint16_t>(index)};
        const Grams delta = AmountAt(now, index) - AmountAt(was, index);
        AddLedgerAmount(
            plot_hour ? book.plot_harvest : book.eaten, resource, plot_hour ? delta : -delta);
      }
    }
  }

  /// The year's book closes on the FIRST tick of the new calendar year,
  /// after its phases 1-6 have run — so the turn's own bookkeeping (the
  /// trudodni burn, the plan delivery, the life-expectancy recompute), all
  /// of which happens at hour 0, lands in the year it settles rather than
  /// in the one that just began. The price, stated in ledger_state.h so
  /// nobody hunts for it: day 0's demography is booked to the year before.
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
    // Area-weighted over the arable, meadows excluded — the reasoning is on
    // the field itself, and it is a decision rather than an average.
    float weighted = 0.0F;
    float area = 0.0F;
    for (const FieldRow& field : current.fields.rows) {
      if (field.kind != LandKind::kArable) {
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
    row.harvest_kcal = static_cast<std::int64_t>(kcal);
    current.ledger.chronicle.push_back(row);
  }

  /// Caloric density by ResourceId, from tables/resources.csv. Read once at
  /// wiring: the chronicle needs it every year and a table cannot change
  /// under a running campaign.
  std::vector<float> kcal_per_gram_;

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
    AppendChronicleYear(current);
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
                     std::unique_ptr<ITimeSystem> time,
                     std::unique_ptr<IResidentsSystem> residents,
                     std::unique_ptr<IProductionSystem> production,
                     std::unique_ptr<ILaborSystem> labor,
                     std::unique_ptr<IConstructionSystem> construction)
      : time_(std::move(time)),
        residents_(std::move(residents)),
        production_(std::move(production)),
        labor_(std::move(labor)),
        construction_(std::move(construction)),
        decisions_slot_(*labor_, *residents_, *production_, *construction_),
        events_slot_(FoodValuePerResource(*config.tables)) {
    const StepPhaseSet phases{
        .time_and_weather = &time_->TimeAndWeatherPhase(),
        .needs = &residents_->NeedsPhase(),
        .decisions = &decisions_slot_,
        .production = &production_->ProductionPhase(),
        .metrics = &residents_->MetricsPhase(),
        .events = &events_slot_,
    };
    engine_ = CreateStepEngine(
        CreateStartWorld(*config.tables, config.world_seed), phases, config.worker_count);
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

  void CollectAlarms(std::vector<Alarm>& alarms) const override {
    const WorldState& completed = engine_->CompletedState();
    labor_->CollectAlarms(completed, alarms);
    residents_->CollectAlarms(completed, alarms);
    production_->CollectAlarms(completed, alarms);
    construction_->CollectAlarms(completed, alarms);
  }

 private:
  std::unique_ptr<ITimeSystem> time_;

  std::unique_ptr<IResidentsSystem> residents_;

  std::unique_ptr<IProductionSystem> production_;

  std::unique_ptr<ILaborSystem> labor_;

  std::unique_ptr<IConstructionSystem> construction_;

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

float LifeSpeedupFromTables(const ITableSet& tables) {
  const ITable* life = tables.FindTable("life");
  if (life == nullptr) {
    return 4.0F;
  }
  const std::uint32_t row = life->FindRowByKey("life_speedup");
  const std::uint32_t value_column = life->FindColumn("value");
  if (row == kNoTableRow || value_column == kNoTableColumn) {
    return 4.0F;
  }
  return life->CellReal(row, value_column).value_or(4.0F);
}

std::unique_ptr<ISimulation> CreateStandardSimulation(const StandardSimulationConfig& config) {
  assert(config.tables != nullptr);
  auto time = CreateTimeSystem(*config.tables);
  auto residents = CreateResidentsSystem(*config.tables);
  auto production = CreateProductionSystem(*config.tables);
  auto labor = CreateLaborSystem(*config.tables);
  auto construction = CreateConstructionSystem(*config.tables);
  if (!time || !residents || !production || !labor || !construction) {
    // A factory refused its configuration (it already logged why).
    return nullptr;
  }
  return std::make_unique<StandardSimulation>(config,
                                              std::move(time),
                                              std::move(residents),
                                              std::move(production),
                                              std::move(labor),
                                              std::move(construction));
}

}  // namespace core
