/// @file
/// @brief A farm's own store of coarse feed (body: feed_home.cpp): «each
///        farm its own rick» (routing stage В3, step two; the human's word of
///        14 October 2026: «что мешает хранить сено на ферме? В реале так и
///        есть, должно быть свое хранилище кормов»; econ's
///        farm-feed-stock-lighter-2026-10-14, way 2; boss's ruling on the
///        reading, the same day). Private to core_production.
///
/// WHAT THE STEP IS. Until it a kolkhoz herd eats its hay out of THE VILLAGE:
/// its own unit, the first stock yard, then every store in row order
/// (herd_system.cpp, TakeFeed) - a rick at the far end of the village is at
/// every manger. From the step:
///   * the feeds A RICK IS THE HOME OF (resource_stores.csv: hay, straw,
///     reed - whatever the tables name for an outline that is hay's home) are
///     BOUND: a kolkhoz herd takes them from ITS FARM's rick - the nearest
///     rick by road from the unit the herd is fed at - after its own unit's
///     stock and before nothing else;
///   * EVERY OTHER FEED (oats, compound feed, roots) is eaten by today's
///     chain, from the village's stores, unchanged;
///   * the binding is A FUNCTION OF THE MAP - the units' places and the
///     roads - recomputed where it is asked. NOTHING IS SAVED;
///   * a farm's rick is refilled FROM THE MEADOWS' STACKS: the meadow's
///     ordinary carting goes to the nearest rick by road from the meadow
///     (field_haul.h, HeapRickRow), and when a farm's rick is SHORT by the
///     urgent rule (labor.csv `hay_cart_urgent_days`, now asked per rick) a
///     stack is carted ahead of all work TO THAT RICK.
///
/// FORM (b), THE ONE BUILT FIRST (boss): THERE IS NO CARTING FROM RICK TO
/// RICK. When a farm's rick is out of a bound feed, no stack holds it and
/// ANOTHER rick does, the herd eats from that other rick BY THE OLD CHAIN -
/// with no cart on the road - and the day is counted (FarFeedDay, below):
/// herd-days «ate from a far rick with no road», with the distance. That
/// count is the step's own instrument: near nought, the form stands; not
/// near nought (econ names the number before the arm reads), rick-to-rick
/// carting is built as a new mode of a store's emptying seam, which widens a
/// saved byte's range and is VERSION_SAVE 162 - form (a), not this contract.
///
/// THE TWO EXCEPTIONS, named so nobody looks for a rule there:
///   * A HERD WITH NO UNIT (the start's stock billeted in yards) is fed at
///     the first stock yard and binds THROUGH that yard. In a village with
///     no stock yard at all it has nothing to bind to and keeps the whole old
///     chain; its heads are printed.
///   * A HOUSEHOLD's HERD is not touched: it eats the family's pantry and the
///     yard's own hay (core_residents, household_plot.cpp).
///
/// THE FIELD's STRAW: it reaches the stores by no road today (field_work.cpp,
/// DeliverToStores in row order - routing stage В4's row). With ricks per
/// farm its landing is NAMED: the nearest rick by road from the field, as
/// the meadow's hay - still without a cart until В4 gives it one.
///
/// THE INTERFACE CHANGE THAT COMES WITH THE BODY (an event, said here first):
/// IProductionSystem gains
///   virtual std::vector<ShortRickId> ShortRicksWithin(const WorldState& world,
///                                                    std::uint32_t days) const = 0;
/// - EVERY rick whose bound herds will be out of a bound feed within `days`
/// (the forecast's own drain, herd by herd, against THAT rick's stock and
/// the village's stores for the unbound feeds), each with its days left, by
/// UnitId; empty when none is. It stands BESIDE StoredHayShortWithin, which
/// keeps its meaning («the village's stores») for `hay_cart_need_days` and
/// `hay_cart_in_cut_days`; labour's callback for `hay_cart_urgent_days`
/// changes from «is the village short» to «which ricks are short», and each
/// stack's job carries the nearest short rick as its destination. The event
/// kStackCartUrgent then also names the rick (SimEvent::unit), and is said
/// once a RICK a day.
///
/// THE TWO SEAM WORDS OF THE STEP (proposed keys; boss enters them with the
/// delivery): `stack_cart_urgent` (0.37.254, here gaining the rick) and
/// `herd_ate_from_far_rick` - the far-rick day, below.
///
/// WHAT THE STEP DOES NOT DO: no buffer in days, no daily leg, no task kind,
/// no save change; the lamps read the village's hay as before; the last
/// metres from a rick to its manger are not a job (the rick by the farm's
/// gate IS the manger's store) - the distance from each farm to its rick is
/// printed, and the run chairman stakes a rick with each farm it raises on
/// the map's suggested places (tests/run/common/fixture_policy.h).
/// @threading SINGLE_THREADED
/// The feeding asks it in the production slot's herd day (one herd at a
/// time, sequential); labour's question comes through the assembly in the
/// decisions slot. Pure reads but for the day's count.

#ifndef CORE_PRODUCTION_FEED_HOME_H_
#define CORE_PRODUCTION_FEED_HOME_H_

