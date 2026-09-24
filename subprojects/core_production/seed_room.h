/// @file
/// @brief The room booked for missing seed (boss, boss-core-epoch1-4 seq 26,
/// 33, 34): «семенное ложится в хранилище первым» (difficulty design §4).
/// @threading SINGLE_THREADED
/// Read by the day's hauling settlement and the field alarms, both on the sim
/// thread in the decisions slot (phase 3); nothing here writes except the
/// door, DeliverHeapToStores, which runs inside SettleHauling.
///
/// THE RULE. A crop whose next sowing the stores cannot cover, while its
/// harvest is still out there — standing, or reaped and lying in a heap —
/// books the room its shortfall needs. Another crop's heap may not take that
/// room: it sees the stores' free room less the part of the booking the seed
/// cannot lay in stores of its own. The booking ends when the seed is in or
/// the harvest is gone. The snow's rules are untouched: whatever heap is left
/// lying is taken by the lying snow, as before.
///
/// Measured before it (seq 33): seed 1939, a village with no orders, year 1.
/// Oats, barley and wheat were carted into the church on days 29-32; the
/// potato was dug on day 33 into a church already full, the room opened by
/// hundreds of kilograms in ten days, and the lying snow wrote off 97 t —
/// the next spring's seed with it.
#ifndef CORE_PRODUCTION_SEED_ROOM_H_
#define CORE_PRODUCTION_SEED_ROOM_H_

#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Grams of seed ONE field's next sowing asks, and which seed: 0 and
/// an invalid `seed` when it is not arable, has nothing to sow, or its crop
/// sows by labour alone. The one rule of "a field's seed need" — the seed
/// alarm's share and the booking's sum both read it.
Grams FieldSeedNeed(const ProductionConfig& config,
                    const WorldState& world,
                    const FieldRow& field,
                    ResourceId& seed);

/// @brief Grams of seed every field's NEXT sowing asks, summed by resource
/// (farming design §7: «площади следующего года × норма высева, по каждой
/// культуре»). By resource and not by crop: winter and spring wheat sow one
/// grain. The seed alarm and the booking read this one sum.
/// @return Dense by ResourceId, sized `config.feed_values.size()`.
std::vector<Grams> SeedNeedByResource(const ProductionConfig& config, const WorldState& world);

/// @brief The seed the stores must keep TODAY for the next sowing: the need
/// of SeedNeedByResource, but only for a seed whose next sowing ends before
/// its next harvest begins. A seed the harvest brings first (the winter rye,
/// reaped in July and sown in September) holds nothing today — the sowing
/// takes it from that harvest. What the district's delivery may not take
/// (DeliverableAboveSeed; boss seq 5, item 8).
/// @return Dense by ResourceId, sized `config.feed_values.size()`.
std::vector<Grams> SeedHeldToSowing(const ProductionConfig& config, const WorldState& world);

/// @brief The room each resource books for its missing seed: the need of
/// its next sowings less what the stores hold, and only while some of its
/// harvest is still out — no more than the heaps hold once nothing of it
/// stands. 0 for a resource that is covered or has nothing out.
///
/// THE NEXT SOWING AND NOT THE SEED RUNG (fund_ladder.h), and the difference
/// is the autumn: the rung holds the sowings before the year's turn, so at the
/// potato digging it holds nothing of the next spring's potato — the very
/// seed the booking exists for. The need carries its ROT MARGIN to the end
/// of the seed's sowing window (RotMarginGrams, the seed fund's own rule).
/// @return Dense by ResourceId, sized `config.feed_values.size()`.
std::vector<Grams> SeedRoomBooked(const ProductionConfig& config, const WorldState& world);

/// @brief The room a heap of `resource` may be carted into: the receivable
/// room (ReceivableRoom) less, for every OTHER resource with a booking, the
/// part of that booking its seed cannot lay in stores that do not take
/// `resource`. Unbounded when an outline is the resource's home.
Grams HeapRoom(const ProductionConfig& config,
               const WorldState& world,
               ResourceId resource,
               const std::vector<Grams>& booked);

/// @brief THE DOOR for a heap: first the numbered stores that take no
/// booked seed, then the door as always (DeliverToStores). The caller offers
/// no more than HeapRoom allows, and the order is what keeps the shared room
/// for the seed: without it the first store in row order — the church —
/// would take the grain while a granary stood empty.
/// @return Grams actually delivered.
Grams DeliverHeapToStores(WorldState& world,
                          const ProductionConfig& config,
                          ResourceId resource,
                          Grams amount,
                          const std::vector<Grams>& booked);

/// @brief kSeedHasNoRoom for every seed whose booking holds another crop's
/// heap on its field today: a heap lies whose HeapRoom is smaller than its
/// ReceivableRoom by that seed's part, and larger than what the booking
/// leaves it. One alarm per seed, however many heaps it holds back.
void CollectSeedRoomAlarms(const ProductionConfig& config,
                           const WorldState& world,
                           std::vector<Alarm>& alarms);

}  // namespace core

#endif  // CORE_PRODUCTION_SEED_ROOM_H_
