/// @file
/// @brief THE NETWORK'S ACCESSES, REMEMBERED (#2147; the 50-year probe after #2118: the ring
///        search - a place the near table cannot answer - was 34-40 % of a grown village's
///        steps, linear but the day's fattest part): RoadIndexImpl::Accesses is a pure function
///        of the network, the mode and the place, and the places asked are mostly fixed - homes,
///        works, fields, stands - so its answer is kept per thread, keyed by the network's
///        process-unique serial, the mode and the place's exact bits, and returned as the very
///        vector a fresh search gives. Private to core_common.
/// @threading PARALLEL_WRITE
/// thread_local: a thread's memo is its own; no lock, no shared state.

#ifndef CORE_COMMON_ROAD_ACCESS_MEMO_H_
#define CORE_COMMON_ROAD_ACCESS_MEMO_H_

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core_common/road_route.h"

namespace core::road_access {

/// @brief A network's serial, never reused in a process (a freed index's address can be).
inline std::uint64_t NextNetworkSerial() {
  static std::atomic<std::uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

using Key = std::array<std::uint32_t, 5>;

struct KeyHash {
  std::size_t operator()(const Key& key) const {
    std::uint64_t hash = 1469598103934665603ULL;  // FNV-1a
    for (const std::uint32_t word : key) {
      hash = (hash ^ word) * 1099511628211ULL;
    }
    return static_cast<std::size_t>(hash);
  }
};

/// @brief This thread's remembered answers; past kKept it starts over (forgetting only searches
///        again).
class Memo {
 public:
  static constexpr std::size_t kKept = std::size_t{1} << 16U;

  static Memo& OfThisThread() {
    thread_local Memo memo;
    return memo;
  }

  static Key KeyOf(std::uint64_t serial, TravelMode mode, Vec2 place) {
    return {static_cast<std::uint32_t>(serial),
            static_cast<std::uint32_t>(serial >> 32U),
            static_cast<std::uint32_t>(mode),
            std::bit_cast<std::uint32_t>(place.x),
            std::bit_cast<std::uint32_t>(place.y)};
  }

  const std::vector<RoadAccess>* Find(const Key& key) const {
    const auto found = kept_.find(key);
    return found != kept_.end() ? &found->second : nullptr;
  }

  void Keep(const Key& key, const std::vector<RoadAccess>& found) {
    if (kept_.size() >= kKept) {
      kept_.clear();
    }
    kept_.emplace(key, found);
  }

 private:
  std::unordered_map<Key, std::vector<RoadAccess>, KeyHash> kept_;
};

/// @brief `search()`'s answer for (network, mode, place), remembered on this thread.
template <class Search>
std::vector<RoadAccess> Remembered(std::uint64_t serial,
                                   TravelMode mode,
                                   Vec2 place,
                                   Search search) {
  Memo& memo = Memo::OfThisThread();
  const Key key = Memo::KeyOf(serial, mode, place);
  if (const std::vector<RoadAccess>* const kept = memo.Find(key)) {
    return *kept;
  }
  std::vector<RoadAccess> found = search();
  memo.Keep(key, found);
  return found;
}

}  // namespace core::road_access

#endif  // CORE_COMMON_ROAD_ACCESS_MEMO_H_
