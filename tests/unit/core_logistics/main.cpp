// unit_core_logistics: the groom's tasks of carting (routing stage B, B2;
// core_logistics/logistics_tasks.h, logistics_config.h).

#include <cstdint>
#include <iostream>
#include <string>

#include "../../common/fake_tables.h"
#include "core_common/herd_state.h"
#include "core_common/land_state.h"
#include "core_common/logistics_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_logistics/logistics_system.h"
#include "logistics_config.h"
#include "logistics_plan.h"
#include "logistics_tasks.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

core::FieldId AddHeap(core::WorldState& world, core::Grams grams, std::uint16_t resource) {
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.reaped_grams = grams;
  field.reaped_resource = core::ResourceId{resource};
  return core::AppendRow(world.fields, field);
}

const core::LogisticsTaskRow* TaskOfField(const core::WorldState& world, core::FieldId field) {
  for (const core::LogisticsTaskRow& task : world.logistics_tasks.rows) {
    if (task.load_kind == core::LogisticsLoadKind::kFieldHeap && task.field.value == field.value) {
      return &task;
    }
  }
  return nullptr;
}

/// ONE TASK A LOAD, MADE AS IT APPEARS AND ENDED WITH IT (boss [9], default
/// 2): a heap, a stand's logs and a dig give three tasks at their kinds'
/// levels; a second sync makes none; the heap carted away ends its task.
int TestOneTaskALoad() {
  int failures = 0;
  const core::LogisticsConfig config;
  core::WorldState world;
  const core::FieldId heap = AddHeap(world, 1'000'000, 0);
  core::TimberStandRow stand;
  stand.load_grams = 2'000'000;
  core::AppendRow(world.stands, stand);
  core::ExtractionSiteRow site;
  site.load_grams = 500'000;
  core::AppendRow(world.extraction_sites, site);
  core::TaskDayCount count;
  core::SyncTasks(config, world, 0, count);
  failures += Expect(world.logistics_tasks.rows.size() == 3 && count.made == 3,
                     "tasks: a heap, logs and a dig make three tasks");
  const core::LogisticsTaskRow* const heap_task = TaskOfField(world, heap);
  failures += Expect(heap_task != nullptr && heap_task->level == core::LogisticsLevel::kTerm,
                     "tasks: a field's heap is due by a date (level 1)");
  core::TaskDayCount again;
  core::SyncTasks(config, world, 1, again);
  failures += Expect(again.made == 0 && world.logistics_tasks.rows.size() == 3,
                     "tasks: a load with a task gets no second one");
  world.fields.rows[core::FindRow(world.fields, heap)].reaped_grams = 0;
  core::TaskDayCount carted;
  core::SyncTasks(config, world, 2, carted);
  failures += Expect(carted.ended == 1 && world.logistics_tasks.rows.size() == 2 &&
                         TaskOfField(world, heap) == nullptr,
                     "tasks: the heap carted away ends its task");
  return failures;
}

/// AGEING FROM THE LAST TRIP (econ [5]): background -> ordinary after 6
/// days, ordinary -> term after 4 more; a served task starts again from its
/// base level; a paused one does not age.
int TestAgeing() {
  int failures = 0;
  const core::LogisticsConfig config;
  core::WorldState world;
  core::UnitRow store;
  store.emptying = 1;
  const core::UnitId church = core::AppendRow(world.units, store);
  core::TaskDayCount count;
  core::SyncTasks(config, world, 0, count);
  core::LogisticsTaskRow& task = world.logistics_tasks.rows[0];
  failures += Expect(task.base_level == core::LogisticsLevel::kBackground,
                     "ageing: the church's transfer is background (level 3)");
  const auto level_on = [&](core::SimDay day) {
    core::TaskDayCount day_count;
    core::AgeAndRaise(config, world, day, static_cast<core::Tick>(day) * 24U, day_count);
    return std::pair{world.logistics_tasks.rows[0].level, day_count.aged_up};
  };
  failures += Expect(level_on(5).first == core::LogisticsLevel::kBackground,
                     "ageing: five days unserved — still background");
  const auto [on_six, raised_six] = level_on(6);
  failures += Expect(on_six == core::LogisticsLevel::kOrdinary && raised_six == 1,
                     "ageing: six days — ordinary, counted raised that day");
  failures +=
      Expect(level_on(10).first == core::LogisticsLevel::kTerm, "ageing: ten days — due by a date");
  failures += Expect(level_on(30).first == core::LogisticsLevel::kTerm,
                     "ageing: a term never ages into urgent");
  // A carter goes to the church: served, back to background.
  core::ResidentRow carter;
  carter.work.kind = core::WorkKind::kHauling;
  carter.work.unit = church;
  core::AppendRow(world.residents, carter);
  core::TaskDayCount served;
  core::MarkServed(world, 30, served);
  failures += Expect(served.served == 1 && level_on(30).first == core::LogisticsLevel::kBackground,
                     "ageing: a trip returns the task to its base level");
  // Paused from day 30: twenty days later it has not aged.
  world.residents.rows.clear();
  world.residents.row_ids.clear();
  core::RebuildLookup(world.residents);
  world.logistics_tasks.rows[0].paused = true;
  for (core::SimDay day = 31; day <= 50; ++day) {
    level_on(day);
  }
  failures += Expect(world.logistics_tasks.rows[0].level == core::LogisticsLevel::kBackground,
                     "ageing: a paused task does not age");
  return failures;
}

