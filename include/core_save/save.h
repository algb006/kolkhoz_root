/// @file
/// @brief core_save boundary: a WorldState to bytes and back — the save
/// format of the core (stage 7, task F1; manual/67-save-format.md).
/// @threading SINGLE_THREADED
/// Called between steps on the sim thread, or before a simulation exists —
/// a menu listing its saves, a test round-tripping a world. The input of a
/// save is ISimulation::CompletedState(), which is immutable between steps;
/// the output of a load goes back in through ISimulation::ResetWorld. No
/// phase code ever enters this module, and the module holds no state of
/// its own: every function here is a pure transformation of its arguments
/// plus, on the file variants, the I/O it names.
///
/// WHAT A SAVE IS. The WorldState and nothing else (state model doc §3: a
/// save is one struct serialized). Subsystem configuration is not state —
/// it is re-read from the tables when the simulation is created — and the
/// row_by_id lookups are rebuilt on load (state_table.h). What the save
/// carries beyond the state is enough to check that the definition tables
/// are the same tables: their KEYS.
///
/// KEYS, NOT INDICES. A DefId is a row index of a balance table
/// (core_common/ids.h), and ResourceAmounts is a vector whose INDEX is the
/// ResourceId — every pantry, every store, the plan. Reorder resources.csv
/// between two runs and a dense vector written by index would put the grain
/// under manure. So the payload begins with a dictionary per definition
/// table — the keys in the order the ids had when the save was written —
/// and the loader maps every saved index onto the live table by key: DefId
/// fields are rewritten, dense vectors are permuted (§ "Remapping" below).
/// The rule stands in manual/61-balance-tables.md §3; this module is where
/// it is enforced.
///
/// BIT-EXACT. Floats are written as their IEEE-754 bit pattern, never as
/// text: a save that loses the last bit of a satiety value breaks
/// determinism exactly as a data race would, and a continued campaign must
/// run on identically to an uninterrupted one. RngState is two exact
/// 64-bit words. Integers are little-endian by construction (byte shifts,
/// never a struct memcpy — padding and the two compilers' ABIs are not part
/// of the format).
///
/// REFUSE, NEVER MIGRATE. A save whose format number is not this build's
/// kSaveFormatVersion is refused — higher AND lower — with an error naming
/// both numbers. Phase 1 has no migrations: there is no game and nobody's
/// campaign to preserve, and a migration written before the first player
/// exists would be maintained for nobody. A refused load leaves the output
/// world untouched: a load is all or nothing, like a table set load.
///
/// FORMAT 1 — the byte layout, fixed for VERSION_SAVE = 1:
///
///   header (fixed 52 bytes, everything little-endian)
///     magic            8 bytes  "KLHZSAVE"
///     format_version   u32      kSaveFormatVersion of the writer
///     header_size      u16      52; a reader positions the payload by it
///     core_major/minor/patch  u16 x3   informational, never checked
///     world_seed       u64      copy of WorldState::world_seed
///     tick             u64      copy of WorldState::calendar.tick
///     payload_size     u64      bytes after the header
///     payload_hash     u64      FNV-1a 64 of the payload
///   payload, sections in this fixed order, each prefixed by its byte
///   length (u64) so that a malformed section is NAMED in the error and a
///   truncated file is caught before a single field is read:
///     dictionaries     resources, crops, unit_types, livestock — each
///                      u16 count, then count keys as u16 length + UTF-8
///     calendar, weather, epoch, world_seed, rng, chairman, plan, vitals
///     residents, families, fields, units, herds     (StateTable sections)
///     ledger
///   No section is optional and none may be skipped: the reader knows
///   exactly one layout, this one, and a file that deviates is refused.
///
/// ENCODING RULES, the same for every field:
///   * u8/u16/u32/u64/i32/i64  little-endian, the declared width
///   * float                    u32 of its bits (std::bit_cast)
///   * enum                     its underlying integer
///   * EntityId                 u32 value, as is — never remapped
///   * DefId                    u16 value, remapped by key on load;
///                              kInvalidDefIdValue passes through
///   * ResourceAmounts          u16 count, then count i64 grams
///   * StateTable<Id, Row>      u32 next_id_value, u32 row count, then the
///                              ids (u32 each), then the rows; row_by_id is
///                              not written
///   * a row                    its fields one by one, in DECLARATION
///                              ORDER of the header that defines it
///   * std::array<float, N>     N floats, no count
///
/// THE SIZEOF TRIPWIRE. The writer names every field of every row; a field
/// added to a row without a line in the writer would be silently lost —
/// the one failure this format cannot detect by itself. So the
/// implementation static_asserts sizeof(RowT) against the size it was
/// written for, per row type: a new field changes the size and the build
/// fails until the writer, the reader and the assert are all updated (and
/// VERSION_SAVE is bumped by the human, manual/setup/57-versioning.md).
/// Both compilers agree on the size of these plain aggregates on x86-64;
/// if a target ever disagrees, the assert says so at compile time, which
/// is still the right moment.
///
/// REMAPPING, the rules the loader applies per dictionary:
///   * identity — every saved key sits at the same live index — costs
///     nothing: DefIds are copied, vectors are copied AS THEY ARE, length
///     included. With unchanged tables Load(Save(w)) == w memberwise, and
///     the unit test asserts exactly that, vector lengths included.
///   * permutation — a saved key found at another live index: every DefId
///     of that dictionary is rewritten, every dense vector of it is
///     rebuilt to the LIVE row count with each amount at its key's new
///     index.
///   * a saved key ABSENT from the live table is refused only if the save
///     uses it: a DefId field naming it, or a non-zero amount under it. A
///     zero column for a key that was removed from the table is dropped
///     silently — removing an unused resource must not kill a campaign.
///   * a live key absent from the save (a resource added since) simply
///     gets zero everywhere. Adding a row is the expected cheap change.
///
/// AFTER DECODING the loader rebuilds every row_by_id (RebuildLookup) and
/// checks the StateTable invariants it can: unique ids, every id below
/// next_id_value, index 0 never issued. Game-level invariants (a spouse
/// that exists, a herd's males within its adults) are the simulation's
/// business, not the loader's — a save is trusted on those, as the buffer
/// it came from was.

