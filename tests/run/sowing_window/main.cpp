// Simulation run: WHAT DOES "THE SOWING WILL NOT FIT" LOOK LIKE FROM OUTSIDE,
// and how many days before the loss can it honestly be said?
//
// Boss entered the alarm `sowing_will_not_fit` into the design and left the
// PREDICATE to be measured here (parcel 94, 2026-09-13): "гектары, вошедшие в
// пахоту, против гектаров, которые окно ещё позволяет засеять". The name is an
// echo of kHarvestWillNotFit on purpose — one about PLACE, one about TIME, both
// saying the next step will not take this much.
//
// THE ECHO RUNS DEEPER THAN THE NAME, and that is what this run is for. The
// harvest alarm's room is the stores', it is SPENT field by field in the order
// the fields are reaped, and the field that finds it gone is the one named.
// Measured against the WHOLE room instead, three fields of fifty tonnes facing
// sixty each all "fit" and nobody is warned. The sowing alarm's room is DAYS,
// and it has the same trap with an extra edge: the fields do not even share one
// window. Rye, oats and potatoes shut on different months, so "the days left"
// is not a number the settlement has — it is a number each field has.
//
// The first draft of this run fell in exactly there: it summed the spring's
// whole work and divided it by the days left in the NEAREST window, which is
// the one crop that shuts first. Every other field was then measured against a
// deadline that was not its own. So the room is spent here in window order,
// field by field, and each field is asked against its own end minus what the
// fields ahead of it have already taken.
//
// WHAT A DAY OF ROOM IS WORTH is the other half, and there is nothing to choose
// between the candidates but a measurement:
//
//   HANDS      every employable resident could deliver one norm-day a day.
//              The optimistic bound, and the only one an alarm can compute
//              without knowing an assignment that has not happened yet: if even
//              this does not fit, nothing does, so it cannot cry wolf.
//   OWN RATE   what this settlement has actually been doing, off its own
//              ledger: field-work norm-days so far this year over the days it
//              has been at them. Honest about horses, hunger and traction,
//              because it does not model them — it watches them.
//   TEAM       the adult horses, which is the capacity the DESIGN names:
//              "окно держат пахота и боронование на том же табуне". Ploughing
//              and harrowing are harnessed work and one horse takes one man;
//              the sowing is hand work and never the thing that holds it.
//
// All three are printed every spring day beside the TRUTH — the day a field was
// actually sown past its window — so the lead time of each is a number and not
// an opinion. A predicate that fires on the day of the loss is worth nothing.
//
// WHAT IT MEASURED, and the reason it is in the tree before the alarm is:
//
//   * THE TEAM IS THE RIGHT CAPACITY of the three. Walked over twelve years it
//     never missed an overrun year that the other two caught, and it is the
//     only one that is ever silent. HANDS is useless — the village has 65 to
//     247 able-bodied and needs about 150 man-days, so on hands it always
//     "fits"; it spoke in two of twelve years and was late in both.
//
//   * BUT THE CONDITION IS PERMANENT, which no capacity can fix. The sowing
//     overruns its window in ALL TWELVE years, the twelfth included, with 247
//     residents and 46 horses. The cause is not labour and not the team: it is
//     that a field's PLOUGHING does not open until the month its crop's SOWING
//     window begins (field_work.cpp, TrySow). Oats name one month, which is
//     four game days, and plough, harrow and sow together are 2.29 game
//     man-days a hectare. Measured in the twelfth year: 49.5 hectares stand
//     UNTOUCHED from day 0 to day 12 while 245 able-bodied people are in the
//     village, and the whole spring's work then starts on day 13.
//
// An alarm lit every spring of every playthrough is not a signal, so the shape
// of the predicate is settled here and the alarm waits on boss: the design
// knows autumn ploughing ("чёрная зябь" stands in its own phase table, and the
// manure laid in winter is ready "под зябь"), and the core has none.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "../common/fixture_policy.h"
#include "../common/orders_policy.h"
#include "../common/repair_policy.h"
#include "../common/run_harness.h"
#include "../common/yard_policy.h"
#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace {

