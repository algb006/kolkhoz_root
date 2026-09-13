/// @file
/// @brief THE OBVIOUS CHAIRMAN: the one land decision a person would plainly
///        make, and the only one this policy makes.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY IT EXISTS, and it is not "a better AI". Every run this project has ever
/// measured plays a village in which the chairman makes NO land decision at
/// all: not one policy issues kSetRotation, so thirty campaign years work
/// exactly the layout genesis laid down — never raising the derelict ninety
/// hectares, never releasing a field. Measured 2026-09-13, and it changed what
/// the numbers mean rather than what they are:
///
///   * `plan_trial` reaching the trial on that run is NOT a broken threshold.
///     A village nobody steered SHOULD be tried. The instrument was honest;
///     the reading was not — we called that play "canonical" when it is the
///     FLOOR.
///   * A run with no decisions also diverges: the yield slope measured 29
///     failed plan years of 30 at its gentlest setting, 16 in the middle and
///     26 at its harshest. Not a curve — noise. A balance number cannot be
///     chosen on an instrument whose trajectory nothing corrects.
///
/// THE RULE IS ONE, AND DELIBERATELY NOT A GOOD ONE (boss, 2026-09-13):
///
///   > The sowing does not fit the window — release the chains of the fields
///   > that will not make it anyway, poorest first.
///
/// A clever policy would prove that a clever chairman copes, which is not the
/// question. The question is whether the one who does the OBVIOUS thing copes.
/// Poorest first for the same reason the autumn ploughing takes them first: the
/// choice has to be a function of the layout and not of luck.

