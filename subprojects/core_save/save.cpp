// The save format's top level (include/core_save/save.h): the header, the
// section framing, the six saved tables, and the file wrappers.
//
// All or nothing, in both directions. EncodeWorld builds the payload first
// and only then the header that describes it; DecodeWorld fills a LOCAL
// world and hands it over on the last line, so a refusal anywhere leaves
// the caller's world exactly as it was.

#include "core_save/save.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core_common/ids.h"
#include "core_common/state_table.h"
#include "core_common/state_table_ops.h"
#include "core_common/version.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_tables/tables.h"
#include "save_blocks.h"
#include "save_dictionary.h"
#include "save_rows.h"
#include "save_stream.h"

namespace core {
namespace {

constexpr std::size_t kHeaderSize = kSaveHeaderSize;

constexpr std::size_t kU64Bytes = 8;

/// Smallest number of bytes one row of any table costs — its entity id.
/// Used only to reject an absurd row count from a corrupt file before it
/// is turned into a reserve().
constexpr std::size_t kMinBytesPerRow = 4;

/// Ceiling on a table's next_id_value. The loader sizes a lookup array to
/// it (state_table_ops.h), so a corrupt four-billion counter would ask for
/// sixteen gigabytes before a single row was read. Sixteen million ids is
/// four orders of magnitude above a thirty-year campaign's appends.
constexpr std::uint32_t kMaxNextIdValue = 1U << 24U;

/// The payload's sections, in their fixed order. Names appear in errors.
constexpr const char* kSectionDictionaries = "dictionaries";
constexpr const char* kSectionWorld = "world";
constexpr const char* kSectionResidents = "residents";
constexpr const char* kSectionFamilies = "families";
constexpr const char* kSectionFields = "fields";
constexpr const char* kSectionUnits = "units";
constexpr const char* kSectionHerds = "herds";
constexpr const char* kSectionOrders = "orders";
constexpr const char* kSectionLedger = "ledger";
constexpr const char* kSectionStaged = "staged";

void Refuse(std::string* error, const std::string& reason) {
  const std::string message = "save: " + reason;
  if (error != nullptr) {
    *error = message;
  }
  LogError(message);
}

// ---------------------------------------------------------------------------
// Section framing
// ---------------------------------------------------------------------------

/// Writes the placeholder length and returns where it sits.
std::size_t OpenSection(ByteWriter& out) {
  const std::size_t offset = out.Size();
  out.WriteU64(0);
  return offset;
}

void CloseSection(ByteWriter& out, std::size_t length_offset) {
  const std::size_t body = out.Size() - length_offset - kU64Bytes;
  out.PatchU64(length_offset, static_cast<std::uint64_t>(body));
}

/// Reads the length prefix and returns the offset the section must end on.
bool OpenSection(ByteReader& in, const char* name, std::size_t* end_offset, std::string* error) {
  const std::uint64_t length = in.ReadU64();
  if (!in.Valid()) {
    Refuse(error, std::string("the payload ends before section '") + name + "'");
    return false;
  }
  if (length > in.Remaining()) {
    Refuse(error,
           std::string("section '") + name + "' claims " + std::to_string(length) +
               " bytes, only " + std::to_string(in.Remaining()) + " are left");
    return false;
  }
  *end_offset = in.Offset() + static_cast<std::size_t>(length);
  return true;
}

bool CloseSection(ByteReader& in, const char* name, std::size_t end_offset, std::string* error) {
  if (!in.Valid()) {
    Refuse(error, std::string("section '") + name + "' ended early");
    return false;
  }
  if (in.Offset() != end_offset) {
    Refuse(error,
           std::string("section '") + name + "' is " + std::to_string(end_offset) +
               " bytes long, the reader consumed " + std::to_string(in.Offset()));
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Entity tables
// ---------------------------------------------------------------------------

template <typename IdT, typename RowT, typename WriteRowFn>
void WriteTable(SaveSink& sink, const StateTable<IdT, RowT>& table, WriteRowFn write_row) {
  assert(table.rows.size() == table.row_ids.size());
  ByteWriter& out = sink.Out();
  out.WriteU32(table.next_id_value);
  out.WriteU32(static_cast<std::uint32_t>(table.rows.size()));
  for (const IdT id : table.row_ids) {
    out.WriteU32(id.value);
  }
  for (const RowT& row : table.rows) {
    write_row(sink, row);
  }
}

/// Reads a table and restores its invariants: ids unique, none of them the
/// invalid 0, all below next_id_value, and row_by_id rebuilt from scratch.
/// Anything else is a refusal — those three are what the rest of the core
/// assumes without checking (state_table.h).
template <typename IdT, typename RowT, typename ReadRowFn>
bool ReadTable(LoadSource& source,
               StateTable<IdT, RowT>* table,
               ReadRowFn read_row,
               const char* name,
               std::string* error) {
  ByteReader& in = source.In();
  const std::uint32_t next_id_value = in.ReadU32();
  const std::uint32_t row_count = in.ReadU32();
  if (!in.Valid()) {
    Refuse(error, std::string("table '") + name + "' ended before its counts");
    return false;
  }
  if (next_id_value == 0 || next_id_value > kMaxNextIdValue) {
    Refuse(error,
           std::string("table '") + name + "' claims next id " + std::to_string(next_id_value));
    return false;
  }
  if (row_count > in.Remaining() / kMinBytesPerRow) {
    Refuse(error,
           std::string("table '") + name + "' claims " + std::to_string(row_count) +
               " rows, more than the bytes left can hold");
    return false;
  }

  table->next_id_value = next_id_value;
  table->row_ids.clear();
  table->row_ids.reserve(row_count);
  for (std::uint32_t row = 0; row < row_count; ++row) {
    table->row_ids.push_back(IdT{in.ReadU32()});
  }
  table->rows.clear();
  table->rows.reserve(row_count);
  for (std::uint32_t row = 0; row < row_count && source.Valid(); ++row) {
    table->rows.push_back(read_row(source));
  }
  if (!source.Valid()) {
    Refuse(error, std::string("table '") + name + "': " + source.Error());
    return false;
  }

  std::vector<bool> seen(next_id_value, false);
  for (const IdT id : table->row_ids) {
    if (id.value == kInvalidEntityIdValue || id.value >= next_id_value) {
      Refuse(error,
             std::string("table '") + name + "' holds id " + std::to_string(id.value) +
                 ", outside 1.." + std::to_string(next_id_value - 1));
      return false;
    }
    if (seen[id.value]) {
      Refuse(error,
             std::string("table '") + name + "' holds id " + std::to_string(id.value) + " twice");
      return false;
    }
    seen[id.value] = true;
  }
  RebuildLookup(*table);
  return true;
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

void WriteHeader(ByteWriter& out,
                 const WorldState& world,
                 std::uint64_t payload_size,
                 std::uint64_t payload_hash) {
  for (const char letter : kSaveMagic) {
    out.WriteU8(static_cast<std::uint8_t>(letter));
  }
  out.WriteU32(static_cast<std::uint32_t>(kSaveFormatVersion));
  out.WriteU16(kSaveHeaderSize);
  out.WriteU16(static_cast<std::uint16_t>(kCoreVersionMajor));
  out.WriteU16(static_cast<std::uint16_t>(kCoreVersionMinor));
  out.WriteU16(static_cast<std::uint16_t>(kCoreVersionPatch));
  out.WriteU64(world.world_seed);
  out.WriteU64(world.calendar.tick);
  out.WriteU64(payload_size);
  out.WriteU64(payload_hash);
}

/// Header fields as they lie, without judging the version — PeekSaveInfo's
/// whole job is to REPORT a foreign version rather than hide the file.
bool ReadHeader(std::span<const std::byte> bytes, SaveInfo* info, std::uint64_t* payload_hash) {
  if (bytes.size() < kHeaderSize) {
    return false;
  }
  ByteReader in(bytes.first(kHeaderSize));
  for (const char letter : kSaveMagic) {
    if (in.ReadU8() != static_cast<std::uint8_t>(letter)) {
      return false;
    }
  }
  info->format_version = in.ReadU32();
  const std::uint16_t header_size = in.ReadU16();
  if (header_size != kSaveHeaderSize) {
    return false;
  }
  info->core_major = in.ReadU16();
  info->core_minor = in.ReadU16();
  info->core_patch = in.ReadU16();
  info->world_seed = in.ReadU64();
  info->tick = in.ReadU64();
  info->payload_size = in.ReadU64();
  *payload_hash = in.ReadU64();
  return in.Valid();
}

std::vector<std::byte> ReadWholeFile(std::string_view file_path, bool* ok) {
  std::ifstream file(std::string(file_path), std::ios::binary | std::ios::ate);
  if (!file) {
    *ok = false;
    return {};
  }
  const std::streamoff size = file.tellg();
  if (size < 0) {
    *ok = false;
    return {};
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  file.seekg(0);
  if (!bytes.empty()) {
    file.read(reinterpret_cast<char*>(bytes.data()), size);  // NOLINT(*-reinterpret-cast)
  }
  *ok = file.good() || file.eof();
  return bytes;
}

}  // namespace

// ---------------------------------------------------------------------------
// The public four
// ---------------------------------------------------------------------------

std::vector<std::byte> EncodeWorld(const WorldState& world,
                                   const StagedOrders& staged,
                                   const ITableSet& tables) {
  const DefDictionaries dictionaries = ReadLiveDictionaries(tables);
  SaveSink sink(dictionaries);
  ByteWriter& out = sink.Out();

  std::size_t length_offset = OpenSection(out);
  WriteDictionaries(out, dictionaries);
  CloseSection(out, length_offset);

  length_offset = OpenSection(out);
  WriteWorldBlocks(sink, world);
  CloseSection(out, length_offset);

  length_offset = OpenSection(out);
  WriteTable(sink, world.residents, WriteResidentRow);
  CloseSection(out, length_offset);

  length_offset = OpenSection(out);
  WriteTable(sink, world.families, WriteFamilyRow);
  CloseSection(out, length_offset);

  length_offset = OpenSection(out);
  WriteTable(sink, world.fields, WriteFieldRow);
  CloseSection(out, length_offset);

  length_offset = OpenSection(out);
  WriteTable(sink, world.units, WriteUnitRow);
  CloseSection(out, length_offset);

  length_offset = OpenSection(out);
  WriteTable(sink, world.herds, WriteHerdRow);
  CloseSection(out, length_offset);

  // The chairman's order book: state like any other table (the boundary,
  // manual/70-boundary.md §2). WorldState::step_events is deliberately NOT
  // here — the outbox describes one step, and the engine empties it at the
  // start of the next one.
  length_offset = OpenSection(out);
  WriteTable(sink, world.orders, WriteOrderRow);
  CloseSection(out, length_offset);

  length_offset = OpenSection(out);
  WriteLedger(sink, world.ledger);
  CloseSection(out, length_offset);

  // The one section that is not the WorldState: what the session had staged
  // and the engine had not applied when the save was made. A campaign is
  // saved on pause, after the day's orders have been handed out, and losing
  // them would punish the player for the unforeseeable (order_state.h,
  // StagedOrders; boss decision of 2026-08-31).
  length_offset = OpenSection(out);
  out.WriteU32(static_cast<std::uint32_t>(staged.issued.size()));
  for (const OrderRow& row : staged.issued) {
    WriteOrderRow(sink, row);
  }
  out.WriteU32(static_cast<std::uint32_t>(staged.cancelled.size()));
  for (const OrderId id : staged.cancelled) {
    out.WriteU32(id.value);
  }
  CloseSection(out, length_offset);

  if (!sink.Valid()) {
    // The world does not match the tables it is being saved with. That is a
    // caller error, not a corrupt file: assert in Debug so it is found where
    // it was made, refuse in Release so nothing half-written reaches disk.
    assert(false && "EncodeWorld: the world names definitions the tables do not have");
    LogError("save: " + sink.Error());
    return {};
  }

  const std::vector<std::byte> payload = sink.Out().TakeBytes();
  ByteWriter header;
  WriteHeader(header, world, static_cast<std::uint64_t>(payload.size()), HashBytes(payload));
  std::vector<std::byte> bytes = header.TakeBytes();
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

std::vector<std::byte> EncodeWorld(const WorldState& world, const ITableSet& tables) {
  return EncodeWorld(world, StagedOrders{}, tables);
}

bool DecodeWorld(std::span<const std::byte> bytes,
                 const ITableSet& tables,
                 WorldState* world,
                 StagedOrders* staged,
                 std::string* error) {
  assert(world != nullptr);
  SaveInfo info;
  std::uint64_t payload_hash = 0;
  if (!ReadHeader(bytes, &info, &payload_hash)) {
    Refuse(error, "not a save file, or its header is truncated");
    return false;
  }
  if (info.format_version != static_cast<std::uint32_t>(kSaveFormatVersion)) {
    // Named on both sides on purpose: "wrong version" without the numbers
    // sends the reader to the source, and there are no migrations to try.
    Refuse(error,
           "the file is save format " + std::to_string(info.format_version) +
               ", this build reads only " + std::to_string(kSaveFormatVersion));
    return false;
  }
  const std::span<const std::byte> payload = bytes.subspan(kHeaderSize);
  if (payload.size() != info.payload_size) {
    Refuse(error,
           "the payload is " + std::to_string(payload.size()) + " bytes, the header says " +
               std::to_string(info.payload_size));
    return false;
  }
  if (HashBytes(payload) != payload_hash) {
    Refuse(error, "the payload checksum does not match — the file is damaged");
    return false;
  }

  ByteReader in(payload);
  std::size_t section_end = 0;

  if (!OpenSection(in, kSectionDictionaries, &section_end, error)) {
    return false;
  }
  DefDictionaries saved;
  if (!ReadDictionaries(in, &saved)) {
    Refuse(error, "the dictionary section is malformed");
    return false;
  }
  if (!CloseSection(in, kSectionDictionaries, section_end, error)) {
    return false;
  }
  const DefRemap remap = BuildRemap(saved, ReadLiveDictionaries(tables));
  LoadSource source(in, remap);

  WorldState loaded;

  if (!OpenSection(in, kSectionWorld, &section_end, error)) {
    return false;
  }
  ReadWorldBlocks(source, &loaded);
  if (!source.Valid()) {
    Refuse(error, std::string("section '") + kSectionWorld + "': " + source.Error());
    return false;
  }
  if (!CloseSection(in, kSectionWorld, section_end, error)) {
    return false;
  }

  const auto read_table_section = [&](const char* name, auto* table, auto read_row) {
    if (!OpenSection(in, name, &section_end, error)) {
      return false;
    }
    if (!ReadTable(source, table, read_row, name, error)) {
      return false;
    }
    return CloseSection(in, name, section_end, error);
  };

  if (!read_table_section(kSectionResidents, &loaded.residents, ReadResidentRow) ||
      !read_table_section(kSectionFamilies, &loaded.families, ReadFamilyRow) ||
      !read_table_section(kSectionFields, &loaded.fields, ReadFieldRow) ||
      !read_table_section(kSectionUnits, &loaded.units, ReadUnitRow) ||
      !read_table_section(kSectionHerds, &loaded.herds, ReadHerdRow) ||
      !read_table_section(kSectionOrders, &loaded.orders, ReadOrderRow)) {
    return false;
  }

  if (!OpenSection(in, kSectionLedger, &section_end, error)) {
    return false;
  }
  loaded.ledger = ReadLedger(source);
  if (!source.Valid()) {
    Refuse(error, std::string("section '") + kSectionLedger + "': " + source.Error());
    return false;
  }
  if (!CloseSection(in, kSectionLedger, section_end, error)) {
    return false;
  }

  StagedOrders loaded_staged;
  if (!OpenSection(in, kSectionStaged, &section_end, error)) {
    return false;
  }
  const std::uint32_t issued_count = in.ReadU32();
  // Bounded against the bytes left before a single row is read, the same
  // guard ReadTable makes: a damaged count cannot ask for a gigabyte of rows.
  if (!in.Valid() || issued_count > in.Remaining() / kMinBytesPerRow) {
    Refuse(error,
           std::string("section '") + kSectionStaged + "' claims " + std::to_string(issued_count) +
               " staged orders");
    return false;
  }
  loaded_staged.issued.reserve(issued_count);
  for (std::uint32_t index = 0; index < issued_count && source.Valid(); ++index) {
    loaded_staged.issued.push_back(ReadOrderRow(source));
  }
  const std::uint32_t cancelled_count = in.ReadU32();
  constexpr std::size_t kBytesPerId = 4;
  if (!in.Valid() || cancelled_count > in.Remaining() / kBytesPerId) {
    Refuse(error,
           std::string("section '") + kSectionStaged + "' claims " +
               std::to_string(cancelled_count) + " cancellations");
    return false;
  }
  loaded_staged.cancelled.reserve(cancelled_count);
  for (std::uint32_t index = 0; index < cancelled_count; ++index) {
    loaded_staged.cancelled.push_back(OrderId{in.ReadU32()});
  }
  if (!source.Valid()) {
    Refuse(error, std::string("section '") + kSectionStaged + "': " + source.Error());
    return false;
  }
  if (!CloseSection(in, kSectionStaged, section_end, error)) {
    return false;
  }

  if (in.Remaining() != 0) {
    Refuse(error, std::to_string(in.Remaining()) + " bytes follow the last section");
    return false;
  }

  *world = std::move(loaded);
  if (staged != nullptr) {
    *staged = std::move(loaded_staged);
  } else if (!loaded_staged.issued.empty() || !loaded_staged.cancelled.empty()) {
    // A caller with no session to resume them into: refuse rather than drop.
    // Those were the player's orders (save.h, DecodeWorld).
    Refuse(error, "the save carries staged orders and this caller cannot resume them");
    return false;
  }
  return true;
}

bool DecodeWorld(std::span<const std::byte> bytes,
                 const ITableSet& tables,
                 WorldState* world,
                 std::string* error) {
  return DecodeWorld(bytes, tables, world, nullptr, error);
}

std::optional<SaveInfo> PeekSaveInfo(std::span<const std::byte> bytes) {
  SaveInfo info;
  std::uint64_t payload_hash = 0;
  if (!ReadHeader(bytes, &info, &payload_hash)) {
    return std::nullopt;
  }
  return info;
}

bool SaveWorldToFile(const WorldState& world,
                     const StagedOrders& staged,
                     const ITableSet& tables,
                     std::string_view file_path,
                     std::string* error) {
  const std::vector<std::byte> bytes = EncodeWorld(world, staged, tables);
  if (bytes.empty()) {
    Refuse(error, "the world could not be encoded");
    return false;
  }
  std::ofstream file(std::string(file_path), std::ios::binary | std::ios::trunc);
  if (!file) {
    Refuse(error, "cannot open '" + std::string(file_path) + "' for writing");
    return false;
  }
  file.write(reinterpret_cast<const char*>(bytes.data()),  // NOLINT(*-reinterpret-cast)
             static_cast<std::streamsize>(bytes.size()));
  if (!file.good()) {
    Refuse(error, "writing '" + std::string(file_path) + "' failed");
    return false;
  }
  return true;
}

bool SaveWorldToFile(const WorldState& world,
                     const ITableSet& tables,
                     std::string_view file_path,
                     std::string* error) {
  return SaveWorldToFile(world, StagedOrders{}, tables, file_path, error);
}

bool LoadWorldFromFile(std::string_view file_path,
                       const ITableSet& tables,
                       WorldState* world,
                       StagedOrders* staged,
                       std::string* error) {
  bool ok = true;
  const std::vector<std::byte> bytes = ReadWholeFile(file_path, &ok);
  if (!ok) {
    Refuse(error, "cannot read '" + std::string(file_path) + "'");
    return false;
  }
  return DecodeWorld(bytes, tables, world, staged, error);
}

bool LoadWorldFromFile(std::string_view file_path,
                       const ITableSet& tables,
                       WorldState* world,
                       std::string* error) {
  return LoadWorldFromFile(file_path, tables, world, nullptr, error);
}

std::optional<SaveInfo> PeekSaveFile(std::string_view file_path) {
  bool ok = true;
  const std::vector<std::byte> bytes = ReadWholeFile(file_path, &ok);
  if (!ok) {
    return std::nullopt;
  }
  return PeekSaveInfo(bytes);
}

}  // namespace core
