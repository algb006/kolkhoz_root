// The packing of a district visit's outcome into an event's amount
// (core_common/district_visit_state.h).

#include "core_common/district_visit_state.h"

#include <cstdint>

namespace core {
namespace {

/// Bits a field takes in the packed amount: one byte each.
constexpr unsigned kFieldBits = 8U;
constexpr std::uint64_t kFieldMask = 0xFFU;

/// Field positions, lowest first — the order the header promises.
constexpr unsigned kFaceShift = 0U;
constexpr unsigned kKindShift = kFieldBits;
constexpr unsigned kFoundShift = 2U * kFieldBits;
constexpr unsigned kMissByShift = 3U * kFieldBits;
constexpr unsigned kGiftShift = 4U * kFieldBits;

/// Everything past the gift byte: an amount with any of it set is not a visit.
constexpr std::uint64_t kBeyondFields = ~((std::uint64_t{1} << (5U * kFieldBits)) - 1U);

std::uint64_t Field(std::uint64_t packed, unsigned shift) {
  return (packed >> shift) & kFieldMask;
}

}  // namespace

std::int64_t PackDistrictVisit(const DistrictVisitOutcome& outcome) {
  const std::uint64_t miss_by =
      outcome.has_miss_by ? static_cast<std::uint64_t>(outcome.miss_by) + 1U : 0U;
  const std::uint64_t packed = (static_cast<std::uint64_t>(outcome.face) << kFaceShift) |
                               (static_cast<std::uint64_t>(outcome.kind) << kKindShift) |
                               (static_cast<std::uint64_t>(outcome.found) << kFoundShift) |
                               (miss_by << kMissByShift) |
                               (static_cast<std::uint64_t>(outcome.gift) << kGiftShift);
  return static_cast<std::int64_t>(packed);
}

bool UnpackDistrictVisit(std::int64_t amount, DistrictVisitOutcome& outcome) {
  if (amount < 0) {
    return false;
  }
  const auto packed = static_cast<std::uint64_t>(amount);
  const std::uint64_t face = Field(packed, kFaceShift);
  const std::uint64_t kind = Field(packed, kKindShift);
  const std::uint64_t found = Field(packed, kFoundShift);
  const std::uint64_t miss_by = Field(packed, kMissByShift);
  const std::uint64_t gift = Field(packed, kGiftShift);
  const auto face_count = static_cast<std::uint64_t>(DistrictFace::kDistrictFaceCount);
  if ((packed & kBeyondFields) != 0U || face >= face_count ||
      kind >= static_cast<std::uint64_t>(DistrictVisitKind::kDistrictVisitKindCount) ||
      found >= static_cast<std::uint64_t>(DistrictVisitFinding::kDistrictVisitFindingCount) ||
      miss_by > face_count ||
      gift >= static_cast<std::uint64_t>(DistrictGiftOutcome::kDistrictGiftOutcomeCount)) {
    return false;
  }
  outcome.face = static_cast<DistrictFace>(face);
  outcome.kind = static_cast<DistrictVisitKind>(kind);
  outcome.found = static_cast<DistrictVisitFinding>(found);
  outcome.has_miss_by = miss_by != 0U;
  outcome.miss_by =
      outcome.has_miss_by ? static_cast<DistrictFace>(miss_by - 1U) : DistrictFace::kKorenev;
  outcome.gift = static_cast<DistrictGiftOutcome>(gift);
  return true;
}

}  // namespace core
