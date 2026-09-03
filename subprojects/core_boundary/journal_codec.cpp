// EncodeJournal / DecodeJournal — the command journal on disk
// (include/core_boundary/session.h).
//
// The journal is what makes a campaign REPLAYABLE: a save says where the
// world was, the journal says what the chairman then did and at which tick,
// and the two together reproduce the run step for step. The run tool is its
// first user, a bug report its second (manual/70-boundary.md §7).
//
// It follows the save format's encoding rules to the letter — little-endian
// by construction, floats as their exact bit pattern, a row field by field in
// declaration order (core_save/save.h) — and is stamped with the SAME number,
// kSaveFormatVersion: an OrderRow is a state row, and a build that cannot
// read the save cannot read the journal either.
//
// THE PRIMITIVES ARE WRITTEN OUT HERE rather than taken from core_save, and
// that is a deliberate twenty lines of duplication. core_save's ByteWriter is
// private to that module, and the boundary is a leaf that depends on
// core_common, core_sim, core_tables and core_log and on nothing else
// (manual/70-boundary.md §9). Sharing them would mean promoting a private
// header of another module into the public include tree — a bigger change to
// the shape of the core than the shifts below are worth. If a third writer of
// bytes ever appears, that promotion becomes the right call and this comment
// is the record of the decision.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core_boundary/session.h"
#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/order_state.h"
#include "core_common/version.h"

namespace core {
namespace {

/// Bytes of an OrderRow on the wire, and of one whole entry around it. Both
/// are FIXED, which is what lets the reader check a file's length before it
/// reads a single field — the journal's answer to a truncated file.
constexpr std::size_t kOrderBytes = 4 + 8 + (4 * 4) + (4 * 2) + (2 * 4);

constexpr std::size_t kEntryBytes = 8 + 4 + 1 + kOrderBytes + 4;

constexpr std::size_t kHeaderBytes = 16;  // magic (8) + format (4) + count (4)

/// THE SIZEOF TRIPWIRE, the same one the save codec carries for the same
/// reason (core_save/save.h). This file is the SECOND hand-written encoder of
/// an OrderRow, and a field appended to the row without a line in WriteOrder
/// would be lost silently: the length check would still pass, because it
/// checks the journal against ITSELF. Tying the assert to the struct's size
/// makes the build fail until WriteOrder, ReadOrder and kOrderBytes have all
/// been brought along — and VERSION_SAVE bumped by the human, since an order
/// row is a state row.
static_assert(sizeof(OrderRow) == 48, "OrderRow changed — update the journal codec too");

constexpr std::uint8_t kMaxJournalVerb = static_cast<std::uint8_t>(JournalVerb::kCancel);
// THE LAST ENUMERATOR, and it has to be the last one: a guard left behind
// refuses every journal carrying a kind newer than itself. THREE of these
// four were stale, all left at task A2's additions — kMaxOrderKind and
// kMaxOrderRefusal found by task A5's design pass, kMaxWorkKind by its
// analysis pass, in this very block and one line under a comment that had
// just declared the matter closed. A journal with kStartBuild,
// kUpgradeUnit, kGateClosed, kTooClose, kNotEmpty or a builder's
// assignment decoded as "corrupt". The save codec had the same bug and the
// same cure (core_save/save_rows.cpp, which had already fixed the work
// kind and not this file).
//
// The lesson is about tests, not about diligence: a codec test that stages
// a MIDDLING value passes for ever while the guard rots behind it. Each of
// these is now covered by staging the NEWEST value of its enum, which is
// the only stage that fails when somebody appends without looking here.
constexpr std::uint8_t kMaxOrderKind = static_cast<std::uint8_t>(OrderKind::kRepairUnit);
constexpr std::uint8_t kMaxOrderStatus = static_cast<std::uint8_t>(OrderStatus::kCancelled);
constexpr std::uint8_t kMaxOrderRefusal = static_cast<std::uint8_t>(OrderRefusal::kNotEmpty);
constexpr std::uint8_t kMaxWorkKind = static_cast<std::uint8_t>(WorkKind::kConstruction);

class Writer {
 public:
  void U8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }

  void U16(std::uint16_t value) {
    U8(static_cast<std::uint8_t>(value));
    U8(static_cast<std::uint8_t>(value >> 8U));
  }

  void U32(std::uint32_t value) {
    U16(static_cast<std::uint16_t>(value));
    U16(static_cast<std::uint16_t>(value >> 16U));
  }

  void U64(std::uint64_t value) {
    U32(static_cast<std::uint32_t>(value));
    U32(static_cast<std::uint32_t>(value >> 32U));
  }

  void Float(float value) { U32(std::bit_cast<std::uint32_t>(value)); }

  std::vector<std::byte> Take() { return std::move(bytes_); }

 private:
  std::vector<std::byte> bytes_;
};

/// Never reads out of bounds: past the end it goes invalid once and yields
/// zeros afterwards, so a truncated buffer walks the whole decode harmlessly
/// and is caught by the one flag at the end (core_save/save_stream.h says the
/// same thing about the save's reader, and for the same reason).
class Reader {
 public:
  explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  std::uint8_t U8() {
    if (offset_ >= bytes_.size()) {
      valid_ = false;
      return 0;
    }
    const auto value = static_cast<std::uint8_t>(bytes_[offset_]);
    ++offset_;
    return value;
  }

