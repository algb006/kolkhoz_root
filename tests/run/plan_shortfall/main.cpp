// Simulation run: WHEN THE PLAN IS FAILED, WHAT WAS ACTUALLY MISSING?
//
// Boss's order of 2026-09-13, and the order matters as much as the question:
// "не «проверь склад». А вот что: в момент КАЖДОГО срыва плана напечатай, чего
// не хватило — числом и в натуре."
//
// THE POINT IS THAT NOBODY NAMES A LEVER FIRST. Three hypotheses were put to
// the runs in one morning — the land, the store, the carting — and all three
// were wrong, in the same way each time: a lever was named and the instrument
// was asked to confirm it. An instrument asked "чего не хватило" cannot be led
// by a hypothesis. It will say what it says, and if that turns out to be the
// weather and nothing else, then there is no lever and that is an answer too.
//
// WHAT IT PRINTS, at the close of every failed year, for the layout under
// question and for the canonical one beside it:
//
//   THE DEBT     per resource: asked, delivered, short — in kilograms
//   THE GRAIN    what the settlement holds when the district comes
//   THE FIELDS   tonnes still lying unfetched on the fields that day
//   THE ROOM     free store room, because "nowhere to put it" is a shortfall
//   THE HANDS    game man-days by kind over the year
//   THE TEAM     horses, which is what the spring is actually bounded by
//   THE LAND     hectares sown and hectares reaped
//
// READ AT THE LAST DAY OF THE YEAR AND NOT AFTER. `PlanState::due` is cleared
// at the turn, a few lines after the verdict is struck, so a reader that waits
// for the verdict finds the debt already gone — the same trap the ledger row
// fell into, where every plan_due cell was structurally zero because the row
// was written at the turn.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "../common/fixture_policy.h"
#include "../common/repair_policy.h"
#include "../common/run_harness.h"
#include "../common/sowing_policy.h"
#include "../common/yard_policy.h"
#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/unit_state.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {

constexpr std::uint32_t kYears = 20;

/// The obvious chairman's agronomy, as in plan_trial: the oat gap and the last
/// day a standing crop is safe from the snow.
constexpr std::int32_t kRipenDays = 13;
constexpr std::uint32_t kSeasonLastDay = 42;

/// Everything the settlement holds of one resource, across every store.
core::Grams VillageStock(const core::WorldState& world, core::ResourceId resource) {
  core::Grams held = 0;
  for (const core::UnitRow& unit : world.units.rows) {
    if (resource.value < unit.stock.size()) {
      held += unit.stock[resource.value];
    }
  }
  return held;
}

/// What is still lying on the fields, unfetched, of every resource together.
core::Grams WaitingOnFields(const core::WorldState& world) {
  core::Grams waiting = 0;
  for (const core::FieldRow& field : world.fields.rows) {
    waiting += field.reaped_grams;
  }
  return waiting;
}

std::uint32_t Horses(const core::WorldState& world) {
  std::uint32_t horses = 0;
  for (const core::HerdRow& herd : world.herds.rows) {
    horses += herd.kind.value == 3 ? herd.adult_count : 0;
  }
  return horses;
}

double Tonnes(core::Grams grams) {
  return static_cast<double>(grams) / 1.0e6;
}

/// WHY THE CARTING DOES NOT GO, counted rather than reasoned about.
///
/// The work queue sorts by DAYS LEFT IN THE WINDOW, ascending
/// (core_labor/assignment.cpp). A load waiting on a field carries the days to
/// the YEAR'S END; a standing crop carries the days to the end of its own
/// harvest window. In the autumn the second is always the smaller, so reaping
/// outranks carting for as long as anything is left to reap — by construction,
/// not by accident. These numbers say what that costs.
struct Carting {
  std::uint32_t days_with_load_waiting = 0;  ///< days a load lay on a field
  std::uint32_t days_nobody_carted = 0;      ///< ...and not one man was carting
  std::uint32_t days_reaping_instead = 0;    ///< ...and somebody was reaping
  core::Grams peak_waiting = 0;              ///< the worst the heap ever got
  std::uint32_t free_horses_when_idle = 0;   ///< horses out of harness on those days

