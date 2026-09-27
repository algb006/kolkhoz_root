// The district plan's alarms (plan_alarms.h).

#include "plan_alarms.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/fund_ladder.h"
#include "core_common/land_state.h"
#include "district_plan.h"
#include "field_work.h"
#include "seed_room.h"

namespace core {
namespace {

/// The three seasons a chain lays out: year0, year1, year2.
constexpr std::uint32_t kChainYears = 3;

std::uint8_t MonthOf(SimDay day) {
  return static_cast<std::uint8_t>((day % kDaysPerYear) / kDaysPerMonth);
}

bool SameYear(SimDay day, SimDay today) {
  return day / kDaysPerYear == today / kDaysPerYear;
}

bool ReapedThisYear(const FieldRow& field, SimDay today) {
  return field.reaped_day != kNeverReapedDay && SameYear(field.reaped_day, today);
}

/// The field is sowing, growing or reaping `crop` now.
bool CropInHand(const FieldRow& field, CropId crop) {
  return field.crop.value == crop.value &&
         (field.phase == FieldPhase::kSowing || field.phase == FieldPhase::kGrowing ||
          field.phase == FieldPhase::kHarvest);
}

/// `crop` is going into the ground or went in THIS calendar year — next
/// year's winter crop, and not this year's still standing unreaped with the
/// same key (a chain of rye after rye; static review of 0.36.39).
bool SownThisYear(const FieldRow& field, CropId crop, SimDay today) {
  if (field.crop.value != crop.value) {
    return false;
  }
  return field.phase == FieldPhase::kSowing ||
         (field.phase == FieldPhase::kGrowing && field.sown_day != kNeverSownDay &&
          SameYear(field.sown_day, today));
}

/// The calendar year a HELD chain's first slot is grown in (land_state.h,
/// rotation_skips_turn): at the next window of its crop (fund_ladder.cpp,
/// NextSowing::held_chain). A spring crop whose window is still open this
/// year is this year's; a winter crop sown this autumn is next year's; a
/// window gone by moves each a year on. A fallow first slot is spent by its
/// ploughing, in fallow_plow_month. (Static review of 0.36.39: read as the
/// running chain's, a chain named in September as (oats, rye, rye) paid next
/// year's rye with the field that grows oats then, and the goods loan's
/// repayment, which asks year 1, took that for cover.)
/// A crop of the OLD chain standing on a held chain's field, and the calendar
/// year it is reaped in (0 this, 1 next): it holds the field, and the chain's
/// first slot waits for it (TrySow opens an idle field only; the turn does not
/// spend the mark on a crop that is not the first slot — production_system.
/// cpp, RunYearStart). NOT an old crop: the chain's own winter first slot
/// sown this autumn under the mark (0.36.10). A spring crop in hand on a held
/// chain IS old, whatever its key: ploughing for the chain's own spends the
/// mark (static review of 0.36.39's second draft: oats standing in June, the
/// chain renamed (oats, potatoes, rye), read next year's potatoes).
struct OldCrop {
  CropId crop;
  std::uint32_t reaped_year = 0;
};

std::optional<OldCrop> OldCropStanding(const ProductionConfig& config,
                                       const FieldRow& field,
                                       SimDay today) {
  if (field.crop.value >= config.crops.size() || !CropInHand(field, field.crop)) {
    return std::nullopt;
  }
  const bool winter_this_autumn =
      config.crops[field.crop.value].is_winter && SownThisYear(field, field.crop, today);
  if (winter_this_autumn && field.crop.value == field.rotation_year0.value) {
    return std::nullopt;
  }
  return OldCrop{.crop = field.crop, .reaped_year = winter_this_autumn ? 1U : 0U};
}

/// The calendar year a HELD chain's first slot is grown in (land_state.h,
/// rotation_skips_turn): at the next window of its crop after the field is
/// free (fund_ladder.cpp, NextSowing::held_chain). On a free field a spring
/// crop whose window is still open is this year's, a winter crop sown this
/// autumn next year's, a window gone by moves each a year on; a fallow first
/// slot is spent by its ploughing, in fallow_plow_month. An old crop standing
/// holds the field to its reaping, and the first slot comes the year after
/// (static review of 0.36.39's second draft: rye sown in September, the chain
/// renamed in October (potatoes, rye, oats) — the rye stands through next
/// year and the potatoes come the year after; read next year's potatoes, the
/// loan kept rye back for a position the standing rye covers).
std::uint32_t HeldFirstSlotYear(const ProductionConfig& config,
                                const FieldRow& field,
                                SimDay today) {
  const std::uint8_t month = MonthOf(today);
  const CropId first = field.rotation_year0;
  const std::optional<OldCrop> old = OldCropStanding(config, field, today);
  const std::uint32_t after_old = old ? old->reaped_year + 1U : 0U;
  if (first.value >= config.crops.size()) {
    return std::max(month <= config.farming.fallow_plow_month ? 0U : 1U, after_old);
  }
  const CropDef& crop = config.crops[first.value];
  if (crop.is_winter) {
    const bool own_sown = field.crop.value == first.value && CropInHand(field, first) &&
                          SownThisYear(field, first, today);
    if (own_sown) {
      return 1U;
    }
    return std::max(month <= crop.sow_to_month ? 1U : 2U, after_old);
  }
  return std::max(month <= crop.sow_to_month ? 0U : 1U, after_old);
}

/// The crop the field's chain grows in calendar year `year` (0 this, 1 next,
/// 2 the one after), or an invalid id. A running chain's slots ARE the
/// years. A held chain starts at HeldFirstSlotYear; before it, the old crop
/// standing gives the year it is reaped in, a crop reaped this year gives
/// this year, and a year between grows nothing.
CropId CropInYear(const ProductionConfig& config,
                  const FieldRow& field,
                  std::uint32_t year,
                  SimDay today) {
  const std::array<CropId, kChainYears> slots = {
      field.rotation_year0, field.rotation_year1, field.rotation_year2};
  if (field.rotation_skips_turn == 0) {
    return slots[year];
  }
  const std::uint32_t first_year = HeldFirstSlotYear(config, field, today);
  if (year >= first_year) {
    return slots[year - first_year];
  }
  const std::optional<OldCrop> old = OldCropStanding(config, field, today);
  if (old && year == old->reaped_year) {
    return old->crop;
  }
  return year == 0 && ReapedThisYear(field, today) ? field.last_crop : CropId{};
}

/// A RUNNING chain names a crop for `year` that will not grow in it: its
/// sowing is past (boss-core-epoch1-resume [98]). Before, the hectares of a
/// lost slot paid the position until the harvest that never came: on the
/// canon's KD arm the rye of year 4 failed in 8 seed-years, all eight after a
/// kWinterSowingLost, and the alarm for next year's rye had stood 0 times of
/// 135 (econ, neglect-floor §10).
/// - Year 1, a winter crop: its window closed this autumn without it sown
///   (TrySow sows none past sow_to_month) — the alarm's one warning of the
///   season before the turn makes it year 0's loss.
/// - Year 0, a winter crop: WinterSlotLost, the same question as the turn's
///   event and the fallow's recovery.
/// - Year 0, a spring crop not in the ground: an idle field past its window
///   is not ploughed for it any more (field_work.cpp, TrySow), and a field
///   being ploughed or harrowed for it is not sown once, sown today, it
///   would not ripen before the snow (the crew's back edge) — the same loss
///   by the spring's door, the tree's kin of the winter's.
/// A held chain loses nothing: its first slot waits for its next window
/// (CropInYear reads it there).
bool SlotLost(const ProductionConfig& config,
              const FieldRow& field,
              std::uint32_t year,
              SimDay today) {
  if (field.rotation_skips_turn != 0 || year >= kChainYears) {
    return false;
  }
  const CropId slot = CropInYear(config, field, year, today);
  if (slot.value >= config.crops.size()) {
    return false;
  }
  const CropDef& crop = config.crops[slot.value];
  const std::uint8_t month = MonthOf(today);
  if (year == 1) {
    return crop.is_winter && month > crop.sow_to_month && !SownThisYear(field, slot, today);
  }
  if (year != 0) {
    return false;
  }
  if (crop.is_winter) {
    return WinterSlotLost(field, true, today);
  }
  if (CropInHand(field, slot) || ReapedThisYear(field, today)) {
    return false;
  }
  const std::int32_t ripen = RipenDays(config, slot);
  const bool past_snow = ripen > 0 && static_cast<std::int32_t>(today % kDaysPerYear) + ripen >
                                          static_cast<std::int32_t>(config.growing_season_last_day);
  const bool preparing = field.crop.value == slot.value && (field.phase == FieldPhase::kPlowing ||
                                                            field.phase == FieldPhase::kHarrowing);
  // A field being prepared is also lost the day a sowing opened on it would
  // not give its seed back (field_work.cpp, the sowing's own gate,
  // LateSowingReturnsItsSeed): the late factor only falls, so it stays lost
  // (static review of 0.36.39's second draft).
  const bool returns_no_seed = !LateSowingReturnsItsSeed(config, field, slot, today);
  return preparing ? (past_snow || returns_no_seed) : (past_snow || month > crop.sow_to_month);
}

/// Hectares of arable whose chain grows `produce` in calendar year `year`,
/// less the slots already lost (SlotLost). BY PRODUCE AND NOT BY CROP KEY:
/// the district's figure is owed in resource, and any crop that yields it
/// pays it.
float ChainHectares(const ProductionConfig& config,
                    const WorldState& world,
                    ResourceId produce,
                    std::uint32_t year,
                    SimDay as_of) {
  float grown_ha = 0.0F;
  for (const FieldRow& field : world.fields.rows) {
    if (field.kind != LandKind::kArable || !HasRotation(field)) {
      continue;
    }
    const CropId grown = CropInYear(config, field, year, as_of);
    if (grown.value < config.crops.size() && config.crops[grown.value].resource == produce &&
        !SlotLost(config, field, year, as_of)) {
      grown_ha += field.area_ga;
    }
  }
  return grown_ha;
}

/// kPlanPositionShort (boss seq 89): on the year's last day, every position
/// the turn's delivery cannot bring to the met share. THE FORECAST ENTERS BY
/// THE TURN'S OWN DOOR since 0.36.23 (boss-core-epoch1-resume [54]):
/// delivered plus the least of owed and DeliverableAboveSeed — the heaps and
/// the stores less the seed held, as of this last day, the day the turn reads
/// the seed as of (SeedDayAtTheTurn). Until then it counted the stores alone:
/// it said "met" while the turn held seed and failed, and "short" while the
/// turn took a heap.
void CollectPlanShortAlarms(const ProductionConfig& config,
                            const WorldState& world,
                            std::vector<Alarm>& alarms) {
  // «ЗА СУТКИ ДО ПОВОРОТА ГОДА», the boss's word: the day the turn follows.
  if (world.plan.announced == 0 || world.calendar.day % kDaysPerYear != kDaysPerYear - 1) {
    return;
  }
  for (std::uint32_t index = 0; index < world.plan.due.size(); ++index) {
    const Grams due = world.plan.due[index];
    if (due <= 0) {
      continue;
    }
    const Grams delivered = index < world.plan.delivered.size() ? world.plan.delivered[index] : 0;
    const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
    const Grams owed = due > delivered ? due - delivered : 0;
    const Grams deliverable = DeliverableAboveSeed(config, world, resource, world.calendar.day);
    const Grams shipped_at_turn = delivered + (deliverable < owed ? deliverable : owed);
    if (PositionDelivered(config, due, shipped_at_turn)) {
      continue;
    }
    const auto met_grams = static_cast<Grams>(
        std::llround(static_cast<double>(due) * static_cast<double>(config.plan_met_share)));
    Alarm alarm;
    alarm.kind = AlarmKind::kPlanPositionShort;
    alarm.resource = resource;
    // At least a gram: the share compares in float and the grams in double,
    // and a position PositionDelivered calls short is never short by nought.
    alarm.amount = std::max<Grams>(met_grams - shipped_at_turn, 1);
    alarms.push_back(alarm);
  }
}

/// The position is covered in `year`, as of `as_of`.
///
/// THE DISTRICT'S OWN RATE, READ BACKWARDS, and not a forecast (boss,
/// 2026-09-13; district design §9: the norm is off the worked arable and a
/// normal yield per hectare). The position is yield × area × share, so the
/// hectares that pay it at a normal yield are area × share. This year is
/// priced off last year's worked land; the two after off the area next
/// spring's figure will be priced off (NextPlanAreaHa, the district's
/// ratchet) — until 0.36.39 off the arable under chains today, and the goods
/// loan's keep-back, priced off the ratchet, disagreed with the alarm about
/// the same position (boss-core-epoch1-queue [30]-[31], boss's rule of
/// 2026-09-13 amended).
///
/// IT WAS PRESENCE UNTIL THE SAME DAY, and presence lied: on seed 1933 the
/// chairman closed a missing potato with a 3.5 ha field against 4.63 ha owed
/// — the alarm went out on a correct-looking action that did not help, and
/// the plan failed anyway. What this one still does not say is how much THAT
/// field's fertility will fall short; fertility is visible on the ground, and
/// that is the player's call.
bool PositionCovered(const ProductionConfig& config,
                     const WorldState& world,
                     const ProductionConfig::PlanPosition& position,
                     std::uint32_t year,
                     SimDay as_of) {
  const ResourceId produce = config.crops[position.crop.value].resource;
  const float grown_ha = ChainHectares(config, world, produce, year, as_of);
  const float priced_ha = year == 0 ? world.plan.worked_ha_last_year : NextPlanAreaHa(world, as_of);
  const float owed_ha = priced_ha * position.area_share * config.plan_grain_share;
  return owed_ha > 0.0F ? grown_ha >= owed_ha : grown_ha > 0.0F;
}

/// A position the walk asks about: a crop of the table and a share above 0.
bool PositionCounts(const ProductionConfig& config,
                    const ProductionConfig::PlanPosition& position) {
  return position.crop.value < config.crops.size() && position.area_share > 0.0F;
}

using CoverYears = std::array<bool, kChainYears>;

/// Whether the fields OR THE STORES cover each position in each year, as of
/// today (0.37.4; boss-core-epoch1-queue [49] (б), econ-boss-rye-hold [1]).
///
/// THE ALARM COUNTED THE FIELDS ALONE and burned beside a full barn: year 1
/// of the rye on 27 seeds of 27 from day 0 — 1.60 t owed, 7.37 t above the
/// seed — 81 seed-years of 540. The stores count now as the goods loan keeps
/// them (goods_loan.cpp): per produce, what is above the seed pays first
/// what this year still owes, then, year by year and position by position in
/// table order, each position the fields leave uncovered, each claim held
/// with the rot of its wait to its delivery at the end of its year
/// (HeldForDeliveryGrams). A claim the stores cannot meet is not covered and
/// takes what is left, so a later year is not covered by grams an earlier
/// one already needed.
///
/// THIS YEAR'S DEBT COMES OFF IN ANY CASE, covered by its fields or not: the
/// turn ships it from the stores, the harvest the fields bring lands in them
/// first, and before the harvest the grams it will bring are not in the barn
/// yet — so the stores are read short rather than twice.
std::vector<CoverYears> CoveredPositions(const ProductionConfig& config, const WorldState& world) {
  const SimDay today = world.calendar.day;
  const auto days_to_turn = static_cast<std::uint32_t>(kDaysPerYear - (today % kDaysPerYear));
  const float next_area = NextPlanAreaHa(world, today);
  const std::size_t count = config.plan_positions.size();
  std::vector<CoverYears> covered(count, CoverYears{});
  for (std::size_t index = 0; index < count; ++index) {
    if (!PositionCounts(config, config.plan_positions[index])) {
      continue;
    }
    for (std::uint32_t year = 0; year < kChainYears; ++year) {
      covered[index][year] =
          PositionCovered(config, world, config.plan_positions[index], year, today);
    }
  }
  const auto produce_of = [&config](std::size_t index) {
    return config.crops[config.plan_positions[index].crop.value].resource;
  };
  std::vector<std::uint32_t> produce_done;
  for (std::size_t first = 0; first < count; ++first) {
    if (!PositionCounts(config, config.plan_positions[first])) {
      continue;
    }
    const ResourceId produce = produce_of(first);
    if (std::ranges::find(produce_done, produce.value) != produce_done.end()) {
      continue;
    }
    produce_done.push_back(produce.value);
    std::vector<std::size_t> positions;
    for (std::size_t index = first; index < count; ++index) {
      if (PositionCounts(config, config.plan_positions[index]) &&
          produce_of(index).value == produce.value) {
        positions.push_back(index);
      }
    }
    Grams stock = DeliverableAboveSeed(config, world, produce, today);
    // This year's owed: the announced figure less what has left. Unannounced
    // only at genesis before the first tick (the letter comes at the turn),
    // where the positions are priced off last year's area as a stand-in —
    // not the first year's start-stock figure AnnouncePlan will name.
    Grams owed_now = 0;
    if (world.plan.announced != 0) {
      const Grams due = AmountOf(world.plan.due, produce);
      const Grams delivered = AmountOf(world.plan.delivered, produce);
      owed_now = due > delivered ? due - delivered : 0;
    } else {
      for (const std::size_t index : positions) {
        owed_now +=
            PlanPositionGrams(config, config.plan_positions[index], world.plan.worked_ha_last_year);
      }
    }
    const Grams claim_now = HeldForDeliveryGrams(config, produce, owed_now, days_to_turn);
    const bool stock_pays_now = owed_now > 0 && stock >= claim_now;
    stock = stock > claim_now ? stock - claim_now : 0;
    for (const std::size_t index : positions) {
      covered[index][0] = covered[index][0] || stock_pays_now;
    }
    // NEXT YEAR BY THE HOLD ITSELF (static review of 0.37.4): what its own
    // harvest will not pay of its positions AND of the year after's seed,
    // once for the produce — the grams the loan, the herds and the issue
    // keep. By the position alone the alarm went quiet over a barn the
    // autumn's sowing would empty first.
    bool year_one_open = false;
    for (const std::size_t index : positions) {
      year_one_open = year_one_open || !covered[index][1];
    }
    if (year_one_open) {
      const Grams claim = HeldForDeliveryGrams(config,
                                               produce,
                                               NextYearUnpaidGrams(config, world, produce, today),
                                               days_to_turn + kDaysPerYear);
      const bool stock_pays = claim > 0 && stock >= claim;
      for (const std::size_t index : positions) {
        covered[index][1] = covered[index][1] || stock_pays;
      }
      stock = stock > claim ? stock - claim : 0;
    }
    // The year after by each position's own grams, with the rot of its wait.
    for (const std::size_t index : positions) {
      if (covered[index][2]) {
        continue;
      }
      const Grams owed = PlanPositionGrams(config, config.plan_positions[index], next_area);
      const Grams claim =
          HeldForDeliveryGrams(config, produce, owed, days_to_turn + (2 * kDaysPerYear));
      covered[index][2] = owed > 0 && stock >= claim;
      stock = stock > claim ? stock - claim : 0;
    }
  }
  return covered;
}

}  // namespace

Grams NextYearUnpaidGrams(const ProductionConfig& config,
                          const WorldState& world,
                          ResourceId resource,
                          SimDay as_of) {
  // What next year owes out of this produce: the district's positions,
  // priced as next spring's figure will be (NextPlanAreaHa)...
  Grams owed = 0;
  const float next_area = NextPlanAreaHa(world, as_of);
  for (const ProductionConfig::PlanPosition& position : config.plan_positions) {
    if (PositionCounts(config, position) &&
        config.crops[position.crop.value].resource.value == resource.value) {
      owed += PlanPositionGrams(config, position, next_area);
    }
  }
  // ...and the seed of the year after's crops of it, which next year's
  // harvest gives: a spring crop of year 2 is sown from it that spring, a
  // winter crop of year 2 that autumn.
  //
  // A SOWING THE SEED RUNG ALREADY HOLDS IS NOT COUNTED AGAIN (static review
  // of 0.37.4): a field whose next sowing is the year after's crop and whose
  // seed no harvest gives first is held by SeedHeldByField, off the stores
  // before this hold is taken — winter rye in the ground for next year, a
  // spring crop after it. Counted here too, the loan repaid less and the
  // herds held twice.
  const std::vector<SeedNorm> seed_norms = SeedNormsOf(config);
  const SeedHold seed_held = SeedHeldByField(world, seed_norms, config.feed_values.size(), as_of);
  float seed_kg = 0.0F;
  float harvest_kg = 0.0F;
  for (std::size_t row = 0; row < world.fields.rows.size(); ++row) {
    const FieldRow& field = world.fields.rows[row];
    if (field.kind != LandKind::kArable || !HasRotation(field)) {
      continue;
    }
    const bool seed_already_held = row < seed_held.by_field_row.size() &&
                                   seed_held.by_field_row[row] > 0 &&
                                   seed_held.seed_of_row[row].value == resource.value;
    const CropId after = CropInYear(config, field, 2, as_of);
    if (!seed_already_held && after.value < config.crops.size() &&
        config.crops[after.value].resource.value == resource.value) {
      seed_kg += config.crops[after.value].sowing_norm_kg_per_ha * field.area_ga;
    }
    const CropId next = CropInYear(config, field, 1, as_of);
    if (next.value < config.crops.size() &&
        config.crops[next.value].resource.value == resource.value &&
        !SlotLost(config, field, 1, as_of)) {
      harvest_kg += config.crops[next.value].yield_kg_per_ha * field.area_ga;
    }
  }
  const Grams needed = owed + GramsFromKilograms(seed_kg);
  const Grams harvest = GramsFromKilograms(harvest_kg);
  return needed > harvest ? needed - harvest : 0;
}

void CollectPlanAlarms(const ProductionConfig& config,
                       const WorldState& world,
                       std::vector<Alarm>& alarms) {
  // Positions in table order, years in chain order: the emission order is a
  // function of the tables and the chains, and the session sorts it anyway.
  const std::vector<CoverYears> covered = CoveredPositions(config, world);
  for (std::size_t index = 0; index < config.plan_positions.size(); ++index) {
    const ProductionConfig::PlanPosition& position = config.plan_positions[index];
    if (!PositionCounts(config, position)) {
      continue;
    }
    const ResourceId produce = config.crops[position.crop.value].resource;
    for (std::uint32_t year = 0; year < kChainYears; ++year) {
      if (covered[index][year]) {
        continue;
      }
      Alarm alarm;
      alarm.kind = AlarmKind::kPlanPositionUncovered;
      alarm.resource = produce;
      alarm.amount = year;
      alarms.push_back(alarm);
    }
  }
  CollectPlanShortAlarms(config, world, alarms);
}

}  // namespace core
