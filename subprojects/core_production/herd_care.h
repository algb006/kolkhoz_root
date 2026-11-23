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

#include "core_common/herd_care_loss_view.h"
#include "core_common/world_state.h"

namespace core {

struct FarmingConfig;
struct ProductionConfig;

/// @brief The care curve: 1 at `coverage` >= the full line, `floor` at <= the
///        floor line, linear between.
/// @param floor The kind's floor, 0..1; 1 makes the curve flat.
/// @param coverage HerdRow::care_coverage, 0..1.
float CareFactor(const FarmingConfig& farming, float floor, float coverage);

/// @brief The care curve with its full line at `full` instead of the common one: the line
///        FullCareCoverage gives a kolkhoz herd's PRODUCE (the milk).
float CareFactorAt(const FarmingConfig& farming, float floor, float coverage, float full);

/// @brief «ФЕРМА ДЕРЖИТСЯ НА НЕЙ» (avatars.md, the acting chairman; #1484): the care coverage
///        at which a kolkhoz herd's produce is full today - the chairman's avatar's
///        herd_full_care_threshold while the village has at most its
///        herd_full_care_max_households households (0 = no limit), else the common
///        farming herd_care_full_coverage. An avatar with threshold 0 always gets the common one.
float FullCareCoverage(const ProductionConfig& config, const WorldState& world);

/// @brief The settlement's draught under undone care, one number as the
///        ration's and the cold's are: the kolkhoz horses' care factor on the
///        kind's draught floor, weighted by heads. 1 with no kolkhoz horse.
float CareDraughtFactor(const ProductionConfig& config, const WorldState& world);

/// @brief #1848: undone care's price at `unit` (core_common/herd_care_loss_view.h) - the
///        lowest coverage of its kolkhoz herds, the highest produce loss by the herd day's own
///        curve (CareFactorAt at FullCareCoverage), and the settlement's draught where a
///        kolkhoz horse herd stands. `measured` false with no kolkhoz herd at the unit.
HerdCareLossView HerdCareLossAt(const ProductionConfig& config,
                                const WorldState& world,
                                UnitId unit);

}  // namespace core

#endif  // CORE_PRODUCTION_HERD_CARE_H_
