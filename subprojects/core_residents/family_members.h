/// @file
/// @brief THE FAMILIES' MEMBERS, ONCE A STEP (#2118, the 50-year probe: the metrics phase grew
///        as residents squared - every family scanned every resident, every step): the
///        residents' rows of each family row, in ascending resident row - the very order the
///        scans visited them in, so every sum over a family's members is the same float sum,
///        bit for bit. Private to core_residents.
/// @threading PARALLEL_READONLY
/// Built on the stepping thread before a parallel phase's ranges (IParallelPhase::PrepareStep);
/// read by its workers, never written while they run.

#ifndef CORE_RESIDENTS_FAMILY_MEMBERS_H_
#define CORE_RESIDENTS_FAMILY_MEMBERS_H_

#include <cstdint>
#include <span>
#include <vector>

#include "core_common/family_state.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace core {

/// @brief The member rows of every family row of one world, by family row (CSR).
class FamilyMembers {
 public:
  /// @brief Indexes `world`: a resident whose family is not a live row belongs to none.
  void Build(const WorldState& world) {
    const std::size_t families = world.families.rows.size();
    offsets_.assign(families + 1U, 0U);
    for (const ResidentRow& resident : world.residents.rows) {
      const std::uint32_t family = FindRow(world.families, resident.family);
      if (family != kNoRow) {
        ++offsets_[family + 1U];
      }
    }
    for (std::size_t at = 0; at < families; ++at) {
      offsets_[at + 1U] += offsets_[at];
    }
    rows_.assign(offsets_[families], 0U);
    std::vector<std::uint32_t> next(offsets_.begin(), offsets_.end() - 1);
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const std::uint32_t family = FindRow(world.families, world.residents.rows[row].family);
      if (family != kNoRow) {
        rows_[next[family]++] = row;
      }
    }
  }

  /// @brief The resident rows of family row `family_item`, ascending; empty past the index.
  std::span<const std::uint32_t> Of(std::uint32_t family_item) const {
    if (family_item + 1U >= offsets_.size()) {
      return {};
    }
    return {rows_.data() + offsets_[family_item],
            offsets_[family_item + 1U] - offsets_[family_item]};
  }

 private:
  std::vector<std::uint32_t> offsets_;
  std::vector<std::uint32_t> rows_;
};

/// @brief A phase's index for one step: built in PrepareStep, read by the ranges only while it
///        fits the world they are handed - the same tick and the same table sizes - so a range
///        run with no PrepareStep (a unit's direct call) takes the plain scan instead of a
///        stale index.
class StepFamilyMembers {
 public:
  void Prepare(const WorldState& world) {
    index_.Build(world);
    tick_ = world.calendar.tick;
    residents_ = world.residents.rows.size();
    families_ = world.families.rows.size();
    built_ = true;
  }

  bool Fits(const WorldState& world) const {
    return built_ && tick_ == world.calendar.tick && residents_ == world.residents.rows.size() &&
           families_ == world.families.rows.size();
  }

  std::span<const std::uint32_t> Of(std::uint32_t family_item) const {
    return index_.Of(family_item);
  }

 private:
  FamilyMembers index_;
  bool built_ = false;
  std::uint64_t tick_ = 0;
  std::size_t residents_ = 0;
  std::size_t families_ = 0;
};

/// @brief One family's member rows by the plain scan, ascending - for a caller with no index
///        (a unit's world, the start's settle): the same rows the index gives.
inline std::vector<std::uint32_t> MemberRowsOf(const WorldState& world, FamilyId id) {
  std::vector<std::uint32_t> rows;
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    if (world.residents.rows[row].family.value == id.value) {
      rows.push_back(row);
    }
  }
  return rows;
}

}  // namespace core

#endif  // CORE_RESIDENTS_FAMILY_MEMBERS_H_
