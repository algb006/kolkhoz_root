// The dictionaries, the remap and the two stream wrappers (save_dictionary.h).
//
// This file is where "saves store keys, not indices" stops being a rule in a
// document (manual/61-balance-tables.md §3) and becomes something the code
// cannot forget: the row codecs have no way to write a DefId except through
// SaveSink, and no way to read one except through LoadSource.

#include "save_dictionary.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core_common/ids.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// The key column every definition table carries (balance tables §3).
constexpr const char* kKeyColumn = "key";

std::string DescribeIndex(DefKind kind, std::uint16_t value) {
  return std::string(DefTableName(kind)) + " row " + std::to_string(value);
}

}  // namespace

const char* DefTableName(DefKind kind) {
  switch (kind) {
    case DefKind::kResource:
      return "resources";
    case DefKind::kCrop:
      return "crops";
    case DefKind::kUnitType:
      return "unit_types";
    case DefKind::kLivestock:
      return "livestock";
  }
  return "unknown";
}

DefDictionaries ReadLiveDictionaries(const ITableSet& tables) {
  DefDictionaries dictionaries;
  for (std::uint32_t index = 0; index < kDefKindCount; ++index) {
    const auto kind = static_cast<DefKind>(index);
    const ITable* table = tables.FindTable(DefTableName(kind));
    if (table == nullptr) {
      continue;  // absent table: an empty dictionary, fatal only if used
    }
    const std::uint32_t key_column = table->FindColumn(kKeyColumn);
    if (key_column == kNoTableColumn) {
      continue;
    }
    std::vector<std::string>& keys = dictionaries.keys[index];
    keys.reserve(table->RowCount());
    for (std::uint32_t row = 0; row < table->RowCount(); ++row) {
      keys.emplace_back(table->CellText(row, key_column));
    }
  }
  return dictionaries;
}

void WriteDictionaries(ByteWriter& out, const DefDictionaries& dictionaries) {
  for (std::uint32_t index = 0; index < kDefKindCount; ++index) {
    const std::vector<std::string>& keys = dictionaries.keys[index];
    out.WriteU16(static_cast<std::uint16_t>(keys.size()));
    for (const std::string& key : keys) {
      out.WriteKey(key);
    }
  }
}

bool ReadDictionaries(ByteReader& in, DefDictionaries* dictionaries) {
  for (std::uint32_t index = 0; index < kDefKindCount; ++index) {
    const std::uint16_t count = in.ReadU16();
    std::vector<std::string>& keys = dictionaries->keys[index];
    keys.clear();
    keys.reserve(count);
    for (std::uint16_t row = 0; row < count && in.Valid(); ++row) {
      keys.push_back(in.ReadKey());
    }
    if (!in.Valid()) {
      return false;
    }
  }
  return true;
}

DefRemap BuildRemap(const DefDictionaries& saved, const DefDictionaries& live) {
  DefRemap remap;
  for (std::uint32_t index = 0; index < kDefKindCount; ++index) {
    const std::vector<std::string>& saved_keys = saved.keys[index];
    const std::vector<std::string>& live_keys = live.keys[index];
    remap.saved_keys[index] = saved_keys;
    remap.live_count[index] = static_cast<std::uint16_t>(live_keys.size());

    std::unordered_map<std::string, std::uint16_t> live_by_key;
    live_by_key.reserve(live_keys.size());
    for (std::size_t row = 0; row < live_keys.size(); ++row) {
      live_by_key.emplace(live_keys[row], static_cast<std::uint16_t>(row));
    }

    std::vector<std::uint16_t>& to_live = remap.to_live[index];
    to_live.assign(saved_keys.size(), kInvalidDefIdValue);
    bool identity = saved_keys.size() == live_keys.size();
    for (std::size_t row = 0; row < saved_keys.size(); ++row) {
      const auto found = live_by_key.find(saved_keys[row]);
      if (found == live_by_key.end()) {
        identity = false;
        continue;  // gone from the live table: fatal only where it is used
      }
      to_live[row] = found->second;
      identity = identity && found->second == row;
    }
    remap.identity[index] = identity;
  }
  return remap;
}

// ---------------------------------------------------------------------------
// SaveSink
// ---------------------------------------------------------------------------