  std::uint16_t U16() {
    const std::uint16_t low = U8();
    return static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(U8()) << 8U));
  }

  std::uint32_t U32() {
    const std::uint32_t low = U16();
    return low | (static_cast<std::uint32_t>(U16()) << 16U);
  }

  std::uint64_t U64() {
    const std::uint64_t low = U32();
    return low | (static_cast<std::uint64_t>(U32()) << 32U);
  }

  float Float() { return std::bit_cast<float>(U32()); }

  /// @brief One u8 enum value, refused when it exceeds the highest name the
  /// build knows. Not pedantry: these values index tables downstream (a
  /// WorkKind reaches the work-rate table), so a corrupt byte would read out
  /// of bounds far from here, with nothing pointing back at the file.
  std::uint8_t EnumValue(std::uint8_t highest) {
    const std::uint8_t value = U8();
    if (value > highest) {
      valid_ = false;
      return 0;
    }
    return value;
  }

  bool Valid() const { return valid_; }

 private:
  std::span<const std::byte> bytes_;

  std::size_t offset_ = 0;

  bool valid_ = true;
};

void WriteOrder(Writer& out, const OrderRow& row) {
  out.U8(static_cast<std::uint8_t>(row.kind));
  out.U8(static_cast<std::uint8_t>(row.status));
  out.U8(static_cast<std::uint8_t>(row.refusal));
  out.U8(static_cast<std::uint8_t>(row.work));
  out.U64(row.issued_tick);

  out.U32(row.resident.value);
  out.U32(row.unit.value);
  out.U32(row.field.value);
  out.U32(row.herd.value);

  // Definition ids go out RAW, unlike a save's: a journal carries no
  // dictionaries and remaps nothing. It is replayed against the save it was
  // written beside and therefore against the same tables; a table reordered
  // between writing and replay is a case the save survives and the journal
  // does not, which is why the journal is a debugging and run-tool artifact
  // and never a second save (session.h, EncodeJournal).
  out.U16(row.unit_type.value);
  out.U16(row.rotation_year0.value);
  out.U16(row.rotation_year1.value);
  out.U16(row.rotation_year2.value);

  out.Float(row.position.x);
  out.Float(row.position.y);
}

OrderRow ReadOrder(Reader& in) {
  OrderRow row;
  row.kind = static_cast<OrderKind>(in.EnumValue(kMaxOrderKind));
  row.status = static_cast<OrderStatus>(in.EnumValue(kMaxOrderStatus));
  row.refusal = static_cast<OrderRefusal>(in.EnumValue(kMaxOrderRefusal));
  row.work = static_cast<WorkKind>(in.EnumValue(kMaxWorkKind));
  row.issued_tick = in.U64();

  row.resident = ResidentId{in.U32()};
  row.unit = UnitId{in.U32()};
  row.field = FieldId{in.U32()};
  row.herd = HerdId{in.U32()};

  row.unit_type = UnitTypeId{in.U16()};
  row.rotation_year0 = CropId{in.U16()};
  row.rotation_year1 = CropId{in.U16()};
  row.rotation_year2 = CropId{in.U16()};

  row.position.x = in.Float();
  row.position.y = in.Float();
  return row;
}

void Fail(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

}  // namespace

std::vector<std::byte> EncodeJournal(std::span<const JournalEntry> entries) {
  Writer out;
  for (const char letter : kJournalMagic) {
    out.U8(static_cast<std::uint8_t>(letter));
  }
  out.U32(static_cast<std::uint32_t>(kSaveFormatVersion));
  out.U32(static_cast<std::uint32_t>(entries.size()));
  for (const JournalEntry& entry : entries) {
    out.U64(entry.tick);
    out.U32(entry.sequence);
    out.U8(static_cast<std::uint8_t>(entry.verb));
    WriteOrder(out, entry.order);
    out.U32(entry.order_id.value);
  }
  return out.Take();
}

bool DecodeJournal(std::span<const std::byte> bytes,
                   std::vector<JournalEntry>* entries,
                   std::string* error) {
  if (entries == nullptr) {
    Fail(error, "journal: no destination");
    return false;
  }
  if (bytes.size() < kHeaderBytes) {
    Fail(error, "journal: shorter than its header");
    return false;
  }
  for (std::size_t index = 0; index < kJournalMagic.size(); ++index) {
    if (static_cast<char>(bytes[index]) != kJournalMagic[index]) {
      Fail(error, "journal: wrong magic");
      return false;
    }
  }
  Reader in(bytes.subspan(kJournalMagic.size()));
  const std::uint32_t format = in.U32();
  if (format != static_cast<std::uint32_t>(kSaveFormatVersion)) {
    // Named in both directions, like a save's refusal: a journal of a newer
    // build is as unreadable as one of an older, and guessing at either is
    // how a replay silently stops being the run it claims to be.
    Fail(error,
         "journal: format " + std::to_string(format) + ", this build reads " +
             std::to_string(kSaveFormatVersion));
    return false;
  }
  const std::uint32_t count = in.U32();
  // The entry is fixed-size, so the whole file's length is checkable before
  // a single field is read — truncation AND bytes glued after the last entry
  // are both caught here, and neither can be mistaken for a short journal.
  if (bytes.size() - kHeaderBytes != static_cast<std::size_t>(count) * kEntryBytes) {
    Fail(error, "journal: length does not match its entry count");
    return false;
  }

  // Decoded into a local first: a journal is all or nothing, like a load, and
  // the caller's vector must be untouched when anything refuses.
  std::vector<JournalEntry> decoded;
  decoded.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    JournalEntry entry;
    entry.tick = in.U64();
    entry.sequence = in.U32();
    entry.verb = static_cast<JournalVerb>(in.EnumValue(kMaxJournalVerb));
    entry.order = ReadOrder(in);
    entry.order_id = OrderId{in.U32()};
    decoded.push_back(entry);
  }
  if (!in.Valid()) {
    Fail(error, "journal: a value is out of range");
    return false;
  }
  *entries = std::move(decoded);
  return true;
}

}  // namespace core
