/// @file
/// @brief The district plan's alarms: a position the chains will not cover
///        (kPlanPositionUncovered) and a position the turn will not bring to
///        the met share (kPlanPositionShort), and what next year's own
///        harvest will not pay (NextYearUnpaidGrams) — the hold the goods
///        loan, the herds and the people's issue keep.
/// @threading SINGLE_THREADED
/// Called BETWEEN steps, off the completed buffer, from the sim thread — and
/// at the year's turn by RepayGoodsLoans, inside the production phase, which
/// only reads through it.
///
/// Its own translation unit since 0.36.39: the plan's walk left
/// production_alarms.cpp at 1011 lines once lost slots were counted. It reads
/// the world and the configuration by const reference and owns no state,
/// for the reason production_alarms.h gives.

#ifndef CORE_PRODUCTION_PLAN_ALARMS_H_
#define CORE_PRODUCTION_PLAN_ALARMS_H_

#include <cstdint>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Appends kPlanPositionUncovered for every position of the district's
/// plan and every one of the three calendar years ahead (0 this one, 1 next,
/// 2 the one after) in which the arable grows its crop on fewer hectares than
/// worked arable × area share × plan share (alarm_state.h; year 0 priced off
/// last year's worked arable) AND the stores above the seed do not hold it
/// either (0.37.4; boss-core-epoch1-queue [49] (б)): per produce, this year's
/// debt first, then year by year the positions the fields leave, each held
/// with the rot of its wait (HeldForDeliveryGrams) — the measure the goods
/// loan keeps by. A slot already lost does not count (question
/// 278; boss-core-epoch1-resume [98]), and a chain that stands before its
/// first season is read by the year its first slot is grown in. On the
/// year's last day, also kPlanPositionShort for every position that
/// delivered plus takeable (TakeableGrams) will not bring to the met share.
/// @param alarms Appended to; never cleared.
void CollectPlanAlarms(const ProductionConfig& config,
                       const WorldState& world,
                       std::vector<Alarm>& alarms);

/// @brief Grams of `resource` NEXT year's own harvest will not pay of what
/// next year needs out of it (0.37.2; boss-core-epoch1-queue [52]-[54]): the
/// district's positions of it, priced off the area next spring's figure will
/// be (NextPlanAreaHa), and the seed of the year after's crops of it, which
/// next year's harvest gives — against next year's chain hectares at a normal
/// yield (neutral fertility), a slot already lost growing nothing. What the
/// herds may not eat today (herd_system.h, FeedAllowance): the rotation
/// gives oats 19 t one year and 3.7 t the next, and the team that ate the
/// good year's carry-over left the lean year's position short of its seed.
/// @param as_of The day `next year` is counted from.
Grams NextYearUnpaidGrams(const ProductionConfig& config,
                          const WorldState& world,
                          ResourceId resource,
                          SimDay as_of);

/// @brief Grams of `resource` next year's district positions will ask: each
/// position of it priced off the area next spring's figure will be
/// (NextPlanAreaHa), as the January letter prices it. No rot margin. The
/// first half of NextYearUnpaidGrams, and what the turn seals
/// (TurnPlanSealOf below).
/// @param as_of The day `next year` is counted from.
Grams NextPlanOwedGrams(const ProductionConfig& config,
                        const WorldState& world,
                        ResourceId resource,
                        SimDay as_of);

/// @brief What next year's plan reserve will seal at the coming turn
/// (IProductionSystem::TurnPlanSeal; 0.37.37; labor-payment §7, «Что
/// делится»): for every produce of a crop, next year's positions of it
/// (NextPlanOwedGrams) less what next year's chains will give at a normal
/// yield — since 0.37.39 the rung holds only that before the harvest
/// (PlanRungGrams) — with the rot of the wait to the end of next year, as
/// NextYearHold holds its grams (HeldForDeliveryGrams). Dense by ResourceId.
/// Never above NextYearHold, which adds the year after's seed: under today's
/// hold the issue's max never picks it (named in the definition).
ResourceAmounts TurnPlanSealOf(const ProductionConfig& config, const WorldState& world);

/// @brief What this calendar year's harvest will still bring into the stores
/// (IProductionSystem::HarvestToComeThisYear; 0.37.39; labor-payment §7, the
/// rung before the harvest): per field, its heap; the crop in hand at its own
/// estimate (StandingYieldGrams), unless it is a winter crop sown this autumn;
/// else the chain's crop of this year still to be sown, at the table's yield
/// on the field's soil, unless its slot is lost. LESS the seed next year's
/// sowings take from this harvest that the seed rung does not hold, never
/// below nought — the plan and the seed are not paid by the same grain.
/// Dense by ResourceId.
ResourceAmounts HarvestToComeThisYearOf(const ProductionConfig& config, const WorldState& world);

}  // namespace core

#endif  // CORE_PRODUCTION_PLAN_ALARMS_H_