void SaveSink::Fail(std::string reason) {
  if (error_.empty()) {
    error_ = std::move(reason);
  }
}

void SaveSink::WriteDefId(DefKind kind, std::uint16_t value) {
  const std::size_t count = dictionaries_->keys[DefKindIndex(kind)].size();
  if (value != kInvalidDefIdValue && value >= count) {
    Fail("the world names " + DescribeIndex(kind, value) + ", the table has " +
         std::to_string(count));
  }
  out_.WriteU16(value);
}

void SaveSink::WriteAmounts(DefKind kind, const ResourceAmounts& amounts) {
  const std::size_t count = dictionaries_->keys[DefKindIndex(kind)].size();
  if (amounts.size() > count) {
    Fail("a dense vector holds " + std::to_string(amounts.size()) + " entries, table " +
         DefTableName(kind) + " has " + std::to_string(count) + " rows");
  }
  out_.WriteU16(static_cast<std::uint16_t>(amounts.size()));
  for (const Grams amount : amounts) {
    out_.WriteI64(amount);
  }
}

// ---------------------------------------------------------------------------
// LoadSource
// ---------------------------------------------------------------------------

void LoadSource::Fail(std::string reason) {
  if (error_.empty()) {
    error_ = std::move(reason);
  }
}

std::string LoadSource::Error() const {
  if (!error_.empty()) {
    return error_;
  }
  return in_->Valid() ? std::string() : std::string("the payload ended early");
}

std::uint16_t LoadSource::ReadDefId(DefKind kind) {
  const std::uint16_t value = in_->ReadU16();
  if (value == kInvalidDefIdValue) {
    return value;
  }
  const std::vector<std::uint16_t>& to_live = remap_->to_live[DefKindIndex(kind)];
  if (value >= to_live.size()) {
    Fail("the save names " + DescribeIndex(kind, value) + ", its own dictionary has " +
         std::to_string(to_live.size()));
    return kInvalidDefIdValue;
  }
  const std::uint16_t live = to_live[value];
  if (live == kInvalidDefIdValue) {
    Fail("the save uses key '" + remap_->saved_keys[DefKindIndex(kind)][value] + "', which " +
         DefTableName(kind) + " no longer has");
  }
  return live;
}

std::uint8_t LoadSource::ReadEnumValue(std::uint8_t min_value,
                                       std::uint8_t max_value,
                                       const char* name) {
  const std::uint8_t value = in_->ReadU8();
  if (!in_->Valid()) {
    return min_value;
  }
  if (value < min_value || value > max_value) {
    Fail(std::string(name) + " holds " + std::to_string(value) + ", outside " +
         std::to_string(min_value) + ".." + std::to_string(max_value));
    return min_value;
  }
  return value;
}

ResourceAmounts LoadSource::ReadAmounts(DefKind kind) {
  const std::uint32_t index = DefKindIndex(kind);
  const std::uint16_t count = in_->ReadU16();
  ResourceAmounts saved;
  saved.reserve(count);
  for (std::uint16_t row = 0; row < count && in_->Valid(); ++row) {
    saved.push_back(in_->ReadI64());
  }
  if (!in_->Valid()) {
    return {};
  }
  if (remap_->identity[index]) {
    return saved;  // verbatim, length included — the round-trip guarantee
  }
  if (saved.empty()) {
    return saved;  // nothing to place; an empty pantry stays an empty pantry
  }
  const std::vector<std::uint16_t>& to_live = remap_->to_live[index];
  ResourceAmounts live(remap_->live_count[index], 0);
  for (std::size_t row = 0; row < saved.size(); ++row) {
    if (row >= to_live.size()) {
      Fail("a dense vector is longer than the save's own " + std::string(DefTableName(kind)) +
           " dictionary");
      return {};
    }
    const std::uint16_t target = to_live[row];
    if (target == kInvalidDefIdValue) {
      if (saved[row] != 0) {
        Fail("the save holds " + std::to_string(saved[row]) + " g of '" +
             remap_->saved_keys[index][row] + "', which " + DefTableName(kind) + " no longer has");
        return {};
      }
      continue;  // an empty column of a removed key: dropped in silence
    }
    live[target] = saved[row];
  }
  return live;
}

}  // namespace core