/// SELF TO LEVEL 0 (econ [5]): a heap that spoils fast is urgent, the same
/// heap of a keeping resource is not; a feed heap is urgent while a herd is
/// hungry, the same heap with the herd fed is not.
int TestThreats() {
  int failures = 0;
  core::LogisticsConfig config;
  config.spoil_days = {2.0F, 0.0F, 0.0F};
  config.feed = {0, 0, 1};
  core::WorldState world;
  const core::FieldId rotting = AddHeap(world, 5'000'000, 0);
  const core::FieldId keeping = AddHeap(world, 5'000'000, 1);
  const core::FieldId hay = AddHeap(world, 5'000'000, 2);
  core::UnitRow barn;
  core::HerdRow herd;
  herd.unit = core::AppendRow(world.units, barn);
  herd.adult_count = 10;
  herd.fed_share = 1.0F;
  core::AppendRow(world.herds, herd);
  core::TaskDayCount count;
  core::SyncTasks(config, world, 0, count);
  core::AgeAndRaise(config, world, 0, 5, count);
  failures += Expect(TaskOfField(world, rotting)->level == core::LogisticsLevel::kUrgent &&
                         TaskOfField(world, rotting)->urgent_since == 5,
                     "threats: a heap to rot by tomorrow is urgent, the tick recorded");
  failures += Expect(TaskOfField(world, keeping)->level == core::LogisticsLevel::kTerm,
                     "threats: the same heap of a keeping resource stays due by a date");
  failures += Expect(TaskOfField(world, hay)->level == core::LogisticsLevel::kTerm,
                     "threats: a feed heap with the herd fed stays due by a date");
  world.herds.rows[0].fed_share = 0.5F;
  core::TaskDayCount hungry;
  core::AgeAndRaise(config, world, 1, 30, hungry);
  failures += Expect(TaskOfField(world, hay)->level == core::LogisticsLevel::kUrgent &&
                         hungry.urgent_by_hunger == 1,
                     "threats: a feed heap is urgent while a herd went underfed");
  return failures;
}

/// THE TABLE: logistics.csv's levels and knobs are read; a level beyond 3
/// refuses it.
int TestConfig() {
  int failures = 0;
  const test::FakeTable good{{"key", "value"},
                             {{"level_stand_logs", "3"}, {"age_ordinary_days", "5"}}};
  const test::FakeTableSet good_tables{{{"logistics", &good}}};
  core::LogisticsConfig config;
  std::string error;
  failures += Expect(core::ParseLogisticsConfig(good_tables, config, error) &&
                         config.default_level[1] == core::LogisticsLevel::kBackground &&
                         config.age_ordinary_days == 5.0F,
                     "config: the table's level and ageing are read");
  const test::FakeTable bad{{"key", "value"}, {{"level_site_dig", "5"}}};
  const test::FakeTableSet bad_tables{{{"logistics", &bad}}};
  core::LogisticsConfig refused;
  failures += Expect(!core::ParseLogisticsConfig(bad_tables, refused, error),
                     "config: a level of 5 refuses the table");
  return failures;
}

