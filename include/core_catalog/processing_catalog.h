/// @file
/// @brief The processing catalogue: the shops of epoch I — the sauerkraut shop,
/// the smokehouse and the workshops' cooperage — as recipes out of
/// production.csv and production_io.csv, and the resource properties the
/// barrels and the stores' room need (resources.csv `space_factor`,
/// `in_barrel`; world_params.csv `barrel_capacity_kg`, `barrel_wear_per_year`,
/// `sauerkraut_fresh_share`).
/// @threading PARALLEL_READONLY
/// Parsed once at assembly on the sim thread; read-only afterwards.
///
/// WHY IN THE CATALOGUE. Two modules read it and neither owns the other: the
/// accountant (core_labor) puts the parent's post holders on a shop, no more
/// than its places; production drains their man-days into batches. The
/// sawmill's places live in the timber catalogue for the same reason. The
/// room a resource takes is read by production's door and by genesis, which
/// checks that the start set fits.
///
/// THE WORD OF THE DESIGN (registers 239-240; production units §8а; the
/// human's word of 2026-09-19, relayed by econ: «Квашню тоже делаем в Эпохе
/// I», «Коптильню делаем в Эпохе I», «Бондарни отдельной нет, есть колхозные
/// мастерские»). Numbers are econ's estimate, accepted by boss (seq 183-189).
///
/// THE SAWMILL IS NOT HERE (boss seq 184, question 6): its door converts logs
/// by volume and yield, and it stays its own. A production.csv recipe with no
/// input or no output (sawing) is skipped.

#ifndef CORE_CATALOG_PROCESSING_CATALOG_H_
#define CORE_CATALOG_PROCESSING_CATALOG_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_tables/tables.h"

namespace core {

/// @brief One resource and its grams in one batch of a recipe.
struct ProcessingAmount {
  ResourceId resource;
  Grams grams = 0;
};

/// @brief What a recipe is FOR, where the design gives it a rule of its own
/// beyond «work while the inputs last». Read from the recipe's key; a recipe
/// the core knows no rule for is kPlain.
enum class ProcessingRule : std::uint8_t {
  /// Works while every input lasts and the outputs have room and barrels.
  kPlain = 0,

  /// `pickling` — September to December only, and only the vegetables above
  /// the fresh reserve: sauerkraut_fresh_share of the vegetables harvested
  /// this year (econ: sauerkraut beats fresh only past ~77 days of keeping,
  /// so the village's own winter is eaten fresh).
  kPickling,

  /// `cooperage` — barrels on demand: while the free barrels fall short of
  /// what the vegetables in the stores would fill as sauerkraut, and with
  /// boards and steel only above what the started building sites still lack
  /// (boss seq 184, question 2).
  kCooperage,

  /// NOT A RULE: the count, for a consumer's mirror.
  kProcessingRuleCount,
};

/// @brief One recipe of production.csv with its inputs and outputs.
struct ProcessingRecipe {
  /// production.csv `key` (pickling, cooperage, smoking_meat, smoking_fish).
  std::string key;

  /// The module that works it (production.csv `unit`).
  UnitTypeId unit_type;

  ProcessingRule rule = ProcessingRule::kPlain;

  /// Game man-days per batch (production.csv `labor_days`).
  float labor_days = 0.0F;

  /// The parent's post holders who may work it at once (production.csv
  /// `places`; blank is 1).
  std::uint16_t places = 1;

  /// Per batch, in grams — production_io.csv `amount` in the resource's own
  /// measure times resources.csv `kg_per_unit`.
  std::vector<ProcessingAmount> inputs;
  std::vector<ProcessingAmount> outputs;
};

/// @brief One line of unit_level_cost.csv, in grams: what building `level`
/// of `unit_type` takes of `resource`. Read here for kCooperage's «only above
/// what the started sites still lack» (boss seq 184).
struct LevelCost {
  UnitTypeId unit_type;
  std::uint8_t level = 0;
  ResourceId resource;
  Grams grams = 0;
};

/// @brief The catalogue. Defaults are the design's figures, kept for a world
/// with no tables; a shipped build reads them.
struct ProcessingCatalog {
  /// production.csv order; only recipes with at least one input and output.
  std::vector<ProcessingRecipe> recipes;

