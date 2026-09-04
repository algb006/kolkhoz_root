// The feed and seed lights (stock_lights.h).

#include "stock_lights.h"

#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "herd_system.h"
#include "stock_ops.h"

namespace core {
namespace {

/// Everything the settlement holds of one resource, wherever a taker would
/// find it. The same reach the feeding itself has.
Grams HeldEverywhere(const WorldState& world, ResourceId resource) {
  Grams total = 0;
  for (const UnitRow& unit : world.units.rows) {
    if (unit.level == 0) {
      continue;  // a marked site holds nothing
    }
    total += StockOf(unit.stock, resource);
  }
  return total;
}

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

std::int32_t DaysToPasture(const ProductionConfig& config, const WorldState& world) {
  return DaysToWindow(world, config.farming.pasture_from_month, config.farming.pasture_to_month);
}

std::int32_t DaysToSowing(const ProductionConfig& config, const WorldState& world) {
  std::int32_t best = kStockForecastHorizonDays;
  for (const CropDef& crop : config.crops) {
    const std::int32_t days = DaysToWindow(world, crop.sow_from_month, crop.sow_to_month);
    best = days < best ? days : best;
  }
  return best;
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
  struct HerdNeed {
    LivestockKindId kind;
    float units_per_day = 0.0F;
  };

  std::vector<HerdNeed> needs;
  if (HasStallSeason(config)) {
    const std::uint8_t winter = WinterMonth(config);
    for (const HerdRow& herd : world.herds.rows) {
      if (herd.household_owned != 0 || herd.kind.value >= config.livestock.size()) {
        continue;  // a family herd eats from its family's pantry, not the farm's
      }
      const float need = FeedNeedUnits(config, config.livestock[herd.kind.value], herd, winter);
      if (need > 0.0F) {
        needs.push_back(HerdNeed{.kind = herd.kind, .units_per_day = need});
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

  // A copy of the stores in KILOGRAMS per feed resource, drained day by day.
  std::vector<float> held_kg(config.feed_values.size(), 0.0F);
  for (std::uint32_t resource = 0; resource < held_kg.size(); ++resource) {
    if (!(config.feed_values[resource] > 0.0F)) {
      continue;
    }
    const ResourceId id{static_cast<std::uint16_t>(resource)};
    held_kg[resource] =
        static_cast<float>(HeldEverywhere(world, id)) / static_cast<float>(kGramsPerKilogram);
  }

  std::int32_t days = 0;
  bool starving_today = false;
  while (days < kStockForecastHorizonDays) {
    bool all_fed = true;
    for (const HerdNeed& herd : needs) {
      float covered = 0.0F;
      for (const FeedLinkDef& link : config.feed_links) {
        if (covered >= herd.units_per_day) {
          break;
        }
        // The kind filter is the whole point: a feeding order belongs to the
        // kind it names, and a cow is never offered the pigs' barley.
        if (link.kind.value != herd.kind.value || link.resource.value >= held_kg.size()) {
          continue;
        }
        // Work-only feeds are the horse's WAGE, not its keep (boss, Q2): in a
        // wintering forecast nobody is in the traces, so they carry nothing.
        // Leaving them in would let the oats forecast the hay.
        if (link.work_only != 0) {
          continue;
        }
        const float value = config.feed_values[link.resource.value] *
                            (link.reserve != 0 ? config.farming.reserve_feed_factor : 1.0F);
        if (!(value > 0.0F)) {
          continue;
        }
        const float ceiling = herd.units_per_day * link.max_share;
        float take_units = herd.units_per_day - covered;
        take_units = take_units < ceiling ? take_units : ceiling;
        const float available_units = held_kg[link.resource.value] * value;
        take_units = take_units < available_units ? take_units : available_units;
        if (!(take_units > 0.0F)) {
          continue;
        }
        held_kg[link.resource.value] -= take_units / value;
        covered += take_units;
      }
      if (covered + 0.001F < herd.units_per_day) {
        all_fed = false;
        break;
      }
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

StockForecast SeedLight(const ProductionConfig& config,
                        const WorldState& world,
                        float eating_kg_per_day) {
  StockForecast light;
  light.kind = StockKind::kSeed;
  light.days_to_date = DaysToSowing(config, world);

  // WHAT IS NEEDED AND WHAT IS THERE, PER RESOURCE — not summed across the
  // crops, and this is two corrections at once.
  //
  // Summing per CROP double-counted: winter wheat and spring wheat are two
  // crops and one resource, and resources.csv says so outright ("grain is
  // per culture; storage is shared"). Summing the TOTAL netted the crops
  // against each other: a mountain of rye cancelled the complete absence of
  // potato seed and read green — the exact opposite of what this function's
  // own comment claimed. You cannot sow oats with rye.
  std::vector<float> need_kg(config.feed_values.size(), 0.0F);
  std::vector<float> have_kg(config.feed_values.size(), 0.0F);
  std::vector<std::uint8_t> is_seed(config.feed_values.size(), 0);
  const auto resource_index = [&](ResourceId resource) {
    return resource.value < need_kg.size() ? static_cast<std::size_t>(resource.value)
                                           : need_kg.size();
  };
  for (const CropDef& crop : config.crops) {
    if (!(crop.sowing_norm_kg_per_ha > 0.0F)) {
      continue;
    }
    const std::size_t index = resource_index(crop.resource);
    if (index < is_seed.size()) {
      is_seed[index] = 1;
    }
  }
  for (const FieldRow& field : world.fields.rows) {
    if (field.kind != LandKind::kArable || field.rotation_year0.value >= config.crops.size()) {
      continue;
    }
    const CropDef& crop = config.crops[field.rotation_year0.value];
    const std::size_t index = resource_index(crop.resource);
    if (!(crop.sowing_norm_kg_per_ha > 0.0F) || index >= need_kg.size()) {
      continue;
    }
    need_kg[index] += crop.sowing_norm_kg_per_ha * field.area_ga;
  }
  float total_need = 0.0F;
  for (std::size_t index = 0; index < need_kg.size(); ++index) {
    total_need += need_kg[index];
    if (is_seed[index] != 0) {
      const ResourceId id{static_cast<std::uint16_t>(index)};
      have_kg[index] =
          static_cast<float>(HeldEverywhere(world, id)) / static_cast<float>(kGramsPerKilogram);
    }
  }

  if (!(total_need > 0.0F)) {
    // Nothing is going into the ground, so no seed fund can be eaten away.
    // Reported as kStockNeverRunsOut because it is the same shape of answer,
    // and said here in words because the constant's own name talks about
    // consumption: here it is the DEMAND that is absent, not the eating.
    light.days_of_stock = kStockNeverRunsOut;
    light.light = LightFrom(light.days_of_stock, light.days_to_date, 0, false);
    return light;
  }

  // THE TIGHTEST CROP DECIDES, because you cannot sow oats with rye. Short of
  // any one of them is short, and the days are the days of the crop that runs
  // out of surplus first — netting them would answer about a village that
  // does not exist.
  bool short_now = false;
  float tightest_surplus = -1.0F;
  for (std::size_t index = 0; index < need_kg.size(); ++index) {
    if (!(need_kg[index] > 0.0F)) {
      continue;
    }
    const float surplus = have_kg[index] - need_kg[index];
    if (surplus < 0.0F) {
      short_now = true;
      break;
    }
    if (tightest_surplus < 0.0F || surplus < tightest_surplus) {
      tightest_surplus = surplus;
    }
  }
  if (short_now) {
    // Already short: the kSeedShort alarm stands beside this and says the
    // same thing about a particular field.
    light.days_of_stock = 0;
    light.light = LightFrom(0, light.days_to_date, 0, true);
    return light;
  }
  if (!(eating_kg_per_day > 0.0F)) {
    light.days_of_stock = kStockNeverRunsOut;  // nobody eats: the fund cannot be eaten
    light.light = LightFrom(light.days_of_stock, light.days_to_date, 0, false);
    return light;
  }
  // How long the village can go on eating before the tightest crop's grain
  // falls THROUGH its sowing norm. Not "how long the food lasts" — that is
  // the food light, and it answers a different question about the same heap.
  const float days = tightest_surplus / eating_kg_per_day;
  light.days_of_stock = days >= static_cast<float>(kStockForecastHorizonDays)
                            ? kStockForecastHorizonDays
                            : static_cast<std::int32_t>(days);
  light.light = LightFrom(light.days_of_stock,
                          light.days_to_date,
                          static_cast<std::int32_t>(config.farming.seed_light_margin_days),
                          false);
  return light;
}

}  // namespace core