/// THE PLAN'S CHAINS (B3; logistics_plan.h): two carts on two of three heaps
/// of level 1, a stand's logs of level 2, a paused heap and a heap 10 km out.
/// Each cart starts with its own heap, goes on to the other heaps of its
/// level — the second cart's ring turned one, so it takes the third heap
/// before the first — then the logs; the paused heap and the one beyond the
/// road limit are in no chain; a carrier on foot has no cart.
int TestThePlansChains() {
  int failures = 0;
  core::LogisticsConfig config;  // 12 km/h: 1 game hour a km; the limit 6 hours
  core::WorldState world;
  const auto heap_at = [&world](float east_m) {
    core::FieldRow field;
    field.kind = core::LandKind::kArable;
    field.center = core::Vec2{.x = east_m, .y = 0.0F};
    field.reaped_grams = 1'000'000;
    field.reaped_resource = core::ResourceId{0};
    field.haul_days_remaining = 2.0F;
    return core::AppendRow(world.fields, field);
  };
  const core::FieldId first = heap_at(100.0F);
  const core::FieldId second = heap_at(200.0F);
  const core::FieldId third = heap_at(250.0F);
  const core::FieldId paused = heap_at(300.0F);
  const core::FieldId far = heap_at(10'000.0F);
  core::TimberStandRow stand;
  stand.position = core::Vec2{.x = 400.0F, .y = 0.0F};
  stand.load_grams = 1'000'000;
  stand.haul_days_remaining = 2.0F;
  const core::TimberStandId logs = core::AppendRow(world.stands, stand);
  core::TaskDayCount count;
  core::SyncTasks(config, world, 0, count);
  for (core::LogisticsTaskRow& task : world.logistics_tasks.rows) {
    task.paused = task.field.value == paused.value;
  }
  const auto carter = [&world](core::FieldId field, std::uint8_t horse) {
    core::ResidentRow person;
    person.work.kind = core::WorkKind::kHauling;
    person.work.field = field;
    person.work.rides_horse = horse;
    core::AppendRow(world.residents, person);
  };
  carter(first, 1);
  carter(second, 1);
  carter(first, 0);  // on foot
  core::LogisticsTally tally;
  const core::GroomPlan plan = core::BuildGroomPlan(config, world, tally);
  const auto load_of = [&world](const core::CartLeg& leg) {
    const core::LogisticsTaskRow& task =
        world.logistics_tasks.rows[core::FindRow(world.logistics_tasks, leg.task)];
    return task.load_kind == core::LogisticsLoadKind::kStandLogs
               ? -1
               : static_cast<int>(task.field.value);
  };
  failures += Expect(plan.carts.size() == 2 && tally.carts == 2,
                     "plan: two carts on a horse, the carrier on foot has none");
  if (plan.carts.size() != 2) {
    return failures;
  }
  const std::vector<core::CartLeg>& one = plan.carts[0].legs;
  const std::vector<core::CartLeg>& two = plan.carts[1].legs;
  std::cout << "  plan: cart 1 —";
  for (const core::CartLeg& leg : one) {
    std::cout << ' ' << load_of(leg);
  }
  std::cout << "; cart 2 —";
  for (const core::CartLeg& leg : two) {
    std::cout << ' ' << load_of(leg);
  }
  std::cout << " (field ids; -1 the logs)\n";
  const auto id = [](core::FieldId field) { return static_cast<int>(field.value); };
  failures +=
      Expect(one.size() == 4 && load_of(one[0]) == id(first) && load_of(one[1]) == id(second) &&
                 load_of(one[2]) == id(third) && load_of(one[3]) == -1,
             "plan: the first cart — its heap, the other heaps of level 1, then the logs");
  failures +=
      Expect(two.size() == 4 && load_of(two[0]) == id(second) && load_of(two[1]) == id(third) &&
                 load_of(two[2]) == id(first) && load_of(two[3]) == -1,
             "plan: the second cart — its own heap, the ring turned one (the third heap "
             "before the first), then the logs");
  bool paused_or_far = false;
  for (const core::CartPlan& cart : plan.carts) {
    for (const core::CartLeg& leg : cart.legs) {
      const int load = load_of(leg);
      paused_or_far = paused_or_far || load == static_cast<int>(paused.value) ||
                      load == static_cast<int>(far.value);
    }
  }
  failures += Expect(!paused_or_far,
                     "plan: the paused heap and the heap past the road limit are in no chain");
  (void)logs;
  return failures;
}

}  // namespace

int main() {
  int failures = 0;
  failures += TestOneTaskALoad();
  failures += TestAgeing();
  failures += TestThreats();
  failures += TestConfig();
  failures += TestThePlansChains();
  if (failures == 0) {
    std::cout << "unit_core_logistics: all checks passed\n";
  }
  return failures == 0 ? 0 : 1;
}
