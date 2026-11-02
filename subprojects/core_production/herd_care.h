/// @file
/// @brief «Недоуход»: what a month of undone herd care costs - the cow's milk,
/// the horse's draught, any herd's produce (#1443).
/// @threading PARALLEL_READONLY
/// Pure reads of a herd row or of the world; called from production's herd
/// day and the horse work's pricing (production phase, slot 4). They write
/// nothing.
///
/// Design: econ, econ/manual/proposals/herd-care-undone-2026-11-02.md; the
/// human's «Делать» (#1623) via boss. «Недоенная корова сбавляет молоко,
/// некормленая-нечищеная лошадь тянет хуже»: a herd's care coverage over the
/// last month (HerdRow::care_coverage, written by labour at the day's close)
/// scales its product - whole at or above farming's herd_care_full_coverage
/// (0.9), the kind's floor at or below herd_care_floor_coverage (0.5), linear
/// between. The floors are livestock.csv care_produce_floor (the cow 0.6,
/// other herds 0.8) and care_draught_floor (the horse 0.85); an empty column
/// is 1, no price. Nothing dies of it: no deaths, no disease, no lost births.

#ifndef CORE_PRODUCTION_HERD_CARE_H_
#define CORE_PRODUCTION_HERD_CARE_H_

#include "core_common/world_state.h"

namespace core {

struct FarmingConfig;
struct ProductionConfig;

/// @brief The care curve: 1 at `coverage` >= the full line, `floor` at <= the
///        floor line, linear between.
/// @param floor The kind's floor, 0..1; 1 makes the curve flat.
/// @param coverage HerdRow::care_coverage, 0..1.
float CareFactor(const FarmingConfig& farming, float floor, float coverage);

/// @brief The settlement's draught under undone care, one number as the
///        ration's and the cold's are: the kolkhoz horses' care factor on the
///        kind's draught floor, weighted by heads. 1 with no kolkhoz horse.
float CareDraughtFactor(const ProductionConfig& config, const WorldState& world);

}  // namespace core

#endif  // CORE_PRODUCTION_HERD_CARE_H_
