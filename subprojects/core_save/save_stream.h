/// @file
/// @brief ByteWriter / ByteReader — the byte primitives of the save format.
/// @threading SINGLE_THREADED
/// Plain cursors over a buffer, used only inside core_save, which is itself
/// called between steps from the sim thread (core_save/save.h). No shared
/// state, no synchronization.
///
/// Little-endian by CONSTRUCTION — every integer is assembled from byte
/// shifts, never memcpy'd out of an object. That is what makes the format
/// the same file on Clang and MSVC, and independent of struct padding
/// (manual/67-save-format.md §4). Floats go through std::bit_cast to their
/// IEEE-754 bit pattern for the same reason and for exactness: a save that
/// rounds a satiety value breaks determinism as surely as a data race.
///
/// The reader NEVER reads out of bounds: a short buffer sets `valid` to
/// false and every later call returns zero, so a truncated file walks the
/// whole decode harmlessly and is caught by the one flag at the end. That
/// is deliberate — a decoder that must be checked after every field is a
/// decoder whose checks get forgotten.

#ifndef CORE_SAVE_SAVE_STREAM_H_
#define CORE_SAVE_SAVE_STREAM_H_

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace core {

/// @brief Appends values to a growing byte buffer. Cannot fail.
class ByteWriter {
 public:
  void WriteU8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }

  void WriteU16(std::uint16_t value) {
    WriteU8(static_cast<std::uint8_t>(value));
    WriteU8(static_cast<std::uint8_t>(value >> 8U));
  }

  void WriteU32(std::uint32_t value) {
    WriteU16(static_cast<std::uint16_t>(value));
    WriteU16(static_cast<std::uint16_t>(value >> 16U));
  }

  void WriteU64(std::uint64_t value) {
    WriteU32(static_cast<std::uint32_t>(value));
    WriteU32(static_cast<std::uint32_t>(value >> 32U));
  }

  void WriteI32(std::int32_t value) { WriteU32(static_cast<std::uint32_t>(value)); }

  void WriteI64(std::int64_t value) { WriteU64(static_cast<std::uint64_t>(value)); }

  /// @brief The float's exact bit pattern, never its decimal form.
  void WriteFloat(float value) { WriteU32(std::bit_cast<std::uint32_t>(value)); }

  /// @brief A table key: u16 byte length, then the UTF-8 bytes.
  /// @pre The key is shorter than 65536 bytes (every real key is a word).
  void WriteKey(std::string_view key);

  /// @brief Overwrites eight bytes at `offset` — for the header fields that
  /// are only known once the payload is written.
  /// @pre offset + 8 <= size().
  void PatchU64(std::size_t offset, std::uint64_t value);

  std::size_t Size() const { return bytes_.size(); }

  const std::vector<std::byte>& Bytes() const { return bytes_; }

  std::vector<std::byte> TakeBytes() { return std::move(bytes_); }

 private:
  std::vector<std::byte> bytes_;
};

/// @brief Consumes values from a byte span. Never reads out of bounds:
/// running past the end sets `valid` false once and yields zeros after.
class ByteReader {
 public:
  explicit ByteReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  std::uint8_t ReadU8();

  std::uint16_t ReadU16() {
    const std::uint16_t low = ReadU8();
    return static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(ReadU8()) << 8U));
  }

  std::uint32_t ReadU32() {
    const std::uint32_t low = ReadU16();
    return low | (static_cast<std::uint32_t>(ReadU16()) << 16U);
  }

  std::uint64_t ReadU64() {
    const std::uint64_t low = ReadU32();
    return low | (static_cast<std::uint64_t>(ReadU32()) << 32U);
  }

  std::int32_t ReadI32() { return static_cast<std::int32_t>(ReadU32()); }

  std::int64_t ReadI64() { return static_cast<std::int64_t>(ReadU64()); }

  float ReadFloat() { return std::bit_cast<float>(ReadU32()); }

  /// @brief A key as written by WriteKey; empty string once invalid.
  std::string ReadKey();

  /// @brief Bytes consumed so far — how a section checks its own length.
  std::size_t Offset() const { return offset_; }

  std::size_t Remaining() const { return valid_ ? bytes_.size() - offset_ : 0; }

  bool Valid() const { return valid_; }

  /// @brief Marks the stream broken; every later read yields zero.
  void Invalidate() { valid_ = false; }

  /// @brief Moves the cursor to an absolute offset; invalidates if it is
  /// past the end. Used by the section framing, never by field decoding.
  void SeekTo(std::size_t offset);

 private:
  std::span<const std::byte> bytes_;

  std::size_t offset_ = 0;

  bool valid_ = true;
};

/// @brief FNV-1a 64 over a byte span — the payload checksum of the header.
/// Not a security hash: it catches truncation and corruption, which is what
/// a local save file is exposed to.
std::uint64_t HashBytes(std::span<const std::byte> bytes);

}  // namespace core

#endif  // CORE_SAVE_SAVE_STREAM_H_