#ifndef CORE_SAVE_SAVE_H_
#define CORE_SAVE_SAVE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/world_state.h"

namespace core {

class ITableSet;  // core_tables/tables.h — the live definition tables.

/// @brief The eight magic bytes every save begins with.
inline constexpr std::string_view kSaveMagic = "KLHZSAVE";

/// @brief Fixed size of the header, bytes. The payload starts here.
inline constexpr std::uint16_t kSaveHeaderSize = 52;

/// @brief What the header says about a save, readable without decoding the
/// payload — for a menu listing its saves, or a tool telling which build
/// wrote a file. Every field is a plain copy from the header.
struct SaveInfo {
  /// The writer's kSaveFormatVersion. Equal to this build's or the load
  /// will refuse; PeekSaveInfo reports it either way.
  std::uint32_t format_version = 0;

  std::uint16_t core_major = 0;

  std::uint16_t core_minor = 0;

  std::uint16_t core_patch = 0;

  std::uint64_t world_seed = 0;

  /// The tick the world was at; the calendar is a pure function of it
  /// (core_common/calendar.h), so year and month follow without the body.
  std::uint64_t tick = 0;

  std::uint64_t payload_size = 0;
};

/// @brief Encodes a world into the save format, in memory.
/// @param world  The completed state of a simulation between steps.
/// @param tables The live definition tables: their keys are written as the
///               dictionaries the loader remaps by. Must be the set the
///               world was created with — a DefId in `world` that exceeds
///               a table's row count is a caller error (asserted in Debug,
///               refused with an empty result otherwise).
/// @return The bytes of the save, header included; empty on refusal. The
///         encoding is deterministic: the same world and tables give the
///         same bytes, so two saves of one state are byte-identical.
std::vector<std::byte> EncodeWorld(const WorldState& world, const ITableSet& tables);

/// @brief Decodes a save into a world, remapping definition ids by key.
/// @param bytes  A whole save as EncodeWorld produced it.
/// @param tables The live definition tables to remap onto — the set the
///               resumed simulation will be created with.
/// @param world  Receives the decoded state on success; UNTOUCHED on any
///               failure (all or nothing).
/// @param error  If non-null, receives a human-readable reason on failure:
///               which check refused — magic, version (both numbers named),
///               size, hash, a named section, a named missing key — so a
///               headless run can print it without a log in hand. Also
///               logged.
/// @return true on success. The loaded world has every row_by_id rebuilt
///         and is ready for ISimulation::ResetWorld.
bool DecodeWorld(std::span<const std::byte> bytes,
                 const ITableSet& tables,
                 WorldState* world,
                 std::string* error);

/// @brief Reads the header of a save without decoding the payload.
/// Checks the magic and the header size only; the format version is
/// REPORTED, not enforced — this is how a menu tells the player "written
/// by an older build" instead of hiding the file.
/// @return nullopt if the bytes are too short or the magic is wrong.
std::optional<SaveInfo> PeekSaveInfo(std::span<const std::byte> bytes);

/// @brief EncodeWorld plus writing the bytes to `file_path` (created or
/// truncated, binary).
/// @return false if encoding refused or the file could not be written; the
///         reason goes to `error` when non-null, and to the log.
bool SaveWorldToFile(const WorldState& world,
                     const ITableSet& tables,
                     std::string_view file_path,
                     std::string* error);

/// @brief Reads `file_path` whole and DecodeWorld's it. Same all-or-nothing
/// contract: `world` is untouched unless the whole file decoded.
bool LoadWorldFromFile(std::string_view file_path,
                       const ITableSet& tables,
                       WorldState* world,
                       std::string* error);

/// @brief PeekSaveInfo over a file: reads only the header bytes.
/// @return nullopt if the file cannot be opened, is too short, or does not
///         start with the magic.
std::optional<SaveInfo> PeekSaveFile(std::string_view file_path);

}  // namespace core

#endif  // CORE_SAVE_SAVE_H_