/// Years to walk. THREE WAS THE FIRST ANSWER AND IT WAS THE WRONG QUESTION: the
/// opening springs are the ones the village is HANDED, so three years say only
/// that the start is tight. Twelve is what separates "the start is tight" from
/// "the model does this" — the team is at 46 head and the village at 247 souls
/// by the twelfth, and if the sowing still does not fit there, no amount of
/// growing was ever going to fix it. It does not.
constexpr std::uint32_t kYears = 12;

/// The crop norms this run needs, read straight off the tables. A second copy
/// of these numbers in the run is the drift this project keeps finding, so
/// nothing here is written down that a table already says.
struct CropNorms {
  /// End of the sowing window, 0-BASED like CalendarState::date.month. The
  /// table counts months from one; the subtraction happens once, here, for the
  /// same reason production_config.cpp does it once — and its absence is what
  /// made labor_year report a week of overrun as three days.
  std::uint32_t sow_to_month = 0;

  /// GAME man-days per hectare (the table keeps real ones).
  float sow_days_per_ha = 0.0F;

  /// End of the crop's HARVEST window, 0-based. Past it there is no reaping
  /// this year at all, so a sowing that finishes later spends its seed for
  /// nothing — the deadline that actually costs grain, as against the sowing
  /// window, which only costs lateness.
  std::uint32_t harvest_to_month = 0;

  /// False for a winter crop and for a perennial: both are cut in a year other
  /// than the one they are sown in, so the reaping deadline below says nothing
  /// about them.
  bool reaped_same_year = true;

  bool named_a_window = false;
};

std::vector<CropNorms> ReadCropNorms(const core::ITableSet& tables) {
  std::vector<CropNorms> norms;
  const core::ITable* const crops = tables.FindTable("crops");
  if (crops == nullptr) {
    return norms;
  }
  const std::uint32_t month_col = crops->FindColumn("sow_to_month");
  const std::uint32_t days_col = crops->FindColumn("sow_days_per_ha");
  const std::uint32_t reap_col = crops->FindColumn("harvest_to_month");
  const std::uint32_t winter_col = crops->FindColumn("is_winter");
  const std::uint32_t perennial_col = crops->FindColumn("is_perennial");
  norms.assign(crops->RowCount(), CropNorms{});
  for (std::uint32_t row = 0; row < crops->RowCount(); ++row) {
    const std::string month{crops->CellText(row, month_col)};
    const std::string days{crops->CellText(row, days_col)};
    const std::string reap{crops->CellText(row, reap_col)};
    if (!reap.empty()) {
      const auto raw = static_cast<std::uint32_t>(std::strtoul(reap.c_str(), nullptr, 10));
      norms[row].harvest_to_month = raw != 0 ? raw - 1U : 0U;
    }
    norms[row].reaped_same_year =
        crops->CellText(row, winter_col) != "1" && crops->CellText(row, perennial_col) != "1";
    if (!month.empty()) {
      const auto raw = static_cast<std::uint32_t>(std::strtoul(month.c_str(), nullptr, 10));
      norms[row].named_a_window = raw != 0;
      norms[row].sow_to_month = raw != 0 ? raw - 1U : 0U;
    }
    if (!days.empty()) {
      norms[row].sow_days_per_ha =
          static_cast<float>(std::strtod(days.c_str(), nullptr)) / core::kRealDaysPerGameDay;
    }
  }
  return norms;
}

/// Ploughing and harrowing, in GAME man-days per hectare, off field_phases.csv.
struct PrepNorms {
  float plow_days_per_ha = 0.0F;
  float harrow_days_per_ha = 0.0F;
};

