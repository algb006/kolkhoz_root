// The byte primitives of the save format (save_stream.h): the out-of-line
// bodies, which are the ones that can run off the end of a buffer.

#include "save_stream.h"

#include <cassert>

namespace core {
namespace {

/// FNV-1a 64, the standard parameters.
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;

constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

/// A table key is a snake_case word; the cap only exists so the length
/// fits the u16 the format writes it with.
constexpr std::size_t kMaxKeyBytes = 0xFFFF;

constexpr std::size_t kU64Bytes = 8;

}  // namespace

void ByteWriter::WriteKey(std::string_view key) {
  assert(key.size() <= kMaxKeyBytes);
  const std::size_t length = key.size() < kMaxKeyBytes ? key.size() : kMaxKeyBytes;
  WriteU16(static_cast<std::uint16_t>(length));
  for (std::size_t index = 0; index < length; ++index) {
    WriteU8(static_cast<std::uint8_t>(key[index]));
  }
}

void ByteWriter::PatchU64(std::size_t offset, std::uint64_t value) {
  assert(offset + kU64Bytes <= bytes_.size());
  if (offset + kU64Bytes > bytes_.size()) {
    return;
  }
  std::uint64_t rest = value;
  for (std::size_t index = 0; index < kU64Bytes; ++index) {
    bytes_[offset + index] = static_cast<std::byte>(rest & 0xFFU);
    rest >>= 8U;
  }
}

std::uint8_t ByteReader::ReadU8() {
  if (!valid_ || offset_ >= bytes_.size()) {
    valid_ = false;
    return 0;
  }
  const auto value = static_cast<std::uint8_t>(bytes_[offset_]);
  ++offset_;
  return value;
}

std::string ByteReader::ReadKey() {
  const std::uint16_t length = ReadU16();
  if (!valid_ || length > Remaining()) {
    valid_ = false;
    return {};
  }
  std::string key;
  key.reserve(length);
  for (std::uint16_t index = 0; index < length; ++index) {
    key.push_back(static_cast<char>(ReadU8()));
  }
  return key;
}

void ByteReader::SeekTo(std::size_t offset) {
  if (offset > bytes_.size()) {
    valid_ = false;
    return;
  }
  offset_ = offset;
}

std::uint64_t HashBytes(std::span<const std::byte> bytes) {
  std::uint64_t hash = kFnvOffsetBasis;
  for (const std::byte value : bytes) {
    hash ^= static_cast<std::uint64_t>(static_cast<std::uint8_t>(value));
    hash *= kFnvPrime;
  }
  return hash;
}

}  // namespace core
