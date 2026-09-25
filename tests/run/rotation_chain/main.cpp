// Simulation run: a chain the chairman names reaches the ground in the order
// he named it, winter crops included (0.36.10; boss, boss-core-epoch1-resume
// [12], econ's fallow map, forgiving-start §5з).
//
// THE KNOWN ANSWER, on the real start with its real hands. Years are counted
// as the campaign counts them, the first year being year 1.
//   A. Named in February of year 2: winter rye, oats, potatoes. The rye goes
//      in that autumn and is reaped in year 3; the oats are sown in the
//      spring of year 4; the potatoes in year 5. Until 0.36.10 the rye's
//      autumn sowing spent the chain's standing mark, the turn into year 3
//      carried the chain on while the rye still stood, and the oats' only
//      spring went by under the rye: the oats were never sown at all.
//   B. Named in August of year 2: oats, winter rye, potatoes. The oats named
//      first wait for their spring, year 3; the rye goes in that autumn and
//      is reaped in year 4; the potatoes in year 5. Until 0.36.10 the oats'
//      window, past in August, read as "the first slot gave up", the field
//      was worked for the rye that September, and the oats came round only
//      two years later.
//
// Each field is one of the start's arable fields, idle on the day of the
// order; the run prints every sowing and every reaping of the two fields.

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "../common/run_harness.h"
#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace {

constexpr std::uint64_t kSeed = 1929;

/// The campaign's year, counted from 1, and the 0-based month of a day.
std::uint32_t YearOf(core::SimDay day) {
  return (day / core::kDaysPerYear) + 1U;
}

std::uint32_t MonthOf(core::SimDay day) {
  return (day % core::kDaysPerYear) / core::kDaysPerMonth;
}

/// One sowing or reaping of a watched field.
struct Mark {
  bool sown = false;  ///< false: reaped
  std::uint16_t crop = 0;
  std::uint32_t year = 0;
  std::uint32_t month = 0;
};

/// A field under watch: the last phase and crop seen, and what happened.
struct Watch {
  core::FieldId id;
  const char* label = "";
  core::FieldPhase phase = core::FieldPhase::kIdle;
  core::CropId crop;
  core::SimDay reaped_day = core::kNeverReapedDay;
  std::vector<Mark> marks;
};

/// Starts a watch where the field stands: last year's reaping is not an event
/// of the chain.
void Begin(const core::WorldState& world, Watch& watch) {
  const std::uint32_t row = core::FindRow(world.fields, watch.id);
  if (row == core::kNoRow) {
    return;
  }
  watch.phase = world.fields.rows[row].phase;
  watch.crop = world.fields.rows[row].crop;
  watch.reaped_day = world.fields.rows[row].reaped_day;
}

void Observe(const core::WorldState& world, Watch& watch) {
  const std::uint32_t row = core::FindRow(world.fields, watch.id);
  if (row == core::kNoRow) {
    return;
  }
  const core::FieldRow& field = world.fields.rows[row];
  const core::SimDay day = world.calendar.day;
  const bool sown_now = field.phase == core::FieldPhase::kGrowing &&
                        watch.phase != core::FieldPhase::kGrowing &&
                        field.crop.value != core::kInvalidDefIdValue;
  if (sown_now) {
    watch.marks.push_back(
        Mark{.sown = true, .crop = field.crop.value, .year = YearOf(day), .month = MonthOf(day)});
  }
  if (field.reaped_day != watch.reaped_day && field.reaped_day != core::kNeverReapedDay) {
    watch.marks.push_back(
        Mark{.sown = false, .crop = watch.crop.value, .year = YearOf(day), .month = MonthOf(day)});
  }
  watch.phase = field.phase;
  watch.crop = field.crop;
  watch.reaped_day = field.reaped_day;
}

/// An arable field of the start, idle today, told something already, not
/// the reserve, and not `other`.
core::FieldId IdleField(const core::WorldState& world, core::FieldId other) {
  for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
    const core::FieldRow& field = world.fields.rows[row];
    if (field.kind == core::LandKind::kArable && core::HasRotation(field) &&
        field.start_reserve == 0 && field.phase == core::FieldPhase::kIdle &&
        field.crop.value == core::kInvalidDefIdValue &&
        world.fields.row_ids[row].value != other.value) {
      return world.fields.row_ids[row];
    }
  }
  return core::FieldId{};
}

void Name(run::Simulation& world, core::FieldId field, std::array<core::CropId, 3> chain) {
  core::OrderRow order;
  order.kind = core::OrderKind::kSetRotation;
  order.field = field;
  order.rotation_year0 = chain[0];
  order.rotation_year1 = chain[1];
  order.rotation_year2 = chain[2];
  world->StageOrders(std::span<const core::OrderRow>(&order, 1), {});
}

/// The first mark of `crop`, sown or reaped, from `from_year` on; year 0 when
/// there is none.
Mark First(const Watch& watch, bool sown, std::uint16_t crop, std::uint32_t from_year) {
  for (const Mark& mark : watch.marks) {
    if (mark.sown == sown && mark.crop == crop && mark.year >= from_year) {
      return mark;
    }
  }
  return Mark{};
}

}  // namespace