PrepNorms ReadPrepNorms(const core::ITableSet& tables) {
  PrepNorms prep;
  const core::ITable* const phases = tables.FindTable("field_phases");
  if (phases == nullptr) {
    return prep;
  }
  const std::uint32_t key_col = phases->FindColumn("key");
  const std::uint32_t days_col = phases->FindColumn("labor_days_per_ha");
  for (std::uint32_t row = 0; row < phases->RowCount(); ++row) {
    const std::string_view key = phases->CellText(row, key_col);
    const std::string days{phases->CellText(row, days_col)};
    if (days.empty()) {
      continue;
    }
    const auto value =
        static_cast<float>(std::strtod(days.c_str(), nullptr)) / core::kRealDaysPerGameDay;
    if (key == "plowing") {
      prep.plow_days_per_ha = value;
    } else if (key == "harrowing") {
      prep.harrow_days_per_ha = value;
    }
  }
  return prep;
}

/// What a field still owes before its seed is in the ground: the phase it is
/// in, plus every phase between that one and the sowing.
///
/// NOT `work_days_remaining`, WHICH IS ONE PHASE'S DEMAND. A field nobody has
/// touched owes ploughing, harrowing and sowing; one halfway through the harrow
/// owes the rest of the harrow and the sowing. Reading only the current phase
/// is the reading that makes an untouched field look nearly done.
float DaysUntilSown(const core::FieldRow& field, const PrepNorms& prep, float sow_days_per_ha) {
  switch (field.phase) {
    case core::FieldPhase::kIdle:
      return field.area_ga * (prep.plow_days_per_ha + prep.harrow_days_per_ha + sow_days_per_ha);
    case core::FieldPhase::kPlowing:
      return field.work_days_remaining +
             (field.area_ga * (prep.harrow_days_per_ha + sow_days_per_ha));
    case core::FieldPhase::kHarrowing:
      return field.work_days_remaining + (field.area_ga * sow_days_per_ha);
    case core::FieldPhase::kSowing:
      return field.work_days_remaining;
    case core::FieldPhase::kGrowing:
    case core::FieldPhase::kHarvest:
    case core::FieldPhase::kFieldPhaseCount:
      return 0.0F;
  }
  return 0.0F;
}

/// The crop this field will be sown with THIS year: the one already in the
/// ground while it is being sown, otherwise the rotation's slot for the year.
core::CropId CropOfTheYear(const core::FieldRow& field) {
  return field.crop.value != core::kInvalidDefIdValue ? field.crop : field.rotation_year0;
}

/// Adult horses of the kolkhoz herds — the capacity candidate the DESIGN
/// names: "окно держат пахота и боронование на том же табуне" (farming §
/// "Сеялка проигрывает рукам по КАЛЕНДАРЮ"). Ploughing and harrowing are
/// harnessed work and one horse takes one man, so the team is the ceiling on
/// the two phases that eat the window — the sowing itself is hand work and
/// never the thing that holds it.
///
/// livestock.csv row 3 is the horse, read the way labor_year reads it.
std::uint32_t Horses(const core::WorldState& world) {
  std::uint32_t horses = 0;
  for (const core::HerdRow& herd : world.herds.rows) {
    horses += herd.kind.value == 3 ? herd.adult_count : 0;
  }
  return horses;
}

/// Field-work norm-days the ledger has recorded so far this year: ploughing,
/// harrowing and sowing together, because they are one queue on one team.
double FieldWorkSoFar(const core::WorldState& world) {
  const auto& by_kind = world.ledger.current.work_days_by_kind;
  const auto at = [&by_kind](core::WorkKind kind) {
    return static_cast<double>(by_kind[static_cast<std::size_t>(kind)]);
  };
  return at(core::WorkKind::kPlowing) + at(core::WorkKind::kHarrowing) +
         at(core::WorkKind::kSowing);
}

