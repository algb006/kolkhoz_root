/// @file
/// @brief What happens to ONE HEAD by its own nature: born, grown, dead of
/// age, dead of hunger, taken under the knife — and what is left of it.
/// @threading SINGLE_THREADED
/// Runs inside the herd day (herd_system.h), in the production sub-step of
/// the decisions phase, from the sim thread. It draws from the world's
/// sequential RNG, so it can only live in a sequential slot.
///
/// THE SEAM IS NATURE AGAINST THE HOUSEHOLD, AND THE SUBJECT IS THE CUT
/// (boss's criterion, 2026-09-07: what moves ONE HEAD against what moves THE
/// HERD). A calf is born, grows, ages and dies whether or not anybody is
/// watching; a roof, a manger, a milking and a gift to the neighbouring yard
/// are the kolkhoz ACTING on its herds. Those are two subjects, and the file
/// that held both had reached 996 lines of a hard limit of 1000 — which
/// names the illness and says nothing about the cure. The line count merely
/// said one was due; the meaning says where.
///
/// THE DEPENDENCY RUNS ONE WAY AND THE COMPILER HOLDS IT THAT WAY.
/// herd_system.cpp includes this header; nothing here includes it back, so
/// no rule of nature can reach up and call the day's walk. The one arc that
/// pointed the wrong way — RunBirths asking herd_system.h whether the month
/// was in the calving band — was TURNED AROUND rather than tolerated, in the
/// shape the function already used next door: the walk decides the season
/// and hands the answer down, exactly as it already hands down whether the
/// stable is built. A seam that is only agreed is not a seam.
///
/// Everything here is a COHORT flow: the row holds counts by rung, never a
/// list of animals, so aging, birth and culling are deterministic fractional
/// streams with a carry (mobs canon). A herd that matures one head every
/// three days matures exactly one head every three days, not zero forever.
///
/// THE UNIT TRAP, restated because this is where it would bite: livestock
/// ages are GAME units with the x4 life acceleration already applied by the
/// design, while feed and care rates are REAL. Nothing here divides an age
/// by the life speedup.

#ifndef CORE_PRODUCTION_HERD_LIFE_H_
#define CORE_PRODUCTION_HERD_LIFE_H_

#include <cstdint>

#include "core_common/ledger_state.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// Where a herd's feed comes from and where its produce goes. A kolkhoz herd
/// draws on the shared store and delivers to it (the phase-1 logistics stub
/// is instant); a herd at a family yard lives out of that family's pantry —
/// which is what "private livestock is ONE aggregate consumer, fed out of
/// the pantry the monthly issue fills" means in code (livestock design §11).
struct HerdPlace {
  ResourceAmounts* pantry = nullptr;  ///< Non-null for a household herd.

  /// The stock of the unit the herd stands at, if any. A herd eats out of
  /// its own barn before it sends to the shared store — which is not a
  /// nicety: hay is delivered to the stock yard by the harvest, and the yard
  /// is not a "storing" unit at all (its table capacity is in HEADS, not
  /// tonnes), so a herd that only knew the shared store would stand beside
  /// a full manger and starve.
  ResourceAmounts* unit_stock = nullptr;

  bool at_unit = false;
};

/// THE LIFETIME CONTRACT OF THOSE TWO POINTERS, WHICH USED TO NEED NO
/// WRITING DOWN (MEM-001, 2026-09-07). While this struct was a private type
/// of one .cpp, the only code that could build one was the walk that also
/// guaranteed its validity, and the guarantee could stay unsaid. Moving the
/// struct into a header did not change a character of it — and changed who
/// may build one, and how long they may keep it. The defect is the same; the
/// blast radius is not.
///
/// So, said out loud:
///
///   A HerdPlace is valid only until `world.units.rows` or
///   `world.families.rows` GROWS OR SHRINKS. It points into those outer
///   vectors, so an AppendRow or a RemoveRow on either invalidates it, and a
///   stale place reads as a full manger rather than as a crash.
///
/// What does NOT invalidate it: any amount of writing into the pantry or the
/// stock it names. Those are `std::vector<Grams>` INSIDE a row, and growing
/// one moves its own buffer, not the row that holds it.
///
/// RunHerdDay honours this by construction rather than by care: it removes
/// herds before the walk begins and appends the herds given away only after
/// the walk has ended, so no row count moves while a place is alive. A new
/// caller gets no such help from the compiler, which is exactly why the rule
/// is written here and not there.

/// @brief A head count from a fractional one: never negative, never past the
/// 16-bit rung.
std::uint16_t AsHeads(float value);

/// @brief How many sires a herd of this many adults keeps. A function of the
/// herd, not a thing the herd remembers.
std::uint16_t TargetMales(const LivestockDef& kind, std::uint16_t adults);

/// @brief Every rung added up: newborn, juvenile and adult.
std::uint16_t TotalHeads(const HerdRow& herd);

/// @brief Puts `amount` of `resource` where this herd's produce belongs —
/// the family pantry for a yard herd, the shared store otherwise — and
/// writes the matching ledger line.
/// @note A non-positive amount is a no-op, not a zero entry.
void DeliverProduce(WorldState& world,
                    const ProductionConfig& config,
                    const HerdPlace& place,
                    ResourceId resource,
                    Grams amount);

/// @brief Turns `heads` into meat, hide, pelt and down at this place. Does
/// not touch the herd's counts: the caller has already taken the heads off
/// their rung, so that a head is never both slaughtered and standing.
void Slaughter(const ProductionConfig& config,
               const LivestockDef& kind,
               const HerdPlace& place,
               std::uint16_t heads,
               WorldState& world);

/// @brief Newborn -> juvenile -> adult for one day, plus the structural cull
/// of males beyond the herd's share of sires.
void RunMaturation(const ProductionConfig& config,
                   const LivestockDef& kind,
                   const HerdPlace& place,
                   HerdRow& herd,
                   WorldState& world);

/// @brief One day's offspring, drawn as a fractional stream inside the
/// calving band.
/// @param in_birth_season Whether today falls inside the kind's calving
///        band. DECIDED BY THE CALLER, and that is the seam: the walk owns
///        the calendar, this file owns the animal. It arrives as a bare
///        bool for the same reason `stable_built` beside it does.
/// @param stable_built Only a roofed stable brings foals (livestock §5).
void RunBirths(const ProductionConfig& config,
               const LivestockDef& kind,
               LivestockKindId kind_id,
               HerdRow& herd,
               HerdId herd_id,
               bool in_birth_season,
               bool stable_built,
               WorldState& world,
               YearLedger& book);

/// @brief One day's deaths of old age, spread over the lifespan band.
void RunAgeDeaths(const LivestockDef& kind, HerdRow& herd, HerdId herd_id, WorldState& world);

/// @brief One day's deaths of a herd that went unfed.
void RunHungerDeaths(const ProductionConfig& config,
                     const LivestockDef& kind,
                     HerdRow& herd,
                     HerdId herd_id,
                     WorldState& world,
                     YearLedger& book);

/// @brief The autumn cull: the heads the kolkhoz does not carry through the
/// winter go under the knife on the season's day.
void RunAutumnSlaughter(const ProductionConfig& config,
                        const LivestockDef& kind,
                        LivestockKindId kind_id,
                        const HerdPlace& place,
                        HerdRow& herd,
                        WorldState& world,
                        const CalendarState& calendar);

}  // namespace core

#endif  // CORE_PRODUCTION_HERD_LIFE_H_
