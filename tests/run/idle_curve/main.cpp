// Simulation run: where the idle hours come from, year by year.
//
// The census says the settlement idles nearly twice as many hours as it
// works, and on some seeds the working hours reach ZERO by the tenth year.
// There is no player in a run, so the village hands out its own orders, and
// "nobody was given anything to do" is therefore a statement about the farm
// and not about a missing chairman.
//
// WHAT THIS MEASURES, AND WHY IT IS NOT THE SAME QUESTION. Idleness has two
// possible causes and they need opposite cures:
//
//   THE HANDING OUT BROKE — there is work standing undone and nobody was
//   sent to it. That is a defect in the assignment.
//
//   THE WORK RAN OUT — the farm's demand is what twenty fields and a
//   handful of sites ask for, and the village grew past it. That is not a
//   defect at all; it is the shape of the model, and the cure is more to
//   do, not better assignment.
//
// The two are told apart by ONE number the activity census does not carry:
// the WORK STANDING UNDONE, in man-days, on the same day the idleness is
// counted. Every seam the labour sub-step can drain is summed — fields,
// hauling, sites, herds — because a seam is exactly "work somebody could be
// sent to right now".
//
// So: if idleness rises while the seams are FULL, the assignment is broken.
// If idleness rises while the seams are EMPTY, the village has outgrown its
// work. Nothing here guesses which; it prints both curves side by side.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../common/fixture_policy.h"
#include "../common/orders_policy.h"
#include "../common/repair_policy.h"
#include "../common/run_harness.h"
#include "../common/yard_policy.h"
#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/order_state.h"
#include "core_common/resident_activity.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {

constexpr std::uint32_t kYears = 12;

core::ActivityRules RulesOfRun(const core::ITableSet& tables) {
  core::ActivityRules rules;
  // The two road rates, from the same table the labour model reads them
  // from. A run that made one up would be measuring its own invention: the
  // half-hour that used to stand here counted a man four hours from his
  // field as working.
  const core::ITable* const transport = tables.FindTable("transport");
  const std::uint32_t speed_column =
      transport == nullptr ? core::kNoTableColumn : transport->FindColumn("speed_kmh");
  const auto rate = [&](std::string_view key, float fallback) {
    const std::uint32_t row =
        transport == nullptr ? core::kNoTableRow : transport->FindRowByKey(key);
    if (row == core::kNoTableRow || speed_column == core::kNoTableColumn) {
      return fallback;
    }
    const float kmh =
        std::strtof(std::string(transport->CellText(row, speed_column)).c_str(), nullptr);
    // Real km/h against game hours: the clock runs four times faster.
    return kmh > 0.0F ? core::kClockScale / kmh : fallback;
  };
  rules.walk_hours_per_km = rate("pedestrian", 2.4F);
  rules.harness_hours_per_km = rate("horse_trot", 1.0F);
  const core::ITable* const life = tables.FindTable("life");
  const std::uint32_t row =
      life == nullptr ? core::kNoTableRow : life->FindRowByKey("life_speedup");
  const std::uint32_t column = life == nullptr ? core::kNoTableColumn : life->FindColumn("value");
  if (row != core::kNoTableRow && column != core::kNoTableColumn) {
    rules.life_speedup = std::strtof(std::string(life->CellText(row, column)).c_str(), nullptr);
  }
  return rules;
}

/// @brief Work standing undone right now, in game man-days: every seam the
/// labour sub-step is able to drain.
///
/// THIS IS THE HALF THE CENSUS CANNOT SEE. A man is idle either because
/// nobody sent him or because there was nowhere to send him, and only this
/// number separates the two.
float SeamsStanding(const core::WorldState& world) {
  float days = 0.0F;
  for (const core::FieldRow& field : world.fields.rows) {
    days += field.work_days_remaining > 0.0F ? field.work_days_remaining : 0.0F;
    days += field.haul_days_remaining > 0.0F ? field.haul_days_remaining : 0.0F;
  }
  for (const core::UnitRow& unit : world.units.rows) {
    days += unit.construction.labor_days_remaining > 0.0F ? unit.construction.labor_days_remaining
                                                          : 0.0F;
  }
  for (const core::HerdRow& herd : world.herds.rows) {
    days += herd.care_days_remaining > 0.0F ? herd.care_days_remaining : 0.0F;
  }
  return days;
}

}  // namespace