/// One field still waiting for its seed, as the arithmetic wants it.
struct Waiting {
  float area_ga = 0.0F;
  float days_owed = 0.0F;        ///< norm-days to get this field sown
  std::uint32_t window_end = 0;  ///< last DAY OF THE YEAR its window allows
};

/// THE HECTARES THAT WILL NOT FIT, at a given capacity in norm-days a day.
///
/// The room is the days, and it is SPENT in the order the fields must be sown —
/// earliest window first, exactly as the harvest alarm spends the stores in the
/// order the fields are reaped. A field is asked against its own end minus what
/// the fields ahead of it have taken; what it cannot get, it cannot sow.
float HectaresThatWillNotFit(std::vector<Waiting> waiting,
                             std::uint32_t day_of_year,
                             double capacity) {
  if (capacity <= 0.0) {
    return 0.0F;  // nothing is known about a settlement that has done nothing
  }
  std::ranges::sort(waiting, [](const Waiting& left, const Waiting& right) {
    return left.window_end < right.window_end;
  });
  double spent = 0.0;
  float over_ha = 0.0F;
  for (const Waiting& field : waiting) {
    // A WINDOW THAT HAS ALREADY SHUT IS NOT A FORECAST. This is the same trap
    // the labour queue's deadline was pulled out of on 2026-09-12, one floor
    // up: a count of 0 said both "closes today" and "closed three months ago",
    // and the two want different KINDS of answer. Here the wrong answer is
    // louder — a field past its window contributes its WHOLE area, so the
    // predicate was reporting the calendar as a shortage. Measured: it then
    // fired in all twelve years walked, including the five in which the sowing
    // fitted comfortably. The loss of a shut window is a condition of its own,
    // and it is not the one this predicate is for.
    if (field.window_end < day_of_year) {
      continue;
    }
    const double window = static_cast<double>(field.window_end - day_of_year) + 1.0;
    const double available = window - spent > 0.0 ? window - spent : 0.0;
    const double needed = static_cast<double>(field.days_owed) / capacity;
    if (needed > available) {
      const double missing = needed - available;
      over_ha += static_cast<float>(static_cast<double>(field.area_ga) * missing / needed);
    }
    spent += needed;
  }
  return over_ha;
}

/// What one spring day looks like.
struct SpringDay {
  std::uint32_t day_of_year = 0;
  float ha_awaiting = 0.0F;
  float days_of_work_left = 0.0F;
  std::uint32_t employable = 0;
  std::uint32_t horses = 0;
  double own_rate = 0.0;
  float over_on_hands = 0.0F;
  float over_on_own_rate = 0.0F;
  float over_on_horses = 0.0F;
  /// WHERE THE WAITING HECTARES ARE STANDING, and this is the column that
  /// answers the question the other columns kept re-asking: a field nobody has
  /// opened is not slow, it is SHUT.
  float ha_unopened = 0.0F;   ///< kIdle: no work has been opened on it at all
  float ha_in_prep = 0.0F;    ///< kPlowing or kHarrowing
  float ha_in_sowing = 0.0F;  ///< kSowing
};

/// Everything the run tracks across one year, so the tick loop can be a
/// function and `main` can stay readable.
struct Year {
  std::vector<SpringDay> spring;
  std::uint32_t truth_day = 0;  ///< first day a field was sown past its window
  /// THE LAST DAY ANY FIELD LEFT THE SOWING, and it answers a question the
  /// first overrun cannot: whether the seed was in the ground in time to be
  /// REAPED. The harvest opens on the calendar alone (production_system.cpp:
  /// phase kGrowing plus the month window), so a field whose sowing drags past
  /// the end of its crop's harvest window is not reaped at all that year —
  /// the seed is spent and nothing comes back.
  std::uint32_t last_sown_day = 0;
  /// Hectares whose sowing finished after their crop's harvest window had
  /// closed: seed spent, nothing reaped.
  float ha_sown_too_late = 0.0F;
  std::int32_t margin = std::numeric_limits<std::int32_t>::max();
  double work_yesterday = 0.0;
  double best_day_rate = 0.0;
  bool overran = false;
};