  // WHY NOBODY CARTED, split into the three things that can stop it (boss,
  // parcel 131 §4: hands, horses, room, place in the queue — measured, not
  // guessed). The core offers a carting job only while the field's demand is
  // above zero, and the demand is the SMALLER of the load and the room the
  // stores can take (field_haul.cpp, SettleHauling): no room, no job at all.
  std::uint32_t days_no_room = 0;        ///< ...a load out, and no demand: the stores are full
  std::uint32_t days_nobody_worked = 0;  ///< ...demand, and not one person at any work: a day off
  std::uint32_t days_outranked = 0;      ///< ...demand, people at work, none carting
  /// On the outranked days: the most adults left with no work at all, and the
  /// most at each of the other kinds — whether the hands were short or busy.
  std::uint32_t idle_adults_when_outranked = 0;
  std::array<std::uint32_t, core::kWorkKindCount> busy_when_outranked{};
};

/// Adult age for "a hand": life.csv adult_age_years. The core's own test also
/// asks for a home, which every resident of the start has; mirrored here, not
/// shared, as a second tally by another road.
constexpr float kAdultAgeYears = 16.0F;

/// One day a load lay out and nobody carried it: which of the three stopped it.
void ClassifyNoCarting(const core::WorldState& world, float life_speedup, Carting& carting) {
  float demand = 0.0F;
  for (const core::FieldRow& field : world.fields.rows) {
    demand += field.reaped_grams > 0 ? field.haul_days_remaining : 0.0F;
  }
  if (!(demand > 0.0F)) {
    ++carting.days_no_room;
    return;
  }
  std::uint32_t idle_adults = 0;
  std::uint32_t working = 0;
  std::array<std::uint32_t, core::kWorkKindCount> busy{};
  for (const core::ResidentRow& resident : world.residents.rows) {
    const auto kind = static_cast<std::size_t>(resident.work.kind);
    if (resident.work.kind != core::WorkKind::kNone && kind < busy.size()) {
      ++busy[kind];
      ++working;
      continue;
    }
    const float age =
        core::BiologicalAgeYears(life_speedup, resident.birth_day, world.calendar.day);
    idle_adults += age >= kAdultAgeYears ? 1U : 0U;
  }
  if (working == 0) {
    ++carting.days_nobody_worked;
    return;
  }
  ++carting.days_outranked;
  carting.idle_adults_when_outranked = std::max(carting.idle_adults_when_outranked, idle_adults);
  for (std::size_t kind = 0; kind < busy.size(); ++kind) {
    carting.busy_when_outranked[kind] = std::max(carting.busy_when_outranked[kind], busy[kind]);
  }
}

/// WHAT THE GAME SAID BEFORE THE VERDICT (boss, 2026-09-13): "зажигается ли
/// хоть одна тревога или прогноз ДО года срыва?" A failure no signal foretold
/// is a trap; one foretold is the price of not listening. Counted over the
/// year that ends in the verdict, every day, from the core's own two outputs a
/// player sees: the standing alarms and the stock lights.
struct Signals {
  /// Days each alarm kind stood, any subject.
  std::array<std::uint32_t, static_cast<std::size_t>(core::AlarmKind::kAlarmKindCount)>
      alarm_days{};
  /// Days an alarm NAMING a given resource stood, per kind — "an alarm about
  /// oat" is not an answer until it says whether it spoke of the plan or of
  /// next spring's seed.
  std::vector<std::array<std::uint32_t, static_cast<std::size_t>(core::AlarmKind::kAlarmKindCount)>>
      alarm_days_by_resource;
  /// Days each light was yellow, and red.
  std::array<std::uint32_t, static_cast<std::size_t>(core::StockKind::kStockKindCount)>
      yellow_days{};
  std::array<std::uint32_t, static_cast<std::size_t>(core::StockKind::kStockKindCount)> red_days{};
  /// Last day of the year each light was not green; -1 when it never left green.
  std::array<std::int32_t, static_cast<std::size_t>(core::StockKind::kStockKindCount)>
      last_warning_day{};
};

