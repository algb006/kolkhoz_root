// The feed and seed lights (stock_lights.h).

#include "stock_lights.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/fund_ladder.h"
#include "core_common/land_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "herd_forecast.h"
#include "herd_system.h"
#include "stock_ops.h"

namespace core {
namespace {

/// The month a wintering forecast asks about: the first one outside the
/// pasture band. Taken from the band rather than written down, so moving the
/// season moves this with it.
///
/// A band covering the WHOLE year has no outside, and then the winter month
/// is a fiction; the parser bounds the two months independently and never
/// checks their order, so that config is legal. The caller must ask whether
/// there is a stall season at all before believing this.
std::uint8_t WinterMonth(const ProductionConfig& config) {
  return static_cast<std::uint8_t>((config.farming.pasture_to_month + 1U) % kMonthsPerYear);
}

/// Is there a stall season at all? A band that runs the whole year means the
/// herd never comes in, and a wintering forecast has nothing to forecast.
/// Days from today to the start of the band [from, to], 0 if today is
/// already inside it. One place, because all four dates are this same
/// question asked of a different band.
///
/// A band with from > to is not a band this module can read — MonthInRange,
/// which the real feeding uses, does not wrap the year either — and the
/// answer is 0 rather than the horizon. Returning the horizon would pin the
/// light yellow for ever with nothing to show for it, and a light that is
/// always yellow is the failure mode the whole mechanism is built to avoid.
/// With 0 the colour rests on the stock alone, which is at least true.
std::int32_t DaysToWindow(const WorldState& world, std::uint8_t from, std::uint8_t to) {
  const auto today = static_cast<std::uint8_t>(world.calendar.date.month);
  if (from > to || MonthInRange(today, from, to)) {
    return 0;
  }
  std::int32_t months = static_cast<std::int32_t>(from) - static_cast<std::int32_t>(today);
  if (months < 0) {
    months += static_cast<std::int32_t>(kMonthsPerYear);
  }
  return months * static_cast<std::int32_t>(kDaysPerMonth);
}

/// The month the nearest sowing campaign opens, or kMonthsPerYear when no
/// crop names a window at all. "Nearest" is by the calendar and forward
/// only: a campaign whose window is open right now is the nearest one.
std::uint8_t NearestSowingMonth(const ProductionConfig& config, const WorldState& world) {
  std::uint8_t best = static_cast<std::uint8_t>(kMonthsPerYear);
  std::int32_t best_days = kStockForecastHorizonDays + 1;
  for (const CropDef& crop : config.crops) {
    if (!(crop.sowing_norm_kg_per_ha > 0.0F) || crop.sow_from_month >= kMonthsPerYear) {
      continue;
    }
    const std::int32_t days = DaysToWindow(world, crop.sow_from_month, crop.sow_to_month);
    // Ties go to the lower month so the answer is a function of the tables
    // and not of their row order.
    if (days < best_days || (days == best_days && crop.sow_from_month < best)) {
      best_days = days;
      best = crop.sow_from_month;
    }
  }
  return best;
}

bool HasStallSeason(const ProductionConfig& config) {
  return !MonthInRange(
      WinterMonth(config), config.farming.pasture_from_month, config.farming.pasture_to_month);
}

}  // namespace

std::int32_t DaysToHarvest(const ProductionConfig& config, const WorldState& world) {
  std::int32_t best = kStockForecastHorizonDays;
  for (const CropDef& crop : config.crops) {
    const std::int32_t days = DaysToWindow(world, crop.harvest_from_month, crop.harvest_to_month);
    best = days < best ? days : best;
  }
  return best;
}

namespace {

/// Is the crop in the ground anywhere: a field being sown with it, growing
/// it or being reaped of it (land_state.h FieldRow::crop).
bool CropStands(const WorldState& world, std::size_t crop_index) {
  return std::ranges::any_of(world.fields.rows, [crop_index](const FieldRow& field) {
    return field.crop.value == crop_index &&
           (field.phase == FieldPhase::kSowing || field.phase == FieldPhase::kGrowing ||
            field.phase == FieldPhase::kHarvest);
  });
}

/// Days from today to the next harvest of a crop that gives `resource`
/// itself, -1 when none does. To the DAY, not the month: the norm divides by
/// the trudodni of those days, and a month is four of them.
///
/// THE FIELDS, NOT THE CALENDAR (0.37.34; production_system.h): a crop that
/// stands in a field is reaped at its window's next opening; one that stands
/// nowhere is reaped at the opening that follows its next sowing — the
/// start's winter rye, not sown the autumn before, in year 2's July. Until
/// 0.37.33 every crop was read as standing, and the norm gave out year 1's
/// rye by day 23 to a July that reaped none.
std::int32_t DaysToOwnHarvest(const ProductionConfig& config,
                              const WorldState& world,
                              ResourceId resource) {
  const auto day_of_year = static_cast<std::int32_t>(world.calendar.day % kDaysPerYear);
  const auto today = static_cast<std::uint8_t>(world.calendar.date.month);
  const auto year = static_cast<std::int32_t>(kDaysPerYear);
  const auto month = static_cast<std::int32_t>(kDaysPerMonth);
  // Days from today forward to a day of the year, 0 for today itself.
  const auto ahead_to = [day_of_year](std::int32_t day) {
    return ((day - day_of_year) % year + year) % year;
  };
  std::int32_t best = -1;
  for (std::size_t index = 0; index < config.crops.size(); ++index) {
    const CropDef& crop = config.crops[index];
    if (crop.resource.value != resource.value || crop.harvest_from_month >= kMonthsPerYear) {
      continue;
    }
    const std::int32_t opening = static_cast<std::int32_t>(crop.harvest_from_month) * month;
    std::int32_t days = 0;
    if (CropStands(world, index)) {
      // Inside the window the next harvest is next year's: the one reaping
      // now is what the stores are filling with, not a date to share it to.
      // The band does not wrap the year (calendar.h, MonthInRange), so an
      // open window opened this year, at or before today.
      const bool open_now = MonthInRange(today, crop.harvest_from_month, crop.harvest_to_month);
      days = open_now ? opening + year - day_of_year : ahead_to(opening);
    } else {
      // Standing nowhere: its next sowing (today, if the window is open),
      // then the opening that follows it — the next July for a winter crop.
      if (crop.sow_from_month >= kMonthsPerYear) {
        continue;  // never sown: no harvest to wait for
      }
      const bool sowing_now = MonthInRange(today, crop.sow_from_month, crop.sow_to_month);
      const std::int32_t sowing_day =
          sowing_now ? day_of_year : static_cast<std::int32_t>(crop.sow_from_month) * month;
      const std::int32_t to_sowing = sowing_now ? 0 : ahead_to(sowing_day);
      const std::int32_t sowing_to_harvest = ((opening - sowing_day) % year + year) % year;
      days = to_sowing + (sowing_to_harvest > 0 ? sowing_to_harvest : year);
    }
    best = best < 0 || days < best ? days : best;
  }
  return best;
}

}  // namespace

std::int32_t DaysToHarvestOf(const ProductionConfig& config,
                             const WorldState& world,
                             ResourceId resource) {
  std::int32_t best = DaysToOwnHarvest(config, world, resource);
  if (best >= 0) {
    return best;
  }
  // Made, not grown: the harvest of what it is made of (one step — a
  // product of a product is not in the tables).
  for (const ProcessingRecipe& recipe : config.processing.recipes) {
    const bool makes_it = std::ranges::any_of(recipe.outputs, [&](const ProcessingAmount& out) {
      return out.resource.value == resource.value;
    });
    if (!makes_it) {
      continue;
    }
    for (const ProcessingAmount& input : recipe.inputs) {
      const std::int32_t days = DaysToOwnHarvest(config, world, input.resource);
      best = days >= 0 && (best < 0 || days < best) ? days : best;
    }
  }
  return best;
}

std::int32_t DaysToPasture(const ProductionConfig& config, const WorldState& world) {
  return DaysToWindow(world, config.farming.pasture_from_month, config.farming.pasture_to_month);
}

StockForecast FeedLight(const ProductionConfig& config, const WorldState& world) {
  StockForecast light;
  light.kind = StockKind::kFeed;
  light.days_to_date = DaysToPasture(config, world);

  // The herds this forecast is about, with the winter need of each. PER HERD
  // and not summed, because the real feeding is per herd: every ceiling
  // (FeedLinkDef::max_share) is a share of ONE herd's day, and every feeding
  // order is that herd's kind's order. Summing first and draining after let
  // the pigs' barley cover the cows' hay and turned every ceiling into a
  // share of the whole settlement — which is to say, into nothing. Found by
  // the delivery cycle, and the first tests could not see it because each
  // used a single kind with a single feed.
  std::vector<FeedDayNeed> needs;
  if (HasStallSeason(config)) {
    const std::uint8_t winter = WinterMonth(config);
    for (const HerdRow& herd : world.herds.rows) {
      if (herd.household_owned != 0 || herd.kind.value >= config.livestock.size()) {
        continue;  // a family herd eats from its family's pantry, not the farm's
      }
      // The winter need, so the grazing flag decides nothing here — and it is
      // passed false rather than true all the same, because a forecast never
      // spends the night pasture's gain (herd_system.h).
      const float need =
          FeedNeedUnits(config, config.livestock[herd.kind.value], herd, winter, false);
      if (need > 0.0F) {
        needs.push_back(FeedDayNeed{.kind = herd.kind, .units = need});
      }
    }
  }
  if (needs.empty()) {
    // No kolkhoz herd, no roster, or a pasture band with no winter in it:
    // nothing eats from the stores, so nothing runs out. An answer, and
    // pointedly not "no data", which belongs to a stock nobody can forecast.
    light.days_of_stock = kStockNeverRunsOut;
    light.light = LightFrom(light.days_of_stock, light.days_to_date, 0, false);
    return light;
  }

  // WHAT THE HERDS MAY EAT, in KILOGRAMS per feed resource, drained day by
  // day — THROUGH THE HERD DAY'S OWN DOOR (HerdFeedAllowance; resources design
  // §6, the row of 28 September: «то, что зима МОЖЕТ съесть»; boss-core-
  // epoch1-queue-2026-09-29 [14] (1), (3); 0.37.15). The stores less every
  // rung the herds stay below: the seed fund, this year's plan, next year's
  // hold and the plough's oats — released on the day the feeding releases
  // them. Until 0.37.14 the light read the whole stores, and 0.37.14 took
  // off the plough's oats alone, the one exclusion the row names, while the
  // feeding kept all four: the light measured a neighbour of the feeding.
  // AND THE HAY LYING REAPED ON THE FIELDS (boss-core-epoch1-queue [96],
  // [97]; 0.37.11) above the district's take (HeapAbovePlanDebt, 0.37.15).
  // One reading with the herds' forecast since 0.37.57 (herd_forecast.h,
  // HerdFeedHeldKg).
  std::vector<float> held_kg = HerdFeedHeldKg(config, world);
  // AND THE FEEDING'S REFUSAL PER LINK: a reserve feed takes none of the
  // people's food (herd_system.cpp, PeoplesFoods; 0.37.5).
  const std::vector<std::uint8_t> peoples_foods = PeoplesFoods(config, world);

  // THE DAY'S DRAIN IS THE FORECAST'S (DrainFeedDay, 0.37.57): the order,
  // the ceilings, the work-only feeds carrying nothing — one function, so
  // the light and the yellow stage cannot part.
  std::vector<float> covered;
  std::int32_t days = 0;
  bool starving_today = false;
  while (days < kStockForecastHorizonDays) {
    DrainFeedDay(config, peoples_foods, needs, held_kg, covered);
    bool all_fed = true;
    for (std::size_t index = 0; index < needs.size(); ++index) {
      all_fed = all_fed && covered[index] + 0.001F >= needs[index].units;
    }
    if (!all_fed) {
      if (days == 0) {
        starving_today = true;  // the stores cannot feed the herds TODAY
      }
      break;
    }
    ++days;
  }
  light.days_of_stock = days;
  light.light = LightFrom(days,
                          light.days_to_date,
                          static_cast<std::int32_t>(config.farming.feed_light_margin_days),
                          starving_today);
  return light;
}

StockForecast SeedLight(const ProductionConfig& config, const WorldState& world) {
  StockForecast light;
  light.kind = StockKind::kSeed;
  light.measure = StockMeasure::kCoverage;

  // THE NEXT SOWING IS A CAMPAIGN ON THE CALENDAR, not an index in the
  // rotation (boss, 2026-09-04). In autumn the nearest campaign is the
  // winter one and the winter crops are counted; once the winter crop is in
  // the ground the nearest campaign is the spring one, and asking about
  // winter seed then is meaningless — it is already sown. The light used to
  // read rotation_year0, which is THIS year's crop and only shifts at the
  // year's turn, so a spring field was charged its seed a second time for a
  // third of the year and the light stood amber for no reason a player
  // could see.
  const std::uint8_t campaign_month = NearestSowingMonth(config, world);
  if (campaign_month >= kMonthsPerYear) {
    // No crop names a sowing window: nothing is ever sown, so nothing can be
    // short of seed.
    light.days_to_date = 0;
    light.coverage = 1.0F;
    light.light = StockLight::kGreen;
    return light;
  }
  light.days_to_date = DaysToWindow(world, campaign_month, campaign_month);

  // What the campaign's fields will want, by RESOURCE — two crops can share
  // one (winter wheat and spring wheat are both `wheat`), and counting per
  // crop would count the same grain twice.
  std::vector<float> need_kg(config.feed_values.size(), 0.0F);
  for (const FieldRow& field : world.fields.rows) {
    // THE FIELD'S NEXT SOWING, not its first slot (0.34.38; boss seq 23). The
    // first slot of a field reaped this summer is the crop just gathered,
    // and the winter campaign's rye stands in the SECOND — so the light
    // counted no autumn rye on any reaped field, the same gap the seed fund
    // had (fund_ladder.h).
    const bool year0_winter = field.rotation_year0.value < config.crops.size() &&
                              config.crops[field.rotation_year0.value].is_winter;
    const CropId next = NextSowingCrop(field, world.calendar.day, year0_winter);
    if (field.kind != LandKind::kArable || next.value >= config.crops.size()) {
      continue;
    }
    // A FIELD THAT IS OCCUPIED IS NOT IN THIS CAMPAIGN. Standing corn is not
    // waiting to be sown: this spring's rye would charge next spring's oats
    // to this spring's campaign. A black fallow "grows" nothing and is
    // waiting for its rye.
    if ((field.phase == FieldPhase::kGrowing || field.phase == FieldPhase::kHarvest) &&
        field.crop.value != kInvalidDefIdValue) {
      continue;
    }
    const CropDef& crop = config.crops[next.value];
    if (crop.sow_from_month != campaign_month || !(crop.sowing_norm_kg_per_ha > 0.0F)) {
      continue;  // this field is sown in some other campaign
    }
    // A SPRING CROP OF THE SECOND SLOT IS SOWN AFTER THE TURN (static review
    // of 0.34.38): a fallow field's oats of next year are not this spring's.
    // Counted only when the campaign's month comes round again after the
    // turn — in the autumn, once this year's spring is behind.
    const bool second_slot =
        next.value == field.rotation_year1.value && next.value != field.rotation_year0.value;
    if (second_slot && !crop.is_winter &&
        static_cast<std::uint32_t>(campaign_month) >=
            static_cast<std::uint32_t>(world.calendar.date.month)) {
      continue;
    }
    if (crop.resource.value < need_kg.size()) {
      need_kg[crop.resource.value] += crop.sowing_norm_kg_per_ha * field.area_ga;
    }
  }

  // THE TIGHTEST CROP DECIDES: you cannot sow oats with rye, and a village
  // short of one seed is short however much of another it has. Netting them
  // let a mountain of rye cancel the absence of potato seed and read green.
  float coverage = -1.0F;
  for (std::uint32_t resource = 0; resource < need_kg.size(); ++resource) {
    if (!(need_kg[resource] > 0.0F)) {
      continue;
    }
    const ResourceId id = DefIdFromIndex<ResourceIdTag>(resource);
    // The stores AND the heaps lying the winter, less what the district takes
    // (boss-core-epoch1-queue-2026-09-29 [14] (2); 0.37.15) — and the
    // district takes no seed: its take is capped above it
    // (DeliverableAboveSeed), so here it is the owed, as far as the stores
    // and heaps hold more than this campaign's seed. Netted of the whole owed
    // (HeapAbovePlanDebt) the light read red over seed the district leaves
    // (static review of 0.37.15), and the stores and heaps are counted
    // together, so carting a heap in moves nothing.
    const Grams lying = HeldEverywhere(world, id) + HeapGrams(world, id);
    const auto need = static_cast<Grams>(need_kg[resource] * static_cast<float>(kGramsPerKilogram));
    const Grams above_seed = lying > need ? lying - need : 0;
    const Grams owed = PlanOwedGrams(world, id);
    const Grams taken = owed < above_seed ? owed : above_seed;
    const float have_kg = static_cast<float>(lying - taken) / static_cast<float>(kGramsPerKilogram);
    const float share = have_kg / need_kg[resource];
    coverage = coverage < 0.0F || share < coverage ? share : coverage;
  }
  if (coverage < 0.0F) {
    // The campaign asks for nothing: no free field is due this crop.
    light.coverage = 1.0F;
    light.light = StockLight::kGreen;
    return light;
  }
  light.coverage = coverage;
  light.light = LightFromCoverage(coverage, config.farming.seed_light_margin_share);
  return light;
}

}  // namespace core