/// The set of fields still owing work before their seed is in, as of now.
std::vector<Waiting> WaitingFields(const core::WorldState& world,
                                   const std::vector<CropNorms>& crops,
                                   const PrepNorms& prep,
                                   SpringDay& sample) {
  std::vector<Waiting> waiting;
  for (const core::FieldRow& field : world.fields.rows) {
    if (field.kind != core::LandKind::kArable) {
      continue;
    }
    const core::CropId crop = CropOfTheYear(field);
    if (crop.value >= crops.size() || !crops[crop.value].named_a_window) {
      continue;  // fallow, or a crop with no window to miss
    }
    const float owed = DaysUntilSown(field, prep, crops[crop.value].sow_days_per_ha);
    if (owed <= 0.0F) {
      continue;  // sown already, or past sowing entirely
    }
    sample.ha_awaiting += field.area_ga;
    sample.days_of_work_left += owed;
    switch (field.phase) {
      case core::FieldPhase::kIdle:
        sample.ha_unopened += field.area_ga;
        break;
      case core::FieldPhase::kPlowing:
      case core::FieldPhase::kHarrowing:
        sample.ha_in_prep += field.area_ga;
        break;
      default:
        sample.ha_in_sowing += field.area_ga;
        break;
    }
    waiting.push_back(
        {.area_ga = field.area_ga,
         .days_owed = owed,
         .window_end = ((crops[crop.value].sow_to_month + 1U) * core::kDaysPerMonth) - 1U});
  }
  return waiting;
}

/// THE TRUTH, AND IT IS A TRANSITION AND NOT A STATE.
///
/// It used to be "a field standing in kSowing in a month past its window", and
/// a state read once a day misses a sowing that opens and closes between two
/// reads. The run then reported "sowed nothing" for every year from the fifth
/// on — the years in which the village has grown enough to sow a field inside
/// one day — and the OVERRUNS of those years would have gone missing in exactly
/// the same silence, which is the half that mattered.
///
/// A field LEAVING the sowing survives sampling: the phase it moves to stands
/// until the harvest.
void WatchTheSowingEnd(const core::WorldState& world,
                       const std::vector<CropNorms>& crops,
                       std::uint32_t day_of_year,
                       std::vector<bool>& was_sowing,
                       Year& year) {
  for (std::uint32_t row = 0; row < world.fields.rows.size() && row < was_sowing.size(); ++row) {
    const core::FieldRow& field = world.fields.rows[row];
    const bool left_sowing = was_sowing[row] && field.phase != core::FieldPhase::kSowing;
    was_sowing[row] = field.phase == core::FieldPhase::kSowing;
    if (!left_sowing || field.crop.value >= crops.size() ||
        !crops[field.crop.value].named_a_window) {
      continue;
    }
    const auto end = static_cast<std::int32_t>(
        ((crops[field.crop.value].sow_to_month + 1U) * core::kDaysPerMonth) - 1U);
    const std::int32_t spare = end - static_cast<std::int32_t>(day_of_year);
    year.margin = std::min(year.margin, spare);
    year.last_sown_day = std::max(year.last_sown_day, day_of_year);
    if (spare < 0) {
      year.truth_day = year.truth_day == 0 ? day_of_year : year.truth_day;
      year.overran = true;
    }
    // AND THE HARVEST'S OWN DEADLINE, which is the one that costs the seed.
    // Past the end of the crop's harvest window there is no reaping this year,
    // so the sowing that finished here bought nothing at all.
    //
    // ASKED ONLY OF CROPS REAPED IN THE YEAR THEY ARE SOWN. A winter crop goes
    // in during the autumn and is cut the FOLLOWING July, so its
    // `harvest_to_month` stands earlier in the year than its sowing and every
    // legitimate winter sowing reads as "too late" — the instrument counted
    // 10.5 ha of perfectly good rye that way before this line.
    if (crops[field.crop.value].reaped_same_year) {
      const auto reaping_ends = static_cast<std::int32_t>(
          ((crops[field.crop.value].harvest_to_month + 1U) * core::kDaysPerMonth) - 1U);
      if (static_cast<std::int32_t>(day_of_year) > reaping_ends) {
        year.ha_sown_too_late += field.area_ga;
      }
    }
  }
}