/// An empty year of signals, sized to the resource table.
Signals FreshSignals(const core::ITable* resources) {
  Signals signals;
  signals.alarm_days_by_resource.resize(resources != nullptr ? resources->RowCount() : 0U);
  signals.last_warning_day.fill(-1);
  return signals;
}

void RecordSignals(const core::ISimulation& simulation, Signals& signals) {
  const std::uint32_t day_of_year = simulation.CompletedState().calendar.day % core::kDaysPerYear;
  std::vector<core::Alarm> alarms;
  simulation.CollectAlarms(alarms);
  std::array<bool, static_cast<std::size_t>(core::AlarmKind::kAlarmKindCount)> kind_seen{};
  std::vector<std::array<bool, static_cast<std::size_t>(core::AlarmKind::kAlarmKindCount)>>
      resource_seen(signals.alarm_days_by_resource.size());
  for (const core::Alarm& alarm : alarms) {
    const auto kind = static_cast<std::size_t>(alarm.kind);
    if (kind >= kind_seen.size()) {
      continue;
    }
    if (!kind_seen[kind]) {
      kind_seen[kind] = true;
      ++signals.alarm_days[kind];
    }
    if (alarm.resource.value < resource_seen.size() && !resource_seen[alarm.resource.value][kind]) {
      resource_seen[alarm.resource.value][kind] = true;
      ++signals.alarm_days_by_resource[alarm.resource.value][kind];
    }
  }
  std::vector<core::StockForecast> lights;
  simulation.CollectStockForecast(lights);
  for (const core::StockForecast& light : lights) {
    const auto kind = static_cast<std::size_t>(light.kind);
    if (kind >= signals.red_days.size()) {
      continue;
    }
    if (light.light == core::StockLight::kYellow || light.light == core::StockLight::kRed) {
      signals.last_warning_day[kind] = static_cast<std::int32_t>(day_of_year);
      ++(light.light == core::StockLight::kRed ? signals.red_days : signals.yellow_days)[kind];
    }
  }
}

/// One year, sampled on its LAST day — before the turn clears the debt.
struct YearEnd {
  std::uint32_t year = 0;
  core::ResourceAmounts due;
  core::ResourceAmounts delivered;
  core::Grams grain_in_store = 0;
  core::Grams waiting_on_fields = 0;
  /// PER POSITION, because a sum across the positions hid the answer on seed
  /// 1936: 19 to 51 t "in store" every year beside a potato delivery of zero.
  core::ResourceAmounts in_store_by_position;
  core::ResourceAmounts on_fields_by_position;
  /// And where each position WENT over the year, because an empty store at
  /// the year's end does not say whether nothing grew or everything left.
  core::YearLedger book;
  std::uint32_t horses = 0;
  float area_sown_ha = 0.0F;
  float area_harvested_ha = 0.0F;
  float area_lost_ha = 0.0F;
  std::array<float, core::kWorkKindCount> work_days{};

  // WHY THE CARTING DOES NOT GO, counted rather than reasoned about.
  //
  // The work queue sorts by DAYS LEFT IN THE WINDOW, ascending
  // (core_labor/assignment.cpp). A load waiting on a field carries the days to
  // the YEAR'S END; a standing crop carries the days to the end of its own
  // harvest window. In the autumn the second is always the smaller, so reaping
  // outranks carting for as long as anything is left to reap — by
  // construction, not by accident. These three numbers say what that costs.
  Carting carting;

  Signals signals;
};