#include <cstdint>
#include <span>
#include <vector>

#include "core_common/herd_state.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Whether `resource` is a BOUND feed: one an outline that is hay's
///        home is also the home of (hay, straw, reed by the shipped tables).
/// @return False for every feed with a numbered home, for an invalid
///         resource, and in a table set with no resource_stores table - a
///         hand-built world binds nothing and eats by the old chain.
bool IsBoundFeed(const ProductionConfig& config, ResourceId resource);

/// @brief The row of the unit a kolkhoz herd is fed at: its own unit, or the
///        first stock yard for a herd with none.
/// @return kNoRow for a household's herd, and for a kolkhoz herd with no unit
///         in a village with no stock yard.
std::uint32_t FarmRowOf(const WorldState& world,
                        const ProductionConfig& config,
                        const HerdRow& herd);

/// @brief A farm's rick: the nearest by road, from the unit at `farm_row`, of
///        the built outlines that are hay's home. The cart's way when the
///        settlement has carts, the carrier's when it has none - the way a
///        load would take, as HeapRickRow measures the meadow's.
struct FeedHome {
  std::uint32_t rick_row = kNoRow;  ///< kNoRow: no rick stands in the village.
  float road_km = 0.0F;             ///< The way from the rick to the farm, effective km.
};

/// @pre `farm_row` is a row of world.units.
/// @return Ties go to the first rick in row order - one answer on every
///         machine.
FeedHome FeedHomeOf(const ProductionConfig& config,
                    const WorldState& world,
                    std::uint32_t farm_row);

/// @brief What a bound herd may take of a bound feed TODAY, by source - the
///        feeding's own split, so the day's count and the take cannot part.
struct BoundTake {
  Grams from_own_unit = 0;  ///< The herd's own unit's stock, as before the step.
  Grams from_its_rick = 0;  ///< Its farm's rick.
  /// From any OTHER store by the old chain, with no cart on the road: a far
  /// rick, when the farm's own is out. Counted (FarFeedDay).
  Grams from_far = 0;
};

/// @brief Takes up to `wanted` grams of the bound feed `resource` for the
///        herd fed at `farm_row`: its own unit, then its rick, then - form
///        (b)'s fallback - the village's stores by the old chain.
/// @pre IsBoundFeed(config, resource); `farm_row` is a row of world.units.
/// @return What was taken, by source; the three sum to no more than `wanted`.
/// @note Side effect: the units' stocks. The allowance and the books are the
///       caller's (TakeFeed), as for every feed.
BoundTake TakeBoundFeed(const ProductionConfig& config,
                        WorldState& world,
                        std::uint32_t farm_row,
                        ResourceId resource,
                        Grams wanted);

/// @brief TakeBoundFeed with the rick ALREADY KNOWN - the herd day finds a
///        herd's rick once and feeds every bound link by it (one road
///        measured a herd-day, not one a link).
/// @param rick_row The farm's rick, or kNoRow when none stands.
/// @param far_row Written, when given and something came by the fallback: the
///        first unit it took from; kNoRow when only a numbered store gave.
BoundTake TakeBoundFeedAt(const ProductionConfig& config,
                          WorldState& world,
                          std::uint32_t farm_row,
                          std::uint32_t rick_row,
                          ResourceId resource,
                          Grams wanted,
                          std::uint32_t* far_row);

/// THE FLOOR OF A FAR-RICK DAY (econ, 14 October 2026): the day counts, and
/// EventKind::kHerdAteFromFarRick is said, when what the herd took by the
/// fallback is at least this share of all it ate of the bound feeds that day
/// - a share of the herd's own day, not a number of heads. Under it the take
/// is a remainder of grams.
inline constexpr float kFarRickDayShare = 0.1F;

/// THE FAR-RICK EVENT's AMOUNT, two fields in one (EventKind::
/// kHerdAteFromFarRick): the way in tens of metres in bits 0-15, its ceiling
/// 65 535 (655.35 km); the kilograms eaten that way from bit 16 up, their
/// ceiling 2^47 - 1. Each half is CLAMPED at its ceiling and never rolls into
/// the other (PackFarRickAmount).
inline constexpr int kFarRickKilogramsShift = 16;
inline constexpr std::int64_t kFarRickWayMax = 0xFFFF;
inline constexpr std::int64_t kFarRickKilogramsMax = (std::int64_t{1} << 47) - 1;
inline constexpr float kFarRickTensOfMetresPerKm = 100.0F;

/// @brief Packs the far-rick event's amount: the way by road, km, and what the
///        herd ate that way, grams - each clamped at its ceiling aloud (a
///        negative way or weight reads nought).
std::int64_t PackFarRickAmount(float road_km, Grams grams);

/// @brief One herd's day of eating a bound feed from a far rick: the step's
///        instrument, counted by village-year with the distance. SAID AS AN
///        EVENT, once a herd a day (a new EventKind with the body - a seam
///        word, no save: the step's events are not saved, and a counter in
///        YearLedger would be a save number this step does not spend). This
///        struct is that event's content before it is packed: herd, unit =
///        the farm, resource, amount = the grams and the way.
struct FarFeedDay {
  HerdId herd;
  UnitId farm;       ///< The unit the herd is fed at.
  UnitId far_store;  ///< The first store the fallback took from.
  ResourceId resource;
  Grams grams = 0;
  float road_km = 0.0F;  ///< From that store to the farm, effective km.
};