/// The day each candidate first spoke, and the worst the team's arithmetic saw.
struct FirstWords {
  std::uint32_t hands = 0;
  std::uint32_t own_rate = 0;
  std::uint32_t team = 0;
  float worst_team_ha = 0.0F;
};

FirstWords WhenEachSpoke(const std::vector<SpringDay>& spring) {
  FirstWords first;
  for (const SpringDay& sample : spring) {
    if (first.hands == 0 && sample.over_on_hands > 0.0F) {
      first.hands = sample.day_of_year;
    }
    if (first.own_rate == 0 && sample.over_on_own_rate > 0.0F) {
      first.own_rate = sample.day_of_year;
    }
    if (first.team == 0 && sample.over_on_horses > 0.0F) {
      first.team = sample.day_of_year;
    }
    first.worst_team_ha = std::max(first.worst_team_ha, sample.over_on_horses);
  }
  return first;
}

void ReportYear(std::uint32_t year_index, const Year& year, const core::WorldState& world) {
  const FirstWords first = WhenEachSpoke(year.spring);
  const auto said = [](std::uint32_t fired) {
    return fired != 0 ? "day " + std::to_string(fired) : std::string("never");
  };
  std::string fit = "sowed nothing";
  if (year.truth_day != 0) {
    fit = "OVERRAN from day " + std::to_string(year.truth_day);
  } else if (year.margin != std::numeric_limits<std::int32_t>::max()) {
    fit = "fitted with " + std::to_string(year.margin) + " days to spare";
  }
  // THE LEDGER'S OWN FIGURE BESIDE IT, because "the run saw no sowing" and "no
  // sowing happened" are two claims and this run only ever made the first —
  // which is how it spent an hour reporting a village that had stopped sowing
  // when the village had merely stopped being watched closely enough.
  // area_sown_ha is written by FinishSowing, so no sampling can miss it.
  std::array<std::uint32_t, 6> by_phase{};
  for (const core::FieldRow& field : world.fields.rows) {
    const auto slot = static_cast<std::size_t>(field.phase);
    by_phase[slot < by_phase.size() ? slot : 0]++;
  }
  // AND WHAT THE SNOW TOOK, which is the price of the gamble and the half that
  // was missing while the refusal stood at the reaping window: a cost nobody
  // could incur is not a cost. Past the window and before the snow a field may
  // be sown, cut late — or lost whole, and this is that column.
  std::cout << "sowing_window: year " << year_index + 1 << " — ledger sowed "
            << world.ledger.closed.area_sown_ha << " ha, lost to snow "
            << world.ledger.closed.area_lost_ha << " ha, " << Horses(world)
            << " horses, fields idle/plough/harrow/sow/grow/harvest " << by_phase[0] << "/"
            << by_phase[1] << "/" << by_phase[2] << "/" << by_phase[3] << "/" << by_phase[4] << "/"
            << by_phase[5] << "; last seed in the ground on day " << year.last_sown_day
            << ", of it " << year.ha_sown_too_late << " ha AFTER the reaping could begin"
            << "; sowing " << fit << "; first said on — hands " << said(first.hands)
            << ", best day " << said(first.own_rate) << ", TEAM " << said(first.team) << " (worst "
            << first.worst_team_ha << " ha)\n";
  if (year.truth_day != 0) {
    const std::uint32_t truth = year.truth_day;
    const auto lead = [truth](std::uint32_t fired) {
      return fired != 0 && fired < truth ? std::to_string(truth - fired) + " days"
                                         : std::string("no warning");
    };
    std::cout << "sowing_window:   lead time — hands " << lead(first.hands) << ", best day "
              << lead(first.own_rate) << ", team " << lead(first.team) << "\n";
  }
  for (const SpringDay& sample : year.spring) {
    std::cout << "sowing_window:   day " << sample.day_of_year << " — " << sample.ha_awaiting
              << " ha awaiting the seed (" << sample.ha_unopened << " never opened, "
              << sample.ha_in_prep << " in plough or harrow, " << sample.ha_in_sowing
              << " being sown), " << sample.days_of_work_left << " man-days of work, "
              << sample.employable << " employable, " << sample.horses << " horses, best day "
              << sample.own_rate << " man-days; will not fit: " << sample.over_on_hands
              << " ha on hands, " << sample.over_on_own_rate << " ha on the best day, "
              << sample.over_on_horses << " ha on the team\n";
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1930;
  run::Simulation started = run::Start(seed);
  if (!started) {
    return 1;
  }
  const std::vector<CropNorms> crops = ReadCropNorms(*started.tables);
  const PrepNorms prep = ReadPrepNorms(*started.tables);
  if (crops.empty() || prep.plow_days_per_ha <= 0.0F) {
    std::cout << "FAIL: the norms did not read off the tables\n";
    return 1;
  }
  std::cout << "sowing_window: seed " << seed << ", norms per hectare in game man-days — plough "
            << prep.plow_days_per_ha << ", harrow " << prep.harrow_days_per_ha << "\n";

  // A CHAIRMAN, BECAUSE A VILLAGE WITHOUT ONE MEASURES NOTHING ABOUT SOWING.
  // The first draft of this run gave no orders at all, and the team died out on
  // it — 13 horses, 11, 10, 7, 4, 2, 0 — because nobody built the yard or
  // appointed a groom. At zero the farm froze solid: two fields standing in the
  // plough and five in the harrow for six years running, not one hectare sown
  // from the seventh year on. That is the closed circle the design names in as
  // many words ("нет лошади — нет вспашки — нет урожая — нет овса — нет
  // лошадей", livestock §, "пашни встали в фазе вспашки навсегда") and it has a
  // designed cure, which is the yard. A run measuring a spring window inside
  // that collapse is measuring the collapse.
  run::YardPolicy yard(*started.tables);
  run::FixturePolicy fixture(*started.tables);
  run::FixturePolicy::Declare();
  run::OrdersPolicy orders;
  run::RepairPolicy repairs(*started.tables);
  run::RepairPolicy::Declare();

  int failures = 0;
  bool ever_overran = false;
  // Which fields stood in the sowing yesterday, by row. Sized once: the field
  // table does not grow during a campaign.
  std::vector<bool> was_sowing(started.State().fields.rows.size(), false);
  for (std::uint32_t year_index = 0; year_index < kYears; ++year_index) {
    Year year;
    // EVERY TICK, NOT EVERY DAY, because that is how often the alarm itself
    // would be asked: predicates are collected between steps (alarm_state.h),
    // so a state that stands for three hours is a state the chairman is shown.
    // A once-a-day reading was coarser than the thing it was measuring, and it
    // showed: from the eighth year on this run reported no land awaiting the
    // seed at all, because a village of seventy hands takes a field from idle
    // to growing between two midnights.
    for (std::uint32_t tick = 0; tick < core::kTicksPerYear; ++tick) {
      started->AdvanceStep();
      // The chairman acts once a day, at the same seam thirty_years uses: after
      // a whole day has closed, never inside one.
      if (core::HourFromTick(started.State().calendar.tick) == 0) {
        yard.RunDay(*started.simulation);
        fixture.RunDay(*started.simulation);
        orders.RunDay(*started.simulation);
        repairs.RunDay(*started.simulation);
      }
      const core::WorldState& world = started.State();
      const std::uint32_t day_of_year = world.calendar.day % core::kDaysPerYear;

      WatchTheSowingEnd(world, crops, day_of_year, was_sowing, year);

      SpringDay sample;
      sample.day_of_year = day_of_year;
      const std::vector<Waiting> waiting = WaitingFields(world, crops, prep, sample);
      if (waiting.empty()) {
        continue;
      }
      sample.employable = started->Workforce().employable;
      sample.horses = Horses(world);
      // THE RATE IS THE BEST DAY SO FAR AND NOT THE AVERAGE SINCE JANUARY, and
      // the first draft of this run used the average. It decays: the ground is
      // shut until its window opens, so eleven idle days drag the divisor up
      // and the figure down, and the run reported "55 ha will not fit" four
      // days before the settlement sowed 49 of them. AN AVERAGE OVER DAYS THE
      // WORK WAS NOT ALLOWED TO HAPPEN MEASURES THE CALENDAR, NOT THE
      // SETTLEMENT. The best day is what this village has been seen to do when
      // it was let.
      if (core::HourFromTick(world.calendar.tick) == 0) {
        const double so_far = FieldWorkSoFar(world);
        year.best_day_rate = std::max(year.best_day_rate, so_far - year.work_yesterday);
        year.work_yesterday = so_far;
      }
      sample.own_rate = year.best_day_rate;
      sample.over_on_hands =
          HectaresThatWillNotFit(waiting, day_of_year, static_cast<double>(sample.employable));
      sample.over_on_own_rate = HectaresThatWillNotFit(waiting, day_of_year, sample.own_rate);
      sample.over_on_horses =
          HectaresThatWillNotFit(waiting, day_of_year, static_cast<double>(sample.horses));
      // ONE ROW A DAY IN THE PRINTOUT, and it is the day's WORST reading rather
      // than a chosen hour: an alarm that stood for three hours stood, and a
      // fixed-hour sample would call that silence.
      if (!year.spring.empty() && year.spring.back().day_of_year == day_of_year) {
        SpringDay& today = year.spring.back();
        today.over_on_hands = std::max(today.over_on_hands, sample.over_on_hands);
        today.over_on_own_rate = std::max(today.over_on_own_rate, sample.over_on_own_rate);
        today.over_on_horses = std::max(today.over_on_horses, sample.over_on_horses);
        today.ha_awaiting = std::max(today.ha_awaiting, sample.ha_awaiting);
      } else {
        year.spring.push_back(sample);
      }
    }
    ever_overran = ever_overran || year.overran;

    ReportYear(year_index, year, started.State());
  }

  // THE ONE GATE, AND IT GUARDS THE MEASUREMENT RATHER THAN THE MODEL.
  //
  // Everything above is a measurement, and a measurement of a condition that
  // stopped happening measures nothing: the lead times would be lead times for
  // an event that does not occur, printed every night by a run nobody reads
  // twice. So the gate says the condition is still there to measure.
  //
  // IT IS NOT AN APPROVAL OF THE CONDITION. On the canonical seed the sowing
  // overruns its window in ALL TWELVE years walked, with the village at 247
  // able-bodied residents and 46 horses in the twelfth — and that is the
  // finding this run was built to produce, reported to boss rather than
  // patched here (field_work.cpp TrySow: a field's PLOUGHING does not open
  // until the month its crop's SOWING window begins, so plough, harrow and sow
  // — 2.29 game man-days a hectare — are all crammed inside a window that is
  // four game days wide for oats). The day that is settled, this gate is the
  // line that must be turned around, and it is written here so that it will be.
  failures += run::Expect(ever_overran, "the sowing window is still being overrun and measurable");
  std::cout << (failures == 0 ? "sowing_window: all checks passed\n"
                              : "sowing_window: FAILURES " + std::to_string(failures) + "\n");
  return failures == 0 ? 0 : 1;
}