void PrintShortfall(const YearEnd& sample, const core::ITable* resources) {
  std::cout << "plan_shortfall:   year " << sample.year << " THE DEBT —";
  bool any = false;
  for (std::uint32_t index = 0; index < sample.due.size(); ++index) {
    if (sample.due[index] <= 0) {
      continue;
    }
    const core::Grams got = index < sample.delivered.size() ? sample.delivered[index] : 0;
    const core::Grams shortfall = sample.due[index] > got ? sample.due[index] - got : 0;
    const std::string key =
        resources != nullptr && index < resources->RowCount()
            ? std::string(resources->CellText(index, resources->FindColumn("key")))
            : std::to_string(index);
    std::cout << ' ' << key << ' ' << std::fixed << std::setprecision(2) << Tonnes(got) << '/'
              << Tonnes(sample.due[index]) << " t";
    if (shortfall > 0) {
      std::cout << " (SHORT " << Tonnes(shortfall) << ")";
    }
    const core::Grams stored =
        index < sample.in_store_by_position.size() ? sample.in_store_by_position[index] : 0;
    const core::Grams lying =
        index < sample.on_fields_by_position.size() ? sample.on_fields_by_position[index] : 0;
    std::cout << " [store " << Tonnes(stored) << ", field " << Tonnes(lying) << "]";
    if (shortfall > 0) {
      const auto amount = [index](const core::ResourceAmounts& amounts) {
        return Tonnes(index < amounts.size() ? amounts[index] : 0);
      };
      const core::YearLedger& book = sample.book;
      std::cout << " {year: harvest " << amount(book.harvest) << ", issued " << amount(book.issued)
                << ", ration " << amount(book.ration) << ", eaten " << amount(book.eaten)
                << ", fed " << amount(book.feed) << ", seed " << amount(book.seed) << ", spoiled "
                << amount(book.spoiled) << ", lost " << amount(book.lost_no_room) << "}";
    }
    any = true;
  }
  if (!any) {
    std::cout << " nothing was asked";
  }
  std::cout << '\n';
  std::cout << "plan_shortfall:     in store " << Tonnes(sample.grain_in_store)
            << " t, waiting on the fields " << Tonnes(sample.waiting_on_fields) << " t, "
            << sample.horses << " horses; sown " << sample.area_sown_ha << " ha, reaped "
            << sample.area_harvested_ha << " ha, lost to snow " << sample.area_lost_ha << " ha\n";
  std::cout << "plan_shortfall:     man-days — plough " << sample.work_days[1] << ", harrow "
            << sample.work_days[2] << ", sow " << sample.work_days[3] << ", reap "
            << sample.work_days[4] << ", barn " << sample.work_days[5] << ", haul "
            << sample.work_days[static_cast<std::size_t>(core::WorkKind::kHauling)] << '\n';
  std::cout << "plan_shortfall:     THE CARTING — a load lay on a field on "
            << sample.carting.days_with_load_waiting << " days of the year; on "
            << sample.carting.days_nobody_carted << " of them NOT ONE MAN was carting, and on "
            << sample.carting.days_reaping_instead
            << " of those somebody was reaping instead; the heap peaked at "
            << Tonnes(sample.carting.peak_waiting) << " t, and up to "
            << sample.carting.free_horses_when_idle
            << " horses stood out of harness on the days nobody carted\n";
  static constexpr std::array<const char*, core::kWorkKindCount> kKindNames = {
      "none", "plough", "harrow", "sow", "reap", "barn", "build", "haul"};
  const Carting& why = sample.carting;
  std::cout << "plan_shortfall:     WHY NOBODY CARTED — no room in the stores " << why.days_no_room
            << " days, a day nobody worked " << why.days_nobody_worked
            << ", outranked by other work " << why.days_outranked << "; on those, up to "
            << why.idle_adults_when_outranked << " adults had no work at all, and up to:";
  for (std::size_t kind = 1; kind < kKindNames.size(); ++kind) {
    std::cout << ' ' << kKindNames[kind] << ' ' << why.busy_when_outranked[kind];
  }
  std::cout << '\n';
  const Signals& signals = sample.signals;
  std::cout << "plan_shortfall:     THE SIGNALS over the year — alarm days by kind:";
  bool any_alarm = false;
  for (std::size_t kind = 1; kind < signals.alarm_days.size(); ++kind) {
    if (signals.alarm_days[kind] > 0) {
      std::cout << " #" << kind << "=" << signals.alarm_days[kind];
      any_alarm = true;
    }
  }
  std::cout << (any_alarm ? "" : " none") << "; alarms naming a short position:";
  for (std::uint32_t index = 0; index < sample.due.size(); ++index) {
    const core::Grams got = index < sample.delivered.size() ? sample.delivered[index] : 0;
    if (sample.due[index] <= got) {
      continue;
    }
    std::cout << " resource " << index << " {";
    if (index < signals.alarm_days_by_resource.size()) {
      for (std::size_t kind = 1; kind < signals.alarm_days_by_resource[index].size(); ++kind) {
        if (signals.alarm_days_by_resource[index][kind] > 0) {
          std::cout << " #" << kind << "=" << signals.alarm_days_by_resource[index][kind];
        }
      }
    }
    std::cout << " }";
  }
  static constexpr std::array<const char*, 4> kLightNames = {"food", "feed", "firewood", "seed"};
  std::cout << "; lights (yellow/red days, last warning day):";
  for (std::size_t kind = 0; kind < kLightNames.size(); ++kind) {
    std::cout << ' ' << kLightNames[kind] << ' ' << signals.yellow_days[kind] << '/'
              << signals.red_days[kind] << " @" << signals.last_warning_day[kind];
  }
  std::cout << '\n';
}

