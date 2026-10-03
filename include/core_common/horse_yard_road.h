/// @file
/// @brief The road of a day with a horse: from home on foot to the horse yard,
///        and from there on the horse to the work (livestock design §5, «Наряд
///        с лошадью начинается и кончается на конном дворе»).
/// @threading PARALLEL_READONLY
/// A pure read of the world (herds, units, the road index); called from the
/// labour hour, the morning placement and the resident's activity, all of
/// them sequential slots or between steps.
///
/// ONE HOME FOR THREE READERS (0.37.158): the labour hour takes the road off
/// the day's light, the placement judges by it who may be sent, and the
/// activity says where a man is — asked three ways, a ploughman would be sent
/// by one road and work by another (time design §7: «Порог и норма считают
/// одно и то же плечо»).

#ifndef CORE_COMMON_HORSE_YARD_ROAD_H_
#define CORE_COMMON_HORSE_YARD_ROAD_H_

#include "core_common/geometry.h"
#include "core_common/ids.h"

namespace core {

struct WorldState;
struct WorkAssignment;

/// @brief Where the kolkhoz team stands, once it is stabled: the unit of the
///        kolkhoz horse herd (StableHorses moves every kolkhoz horse there).
/// @param horse_kind The livestock row of the horse; an invalid id answers
///        false.
/// @param yard Written only when the answer is true.
/// @return False before the team is stabled (`chairman.horses_stabled`), and
///         when its unit is gone or still pegs and string — then a horse is
///         taken where the old rule took it, at home.
bool HorseYardPositionOf(const WorldState& world, LivestockKindId horse_kind, Vec2& yard);

/// @brief True when this assignment's day begins and ends at the horse yard:
///        the team is stabled and he holds a horse — a ploughman or a harrower
///        (IsHorseWork), or a hand the placement wrote a horse on
///        (WorkAssignment::rides_horse: a carter with a horse, a mower with
///        one, the brigade cart's driver).
/// @note NOT the brigade's passengers and not the fellers: they hold no
///       horse, and until the cart carries people (routing stage A) they set
///       out from home as before — an approximation, named.
bool DayStartsAtHorseYard(const WorldState& world, const WorkAssignment& work);

/// @brief Game hours, one way, of the road that takes light from the work.
///        A day at the horse yard: on foot home → yard, then by the work's
///        riding mode (WorkTravelMode) yard → work. Any other day: home →
///        work by WorkTravelMode, as since 0.36.2.
/// @param walk_hours_per_km, ride_hours_per_km Game hours a kilometre on foot
///        and behind a harness (the labour config's HoursPerKm).
/// @note A CARTER'S RIDE TO THE LOAD IS NOT IN IT when his day starts at the
///       yard: it is his first trip's empty half, which the load's seam
///       already prices (haul.h; 0.37.139) — only the walk to the horse is
///       his road. A carter's day that starts at home answers the whole way,
///       as before; the labour hour does not take it off his light.
float WorkRoadHours(const WorldState& world,
                    const WorkAssignment& work,
                    LivestockKindId horse_kind,
                    Vec2 home,
                    Vec2 target,
                    float walk_hours_per_km,
                    float ride_hours_per_km);

}  // namespace core

#endif  // CORE_COMMON_HORSE_YARD_ROAD_H_