int main(int argc, char** argv) {
  const std::uint32_t seed = argc > 1 ? static_cast<std::uint32_t>(std::atoi(argv[1])) : 1930;
  bool sow_idle_land = false;
  // ONE ARM, ONE VARIABLE. The other instrument runs without a chairman and
  // finds winter the QUIET season; this one runs with one and finds winter
  // among the busiest. The chairman builds, and building is winter work, so
  // he was the obvious candidate — and he PREDICTS THE WRONG SIGN: his work
  // should have lowered winter idleness here, and winter idleness here is
  // higher. A candidate that predicts the opposite of what was measured
  // explains something else. Switching him off settles it in one run.
  bool no_chairman = false;
  // And the chairman is four policies, so "the chairman" is not yet an
  // answer: --yard-only keeps the one that raises the horse yard and
  // appoints a groom, and drops the other three.
  bool yard_only = false;
  bool watch_build = false;
  // --yard-delay=N: the chairman does not touch the horse yard until day N.
  //
  // WHAT IT MEASURES. Until the yard is raised the team only ages — that is
  // the design's own rule, "no stable, no foals" — so there is a number of
  // days after which it is too late, and the number is what a hint has to
  // be built on. A hint without a deadline is an alarm with no answer.
  std::uint32_t yard_delay = 0;
  bool far_site = false;
  // --herd-alarm: the kHerdWithoutStable timeline, and nothing else.
  //
  // Three dates decide whether the kind is a warning or an obituary: the day
  // it lights, the day the team first loses a head, and the day the team is
  // gone. Combined with --yard-delay=N it is also the damage guard boss
  // asked for: take the stable out of a run that had one and the kind must
  // light and never go out again.
  bool herd_alarm = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    sow_idle_land = sow_idle_land || argument == "--sow-idle-land";
    no_chairman = no_chairman || argument == "--no-chairman";
    yard_only = yard_only || argument == "--yard-only";
    watch_build = watch_build || argument == "--watch-build";
    if (argument.starts_with("--yard-delay=")) {
      yard_delay = static_cast<std::uint32_t>(std::atoi(
          std::string(argument.substr(std::string_view("--yard-delay=").size())).c_str()));
    }
    far_site = far_site || argument == "--far";
    herd_alarm = herd_alarm || argument == "--herd-alarm";
  }
  const run::Simulation world = run::Start(seed);
  if (!world) {
    return 1;
  }
  // THE CONTROL ARM, and it answers the question the two curves alone
  // cannot. Ninety-three of the start canon's hundred and sixty-three
  // hectares lie unworked — two fields of forty-five and a reserve of three —
  // so the farm's labour demand is bounded by seventy hectares for ever, and
  // the settlement grows sixteenfold over thirty years without touching one
  // of them.
  //
  // If the handing out of work were broken, giving the village more land
  // would change nothing. If the demand is the ceiling, the working hours
  // must rise.
  //
  // AND THE RUN STANDS IN FOR THE PLAYER HERE, deliberately and out loud.
  // Until 2026-09-12 this arm had to change a LandKind, because the core
  // refused every order on unraised land; that kind is gone and raising the
  // ground is now ploughing it at the ordinary norm. What is still missing
  // is not a mechanic but a DECISION: the three-year rotation is the
  // player's to set (farming design §7, "the player gives each field a chain
  // of three seasons"), and a headless run has no player, so a field nobody
  // has told what to grow is sown with nothing for ever.
  //
  // So the arm hands the idle fields the rotation of the first field that
  // has one. That is the run playing chairman, exactly as food_year's
  // MinimalChairman plays him at the door of the sealed funds — a PROBE, and
  // nothing of it ships. Inventing a rotation in the core instead would be
  // the core making the player's decision for him.
  if (sow_idle_land) {
    core::WorldState raised = world.State();
    core::CropId slots[3]{};
    for (const core::FieldRow& field : raised.fields.rows) {
      if (field.kind == core::LandKind::kArable &&
          field.rotation_year0.value != core::kInvalidDefIdValue) {
        slots[0] = field.rotation_year0;
        slots[1] = field.rotation_year1;
        slots[2] = field.rotation_year2;
        break;
      }
    }
    std::uint32_t lifted = 0;
    for (core::FieldRow& field : raised.fields.rows) {
      // ALL THREE SLOTS EMPTY, and not just the first: a field on a fallow
      // YEAR has a chain with a gap in it, and handing it somebody else's
      // rotation would overwrite a decision rather than supply a missing one.
      if (field.kind != core::LandKind::kArable ||
          field.rotation_year0.value != core::kInvalidDefIdValue ||
          field.rotation_year1.value != core::kInvalidDefIdValue ||
          field.rotation_year2.value != core::kInvalidDefIdValue) {
        continue;
      }
      field.rotation_year0 = slots[0];
      field.rotation_year1 = slots[1];
      field.rotation_year2 = slots[2];
      // AND THE BYTE, WHICH IS WHAT THE CORE ACTUALLY ASKS. Since 2026-09-12
      // a chain is announced by rotation_assigned and not by the slots: the
      // sowing gates on the slot, but the manure queue, the year's fallow
      // recovery and both mean-fertility walks gate on HasRotation. Set the
      // three and not the byte and this arm sows its lifted fields, looks as
      // if it worked, and measures ground that is never manured, never
      // recovered and absent from the very figure the arm exists to compare.
      field.rotation_assigned = 1;
      ++lifted;
    }
    world.simulation->ResetWorld(raised);
    std::cout << "idle_curve: CONTROL ARM — " << lifted
              << " idle fields given a rotation. A PROBE, not a mechanic: the rotation is the "
                 "PLAYER's decision and the core does not make it, so a headless run has to "
                 "stand in for him. Nothing of this ships\n";
  }
  int failures = 0;
  const core::ActivityRules rules = RulesOfRun(*world.tables);

  // --watch-build: THE SEAM ITSELF, day by day, and nothing else.
  //
  // Boss's objection is exact and my earlier answer was half of one. I
  // showed that the arm ISSUES the orders and that the yard ends at level 2
  // with nothing left to do — and from that screen "the labour paid for it"
  // and "an order zeroed it" are indistinguishable, because the third
  // command in my own three is kUpgradeUnit and an upgrade opens a NEW
  // seam. So: the first two commands only, and then watch the number.
  if (watch_build) {
    const core::ITable* const types = world.tables->FindTable("unit_types");
    const std::uint32_t type_row =
        types == nullptr ? core::kNoTableRow : types->FindRowByKey("horse_yard");
    if (type_row == core::kNoTableRow) {
      std::cout << "FAIL: no horse_yard in the tables\n";
      return 1;
    }
    core::OrderRow mark;
    mark.kind = core::OrderKind::kBuildUnit;
    mark.unit_type = core::UnitTypeId{static_cast<std::uint16_t>(type_row)};
    // --far reproduces the other instrument's frame exactly: (505, 495) is
    // the OPPOSITE CORNER of a 12 km map from a village that lives between
    // x 7380..9551 and y 8509..10475 — eleven and nine tenths kilometres of
    // diagonal. He did not choose that corner, he ALLOWED it: a pair of
    // numbers "about the middle" without asking the map where the middle
    // is. The question this run asks is not his arithmetic but MINE: does
    // the order book say anything at all about a site nobody can reach?
    mark.position =
        far_site ? core::Vec2{.x = 505.0F, .y = 495.0F} : core::Vec2{.x = 8600.0F, .y = 9700.0F};
    world->StageOrders(std::span<const core::OrderRow>(&mark, 1), {});
    world->AdvanceStep();
    std::uint32_t site = core::kNoRow;
    for (std::uint32_t row = 0; row < world.State().units.rows.size(); ++row) {
      if (world.State().units.rows[row].type.value == type_row) {
        site = row;
      }
    }
    if (site == core::kNoRow) {
      std::cout << "FAIL: the mark was refused\n";
      return 1;
    }
    const core::UnitId id = world.State().units.row_ids[site];
    std::cout << "idle_curve: разметка — отказ " << static_cast<int>(mark.refusal) << " ("
              << (mark.refusal == core::OrderRefusal::kNone ? "принята" : "отклонена") << ")\n";
    core::OrderRow start;
    start.kind = core::OrderKind::kStartBuild;
    start.unit = id;
    world->StageOrders(std::span<const core::OrderRow>(&start, 1), {});
    float last_seam = -1.0F;
    for (std::uint32_t day = 0; day < 200; ++day) {
      for (std::uint32_t tick = 0; tick < core::kTicksPerDay; ++tick) {
        world->AdvanceStep();
      }
      const std::uint32_t row = core::FindRow(world.State().units, id);
      if (row == core::kNoRow) {
        std::cout << "idle_curve: площадка исчезла на сутках " << day << '\n';
        break;
      }
      const core::UnitRow& unit = world.State().units.rows[row];
      std::uint32_t crew = 0;
      for (const core::ResidentRow& resident : world.State().residents.rows) {
        crew += resident.work.kind == core::WorkKind::kConstruction ? 1U : 0U;
      }
      // AND WHAT DOES THE VILLAGE SAY ABOUT IT? A site nobody can reach is
      // not silent in the core: kSiteWithoutCrew counts the assignments and
      // finds none. Whether the ORDER should have refused is a separate
      // question; whether the state says anything at all is this one.
      std::vector<core::Alarm> alarms;
      world->CollectAlarms(alarms);
      std::uint32_t crewless = 0;
      std::uint32_t unreachable = 0;
      std::int64_t road = 0;
      for (const core::Alarm& alarm : alarms) {
        if (alarm.unit.value != id.value) {
          continue;
        }
        crewless += alarm.kind == core::AlarmKind::kSiteWithoutCrew ? 1U : 0U;
        if (alarm.kind == core::AlarmKind::kSiteUnreachable) {
          ++unreachable;
          road = alarm.amount;
        }
      }
      // THE GUARD, and it is the one boss asked for: a site in the corner of
      // the map must say so from the FIRST day, and a site by the village
      // must never say it. Checked by damage — the same run raises both.
      if (day == 0) {
        failures += run::Expect(far_site == (unreachable == 1),
                                far_site ? "a site eleven kilometres out says so on day zero"
                                         : "and a site by the village never says it");
      }
      if (unit.construction.labor_days_remaining != last_seam || day < 3 || unit.level > 0) {
        std::cout << "idle_curve:   тревоги — без бригады " << crewless << ", НЕДОСТИЖИМА "
                  << unreachable << " (дороги " << road << " ч в одну сторону)\n";
        std::cout << "idle_curve: сутки " << day << " ступень " << static_cast<int>(unit.level)
                  << " фаза " << static_cast<int>(unit.construction.phase) << " шов "
                  << unit.construction.labor_days_remaining << " бригада " << crew << '\n';
        last_seam = unit.construction.labor_days_remaining;
      }
      if (unit.level > 0) {
        std::cout << "idle_curve: ПОСТРОЕНА на сутках " << day << ", БЕЗ kUpgradeUnit\n";
        return failures;
      }
    }
    std::cout << "idle_curve: за двести суток НЕ ПОСТРОЕНА"
              << (far_site ? " — и это правильно: до неё не дойти\n" : "\n");
    return far_site ? failures : failures + 1;
  }
  run::YardPolicy yard(*world.tables);
  run::FixturePolicy fixture(*world.tables);
  run::OrdersPolicy orders;
  run::RepairPolicy repairs(*world.tables);

  if (herd_alarm) {
    const core::ITable* const kinds = world.tables->FindTable("livestock");
    const std::uint32_t horse = kinds == nullptr ? core::kNoTableRow : kinds->FindRowByKey("horse");
    const core::ITable* const types = world.tables->FindTable("unit_types");
    const std::uint32_t yard_type =
        types == nullptr ? core::kNoTableRow : types->FindRowByKey("horse_yard");
    if (horse == core::kNoTableRow || yard_type == core::kNoTableRow) {
      std::cout << "FAIL: no horse or horse_yard in the tables\n";
      return 1;
    }
    const auto heads = [&](const core::WorldState& state) {
      std::uint32_t count = 0;
      for (const core::HerdRow& herd : state.herds.rows) {
        if (herd.kind.value == horse && herd.household_owned == 0) {
          count += static_cast<std::uint32_t>(herd.adult_count) + herd.juvenile_count +
                   herd.newborn_count;
        }
      }
      return count;
    };
    const std::uint32_t start_heads = heads(world.State());
    {
      std::uint32_t rows = 0;
      for (const core::HerdRow& herd : world.State().herds.rows) {
        if (herd.kind.value == horse && herd.household_owned == 0) {
          ++rows;
          std::cout << "idle_curve:   табун-строка " << rows << " — взрослых " << herd.adult_count
                    << ", сумма лет " << herd.adult_age_game_years_total << '\n';
        }
      }
      std::cout << "idle_curve: колхозных конских строк на старте " << rows << '\n';
    }
    std::int32_t lit = -1;
    std::int32_t first_loss = -1;
    std::int32_t went_out = -1;
    std::int32_t relit = -1;
    std::int32_t stable_day = -1;
    std::int32_t groom_day = -1;
    std::int32_t gone = -1;
    std::int32_t groom_silenced_yard = -1;
    bool burning = false;
    const std::uint32_t days = 3U * core::kDaysPerYear;
    for (std::uint32_t day = 0; day < days; ++day) {
      if (world.State().calendar.day >= yard_delay) {
        yard.RunDay(*world.simulation);
      }
      for (std::uint32_t tick = 0; tick < core::kTicksPerDay; ++tick) {
        world->AdvanceStep();
      }
      const core::WorldState& state = world.State();
      const auto today = static_cast<std::int32_t>(state.calendar.day);
      std::vector<core::Alarm> alarms;
      world->CollectAlarms(alarms);
      bool on = false;
      bool yard_alarm = false;
      std::int64_t left = 0;
      for (const core::Alarm& alarm : alarms) {
        if (alarm.kind == core::AlarmKind::kHerdWithoutStable) {
          on = true;
          left = alarm.amount;
        }
        yard_alarm = yard_alarm || alarm.kind == core::AlarmKind::kYardWithoutGroom;
      }
      if (on && lit < 0) {
        lit = today;
        std::cout << "idle_curve: ТАБУН БЕЗ КОНЮШНИ загорелась на сутках " << today << ", голов "
                  << left << '\n';
        // AND IT IS THE TEAM IT COUNTS, not the byre. The kind filter and the
        // ownership filter are both invisible to a check that only asks
        // whether something burns: damage either one and the alarm still
        // burns, on thirty-nine head of cattle instead of sixteen horses.
        failures += run::Expect(left == static_cast<std::int64_t>(start_heads),
                                "the alarm counts the kolkhoz horse team and nothing else");
      }
      if (!on && burning && went_out < 0) {
        went_out = today;
      }
      if (on && went_out >= 0 && relit < 0) {
        relit = today;
      }
      burning = on;
      const std::uint32_t now = heads(state);
      if (first_loss < 0 && now < start_heads) {
        first_loss = today;
      }
      if (gone < 0 && now == 0) {
        gone = today;
      }
      for (const core::UnitRow& unit : state.units.rows) {
        if (unit.type.value == yard_type && unit.level >= 2 && stable_day < 0) {
          stable_day = today;
        }
      }
      // THE GROOM IS THE POINT OF THE WHOLE KIND: the day kYardWithoutGroom
      // stops sounding is the day the player is told he is done. Whether the
      // team is still dying on that day is what the new kind answers.
      if (groom_day < 0 && lit >= 0 && !yard_alarm && groom_silenced_yard < 0) {
        groom_silenced_yard = today;
      }
    }
    std::cout << "idle_curve: голов на старте " << start_heads << ", в конце "
              << heads(world.State()) << "; конюшня " << stable_day << ", первая потеря "
              << first_loss << ", табун сгинул " << gone << '\n';
    std::cout << "idle_curve: тревога — загорелась " << lit << ", погасла " << went_out
              << ", загорелась снова " << relit << "; «двор без конюха» замолк на сутках "
              << groom_silenced_yard << '\n';
    // THE GUARDS. Without a stable the kind must light and stay lit to the
    // last day; with one it must be out at the end. And it must NOT be the
    // groom that puts it out: the day the yard's own alarm falls silent, this
    // one is still burning.
    if (stable_day < 0) {
      failures += run::Expect(lit >= 0, "with no stable the herd alarm lights");
      failures += run::Expect(burning, "and it is still burning on the last day");
      failures += run::Expect(went_out < 0, "and it never went out in between");
      failures +=
          run::Expect(groom_silenced_yard < 0 || lit <= groom_silenced_yard,
                      "the groom's silence does not put it out — it is already burning by then");
    } else {
      failures += run::Expect(!burning, "once the stable stands the herd alarm is out");
    }
    std::cout << (failures == 0 ? "idle_curve: all checks passed\n" : "idle_curve: FAILED\n");
    return failures;
  }

  // DAY ZERO, before anything has had a chance to die: the canon says
  // sixteen kolkhoz horses stand in private yards from the first morning
  // (start canon §11), and the claim under test is that the core has none.
  {
    const core::WorldState& dawn = world.State();
    const core::ITable* const kinds = world.tables->FindTable("livestock");
    const std::uint32_t horse = kinds == nullptr ? core::kNoTableRow : kinds->FindRowByKey("horse");
    std::uint32_t adults = 0;
    std::uint32_t billeted = 0;
    std::uint32_t owned_by_family = 0;
    for (const core::HerdRow& herd : dawn.herds.rows) {
      if (horse == core::kNoTableRow || herd.kind.value != horse) {
        continue;
      }
      adults += herd.adult_count;
      billeted += herd.household.value != core::kInvalidEntityIdValue ? herd.adult_count : 0U;
      owned_by_family += herd.household_owned != 0 ? herd.adult_count : 0U;
    }
    // And the other half of the canon: ONE ADULT PER HORSE is held to horse
    // work and cannot be sent elsewhere (MarkHorseHosts). Counted here the
    // way the labour model counts it — an adult of a family that hosts one.
    std::uint32_t hosts = 0;
    for (const core::HerdRow& herd : dawn.herds.rows) {
      if (horse == core::kNoTableRow || herd.kind.value != horse ||
          herd.household.value == core::kInvalidEntityIdValue) {
        continue;
      }
      std::uint16_t left = herd.adult_count;
      for (const core::ResidentRow& resident : dawn.residents.rows) {
        if (left == 0) {
          break;
        }
        if (resident.family.value == herd.household.value &&
            core::BiologicalAgeYears(rules.life_speedup, resident.birth_day, dawn.calendar.day) >=
                16.0F) {
          ++hosts;
          --left;
        }
      }
    }
    std::cout << "idle_curve: нулевые сутки — лошадей взрослых " << adults << ", из них стоят по "
              << "дворам " << billeted << ", в личной собственности " << owned_by_family
              << "; при лошадях держат по взрослому " << hosts << " дворов\n";
  }
  std::cout << "idle_curve: seed " << seed << ", " << kYears << " years"
            << (no_chairman ? ", БЕЗ ПРЕДСЕДАТЕЛЯ" : (yard_only ? ", ТОЛЬКО КОНЮШНЯ" : "")) << '\n';
  std::cout << "idle_curve: год | жителей | рабочего возраста | работали | бездельничали | "
               "стояло работы, чел-дней (среднее за год)\n";
  // BY SEASON, because boss's second instrument found idleness sitting on
  // the busy season and asked whether that is a distribution defect. It is
  // only a distribution defect if there was work for those hands: the
  // standing seam against the working-age count in the SAME season is what
  // says so, and comparing one season with another cannot.
  std::array<double, 4> seam_by_season{};
  std::array<std::uint64_t, 4> idle_by_season{};
  std::array<std::uint64_t, 4> work_by_season{};
  std::array<std::uint64_t, 4> hands_by_season{};
  // HIS DENOMINATOR, not a second one. The other instrument counts the
  // share of an able adult's WORKING DAY: the hours that belonged to
  // idleness, work, the road, "nothing to work with" and truancy, and no
  // others. Two instruments that disagree because their denominators differ
  // have not disagreed about the world at all, and saying so would be a
  // false alarm dressed as a finding.
  std::array<std::uint64_t, 4> day_by_season{};
  // BY AGE BAND, because the other instrument found the young idling twice
  // as much as the old on all four seeds and that is not explained by there
  // being too little work: too little work falls on everybody.
  static constexpr std::array<float, 5> kBandFrom = {16.0F, 26.0F, 36.0F, 46.0F, 56.0F};
  std::array<std::uint64_t, 5> idle_by_band{};
  std::array<std::uint64_t, 5> day_by_band{};
  // AND BY ROW, which is the discriminator. At placement_level 0 — the
  // shipped value, "the start has no accountant and the chairman places
  // naively" — every candidate scores zero and the sort falls through to
  // its stable tiebreaker: the RESIDENT'S ROW, ascending. Rows are appended
  // at birth, so a fixed queue in birth order is exactly what a village of
  // growing population would see as "the young never work".
  //
  // Age and row are correlated, so a gradient by age proves nothing on its
  // own. If the gradient follows the ROW at least as tightly, the cause is
  // the queue and not the years.
  std::array<std::uint64_t, 5> idle_by_fifth{};
  std::array<std::uint64_t, 5> day_by_fifth{};
  for (std::uint32_t year = 0; year < kYears; ++year) {
    std::uint64_t worked = 0;
    std::uint64_t idled = 0;
    std::uint64_t blocked = 0;
    std::uint64_t walking = 0;
    std::uint64_t truant = 0;
    double seam_sum = 0.0;
    std::uint32_t samples = 0;
    std::uint32_t of_age = 0;
    for (std::uint32_t day = 0; day < core::kDaysPerYear; ++day) {
      if (!no_chairman) {
        if (world.State().calendar.day >= yard_delay) {
          yard.RunDay(*world.simulation);
        }
        if (!yard_only) {
          fixture.RunDay(*world.simulation);
          orders.RunDay(*world.simulation);
          repairs.RunDay(*world.simulation);
        }
      }
      for (std::uint32_t tick = 0; tick < core::kTicksPerDay; ++tick) {
        world->AdvanceStep();
        const core::WorldState& state = world.State();
        for (std::uint32_t row = 0; row < state.residents.rows.size(); ++row) {
          const core::ResidentActivity what = core::ActivityOfResident(state, row, rules).activity;
          worked += what == core::ResidentActivity::kWorking ? 1U : 0U;
          idled += what == core::ResidentActivity::kIdle ? 1U : 0U;
          blocked += what == core::ResidentActivity::kBlocked ? 1U : 0U;
          walking += what == core::ResidentActivity::kWalking ? 1U : 0U;
          truant += what == core::ResidentActivity::kTruant ? 1U : 0U;
          const auto now = static_cast<std::size_t>(state.calendar.season);
          if (now < idle_by_season.size()) {
            idle_by_season[now] += what == core::ResidentActivity::kIdle ? 1U : 0U;
            work_by_season[now] += what == core::ResidentActivity::kWorking ? 1U : 0U;
            const float years = core::BiologicalAgeYears(
                rules.life_speedup, state.residents.rows[row].birth_day, state.calendar.day);
            std::size_t band = kBandFrom.size();
            for (std::size_t index = 0; index < kBandFrom.size(); ++index) {
              if (years >= kBandFrom[index]) {
                band = index;
              }
            }
            const bool counted =
                what == core::ResidentActivity::kIdle || what == core::ResidentActivity::kWorking ||
                what == core::ResidentActivity::kWalking ||
                what == core::ResidentActivity::kBlocked || what == core::ResidentActivity::kTruant;
            if (band < kBandFrom.size() && counted && years < 70.0F) {
              ++day_by_band[band];
              idle_by_band[band] += what == core::ResidentActivity::kIdle ? 1U : 0U;
              const std::size_t fifth =
                  std::min<std::size_t>(4,
                                        (static_cast<std::size_t>(row) * 5) /
                                            std::max<std::size_t>(1, state.residents.rows.size()));
              ++day_by_fifth[fifth];
              idle_by_fifth[fifth] += what == core::ResidentActivity::kIdle ? 1U : 0U;
            }
            day_by_season[now] += what == core::ResidentActivity::kIdle ||
                                          what == core::ResidentActivity::kWorking ||
                                          what == core::ResidentActivity::kWalking ||
                                          what == core::ResidentActivity::kBlocked ||
                                          what == core::ResidentActivity::kTruant
                                      ? 1U
                                      : 0U;
          }
        }
      }
      const core::WorldState& evening = world.State();
      const auto season = static_cast<std::size_t>(evening.calendar.season);
      if (season < seam_by_season.size()) {
        seam_by_season[season] += static_cast<double>(SeamsStanding(evening));
        ++hands_by_season[season];
      }
      seam_sum += static_cast<double>(SeamsStanding(evening));
      ++samples;
      of_age = 0;
      for (const core::ResidentRow& resident : evening.residents.rows) {
        const float age =
            core::BiologicalAgeYears(rules.life_speedup, resident.birth_day, evening.calendar.day);
        of_age += age >= rules.work_from_bio_years && age < rules.work_to_bio_years ? 1U : 0U;
      }
    }
    const core::WorldState& done = world.State();
    // THE ONE BINDING CLAIM, and it is the trap this run found: a year in
    // which NOBODY worked while work stood is a village that cannot get
    // out. Ploughing needs a horse and a man cannot pull a plough
    // (assignment.cpp), so a settlement that loses its last draught horse
    // stalls every arable field in kPlowing for ever — no ploughing, no
    // sowing, no harvest, no oats, no horses. Nothing in the model breaks
    // that circle, and "no way out" is a red line of the design.
    if (worked == 0 && seam_sum / (samples == 0 ? 1 : samples) > 1.0) {
      failures += run::Expect(false,
                              "a year with work standing and nobody working at all: the village "
                              "has no way out of this, and that is a red line");
    }
    std::cout << "idle_curve: " << (year + 1) << " | " << done.residents.rows.size() << " | "
              << of_age << " | " << worked << " | " << idled << " | "
              << (seam_sum / (samples == 0 ? 1 : samples)) << " | назначено-но-нечем " << blocked
              << " | в дороге " << walking << " | прогул " << truant;
    {
      // The team, year by year: the other instrument sees no foal in four
      // hundred days, and four hundred days is eight and a third years.
      const core::ITable* const kinds = world.tables->FindTable("livestock");
      const std::uint32_t horses_kind =
          kinds == nullptr ? core::kNoTableRow : kinds->FindRowByKey("horse");
      std::uint32_t adults = 0;
      std::uint32_t sires = 0;
      std::uint32_t young = 0;
      for (const core::HerdRow& herd : done.herds.rows) {
        if (horses_kind == core::kNoTableRow || herd.kind.value != horses_kind) {
          continue;
        }
        adults += herd.adult_count;
        sires += herd.adult_male_count;
        young += static_cast<std::uint32_t>(herd.newborn_count) + herd.juvenile_count;
      }
      std::cout << " | лошадей " << adults << "/" << sires << " жеребцов, молодняк " << young;
    }
    // WHAT RUNS OUT FIRST, and it is printed only under the control arm
    // (2026-09-12, boss's question). The arm's red line says the team can
    // die; it does not say WHY, and "the herd died" has four different
    // repairs depending on the cause. The year's closed book carries the
    // causes apart: births, deaths by age, deaths by hunger, the cull, the
    // hay cut against the hay eaten, and the man-days that went to the
    // plough instead. Measured answer: the HAY, and not because there is too
    // little meadow — because a closed window outranks an open one
    // (labor_system.cpp, DaysLeftInWindow) and the village ploughs ground it
    // can no longer sow while the grass stands uncut.
    if (sow_idle_land) {
      const core::YearLedger& book = done.ledger.closed;
      const core::ITable* const resources = world.tables->FindTable("resources");
      const auto stock_of = [&done](std::uint32_t resource_row) {
        core::Grams total = 0;
        if (resource_row == core::kNoTableRow) {
          return total;
        }
        for (const core::UnitRow& unit : done.units.rows) {
          if (unit.stock.size() > resource_row) {
            total += unit.stock[resource_row];
          }
        }
        return total;
      };
      const std::uint32_t hay =
          resources == nullptr ? core::kNoTableRow : resources->FindRowByKey("hay");
      const std::uint32_t oat =
          resources == nullptr ? core::kNoTableRow : resources->FindRowByKey("oat");
      std::cout << "\nidle_curve:   год " << (year + 1) << " — приплод " << book.herd_births
                << ", пало от старости " << book.herd_deaths_age << ", от бескормицы "
                << book.herd_deaths_hunger << ", забито " << book.herd_culled
                << ", голодных голово-дней " << book.herd_hungry_head_days << "; сена "
                << (stock_of(hay) / 1000000) << " ц, овса " << (stock_of(oat) / 1000000)
                << " ц; рабочий паёк " << done.traction_ration << "; посеяно " << book.area_sown_ha
                << " га, убрано " << book.area_harvested_ha << " га";
      const core::ITable* const kinds2 = world.tables->FindTable("livestock");
      const std::uint32_t horse_row =
          kinds2 == nullptr ? core::kNoTableRow : kinds2->FindRowByKey("horse");
      float age_total = 0.0F;
      std::uint32_t horse_adults = 0;
      float unfed_max = 0.0F;
      float care_left_horses = 0.0F;
      std::uint32_t horse_newborn = 0;
      for (const core::HerdRow& herd : done.herds.rows) {
        if (horse_row == core::kNoTableRow || herd.kind.value != horse_row) {
          continue;
        }
        horse_adults += herd.adult_count;
        age_total += herd.adult_age_game_years_total;
        unfed_max = herd.unfed_days > unfed_max ? herd.unfed_days : unfed_max;
        care_left_horses += herd.care_days_remaining;
        horse_newborn += static_cast<std::uint32_t>(herd.newborn_count) + herd.juvenile_count;
      }
      std::cout << "; ЛОШАДИ: взрослых " << horse_adults << ", средний возраст "
                << (horse_adults == 0 ? 0.0F : age_total / static_cast<float>(horse_adults))
                << ", молодняк " << horse_newborn << ", без корма дней " << unfed_max
                << ", ухода не сделано " << care_left_horses;
      const auto column = [](const core::ResourceAmounts& amounts, std::uint32_t row) {
        return row != core::kNoTableRow && amounts.size() > row ? amounts[row] : core::Grams{0};
      };
      // AND THE TABLE, because the arm's newest question is why the opened
      // land carries FEWER people while doing more work (boss, 2026-09-12).
      float satiety_sum = 0.0F;
      float health_sum = 0.0F;
      for (const core::ResidentRow& resident : done.residents.rows) {
        satiety_sum += resident.satiety;
        health_sum += resident.health;
      }
      const auto people = static_cast<float>(done.residents.rows.size());
      std::cout << "; сытость " << (people > 0.0F ? satiety_sum / people : 0.0F) << ", здоровье "
                << (people > 0.0F ? health_sum / people : 0.0F) << ", ожидаемая жизнь "
                << done.vitals.life_expectancy_years << ", родилось " << book.births << ", умерло "
                << book.deaths;
      std::cout << "; СЕНО: накошено " << (column(book.harvest, hay) / 1000000) << " ц, съедено "
                << (column(book.feed, hay) / 1000000) << " ц; уборка и косьба "
                << book.work_days_by_kind[static_cast<std::size_t>(core::WorkKind::kHarvest)]
                << " чел-дней, пахота "
                << book.work_days_by_kind[static_cast<std::size_t>(core::WorkKind::kPlowing)]
                << ", сев "
                << book.work_days_by_kind[static_cast<std::size_t>(core::WorkKind::kSowing)];
    }
    std::cout << '\n';
  }
  // THE SEASONAL ARITHMETIC, and it answers the question boss's second
  // instrument raised: is the busy season's idleness a matter of who was
  // picked, or was there never work for those hands in the first place?
  static constexpr std::array<std::string_view, 4> kSeasons = {"зима", "весна", "лето", "осень"};
  std::cout << "idle_curve: сезон | стояло работы, чел-дней | работали, чел-часов | "
               "бездельничали | доля простоя\n";
  for (std::size_t index = 0; index < kSeasons.size(); ++index) {
    const double days = hands_by_season[index] == 0
                            ? 0.0
                            : seam_by_season[index] / static_cast<double>(hands_by_season[index]);
    const double total = static_cast<double>(day_by_season[index]);
    const double share = total == 0.0 ? 0.0 : static_cast<double>(idle_by_season[index]) / total;
    std::cout << "idle_curve: " << kSeasons[index] << " | " << days << " | "
              << work_by_season[index] << " | " << idle_by_season[index] << " | " << share << '\n';
  }

  static constexpr std::array<std::string_view, 5> kBandName = {
      "16-25", "26-35", "36-45", "46-55", "56-70"};
  std::cout << "idle_curve: полоса | доля простоя\n";
  for (std::size_t index = 0; index < kBandName.size(); ++index) {
    const double total = static_cast<double>(day_by_band[index]);
    std::cout << "idle_curve: " << kBandName[index] << " | "
              << (total == 0.0 ? 0.0 : static_cast<double>(idle_by_band[index]) / total) << '\n';
  }

  std::cout << "idle_curve: пятина строк | доля простоя\n";
  for (std::size_t index = 0; index < day_by_fifth.size(); ++index) {
    const double total = static_cast<double>(day_by_fifth[index]);
    std::cout << "idle_curve: " << (index + 1) << " | "
              << (total == 0.0 ? 0.0 : static_cast<double>(idle_by_fifth[index]) / total) << '\n';
  }

  // WHAT IS STANDING AT THE END, field by field. A seam that neither
  // drains nor changes for years is not "work waiting" — it is work nobody
  // can be sent to, and the difference is the whole question.
  const core::WorldState& last = world.State();
  const core::ITableSet* const last_tables = world.tables.get();
  // DID ANYTHING ACTUALLY GET BUILT? The other instrument reports that a
  // site never finishes on any core, ever. This run's chairman raises the
  // horse yard THROUGH THE ORDER BOOK — kBuildUnit, then kStartBuild, then
  // kUpgradeUnit — so if construction never completed, the yard would still
  // be at level 0 and there would be no draught horses at all.
  const core::ITable* const types = last_tables->FindTable("unit_types");
  const std::uint32_t yard_type =
      types == nullptr ? core::kNoTableRow : types->FindRowByKey("horse_yard");
  for (const core::UnitRow& unit : last.units.rows) {
    if (yard_type != core::kNoTableRow && unit.type.value == yard_type) {
      std::cout << "idle_curve: конюшня — ступень " << static_cast<int>(unit.level)
                << ", фаза стройки " << static_cast<int>(unit.construction.phase) << ", осталось "
                << unit.construction.labor_days_remaining << " чел-дней\n";
    }
  }
  std::uint32_t built = 0;
  for (const core::UnitRow& unit : last.units.rows) {
    built += unit.level > 0 ? 1U : 0U;
  }
  std::cout << "idle_curve: юнитов стоит " << built << " из " << last.units.rows.size() << '\n';

  // Draught horses specifically: ploughing cannot be done without one, and
  // a job that merely PREFERS a horse still takes one (assignment.cpp).
  const core::ITable* const livestock = last_tables->FindTable("livestock");
  const std::uint32_t horse_row =
      livestock == nullptr ? core::kNoTableRow : livestock->FindRowByKey("horse");
  std::uint32_t horses = 0;
  std::uint32_t all_head = 0;
  for (const core::HerdRow& herd : last.herds.rows) {
    all_head += herd.adult_count;
    if (horse_row != core::kNoTableRow && herd.kind.value == horse_row) {
      horses += herd.adult_count;
    }
  }
  std::uint32_t mowing = 0;
  std::uint32_t ploughing = 0;
  for (const core::FieldRow& field : last.fields.rows) {
    const bool meadow =
        field.kind == core::LandKind::kMeadow || field.kind == core::LandKind::kFloodplainMeadow;
    mowing += meadow && field.work_days_remaining > 0.0F ? 1U : 0U;
    ploughing += field.kind == core::LandKind::kArable &&
                         field.phase == core::FieldPhase::kPlowing &&
                         field.work_days_remaining > 0.0F
                     ? 1U
                     : 0U;
  }
  std::cout << "idle_curve: в конце — голов всего " << all_head << ", ТЯГЛОВЫХ ЛОШАДЕЙ " << horses
            << "; лугов под косой " << mowing << ", пашни под плугом " << ploughing << '\n';
  // THE TEAM ITSELF, head by head. The other instrument found that horses
  // never foal in four hundred days and that the stable changes nothing;
  // the "why" is this side's. A herd with no sire cannot breed, and a herd
  // that never breeds never grows, and the sire count is re-derived only
  // where heads GROW UP.
  for (std::uint32_t row = 0; row < last.herds.rows.size(); ++row) {
    const core::HerdRow& herd = last.herds.rows[row];
    if (horse_row == core::kNoTableRow || herd.kind.value != horse_row) {
      continue;
    }
    std::cout << "idle_curve: табун " << row << " — взрослых " << herd.adult_count << ", из них "
              << "жеребцов " << herd.adult_male_count << ", молодняк "
              << (herd.newborn_count + herd.juvenile_count) << ", на постое " << herd.billeted_count
              << ", при дворе " << (herd.household.value != core::kInvalidEntityIdValue ? 1 : 0)
              << ", у юнита " << (herd.unit.value != core::kInvalidEntityIdValue ? 1 : 0) << '\n';
  }

  // WHY NOBODY IS SENT is the question, and there are only so many ways to
  // be unsendable: no house (a man with no home has no day to travel from),
  // too young, or a job that needs a horse and no horse free.
  std::uint32_t homeless = 0;
  std::uint32_t of_age = 0;
  for (const core::ResidentRow& resident : last.residents.rows) {
    const float age =
        core::BiologicalAgeYears(rules.life_speedup, resident.birth_day, last.calendar.day);
    if (!(age >= rules.work_from_bio_years && age < rules.work_to_bio_years)) {
      continue;
    }
    ++of_age;
    core::Vec2 home{};
    homeless += core::HomePositionOf(last, resident.family, home) ? 0U : 1U;
  }
  std::cout << "idle_curve: в конце — " << last.herds.rows.size() << " стад, взрослых голов "
            << horses << "; рабочего возраста " << of_age << ", из них БЕЗ ДОМА " << homeless
            << '\n';
  for (std::uint32_t row = 0; row < last.fields.rows.size(); ++row) {
    const core::FieldRow& field = last.fields.rows[row];
    if (field.work_days_remaining <= 0.0F && field.haul_days_remaining <= 0.0F) {
      continue;
    }
    std::cout << "idle_curve:   поле " << row << " вид " << static_cast<int>(field.kind) << " фаза "
              << static_cast<int>(field.phase) << " га " << field.area_ga << " работы "
              << field.work_days_remaining << " подвоза " << field.haul_days_remaining << " лежит "
              << field.reaped_grams << '\n';
  }
  std::cout << (failures == 0 ? "idle_curve: all checks passed\n" : "idle_curve: FAILED\n");
  return failures;
}