/// Walks one layout and prints a line for every year the district was not paid.
/// @brief One day of every field standing in `crop`: its phase, the day it was
/// sown, the work left on it and who is on it doing what.
///
/// WHY IT EXISTS: on the layout the human approved on 2026-09-13 the obvious
/// chairman failed the plan in 70 years of 180 against 35 before, every
/// failure a potato, and on seed 1935 year 1's potato was dug to zero tonnes
/// against 230 on the old yards, with 28 ha under the snow. The year's totals
/// cannot say whether the crop never ripened inside its reaping window, ripened
/// and was never opened, or was opened and nobody came. A day-by-day line can.
void TraceCropDay(const core::WorldState& world, core::CropId crop) {
  for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
    const core::FieldRow& field = world.fields.rows[row];
    // THE LAST CROP TOO: a harvest that ends, or a snow that takes the stand,
    // clears `crop` the same day, and the trace went silent at exactly the
    // moment it was written to watch.
    if (field.crop.value != crop.value && field.last_crop.value != crop.value) {
      continue;
    }
    std::array<std::uint32_t, 4> crew{};  // plow+harrow, sow, harvest, haul
    for (const core::ResidentRow& resident : world.residents.rows) {
      if (resident.work.field != world.fields.row_ids[row]) {
        continue;
      }
      const core::WorkKind kind = resident.work.kind;
      crew[0] += kind == core::WorkKind::kPlowing || kind == core::WorkKind::kHarrowing ? 1U : 0U;
      crew[1] += kind == core::WorkKind::kSowing ? 1U : 0U;
      crew[2] += kind == core::WorkKind::kHarvest ? 1U : 0U;
      crew[3] += kind == core::WorkKind::kHauling ? 1U : 0U;
    }
    std::cout << "plan_shortfall:   trace day " << world.calendar.day << " month "
              << static_cast<int>(world.calendar.date.month) << " weekday "
              << static_cast<int>(world.calendar.weekday) << " precip "
              << static_cast<int>(world.weather.precipitation) << " phenomenon "
              << static_cast<int>(world.weather.phenomenon) << " t "
              << world.weather.air_temperature_celsius << " field " << row << " (" << field.area_ga
              << " ha) phase " << static_cast<int>(field.phase) << " sown "
              << (field.sown_day == core::kNeverSownDay ? -1
                                                        : static_cast<std::int64_t>(field.sown_day))
              << " work_left " << field.work_days_remaining << " crop " << field.crop.value
              << " last " << field.last_crop.value << " reaped_t " << Tonnes(field.reaped_grams)
              << " crew plow " << crew[0] << " sow " << crew[1] << " reap " << crew[2] << " haul "
              << crew[3] << "\n";
  }
}

