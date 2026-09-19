/// @file
/// @brief Internal to core_residents: the ladder a family without a roof goes
/// down (housing design §20), the chairman's answer to its request for the
/// certificate, and the house held for a specialist.
/// @threading SINGLE_THREADED
/// Called from the residents' decisions sub-step (phase 3), where every
/// structural change of the settlement is made: rows of families and
/// residents are added, moved and removed here.
///
/// THE LADDER, top to bottom, and the game goes down it by itself to the
/// first rung that holds (§20): a free house; the barrack (the next delivery
/// of this stage — the rung is skipped until then); a tent on the old plot in
/// the warm months; and in the cold, THE REQUEST FOR THE CERTIFICATE — the
/// human's word of 2026-09-19, «Без подписи председателя уехать нельзя».
/// Signed, the family leaves for good; refused, or unanswered for
/// `leave_request_answer_days`, it is LODGED with kin, or with the nearest
/// neighbour, one lodged family to a house while any other is free. Every
/// morning the lodged climb first, then the rest.
///
/// WHAT LODGING COSTS IS NOT HERE, and says so: the design's two multipliers
/// (comfort of both families, health in the cold) have nowhere to act — the
/// core has no housing comfort (phase 2) and simulates no cold (ResidentRow::
/// cold stays at its default). A tent costs nothing today either. Both wait
/// for those metrics; keys without a reader would be rules that cannot fire.

#ifndef CORE_RESIDENTS_HOUSING_LADDER_H_
#define CORE_RESIDENTS_HOUSING_LADDER_H_

#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "life_config.h"

namespace core {

/// @brief Whether a family may live in a tent in `month`: inside
/// `tent_from_month`..`tent_to_month`. Outside is «the cold».
bool TentWeather(const LifeConfig& config, Month month);

/// @brief The day's ladder for every family without a roof of its own: the
/// lodged first (a free house takes them out), then the rest — a free house,
/// a tent in the warm months, and in the cold a request for the certificate
/// (kLeaveRequested) that silence refuses after `leave_request_answer_days`.
/// @note Runs first in the day, so a house freed yesterday goes to a family
///       out in the open before any couple.
void RunRoofless(const LifeConfig& config, WorldState& current);

/// @brief Into a barrack place (housing §9): `house` is the barrack, its
/// `household` stays unset — many families share it — `in_barrack` is set,
/// the ladder's other marks are cleared, and the family's own herds go to the
/// kolkhoz (no yard, no animals; boss seq 197). Shared with the wedding and
/// the migrant's arrival, who take a barrack place when no house is free.
void MoveIntoBarrack(WorldState& current, std::uint32_t family_row, UnitId barrack);

/// @brief kReserveHouse and kAnswerLeaveRequest (order_state.h), in whatever
/// hour they came: a house marked or unmarked for a specialist; a family
/// sent away with the certificate, or lodged.
void ConsumeHousingOrders(const LifeConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_HOUSING_LADDER_H_