#ifndef TESTS_RUN_COMMON_SOWING_POLICY_H_
#define TESTS_RUN_COMMON_SOWING_POLICY_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief Issues the one order an obvious chairman would, and gives back what
/// he had to take away.
class SowingPolicy {
 public:
  /// @param ripen_days How long a spring crop takes to ripen, in game days —
  ///        the gap between the end of its sowing window and the start of its
  ///        reaping, which is what the core uses (field_work.h, RipenDays).
  ///        One figure for every crop here on purpose: the policy is the
  ///        obvious chairman's, and he does not keep a table in his head.
  /// @param growing_season_last_day The last day a standing crop is safe from
  ///        the snow.
  /// @param looks_ahead THE SMARTER RULE, and it is one rule more rather than a
  ///        cleverer one. The obvious chairman gives up on a field the day it
  ///        is plainly too late; this one asks at the START of the spring how
  ///        much ground the team can prepare in time, and lets the rest go at
  ///        once. Same verb, same order, same poorest-first — only the day it
  ///        is said on.
  ///
  ///        It exists to answer one question and not to win: is a layout on
  ///        which the OBVIOUS chairman is taken to court survivable by anybody
  ///        at all? "Nobody could have coped" is a defect of the world and
  ///        "somebody cleverer could" is ordinary difficulty, and the two look
  ///        identical until one of them is tried (boss, 2026-09-13).
  /// @param tables When given, the chairman also ANSWERS THE PLAN ALARM
  ///        (kPlanPositionUncovered): the obvious chairman is the one who
  ///        reacts to what the game shows him, one obvious answer per signal
  ///        (boss, 2026-09-13). Null keeps the release-only policy.
  SowingPolicy(std::int32_t ripen_days,
               std::uint32_t growing_season_last_day,
               bool looks_ahead = false,
               const core::ITableSet* tables = nullptr)
      : ripen_days_(ripen_days),
        growing_season_last_day_(growing_season_last_day),
        looks_ahead_(looks_ahead) {
    if (tables != nullptr) {
      ReadCropsAndPositions(*tables);
    }
  }

  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t day_of_year = world.calendar.day % core::kDaysPerYear;
    if (day_of_year == 0) {
      GiveTheChainsBack(simulation, world);
      return;
    }
    if (day_of_year == 1 && !crop_of_resource_.empty()) {
      // ON THE FIRST WORKING DAY and not on the day the alarm lights: every
      // borrowed chain is back by then, and a chain set before any work opens
      // from it is spent the ordinary way (rotation_skips_turn) instead of
      // standing still through a turn.
      AnswerThePlanAlarm(simulation, world);
    }
    if (looks_ahead_ && day_of_year == 1) {
      KeepOnlyWhatTheSpringCanSow(simulation, world);
      return;
    }
    ReleaseWhatWillNotMakeIt(simulation, world);
  }

  /// @brief The line that keeps the prosthetic from reading as a mechanic.
  void Report() const {
    std::cout << "sowing_policy: the obvious chairman released " << released_
              << " chains from fields whose sowing would not have ripened, and gave " << restored_
              << " of them back at the year's turn\n";
    if (!crop_of_resource_.empty()) {
      std::cout
          << "sowing_policy: he answered " << answered_
          << " plan alarms by putting the missing crop onto the best fields of that year, and "
          << unanswered_ << " found no field to take it\n";
    }
    std::cout << "sowing_policy: THE GIVING BACK IS A PROSTHETIC, NOT A DECISION — the order book "
                 "has no verb for \"do not sow this field THIS year\". kSetRotation with empty "
                 "slots means \"I take my word back\", and it means it for ever, so a policy that "
                 "only released would strip the farm bare in twenty years and this arm would be "
                 "measuring my implementation instead of the chairman\n";
  }

 private:
  /// Which crop yields each resource (the first crops.csv row that does), and
  /// which crops the district's plan asks by — read once off the tables.
  void ReadCropsAndPositions(const core::ITableSet& tables) {
    const core::ITable* const crops = tables.FindTable("crops");
    const core::ITable* const resources = tables.FindTable("resources");
    const core::ITable* const campaign = tables.FindTable("campaign");
    if (crops == nullptr || resources == nullptr || campaign == nullptr) {
      return;
    }
    const std::uint32_t resource_col = crops->FindColumn("resource");
    crop_of_resource_.assign(resources->RowCount(), core::CropId{});
    for (std::uint32_t row = 0; row < crops->RowCount(); ++row) {
      const std::uint32_t resource = resources->FindRowByKey(crops->CellText(row, resource_col));
      if (resource < crop_of_resource_.size() &&
          crop_of_resource_[resource].value == core::kInvalidDefIdValue) {
        crop_of_resource_[resource] = core::CropId{static_cast<std::uint16_t>(row)};
      }
      crop_resource_.push_back(resource);
    }
    const std::uint32_t positions_row = campaign->FindRowByKey("plan_positions");
    if (positions_row == core::kNoTableRow) {
      return;
    }
    const std::uint32_t value_col = campaign->FindColumn("value");
    const std::string list(campaign->CellText(positions_row, value_col));
    std::size_t start = 0;
    while (start < list.size()) {
      const std::size_t end = std::min(list.find(' ', start), list.size());
      const std::string token = list.substr(start, end - start);
      const std::size_t equals = token.find('=');
      const std::uint32_t crop = crops->FindRowByKey(token.substr(0, equals));
      if (crop < crop_resource_.size() && equals != std::string::npos) {
        plan_resources_.push_back(crop_resource_[crop]);
        plan_shares_.push_back(std::strtof(token.substr(equals + 1).c_str(), nullptr) / 100.0F);
      }
      start = end + 1;
    }
    const std::optional<float> percent =
        campaign->CellReal(campaign->FindRowByKey("plan_grain_share_percent"), value_col);
    plan_grain_share_ = percent.has_value() ? *percent / 100.0F : 0.0F;
  }

  /// A field's chain once today's orders land.
  static std::array<core::CropId, 3> ChainOf(const core::WorldState& world,
                                             const std::vector<core::OrderRow>& orders,
                                             std::uint32_t row) {
    const core::FieldRow& field = world.fields.rows[row];
    std::array<core::CropId, 3> chain = {
        field.rotation_year0, field.rotation_year1, field.rotation_year2};
    for (const core::OrderRow& order : orders) {
      if (order.field == world.fields.row_ids[row]) {
        chain = {order.rotation_year0, order.rotation_year1, order.rotation_year2};
      }
    }
    return chain;
  }

  /// The produce a crop yields, or an out-of-range value for none.
  std::uint32_t ResourceOf(core::CropId crop) const {
    return crop.value < crop_resource_.size() ? crop_resource_[crop.value] : ~0U;
  }

  /// Hectares the district's rate asks for `resource` in `year`, the core's
  /// own arithmetic mirrored (production_alarms.cpp): priced area × position
  /// share × plan share. 0 for a resource no position asks by.
  float OwedHectares(const core::WorldState& world,
                     const std::vector<std::uint32_t>& fields,
                     std::uint32_t resource,
                     std::size_t year) const {
    const auto at = std::ranges::find(plan_resources_, resource);
    if (at == plan_resources_.end()) {
      return 0.0F;
    }
    float worked_ha = 0.0F;
    for (const std::uint32_t row : fields) {
      worked_ha += world.fields.rows[row].area_ga;
    }
    const float priced_ha = year == 0 ? world.plan.worked_ha_last_year : worked_ha;
    return priced_ha * plan_shares_[static_cast<std::size_t>(at - plan_resources_.begin())] *
           plan_grain_share_;
  }

  /// Hectares whose chain (after today's orders) grows `resource` in `year`.
  float GrownHectares(const core::WorldState& world,
                      const std::vector<std::uint32_t>& fields,
                      const std::vector<core::OrderRow>& orders,
                      std::uint32_t resource,
                      std::size_t year) const {
    float grown_ha = 0.0F;
    for (const std::uint32_t row : fields) {
      grown_ha += ResourceOf(ChainOf(world, orders, row)[year]) == resource
                      ? world.fields.rows[row].area_ga
                      : 0.0F;
    }
    return grown_ha;
  }

  /// THE ONE OBVIOUS ANSWER TO THE PLAN ALARM (boss, 2026-09-13): a plan
  /// position is a QUANTITY, so the missing crop goes onto the BEST fields of
  /// year N — best first, the opposite of the release rule, which is about
  /// giving land away — until the hectares reach the district's rate. First the
  /// years growing no position; then, only if still short, years growing a
  /// position whose hectares would stay covered without this field.
  ///
  /// POOREST FIRST UNTIL THE SAME DAY, and measured wrong: on seed 1933 the
  /// answer closed a missing potato with a 3.5 ha field at fertility 20 —
  /// 18 t reaped against 41.63 t owed.
  void AnswerThePlanAlarm(core::ISimulation& simulation, const core::WorldState& world) {
    std::vector<core::Alarm> alarms;
    simulation.CollectAlarms(alarms);
    std::vector<std::uint32_t> fields;
    for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
      if (world.fields.rows[row].kind == core::LandKind::kArable &&
          core::HasRotation(world.fields.rows[row])) {
        fields.push_back(row);
      }
    }
    std::ranges::stable_sort(fields, [&world](std::uint32_t a, std::uint32_t b) {
      return world.fields.rows[a].fertility > world.fields.rows[b].fertility;
    });
    std::vector<core::OrderRow> orders;
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind != core::AlarmKind::kPlanPositionUncovered ||
          alarm.resource.value >= crop_of_resource_.size() || alarm.amount < 0 ||
          alarm.amount > 2) {
        continue;
      }
      const auto year = static_cast<std::size_t>(alarm.amount);
      const std::uint32_t wanted = alarm.resource.value;
      const float owed_ha = OwedHectares(world, fields, wanted, year);
      bool placed_any = false;
      for (int pass = 0; pass < 2; ++pass) {
        for (const std::uint32_t row : fields) {
          if (GrownHectares(world, fields, orders, wanted, year) >= owed_ha &&
              (owed_ha > 0.0F || placed_any)) {
            break;
          }
          std::array<core::CropId, 3> chain = ChainOf(world, orders, row);
          const std::uint32_t there = ResourceOf(chain[year]);
          if (there == wanted) {
            continue;
          }
          const float there_owed = OwedHectares(world, fields, there, year);
          const bool position_there =
              std::ranges::find(plan_resources_, there) != plan_resources_.end();
          const bool free_year = !position_there;
          const bool spare_year =
              position_there &&
              GrownHectares(world, fields, orders, there, year) - world.fields.rows[row].area_ga >=
                  there_owed;
          if (!(pass == 0 ? free_year : spare_year)) {
            continue;
          }
          chain[year] = crop_of_resource_[wanted];
          SetChain(world, orders, row, chain);
          placed_any = true;
        }
      }
      ++(placed_any ? answered_ : unanswered_);
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
    }
  }

  /// Writes (or rewrites) today's order for a field's chain.
  static void SetChain(const core::WorldState& world,
                       std::vector<core::OrderRow>& orders,
                       std::uint32_t row,
                       const std::array<core::CropId, 3>& chain) {
    core::OrderRow* pending = nullptr;
    for (core::OrderRow& order : orders) {
      pending = order.field == world.fields.row_ids[row] ? &order : pending;
    }
    if (pending == nullptr) {
      core::OrderRow order;
      order.kind = core::OrderKind::kSetRotation;
      order.field = world.fields.row_ids[row];
      orders.push_back(order);
      pending = &orders.back();
    }
    pending->rotation_year0 = chain[0];
    pending->rotation_year1 = chain[1];
    pending->rotation_year2 = chain[2];
  }

  std::vector<float> plan_shares_;
  float plan_grain_share_ = 0.0F;

  std::vector<core::CropId> crop_of_resource_;
  std::vector<std::uint32_t> crop_resource_;
  std::vector<std::uint32_t> plan_resources_;
  std::uint32_t answered_ = 0;
  std::uint32_t unanswered_ = 0;

  /// A chain taken away, and the field it came from.
  struct Borrowed {
    core::FieldId field;
    core::CropId year0;
    core::CropId year1;
    core::CropId year2;
  };

  /// Fields still owing their sowing whose crop can no longer ripen in time.
  /// The test is the core's own, spelled the obvious chairman's way: he can see
  /// that the ground is not ready and that the season is running out.
  void ReleaseWhatWillNotMakeIt(core::ISimulation& simulation, const core::WorldState& world) {
    const std::uint32_t day_of_year = world.calendar.day % core::kDaysPerYear;
    if (static_cast<std::int32_t>(day_of_year) + ripen_days_ <=
        static_cast<std::int32_t>(growing_season_last_day_)) {
      return;  // there is still time: nothing to give up on
    }
    std::vector<std::uint32_t> doomed;
    for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
      const core::FieldRow& field = world.fields.rows[row];
      const bool awaiting = field.phase == core::FieldPhase::kIdle ||
                            field.phase == core::FieldPhase::kPlowing ||
                            field.phase == core::FieldPhase::kHarrowing;
      if (field.kind != core::LandKind::kArable || !awaiting || !core::HasRotation(field)) {
        continue;
      }
      // AN IDLE FIELD IS NOT NECESSARILY WAITING — it may already have been
      // sown and reaped this year. Until 2026-09-13 this test took those too:
      // every field harvested before the deadline was "released" and handed
      // back unmoved at the turn, and a chain handed back unmoved does not
      // advance (rotation_skips_turn). The obvious chairman froze the rotation
      // of nearly the whole farm, every year — 139 chains in twenty years — and
      // on seed 1936, whose potato stood in no field's first slot, the district's
      // largest position was never grown again: twenty failed years of twenty,
      // read as the harshest answer the game had given. It was this line.
      // A crop sown within the last year has had its season.
      const bool sown_this_season =
          field.sown_day != core::kNeverSownDay &&
          static_cast<std::uint64_t>(field.sown_day) + core::kDaysPerYear > world.calendar.day;
      if (sown_this_season) {
        continue;
      }
      doomed.push_back(row);
    }
    if (doomed.empty()) {
      return;
    }
    // Poorest first, then row order: a function of the layout, not of luck.
    std::ranges::stable_sort(doomed, [&world](std::uint32_t a, std::uint32_t b) {
      return world.fields.rows[a].fertility < world.fields.rows[b].fertility;
    });
    std::vector<core::OrderRow> orders;
    for (const std::uint32_t row : doomed) {
      const core::FieldRow& field = world.fields.rows[row];
      borrowed_.push_back({.field = world.fields.row_ids[row],
                           .year0 = field.rotation_year0,
                           .year1 = field.rotation_year1,
                           .year2 = field.rotation_year2});
      core::OrderRow order;
      order.kind = core::OrderKind::kSetRotation;
      order.field = world.fields.row_ids[row];
      orders.push_back(order);  // every slot invalid: "I take my word back"
      ++released_;
    }
    simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
  }

  /// THE SMARTER RULE, in one sentence: **what I had to give up last year, I do
  /// not take on this year.**
  ///
  /// Said on the first working day instead of at the deadline, and that is the
  /// whole of the difference: a field released in the last week has already
  /// eaten its ploughing and its harrowing, and the team-days it ate are the
  /// ones the fields beside it needed. Released in the first week it costs
  /// nothing and gives its share away.
  ///
  /// IT CARRIES NO CAPACITY MODEL, and the first draft did — team days over a
  /// norm per hectare — which never fired once: the arithmetic said 171
  /// hectares would fit where seventy do not. Every term in it was a guess
  /// (every horse on field work, every day of the season, no crew cap), and a
  /// guess dressed as a calculation is worse than the memory it replaced. Last
  /// year's surplus is not a model of anything: it is what happened.
  void KeepOnlyWhatTheSpringCanSow(core::ISimulation& simulation, const core::WorldState& world) {
    if (gave_up_last_year_.empty()) {
      return;  // nothing learned yet; the obvious rule still runs each day
    }
    // A FIELD THAT RESTED LAST YEAR IS WORKED THIS YEAR, and without this line
    // the rule eats itself: the surplus is released on the first day, given
    // back at the turn, and released again on the first day of the next year,
    // so the same ground is never worked at all. Measured before the guard —
    // twenty failed plan years of twenty, against the obvious chairman's four.
    // That was the policy degenerating, not the world answering.
    std::vector<core::FieldId> resting;
    resting.swap(gave_up_last_year_);
    std::vector<core::OrderRow> orders;
    for (const core::FieldId field : resting) {
      if (std::ranges::find(rested_last_year_, field) != rested_last_year_.end()) {
        continue;  // it has had its year off; it takes its turn now
      }
      const std::uint32_t row = core::FindRow(world.fields, field);
      if (row == core::kNoRow || !core::HasRotation(world.fields.rows[row])) {
        continue;
      }
      const core::FieldRow& carried = world.fields.rows[row];
      borrowed_.push_back({.field = field,
                           .year0 = carried.rotation_year0,
                           .year1 = carried.rotation_year1,
                           .year2 = carried.rotation_year2});
      core::OrderRow order;
      order.kind = core::OrderKind::kSetRotation;
      order.field = field;
      orders.push_back(order);
      ++released_;
    }
    rested_last_year_.clear();
    for (const core::OrderRow& order : orders) {
      rested_last_year_.push_back(order.field);
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
    }
  }

  /// THE PROSTHETIC. Hands every borrowed chain back on the first day of the
  /// year, because the seam has no way to say "skip this field for one season".
  void GiveTheChainsBack(core::ISimulation& simulation, const core::WorldState& world) {
    if (borrowed_.empty()) {
      return;
    }
    std::vector<core::OrderRow> orders;
    for (const Borrowed& chain : borrowed_) {
      if (core::FindRow(world.fields, chain.field) == core::kNoRow) {
        continue;  // the field is gone; nothing to give back to
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kSetRotation;
      order.field = chain.field;
      // GIVEN BACK UNMOVED, and the core's own rule is the reason: "a chain
      // whose first season has not been USED stands still"
      // (land_state.h, rotation_skips_turn). The field was not sown, so the
      // season was not spent, so the chain has nothing to advance past.
      //
      // Tried the other way and measured it: advancing the chain by one skips
      // the crop the field never grew, and the district's positions go with it
      // — the arm went from 2 failed plan years of 20 to 18, with a run of 15.
      // The skipped season is not free, and handing it back advanced charges
      // for it twice.
      order.rotation_year0 = chain.year0;
      order.rotation_year1 = chain.year1;
      order.rotation_year2 = chain.year2;
      orders.push_back(order);
      ++restored_;
    }
    // AND WHAT WAS GIVEN UP IS REMEMBERED, for the rule that acts a year
    // earlier. It is recorded at the turn rather than at each release, so a
    // field that was let go twice in one year is remembered once.
    gave_up_last_year_.clear();
    for (const Borrowed& chain : borrowed_) {
      gave_up_last_year_.push_back(chain.field);
    }
    borrowed_.clear();
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
    }
  }

  std::vector<core::FieldId> gave_up_last_year_;

  /// Fields the look-ahead rule rested at the start of last year: they take
  /// their turn this year, whatever the surplus says.
  std::vector<core::FieldId> rested_last_year_;

  std::int32_t ripen_days_;

  std::uint32_t growing_season_last_day_;

  bool looks_ahead_ = false;

  std::vector<Borrowed> borrowed_;

  std::uint32_t released_ = 0;

  std::uint32_t restored_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SOWING_POLICY_H_
