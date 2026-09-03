/// @file
/// @brief Identifier model: runtime entity ids and balance-table definition ids.
/// @threading PARALLEL_READONLY
/// Type aliases and compile-time constants only; no mutable state. Readable
/// from any phase and any thread.
///
/// Two kinds of identifiers, deliberately different in width and lifetime:
///
///   * EntityId — names a live simulation entity (a resident, a family, a
///     unit). Issued at run time, monotonically increasing per table, never
///     reused, survives in saves forever. 0 is "no entity".
///
///   * DefId — names a row of a balance definition table (a crop, a resource,
///     a unit type). It IS the dense row index of that table, so per-kind data
///     lives in plain vectors (see ResourceAmounts). Valid only against the
///     table it indexes; a save stores the table's string keys next to the
///     data, and the loader remaps ids if the table was edited between runs.
///
/// The tag template exists to make ids of different tables incompatible at
/// compile time — assigning a FieldId to a UnitId must not compile. One
/// pattern, ten id kinds: this is the case where a template pays for itself.

#ifndef CORE_COMMON_IDS_H_
#define CORE_COMMON_IDS_H_

#include <compare>
#include <cstdint>

namespace core {

// ---------------------------------------------------------------------------
// Runtime entity identifiers
// ---------------------------------------------------------------------------

/// @brief Raw value of a not-yet-issued / absent entity id.
/// The first real id in every table is 1, so a default-constructed id is
/// always "nothing" and a zeroed save chunk never aliases a live entity.
inline constexpr std::uint32_t kInvalidEntityIdValue = 0;

/// @brief Typed identifier of a live simulation entity.
/// @tparam Tag An empty tag type naming the table this id belongs to.
/// Issued by the owning StateTable (state_table.h): monotonically increasing,
/// never reused within a campaign. 32 bits hold decades of births at village
/// scale with nine orders of magnitude to spare.
/// Comparison is defaulted so ids can be sorted and used as keys; ordering
/// follows issue order, which is deterministic.
template <typename Tag>
struct EntityId {
  std::uint32_t value = kInvalidEntityIdValue;

  friend constexpr auto operator<=>(const EntityId&, const EntityId&) = default;
};

struct ResidentIdTag {};

struct FamilyIdTag {};

struct UnitIdTag {};

struct FieldIdTag {};

struct HerdIdTag {};

struct OrderIdTag {};

/// @brief One person. The central entity of the game (stage 3 of the plan).
using ResidentId = EntityId<ResidentIdTag>;

/// @brief One household: family, its yard and its accounts (stage 3).
using FamilyId = EntityId<FamilyIdTag>;

/// @brief One built unit: production, social, housing (stages 4–5).
using UnitId = EntityId<UnitIdTag>;

/// @brief One field: a marked-out piece of arable land (stage 4).
using FieldId = EntityId<FieldIdTag>;

/// @brief One herd: animals of one kind kept at one unit (stage 4).
using HerdId = EntityId<HerdIdTag>;

/// @brief One standing order of the chairman: a command that came across the
/// boundary and lives in the order book until it is done (project phase 2,
/// core_common/order_state.h). Issued by the step engine alone, so the
/// boundary can name the id the row will carry before the row exists.
using OrderId = EntityId<OrderIdTag>;

// ---------------------------------------------------------------------------
// Balance-table definition identifiers
// ---------------------------------------------------------------------------

/// @brief Raw value of an absent definition id.
/// Definition ids are 0-based dense row indices, so the sentinel sits at the
/// top of the range instead of stealing index 0.
inline constexpr std::uint16_t kInvalidDefIdValue = 0xFFFF;

/// @brief Typed index of a row in a balance definition table.
/// @tparam Tag An empty tag type naming the definition table.
/// The value is the dense 0-based row index, so `vector[id.value]` is the
/// canonical lookup and per-kind state (ResourceAmounts) needs no map. 16 bits
/// bound every definition table at 65535 rows — two orders of magnitude above
/// anything the design describes.
template <typename Tag>
struct DefId {
  std::uint16_t value = kInvalidDefIdValue;

  friend constexpr auto operator<=>(const DefId&, const DefId&) = default;
};

struct ResourceIdTag {};

struct CropIdTag {};

struct UnitTypeIdTag {};

struct LivestockKindIdTag {};

/// @brief A storable resource: grain, milk, firewood, manure, nails.
/// The resource list is data (balance tables), not code.
using ResourceId = DefId<ResourceIdTag>;

/// @brief A crop from the crop table: rye, potato, clover, flax.
/// Fallow (пар) is not a crop; a field with no crop holds an invalid CropId.
using CropId = DefId<CropIdTag>;

/// @brief A unit type from the unit table: barn, school, smithy.
/// One generic Unit entity plus a type row — never a class per building.
using UnitTypeId = DefId<UnitTypeIdTag>;

/// @brief A livestock kind from the livestock table: cow, sheep, pig, hen, duck.
using LivestockKindId = DefId<LivestockKindIdTag>;

struct ProfessionIdTag {};

/// @brief Row of tables/professions.csv — a post a resident can be appointed
/// to (professions design §1; project phase 2, task A7). The eleventh id
/// kind, and the first that names a role rather than a thing.
using ProfessionId = DefId<ProfessionIdTag>;

}  // namespace core

#endif  // CORE_COMMON_IDS_H_