  /// The room one gram takes in a store, by ResourceId (resources.csv
  /// `space_factor`, blank is 1): sauerkraut 0.5 — «намного компактнее
  /// сырой». Entries past the end are 1.
  std::vector<float> space_factor;

  /// Whether the resource lives in barrels, by ResourceId (resources.csv
  /// `in_barrel`): sauerkraut, smoked meat, smoked fish — one shared pool.
  std::vector<std::uint8_t> in_barrel;

  /// resources.csv `barrel`; invalid when the tables have none, and then no
  /// shop waits for barrels.
  ResourceId barrel_resource;

  /// The mass of one barrel (resources.csv `barrel` kg_per_unit, 15 kg): the
  /// stores hold barrels as grams, and a barrel is counted by this.
  Grams barrel_grams = 15'000;

  /// What one barrel holds (world_params.csv `barrel_capacity_kg`, 100 kg).
  Grams barrel_capacity_grams = 100'000;

  /// The share of the barrels lost at the year's turn (world_params.csv
  /// `barrel_wear_per_year`, 0.1). STUB (econ's estimate).
  float barrel_wear_per_year = 0.1F;

  /// kPickling's fresh reserve (world_params.csv `sauerkraut_fresh_share`,
  /// 0.25). STUB until host's measure.
  float sauerkraut_fresh_share = 0.25F;

  /// unit_level_cost.csv, every line (kCooperage asks it of started sites).
  std::vector<LevelCost> level_costs;

  /// kPickling's season, 0-based months inclusive: September to December
  /// (world_params.csv `sauerkraut_from_month`, `sauerkraut_to_month`, human
  /// 1..12 — econ: «сезон — только осень, с сентября по декабрь»).
  std::uint8_t pickling_from_month = 8;
  std::uint8_t pickling_to_month = 11;

  /// kCooperage's season, 0-based months inclusive: July to December
  /// (world_params.csv `cooperage_from_month`, `cooperage_to_month`, human
  /// 1..12 — production units §8а, «Когда»: «с июля по декабрь и только пока
  /// свободных бочек меньше нужды»). Outside it the cooper makes nothing.
  std::uint8_t cooperage_from_month = 6;
  std::uint8_t cooperage_to_month = 11;

  /// Dense by ResourceId: 1 where resources.csv `spoilage` is `very_fast`
  /// (meat, fish, milk — two days). A shop whose main input is such works
  /// the day it arrives (boss, host-econ-shops seq 31 on econ seq 30):
  /// «после дневной выдачи и до ночной порчи», not tomorrow.
  std::vector<std::uint8_t> very_fast;
};

/// @brief Whether `recipe`'s main input spoils very fast, so its shop works
/// the day the input arrives (ProcessingCatalog::very_fast).
bool WorksTheSameDay(const ProcessingCatalog& catalog, const ProcessingRecipe& recipe);

/// @brief The world_params.csv keys this catalogue reads, for the assembly's
/// check that every row declared for the core has a reader.
std::span<const std::string_view> ProcessingWorldParamKeys();

/// @brief Reads the catalogue. A missing table keeps the defaults; a present
/// one with an unknown unit, resource or direction is an error named by row.
bool ParseProcessingCatalog(const ITableSet& tables,
                            ProcessingCatalog& catalog,
                            std::string& error);

/// @brief How many may work at once at a shop of `type`: the most places of
/// the recipes it works; 0 when it works none.
std::uint32_t ProcessingPlaces(const ProcessingCatalog& catalog, UnitTypeId type);

/// @brief The room `grams` of `resource` take in a store (× space_factor).
Grams RoomTaken(const ProcessingCatalog& catalog, ResourceId resource, Grams grams);

/// @brief The grams of `resource` that fit in `room` (÷ space_factor).
Grams GramsFitting(const ProcessingCatalog& catalog, ResourceId resource, Grams room);

}  // namespace core

#endif  // CORE_CATALOG_PROCESSING_CATALOG_H_