/// @brief A rick whose bound herds will be out of a bound feed soon.
struct ShortRick {
  std::uint32_t rick_row = kNoRow;
  /// The whole days its stock still feeds its herds, today the first: 0 -
  /// this morning's feeding is not covered.
  std::uint32_t days_left = 0;
};

/// @brief EVERY rick whose bound herds will be out of a bound feed within
///        `days` days - today the first - by the forecast's own drain on the
///        herds standing today, against that rick's stock for the bound
///        feeds and the village's allowance for the rest. In row order. The
///        body of IProductionSystem::ShortRicksWithin.
///
/// ALL OF THEM, NOT THE FIRST (boss, 14 October 2026): «the first in row
/// order» made a second farm wait on the first's mending by an accident of
/// the table, and two ricks a village is the layout the stage needs - and
/// the form that takes Epoch II's several farms without changing again.
/// @return Empty when no rick is short, when no kolkhoz herd eats a bound
///         feed, and in a month the herds graze their whole need. A rick no
///         herd is bound to is never short.
std::vector<ShortRick> ShortRicksWithin(const ProductionConfig& config,
                                        const WorldState& world,
                                        std::uint32_t days);

/// @brief How far a rick's stock falls short of carrying its bound herds to
///        THE HAY LAMP's HORIZON - the next cut's opening and the days until
///        its hay is in store (farming.csv `hay_new_cut_in_store_days`; the
///        forecast's own horizon, herd_forecast.h - no new number).
struct RickGap {
  std::uint32_t rick_row = kNoRow;
  /// The days of its herds' ration it lacks to reach the horizon; 0 - it
  /// carries them there.
  std::uint32_t gap_days = 0;
};

/// @brief Every rick with herds bound to it and its gap to the lamp's
///        horizon, in row order. Production's own read: the forecast lives
///        here, and nobody outside asks it.
std::vector<RickGap> RickGapsToTheHorizon(const ProductionConfig& config, const WorldState& world);

/// @brief THE DOOR's CHOICE - the rick a load of a bound feed lying on
///        `field` is carted to, on EVERY carting of it, ordinary or urgent:
///        the rick with THE LARGEST GAP IN DAYS to the lamp's horizon; ties
///        by the nearer by road from the field, then the first in row order;
///        when no rick has a gap - the nearest by road (HeapRickRow's answer
///        until this step). Also where a field's straw lands.
///
/// «SHORT» IS TWO QUESTIONS (econ, 14 October 2026, her §4l). WHEN a stack
/// goes ahead of all work is the urgent rule's, the short horizon
/// (ShortRicksWithin, asked by labour). WHERE a stack's hay goes when it is
/// carted ANYWAY is this function's, the long horizon: read «under a day»,
/// the ricks would be balanced only in an emergency, and by April no stack
/// is left to balance with - the team's rick runs out while the cattle
/// yard's holds its surplus. Balanced at the source, through the months the
/// carts run anyway.
///
/// ONE RULE FOR BOTH CARTINGS, AND WHY: the destination is chosen HERE, in
/// production, at the hour the carted share is delivered - which does not
/// know whether labour sent the cart by the urgent rule. A rick under its
/// last day has the largest gap there is, so an urgent cart goes to it by
/// this rule too; a second rule «the nearest short one» for urgent carts
/// would need the urgency carried on the field's saved row, or the urgent
/// predicate in two homes.
///
/// MORE RICKS IN NEED THAN STACKS: a stack feeds one rick at a time, the
/// largest gap first; the tonnes that went «to the larger gap and not to the
/// nearest» and the extra road they cost are counted for the arm's print.
/// @return kNoRow when no rick stands; the load then goes through the store
///         door as every other.
std::uint32_t RickForLoadFrom(const ProductionConfig& config,
                              const WorldState& world,
                              const FieldRow& field);

/// @brief What the door chose and what the nearest would have been - the
///        arm's print of «tonnes to the larger gap and not to the nearest»
///        and the extra road they cost.
struct RickChoice {
  std::uint32_t rick_row = kNoRow;
  std::uint32_t nearest_row = kNoRow;
  float road_km = 0.0F;     ///< To the chosen rick; 0 with one rick (not measured).
  float nearest_km = 0.0F;  ///< To the nearest; 0 with one rick.
};

/// @brief RickForLoadFrom's rule from a POINT, for a load that lies on no
///        field's heap (the field's straw, landed at the threshing).
/// @param choice Written when given.
/// @return kNoRow when no built outline is `resource`'s home. With ONE such
///         rick it is the answer and nothing is drained or measured.
std::uint32_t RickForLoadAt(const ProductionConfig& config,
                            const WorldState& world,
                            Vec2 from,
                            ResourceId resource,
                            RickChoice* choice);

}  // namespace core

#endif  // CORE_PRODUCTION_FEED_HOME_H_