int WalkOneSeed(std::uint64_t seed, const char* label, std::uint32_t trace_year) {
  run::Simulation started = run::Start(seed);
  if (!started) {
    return 1;
  }
  const core::ITable* const resources = started.tables->FindTable("resources");
  // life.csv life_speedup, for telling a hand from a child on the days nobody
  // carted. A missing table falls back to the canon's four.
  float life_speedup = 4.0F;
  if (const core::ITable* const life = started.tables->FindTable("life")) {
    const std::optional<float> cell =
        life->CellReal(life->FindRowByKey("life_speedup"), life->FindColumn("value"));
    life_speedup = cell.has_value() ? *cell : life_speedup;
  }
  run::YardPolicy yard(*started.tables);
  run::FixturePolicy fixture(*started.tables);
  run::RepairPolicy repairs(*started.tables);
  // The obvious chairman, so that what is measured is a village somebody
  // steers. Without him the run is the FLOOR and the answer would be "nobody
  // was making decisions", which we already know.
  run::SowingPolicy chairman(kRipenDays, kSeasonLastDay, false, started.tables.get());
  const core::ITable* const crops = started.tables->FindTable("crops");
  const std::uint32_t potato_row =
      crops == nullptr ? core::kNoTableRow : crops->FindRowByKey("potato");
  const core::CropId potato{static_cast<std::uint16_t>(potato_row)};

  std::cout << "plan_shortfall: === " << label << " (seed " << seed << ") ===\n";
  YearEnd last_day;
  Carting running;
  Signals signals_running = FreshSignals(resources);
  std::uint8_t failed_before = 0;
  std::uint32_t failures = 0;
  for (std::uint32_t year = 0; year < kYears; ++year) {
    for (std::uint32_t day = 0; day < core::kDaysPerYear; ++day) {
      run::AdvanceDays(*started, 1);
      yard.RunDay(*started.simulation);
      fixture.RunDay(*started.simulation);
      repairs.RunDay(*started.simulation);
      chairman.RunDay(*started.simulation);
      const core::WorldState& world = started.State();
      if (year == trace_year && potato_row != core::kNoTableRow) {
        TraceCropDay(world, potato);
      }

      // THE CARTING, WATCHED EVERY DAY. Counted where it happens rather than
      // inferred from the year's totals: "hauling got 34 man-days" cannot tell
      // a village that carted a little every day from one that never carted
      // at all until November.
      // THE TALLIES ACCUMULATE INTO `running` AND ARE SNAPSHOTTED AT THE
      // YEAR'S LAST DAY, never printed live — because the verdict is struck at
      // the TURN, which is the next year's first day, and a counter cleared on
      // that day is cleared BEFORE the print. The first draft did exactly that
      // and reported "a load lay on a field on 1 day of the year" beside a heap
      // of 367 tonnes. The two numbers came from different years and neither
      // was wrong on its own.
      if (world.calendar.day % core::kDaysPerYear == 0) {
        running = Carting{};
        signals_running = FreshSignals(resources);
      }
      RecordSignals(*started.simulation, signals_running);
      const core::Grams waiting_today = WaitingOnFields(world);
      if (waiting_today > 0) {
        ++running.days_with_load_waiting;
        running.peak_waiting = std::max(running.peak_waiting, waiting_today);
        std::uint32_t carting = 0;
        std::uint32_t reaping = 0;
        std::uint32_t harnessed = 0;
        for (const core::ResidentRow& resident : world.residents.rows) {
          carting += resident.work.kind == core::WorkKind::kHauling ? 1U : 0U;
          reaping += resident.work.kind == core::WorkKind::kHarvest ? 1U : 0U;
          harnessed += core::IsHorseWork(resident.work.kind) ? 1U : 0U;
        }
        if (carting == 0) {
          ++running.days_nobody_carted;
          running.days_reaping_instead += reaping > 0 ? 1U : 0U;
          const std::uint32_t horses = Horses(world);
          running.free_horses_when_idle =
              std::max(running.free_horses_when_idle, horses > harnessed ? horses - harnessed : 0U);
          ClassifyNoCarting(world, life_speedup, running);
        }
      }

      // THE LAST DAY OF THE YEAR, sampled before the turn clears the debt.
      if (world.calendar.day % core::kDaysPerYear == core::kDaysPerYear - 1) {
        last_day.year = year + 1;
        last_day.due = world.plan.due;
        last_day.delivered = world.plan.delivered;
        last_day.waiting_on_fields = WaitingOnFields(world);
        last_day.horses = Horses(world);
        last_day.area_sown_ha = world.ledger.current.area_sown_ha;
        last_day.area_harvested_ha = world.ledger.current.area_harvested_ha;
        last_day.area_lost_ha = world.ledger.current.area_lost_ha;
        last_day.work_days = world.ledger.current.work_days_by_kind;
        last_day.book = world.ledger.current;
        last_day.carting = running;
        last_day.signals = signals_running;
        core::Grams grain = 0;
        last_day.in_store_by_position.assign(world.plan.due.size(), 0);
        last_day.on_fields_by_position.assign(world.plan.due.size(), 0);
        for (std::uint32_t index = 0; index < world.plan.due.size(); ++index) {
          if (world.plan.due[index] > 0) {
            const core::Grams held =
                VillageStock(world, core::DefIdFromIndex<core::ResourceIdTag>(index));
            last_day.in_store_by_position[index] = held;
            grain += held;
          }
        }
        for (const core::FieldRow& field : world.fields.rows) {
          if (field.reaped_grams > 0 &&
              field.reaped_resource.value < last_day.on_fields_by_position.size()) {
            last_day.on_fields_by_position[field.reaped_resource.value] += field.reaped_grams;
          }
        }
        last_day.grain_in_store = grain;
      }

      // The verdict lands at the turn; the rising edge is the judgement.
      const std::uint8_t failed_now = world.plan.failed_years_in_a_row;
      if (failed_now > failed_before) {
        ++failures;
        // THE DELIVERY IS READ HERE, ON THE TURN, and not with the rest of the
        // last day. The core delivers and judges in one call at the year's
        // turn (RunYearStart: DeliverPlan, then JudgePlan, which clears `due`),
        // so on the last day `plan.delivered` still holds LAST year's shipment.
        // Until 2026-09-13 this run printed the due of one year beside the
        // delivery of the year before — "potato 41.63/41.63" on a failed year,
        // and every debt reported that morning one year out of step.
        last_day.delivered = world.plan.delivered;
        PrintShortfall(last_day, resources);
      }
      failed_before = failed_now;
    }
  }
  // What the building chairman did, because "no room in the stores" is only
  // half an answer until it says whether anybody built one.
  fixture.Report(started.State());
  chairman.Report();
  std::cout << "plan_shortfall: " << label << " — the plan was failed in " << failures << " of "
            << kYears << " years\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  // The layout under question and the canonical one beside it: the DIFFERENCE
  // between the two lists is the answer to "what makes 1931 worse", and that
  // question has never once been asked.
  const std::uint64_t suspect = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1931;
  const std::uint64_t canon = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1929;
  // An optional third argument traces the potato fields of that run year,
  // day by day, on the layout under question only. COUNTED FROM ZERO, while
  // the shortfall lines print the year from one: "year 1 THE DEBT" is traced
  // with 0. The first trace of 2026-09-13 was run on 1 and watched a year
  // whose potato came in whole.
  const std::uint32_t trace_year =
      argc > 3 ? static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 10)) : kYears;

  int failures = WalkOneSeed(suspect, "THE LAYOUT UNDER QUESTION", trace_year);
  failures += WalkOneSeed(canon, "THE CANONICAL LAYOUT", kYears);

  // NO GATE, AND THAT IS DELIBERATE. This run answers a question; it does not
  // hold a claim. A gate here would be a claim invented to give the file one,
  // and the project has spent the day removing exactly that shape.
  std::cout << (failures == 0 ? "plan_shortfall: both layouts walked\n"
                              : "plan_shortfall: a layout did not start\n");
  return failures == 0 ? 0 : 1;
}
