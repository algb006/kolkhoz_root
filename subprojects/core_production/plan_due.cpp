// One home for a plan position's due (plan_due.h).

#include "plan_due.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "core_catalog/table_value.h"
#include "core_tables/tables.h"
#include "district_plan.h"
#include "plan_alarms.h"

namespace core {
namespace {

/// The bands' upper edges (district §5: 0-20 «на карандаше», 61-80 «на
/// хорошем счету»), inclusive; above the last, the top band.
constexpr std::array<float, 4> kBandTops = {20.0F, 40.0F, 60.0F, 80.0F};

/// The campaign.csv keys of the band factors, in band order.
constexpr std::array<const char*, 5> kBandKeys = {"plan_band_factor_0_20",
                                                  "plan_band_factor_21_40",
                                                  "plan_band_factor_41_60",
                                                  "plan_band_factor_61_80",
                                                  "plan_band_factor_81_100"};

/// A position the district can price: a crop the tables have, with a yield.
bool Priceable(const ProductionConfig& config, const ProductionConfig::PlanPosition& position) {
  return position.crop.value < config.crops.size() &&
         config.crops[position.crop.value].yield_kg_per_ha > 0.0F;
}

}  // namespace

float PlanBandFactor(const ProductionConfig& config, const WorldState& world) {
  const float rating = world.chairman.raikom_reputation;
  std::size_t band = 0;
  while (band < kBandTops.size() && rating > kBandTops[band]) {
    ++band;
  }
  // ONE PRODUCT with the plan's weight (#1043; boss, the #1043 thread [2]).
  return config.plan_band_factor[band] * config.plan_total_factor;
}

float PlanPricedHectares(const ProductionConfig& config,
                         const WorldState& world,
                         std::size_t index,
                         float area_ha,
                         std::uint32_t year,
                         SimDay as_of) {
  // A crop with no yield still has its hectares (the uncovered lamp asks them,
  // PositionCovered); only the split's weights need a yield.
  if (index >= config.plan_positions.size() ||
      config.plan_positions[index].crop.value >= config.crops.size()) {
    return 0.0F;
  }
  float hectares = area_ha * config.plan_positions[index].area_share;
  const CropDef& own = config.crops[config.plan_positions[index].crop.value];
  // A crop the tables weigh nothing in grain keeps its fixed share: the split
  // keeps the total in grain and cannot weigh it.
  if (config.plan_split_by_sown_mix && GrainPerGram(config, own.resource) > 0.0) {
    // THE TOTAL IN GRAIN EQUIVALENT, each crop by its own yield (0.37.278;
    // boss on host's finding, the #1036 thread [9]; district.md §9, «Обязательство
    // не меняется: зерна должен столько же»): the split moves the positions,
    // never the grain owed. Weighed in crop kilograms (0.37.275) a year whose
    // rye did not stand owed 7.1 % less grain (1933 y4) - the potato's
    // kilograms are not the rye's grain. Two positions of one produce would
    // each read that produce's whole planned ha - the tables have none (named).
    double total_today = 0.0;
    double total_planned = 0.0;
    float planned_here = 0.0F;
    for (std::size_t other = 0; other < config.plan_positions.size(); ++other) {
      const ProductionConfig::PlanPosition& position = config.plan_positions[other];
      if (!Priceable(config, position)) {
        continue;
      }
      const CropDef& crop = config.crops[position.crop.value];
      const double grain = GrainPerGram(config, crop.resource);
      if (!(grain > 0.0)) {
        continue;
      }
      const double weight = static_cast<double>(crop.yield_kg_per_ha) * grain;
      const float planned = PlannedChainHectares(config, world, crop.resource, year, as_of);
      total_today += weight * static_cast<double>(area_ha * position.area_share);
      total_planned += weight * static_cast<double>(planned);
      if (other == index) {
        planned_here = planned;
      }
    }
    // NOTHING PLANNED AT ALL falls back to the fixed shares: the total can
    // never vanish by a decision not to sow (district.md §9, row 1199).
    if (total_planned > 0.0) {
      hectares =
          static_cast<float>(total_today / total_planned * static_cast<double>(planned_here));
    }
  }
  return hectares * PlanBandFactor(config, world);
}

Grams PlanDueGrams(const ProductionConfig& config,
                   const WorldState& world,
                   std::size_t index,
                   float area_ha,
                   std::uint32_t year,
                   SimDay as_of) {
  if (index >= config.plan_positions.size() || !Priceable(config, config.plan_positions[index])) {
    return 0;
  }
  const CropDef& crop = config.crops[config.plan_positions[index].crop.value];
  // The multiplication in PlanPositionGrams's own order, so the figure off
  // is the figure of today to the gram.
  const float hectares = PlanPricedHectares(config, world, index, area_ha, year, as_of);
  return GramsFromKilograms(crop.yield_kg_per_ha * hectares * config.plan_grain_share);
}

bool ParsePlanDue(const ITableSet& tables, ProductionConfig& config, std::string& error) {
  const ITable* const campaign = tables.FindTable("campaign");
  if (campaign == nullptr) {
    return true;
  }
  const std::uint32_t value_col = campaign->FindColumn("value");
  float split = config.plan_split_by_sown_mix ? 1.0F : 0.0F;
  if (!CellOrDefault(*campaign,
                     campaign->FindRowByKey("plan_split_by_sown_mix"),
                     value_col,
                     Range{.low = 0.0F, .high = 1.0F},
                     split,
                     split,
                     error)) {
    error = "campaign: plan_split_by_sown_mix: " + error;
    return false;
  }
  config.plan_split_by_sown_mix = split > 0.5F;
  for (std::size_t band = 0; band < kBandKeys.size(); ++band) {
    float factor = config.plan_band_factor[band];
    if (!CellOrDefault(*campaign,
                       campaign->FindRowByKey(kBandKeys[band]),
                       value_col,
                       Range{.low = 0.0F, .high = 5.0F},
                       factor,
                       factor,
                       error)) {
      error = std::string("campaign: ") + kBandKeys[band] + ": " + error;
      return false;
    }
    config.plan_band_factor[band] = factor;
  }
  // The plan's weight (#1043), on the same scale as a band's factor.
  if (!CellOrDefault(*campaign,
                     campaign->FindRowByKey("plan_total_factor"),
                     value_col,
                     Range{.low = 0.0F, .high = 5.0F},
                     config.plan_total_factor,
                     config.plan_total_factor,
                     error)) {
    error = "campaign: plan_total_factor: " + error;
    return false;
  }
  return true;
}

}  // namespace core
