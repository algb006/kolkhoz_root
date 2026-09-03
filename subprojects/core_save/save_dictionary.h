/// @file
/// @brief The definition-table dictionaries and the remap they drive, plus
/// the two domain-aware stream wrappers the codecs actually use.
/// @threading SINGLE_THREADED
/// Internal to core_save; every function here is called from the single
/// encode/decode path, which runs between steps on the sim thread.
///
/// THE PROBLEM these types exist for (manual/67-save-format.md §3). A DefId
/// is a row index of a balance table, and ResourceAmounts is a vector whose
/// INDEX is the ResourceId — every pantry, every store, the plan. Reorder
/// resources.csv between two runs and a vector written by index puts the
/// grain under manure, with nothing to notice it. So the save carries the
/// KEYS of the four definition tables in the order their ids had when it
/// was written, and the loader maps saved indices onto live ones by key.
///
/// SaveSink and LoadSource are where that rule is enforced rather than
/// remembered: the row codecs never touch a raw DefId or a raw amounts
/// vector, they call these, and the bounds check and the remap come along
/// for free.

#ifndef CORE_SAVE_SAVE_DICTIONARY_H_
#define CORE_SAVE_SAVE_DICTIONARY_H_

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core_common/quantities.h"
#include "save_stream.h"

namespace core {

class ITableSet;

/// @brief The five definition tables a save remaps by key. The value is the
/// dictionary's position in the payload — appending a kind is a format
/// change (VERSION_SAVE), which is why the list is an enum and not data.
enum class DefKind : std::uint8_t {
  kResource = 0,
  kCrop,
  kUnitType,
  kLivestock,
  kProfession,  ///< tables/professions.csv; ResidentRow::post, OrderRow::profession (task A7).
};

inline constexpr std::uint32_t kDefKindCount = 5;

/// @brief The tables/ file name backing a kind: "resources", "crops"...
const char* DefTableName(DefKind kind);

constexpr std::uint32_t DefKindIndex(DefKind kind) {
  return static_cast<std::uint32_t>(kind);
}

/// @brief The keys of the five definition tables, each in DefId order.
/// A missing table gives an empty vector — which is not an error here: it
/// only becomes one if the world actually uses an id of that kind.
struct DefDictionaries {
  std::array<std::vector<std::string>, kDefKindCount> keys;
};

/// @brief Reads the live tables' key columns into dictionaries.
DefDictionaries ReadLiveDictionaries(const ITableSet& tables);

void WriteDictionaries(ByteWriter& out, const DefDictionaries& dictionaries);

/// @brief Reads the dictionary section. False on a malformed section.
bool ReadDictionaries(ByteReader& in, DefDictionaries* dictionaries);

/// @brief Saved index -> live index, per kind. Built once per load.
struct DefRemap {
  /// to_live[kind][saved index] = live index, or kInvalidDefIdValue when
  /// the saved key is gone from the live table. Absence is only fatal if
  /// the save uses that index — checked at the point of use, not here.
  std::array<std::vector<std::uint16_t>, kDefKindCount> to_live;

  /// Row count of the LIVE table: the length a permuted dense vector gets.
  std::array<std::uint16_t, kDefKindCount> live_count = {};

  /// True when every saved key sits at the same live index and the counts
  /// match. Then ids are copied and dense vectors are copied AS THEY ARE,
  /// length included — which is what makes an unchanged-tables round trip
  /// equal the original memberwise.
  std::array<bool, kDefKindCount> identity = {};

  /// Kept for error messages: naming the key that vanished is the whole
  /// difference between a usable refusal and "load failed".
  std::array<std::vector<std::string>, kDefKindCount> saved_keys;
};

DefRemap BuildRemap(const DefDictionaries& saved, const DefDictionaries& live);

/// @brief The writing side: bytes out, with every DefId checked against the
/// dictionary it indexes. A value out of range is a caller error — the
/// world does not match the tables it is being saved with — and sets the
/// error, which aborts the encode.
class SaveSink {
 public:
  explicit SaveSink(const DefDictionaries& dictionaries) : dictionaries_(&dictionaries) {}

  ByteWriter& Out() { return out_; }

  /// @brief A DefId value: kInvalidDefIdValue passes through untouched.
  void WriteDefId(DefKind kind, std::uint16_t value);

  /// @brief A dense per-definition vector: u16 count, then that many i64.
  void WriteAmounts(DefKind kind, const ResourceAmounts& amounts);

  bool Valid() const { return error_.empty(); }

  const std::string& Error() const { return error_; }

  /// @brief Records the first refusal; later ones are ignored so the
  /// message names the cause rather than the last symptom.
  void Fail(std::string reason);

 private:
  ByteWriter out_;

  const DefDictionaries* dictionaries_;

  std::string error_;
};

/// @brief The reading side: bytes in, with every DefId and every dense
/// vector remapped by key on the way.
class LoadSource {
 public:
  LoadSource(ByteReader& in, const DefRemap& remap) : in_(&in), remap_(&remap) {}

  ByteReader& In() { return *in_; }

  /// @brief Reads a DefId value and maps it onto the live table. A saved
  /// key that no longer exists is refused HERE — this is the "only if the
  /// save uses it" half of the rule.
  std::uint16_t ReadDefId(DefKind kind);

  /// @brief Reads a u8 enum value and refuses one outside [min, max].
  /// Not a gameplay check — those belong to the simulation — but a memory
  /// one: several of these enums are ARRAY INDICES downstream (WorkKind
  /// into the work-rate table, Epoch into the per-epoch configs), so a
  /// corrupt byte would be an out-of-bounds read in a phase, far from
  /// here, with nothing to point back at the file.
  std::uint8_t ReadEnumValue(std::uint8_t min_value, std::uint8_t max_value, const char* name);

  /// @brief Reads a dense vector and remaps it. Identity remap returns it
  /// verbatim; otherwise a non-empty vector is rebuilt to the live row
  /// count and an empty one stays empty. A NON-ZERO amount under a
  /// vanished key is refused; a zero one is dropped in silence.
  ResourceAmounts ReadAmounts(DefKind kind);

  bool Valid() const { return error_.empty() && in_->Valid(); }

  /// @brief The refusal reason; "truncated" when the stream ran out.
  std::string Error() const;

  void Fail(std::string reason);

 private:
  ByteReader* in_;

  const DefRemap* remap_;

  std::string error_;
};

}  // namespace core

#endif  // CORE_SAVE_SAVE_DICTIONARY_H_