int main() {
  int failures = 0;
  run::Simulation world = run::Start(kSeed);
  if (!world) {
    return 1;
  }
  const core::ITable* const crops = world.tables->FindTable("crops");
  const auto crop = [crops](const char* key) {
    const std::uint32_t row = crops != nullptr ? crops->FindRowByKey(key) : core::kNoTableRow;
    return core::CropId{static_cast<std::uint16_t>(row)};
  };
  const core::CropId rye = crop("rye_winter");
  const core::CropId oat = crop("oat");
  const core::CropId potato = crop("potato");
  if (run::Expect(crops != nullptr && rye.value != core::kInvalidDefIdValue &&
                      oat.value != core::kInvalidDefIdValue &&
                      potato.value != core::kInvalidDefIdValue,
                  "the crop table names winter rye, oats and potatoes") != 0) {
    return 1;
  }

  // Year 2, February: the first chain. (Day 48 is the first of year 2; four
  // days a month, so the fifth day of the year is February's first.)
  constexpr core::SimDay kFebruaryYear2 = core::kDaysPerYear + core::kDaysPerMonth;
  constexpr core::SimDay kAugustYear2 = core::kDaysPerYear + (7U * core::kDaysPerMonth);
  constexpr core::SimDay kEnd = 5U * core::kDaysPerYear;

  Watch first;
  first.label = "A (rye, oats, potatoes), named in February of year 2";
  Watch second;
  second.label = "B (oats, rye, potatoes), named in August of year 2";

  while (world.State().calendar.day < kEnd) {
    const core::SimDay day = world.State().calendar.day;
    if (day == kFebruaryYear2 && first.id.value == core::kInvalidEntityIdValue &&
        world.State().calendar.tick % core::kTicksPerDay == 0) {
      first.id = IdleField(world.State(), core::FieldId{});
      if (run::Expect(first.id.value != core::kInvalidEntityIdValue,
                      "an idle arable field of the start in February of year 2") != 0) {
        return failures + 1;
      }
      Name(world, first.id, {rye, oat, potato});
      Begin(world.State(), first);
    }
    if (day == kAugustYear2 && second.id.value == core::kInvalidEntityIdValue &&
        world.State().calendar.tick % core::kTicksPerDay == 0) {
      second.id = IdleField(world.State(), first.id);
      if (run::Expect(second.id.value != core::kInvalidEntityIdValue,
                      "an idle arable field of the start in August of year 2") != 0) {
        return failures + 1;
      }
      Name(world, second.id, {oat, rye, potato});
      Begin(world.State(), second);
    }
    world->AdvanceStep();
    if (first.id.value != core::kInvalidEntityIdValue) {
      Observe(world.State(), first);
    }
    if (second.id.value != core::kInvalidEntityIdValue) {
      Observe(world.State(), second);
    }
  }

  const auto name_of = [&](std::uint16_t value) -> std::string {
    if (value == rye.value) {
      return "rye";
    }
    if (value == oat.value) {
      return "oats";
    }
    if (value == potato.value) {
      return "potatoes";
    }
    return "crop " + std::to_string(value);
  };
  for (const Watch* watch : {&first, &second}) {
    std::cout << "rotation_chain: field " << watch->id.value << ", " << watch->label << ":";
    for (const Mark& mark : watch->marks) {
      std::cout << ' ' << (mark.sown ? "sown " : "reaped ") << name_of(mark.crop) << " y"
                << mark.year << "m" << (mark.month + 1);
    }
    std::cout << " (" << watch->marks.size() << " events)\n";
  }

  // -- A: the winter crop named first -----------------------------------------
  const Mark a_rye_sown = First(first, true, rye.value, 2);
  const Mark a_rye_reaped = First(first, false, rye.value, 2);
  const Mark a_oat_sown = First(first, true, oat.value, 2);
  const Mark a_potato_sown = First(first, true, potato.value, 2);
  failures += run::Expect(a_rye_sown.year == 2 && a_rye_sown.month >= 7,
                          "A: the rye named first goes in the autumn of year 2");
  failures += run::Expect(a_rye_reaped.year == 3, "A: and is reaped in year 3");
  failures += run::Expect(a_oat_sown.year == 4 && a_oat_sown.month <= 5,
                          "A: the oats named second are sown in the spring of year 4 — the first "
                          "spring the rye leaves free");
  failures += run::Expect(a_potato_sown.year == 5, "A: the potatoes named third in year 5");

  // -- B: a spring crop named first in August, a winter crop second ------------
  const Mark b_oat_sown = First(second, true, oat.value, 2);
  const Mark b_rye_sown = First(second, true, rye.value, 2);
  const Mark b_rye_reaped = First(second, false, rye.value, 3);
  const Mark b_potato_sown = First(second, true, potato.value, 2);
  failures += run::Expect(b_oat_sown.year == 3 && b_oat_sown.month <= 5,
                          "B: the oats named first in August wait for their spring, year 3");
  failures += run::Expect(b_rye_sown.year == 3 && b_rye_sown.month >= 7,
                          "B: the rye named second goes in the autumn of year 3, after the oats");
  failures += run::Expect(b_rye_reaped.year == 4, "B: and is reaped in year 4");
  failures += run::Expect(b_potato_sown.year == 5, "B: the potatoes named third in year 5");

  std::cout << (failures == 0 ? "rotation_chain: all checks passed\n"
                              : "rotation_chain: FAILURES ABOVE\n");
  return failures;
}
